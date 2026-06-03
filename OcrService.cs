using System.IO;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Windows.Media.Imaging;
using Windows.Graphics.Imaging;
using Windows.Media.Ocr;
using Windows.Storage.Streams;
using BitmapDecoder = Windows.Graphics.Imaging.BitmapDecoder;

namespace SnapLingo;

public class OcrService
{
    private const string VolcengineEndpoint = "https://visual.volcengineapi.com/";
    private const string VolcengineHost = "visual.volcengineapi.com";
    private const string VolcengineRegion = "cn-north-1";
    private const string VolcengineService = "cv";

    private static readonly HttpClient HttpClient = new();

    private OcrEngine? _engine;
    private string _languageTag = "zh-Hans-CN";

    public bool IsAvailable => _engine != null;
    public string LanguageTag => _languageTag;

    public OcrService()
    {
        InitializeEngine();
    }

    private void InitializeEngine()
    {
        var zhLang = OcrEngine.AvailableRecognizerLanguages
            .FirstOrDefault(l => l.LanguageTag.StartsWith("zh-Hans", StringComparison.OrdinalIgnoreCase));

        if (zhLang != null)
        {
            _engine = OcrEngine.TryCreateFromLanguage(zhLang);
            _languageTag = zhLang.LanguageTag;
            return;
        }

        var enLang = OcrEngine.AvailableRecognizerLanguages
            .FirstOrDefault(l => l.LanguageTag.StartsWith("en", StringComparison.OrdinalIgnoreCase));

        if (enLang != null)
        {
            _engine = OcrEngine.TryCreateFromLanguage(enLang);
            _languageTag = enLang.LanguageTag;
        }
    }

    public async Task<string> RecognizeAsync(BitmapSource bitmapSource, AppSettings settings)
    {
        var ocr = settings.GetActiveOcr();
        return ocr.Provider switch
        {
            OcrProvider.Volcengine => await RecognizeWithVolcengineAsync(bitmapSource, ocr),
            OcrProvider.Baidu => await RecognizeWithBaiduAsync(bitmapSource, ocr),
            OcrProvider.Tencent => await RecognizeWithTencentAsync(bitmapSource, ocr),
            _ => await RecognizeWithWindowsAsync(bitmapSource)
        };
    }

    public async Task<string> RecognizeAsync(BitmapSource bitmapSource)
    {
        return await RecognizeWithWindowsAsync(bitmapSource);
    }

    private async Task<string> RecognizeWithWindowsAsync(BitmapSource bitmapSource)
    {
        if (_engine == null)
            return "[OCR 引擎不可用，请安装语言包]";

        var softwareBitmap = await ConvertToSoftwareBitmap(bitmapSource);
        var result = await _engine.RecognizeAsync(softwareBitmap);

        return string.Join(Environment.NewLine, result.Lines.Select(l => l.Text));
    }

    private static async Task<string> RecognizeWithVolcengineAsync(BitmapSource bitmapSource, OcrServiceConfig config)
    {
        if (string.IsNullOrWhiteSpace(config.VolcengineAccessKeyId) ||
            string.IsNullOrWhiteSpace(config.VolcengineSecretAccessKey))
        {
            throw new InvalidOperationException("请先在设置中配置火山引擎 OCR 的 Access Key ID 和 Secret Access Key");
        }

        var imageBase64 = await EncodePngBase64Async(bitmapSource);
        var formValues = new Dictionary<string, string>
        {
            ["image_base64"] = imageBase64,
            ["mode"] = "default"
        };
        var body = string.Join("&", formValues.Select(kvp =>
            $"{Uri.EscapeDataString(kvp.Key)}={Uri.EscapeDataString(kvp.Value)}"));

        var now = DateTimeOffset.UtcNow;
        var xDate = now.ToString("yyyyMMdd'T'HHmmss'Z'");
        var shortDate = now.ToString("yyyyMMdd");
        var query = "Action=MultiLanguageOCR&Version=2022-08-31";
        var authorization = CreateVolcengineAuthorization(
            config.VolcengineAccessKeyId.Trim(),
            config.VolcengineSecretAccessKey.Trim(),
            shortDate,
            xDate,
            query,
            body);

        using var request = new HttpRequestMessage(HttpMethod.Post, $"{VolcengineEndpoint}?{query}");
        request.Content = new ByteArrayContent(Encoding.UTF8.GetBytes(body));
        request.Content.Headers.ContentType = new System.Net.Http.Headers.MediaTypeHeaderValue("application/x-www-form-urlencoded");
        request.Headers.TryAddWithoutValidation("X-Date", xDate);
        request.Headers.TryAddWithoutValidation("Authorization", authorization);

        using var response = await HttpClient.SendAsync(request);
        var responseText = await response.Content.ReadAsStringAsync();
        if (!response.IsSuccessStatusCode)
            throw new InvalidOperationException($"火山引擎 OCR 请求失败：HTTP {(int)response.StatusCode} {responseText}");

        return ParseVolcengineOcrText(responseText);
    }

    // ========== Baidu OCR ==========

    private static async Task<string> RecognizeWithBaiduAsync(BitmapSource bitmapSource, OcrServiceConfig config)
    {
        if (string.IsNullOrWhiteSpace(config.BaiduApiKey) || string.IsNullOrWhiteSpace(config.BaiduSecretKey))
            throw new InvalidOperationException("请先在设置中配置百度 OCR 的 API Key 和 Secret Key");

        var accessToken = await GetBaiduAccessToken(config.BaiduApiKey.Trim(), config.BaiduSecretKey.Trim());
        var imageBase64 = await EncodePngBase64Async(bitmapSource);

        var content = new FormUrlEncodedContent(new Dictionary<string, string>
        {
            ["image"] = imageBase64,
            ["detect_language"] = "true"
        });

        var url = $"https://aip.baidubce.com/rest/2.0/ocr/v1/general_basic?access_token={accessToken}";
        using var response = await HttpClient.PostAsync(url, content);
        var responseText = await response.Content.ReadAsStringAsync();

        if (!response.IsSuccessStatusCode)
            throw new InvalidOperationException($"百度 OCR 请求失败：HTTP {(int)response.StatusCode} {responseText}");

        return ParseBaiduOcrText(responseText);
    }

    private static async Task<string> GetBaiduAccessToken(string apiKey, string secretKey)
    {
        var url = $"https://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials&client_id={apiKey}&client_secret={secretKey}";
        using var response = await HttpClient.PostAsync(url, null);
        var responseText = await response.Content.ReadAsStringAsync();

        using var doc = JsonDocument.Parse(responseText);
        if (doc.RootElement.TryGetProperty("access_token", out var token))
            return token.GetString()!;

        var error = doc.RootElement.TryGetProperty("error_description", out var desc)
            ? desc.GetString() : responseText;
        throw new InvalidOperationException($"百度 OCR 获取 token 失败：{error}");
    }

    private static string ParseBaiduOcrText(string responseText)
    {
        using var doc = JsonDocument.Parse(responseText);
        var root = doc.RootElement;

        if (root.TryGetProperty("error_code", out var errCode))
        {
            var msg = root.TryGetProperty("error_msg", out var errMsg) ? errMsg.GetString() : responseText;
            throw new InvalidOperationException($"百度 OCR 返回错误：{errCode} {msg}");
        }

        if (!root.TryGetProperty("words_result", out var words) || words.ValueKind != JsonValueKind.Array)
            return string.Empty;

        var lines = new List<string>();
        foreach (var item in words.EnumerateArray())
        {
            if (item.TryGetProperty("words", out var w))
            {
                var text = w.GetString();
                if (!string.IsNullOrWhiteSpace(text))
                    lines.Add(text);
            }
        }
        return string.Join(Environment.NewLine, lines);
    }

    // ========== Tencent OCR ==========

    private static async Task<string> RecognizeWithTencentAsync(BitmapSource bitmapSource, OcrServiceConfig config)
    {
        if (string.IsNullOrWhiteSpace(config.TencentSecretId) || string.IsNullOrWhiteSpace(config.TencentSecretKey))
            throw new InvalidOperationException("请先在设置中配置腾讯云 OCR 的 SecretId 和 SecretKey");

        var imageBase64 = await EncodePngBase64Async(bitmapSource);
        var body = JsonSerializer.Serialize(new { ImageBase64 = imageBase64 });

        var now = DateTimeOffset.UtcNow;
        var timestamp = now.ToUnixTimeSeconds().ToString();
        var date = now.ToString("yyyy-MM-dd");

        var authorization = CreateTencentAuthorization(
            config.TencentSecretId.Trim(),
            config.TencentSecretKey.Trim(),
            timestamp, date, body);

        using var request = new HttpRequestMessage(HttpMethod.Post, "https://ocr.tencentcloudapi.com/");
        request.Content = new StringContent(body, Encoding.UTF8, "application/json");
        request.Headers.TryAddWithoutValidation("Authorization", authorization);
        request.Headers.TryAddWithoutValidation("X-TC-Action", "GeneralBasicOCR");
        request.Headers.TryAddWithoutValidation("X-TC-Version", "2018-11-19");
        request.Headers.TryAddWithoutValidation("X-TC-Timestamp", timestamp);
        request.Headers.TryAddWithoutValidation("X-TC-Region", "ap-beijing");

        using var response = await HttpClient.SendAsync(request);
        var responseText = await response.Content.ReadAsStringAsync();

        if (!response.IsSuccessStatusCode)
            throw new InvalidOperationException($"腾讯云 OCR 请求失败：HTTP {(int)response.StatusCode} {responseText}");

        return ParseTencentOcrText(responseText);
    }

    private static string CreateTencentAuthorization(
        string secretId, string secretKey,
        string timestamp, string date, string body)
    {
        const string service = "ocr";
        const string host = "ocr.tencentcloudapi.com";

        var canonicalHeaders = $"content-type:application/json; charset=utf-8\nhost:{host}\n";
        const string signedHeaders = "content-type;host";
        var canonicalRequest = string.Join("\n", new[]
        {
            "POST", "/", "", canonicalHeaders, signedHeaders, Sha256Hex(body)
        });

        var credentialScope = $"{date}/{service}/tc3_request";
        var stringToSign = string.Join("\n", new[]
        {
            "TC3-HMAC-SHA256", timestamp, credentialScope, Sha256Hex(canonicalRequest)
        });

        var secretDate = HmacSha256(Encoding.UTF8.GetBytes("TC3" + secretKey), date);
        var secretService = HmacSha256(secretDate, service);
        var secretSigning = HmacSha256(secretService, "tc3_request");
        var signature = ToHex(HmacSha256(secretSigning, stringToSign));

        return $"TC3-HMAC-SHA256 Credential={secretId}/{credentialScope}, SignedHeaders={signedHeaders}, Signature={signature}";
    }

    private static string ParseTencentOcrText(string responseText)
    {
        using var doc = JsonDocument.Parse(responseText);
        var root = doc.RootElement;

        if (root.TryGetProperty("Response", out var resp))
        {
            if (resp.TryGetProperty("Error", out var error))
            {
                var msg = error.TryGetProperty("Message", out var m) ? m.GetString() : responseText;
                throw new InvalidOperationException($"腾讯云 OCR 返回错误：{msg}");
            }

            if (resp.TryGetProperty("TextDetections", out var detections) &&
                detections.ValueKind == JsonValueKind.Array)
            {
                var lines = new List<string>();
                foreach (var item in detections.EnumerateArray())
                {
                    if (item.TryGetProperty("DetectedText", out var t))
                    {
                        var text = t.GetString();
                        if (!string.IsNullOrWhiteSpace(text))
                            lines.Add(text);
                    }
                }
                return string.Join(Environment.NewLine, lines);
            }
        }
        return string.Empty;
    }

    // ========== Volcengine Helpers ==========

    private static string CreateVolcengineAuthorization(
        string accessKeyId,
        string secretAccessKey,
        string shortDate,
        string xDate,
        string query,
        string body)
    {
        const string signedHeaders = "host;x-date";
        var credentialScope = $"{shortDate}/{VolcengineRegion}/{VolcengineService}/request";
        var canonicalHeaders = $"host:{VolcengineHost}\nx-date:{xDate}\n";
        var canonicalRequest = string.Join("\n", new[]
        {
            "POST",
            "/",
            query,
            canonicalHeaders,
            signedHeaders,
            Sha256Hex(body)
        });
        var stringToSign = string.Join("\n", new[]
        {
            "HMAC-SHA256",
            xDate,
            credentialScope,
            Sha256Hex(canonicalRequest)
        });

        var signingKey = HmacSha256(Encoding.UTF8.GetBytes(secretAccessKey), shortDate);
        signingKey = HmacSha256(signingKey, VolcengineRegion);
        signingKey = HmacSha256(signingKey, VolcengineService);
        signingKey = HmacSha256(signingKey, "request");
        var signature = ToHex(HmacSha256(signingKey, stringToSign));

        return $"HMAC-SHA256 Credential={accessKeyId}/{credentialScope}, SignedHeaders={signedHeaders}, Signature={signature}";
    }

    private static string ParseVolcengineOcrText(string responseText)
    {
        using var doc = JsonDocument.Parse(responseText);
        var root = doc.RootElement;
        var code = TryGetInt(root, "code") ?? TryGetInt(root, "status");
        if (code.HasValue && code.Value != 10000)
        {
            var message = root.TryGetProperty("message", out var msg) ? msg.GetString() : responseText;
            throw new InvalidOperationException($"火山引擎 OCR 返回错误：{code.Value} {message}");
        }

        if (!root.TryGetProperty("data", out var data) ||
            !data.TryGetProperty("ocr_infos", out var infos) ||
            infos.ValueKind != JsonValueKind.Array)
        {
            return string.Empty;
        }

        var lines = new List<string>();
        foreach (var item in infos.EnumerateArray())
        {
            if (item.TryGetProperty("text", out var textElement))
            {
                var text = textElement.GetString();
                if (!string.IsNullOrWhiteSpace(text))
                    lines.Add(text);
            }
        }

        return string.Join(Environment.NewLine, lines);
    }

    private static int? TryGetInt(JsonElement element, string propertyName)
    {
        if (!element.TryGetProperty(propertyName, out var value))
            return null;

        if (value.ValueKind == JsonValueKind.Number && value.TryGetInt32(out var intValue))
            return intValue;

        if (value.ValueKind == JsonValueKind.String && int.TryParse(value.GetString(), out intValue))
            return intValue;

        return null;
    }

    private static async Task<string> EncodePngBase64Async(BitmapSource bitmapSource)
    {
        await using var stream = new MemoryStream();
        var encoder = new PngBitmapEncoder();
        encoder.Frames.Add(System.Windows.Media.Imaging.BitmapFrame.Create(bitmapSource));
        encoder.Save(stream);
        return Convert.ToBase64String(stream.ToArray());
    }

    private static string Sha256Hex(string text)
    {
        return ToHex(SHA256.HashData(Encoding.UTF8.GetBytes(text)));
    }

    private static byte[] HmacSha256(byte[] key, string text)
    {
        using var hmac = new HMACSHA256(key);
        return hmac.ComputeHash(Encoding.UTF8.GetBytes(text));
    }

    private static string ToHex(byte[] bytes)
    {
        return Convert.ToHexString(bytes).ToLowerInvariant();
    }

    private static async Task<SoftwareBitmap> ConvertToSoftwareBitmap(BitmapSource bitmapSource)
    {
        using var stream = new InMemoryRandomAccessStream();
        var encoder = await Windows.Graphics.Imaging.BitmapEncoder.CreateAsync(
            Windows.Graphics.Imaging.BitmapEncoder.PngEncoderId, stream);

        var pixelWidth = bitmapSource.PixelWidth;
        var pixelHeight = bitmapSource.PixelHeight;
        var stride = pixelWidth * 4;
        var pixels = new byte[pixelHeight * stride];
        bitmapSource.CopyPixels(pixels, stride, 0);

        encoder.SetPixelData(
            Windows.Graphics.Imaging.BitmapPixelFormat.Bgra8,
            Windows.Graphics.Imaging.BitmapAlphaMode.Premultiplied,
            (uint)pixelWidth, (uint)pixelHeight,
            bitmapSource.DpiX, bitmapSource.DpiY,
            pixels);

        await encoder.FlushAsync();
        stream.Seek(0);

        var decoder = await BitmapDecoder.CreateAsync(stream);
        return await decoder.GetSoftwareBitmapAsync(
            Windows.Graphics.Imaging.BitmapPixelFormat.Bgra8,
            Windows.Graphics.Imaging.BitmapAlphaMode.Premultiplied);
    }

    public IReadOnlyList<string> GetAvailableLanguages()
    {
        return OcrEngine.AvailableRecognizerLanguages
            .Select(l => l.LanguageTag)
            .ToList();
    }

    public bool HasChineseLanguagePack()
    {
        return OcrEngine.AvailableRecognizerLanguages
            .Any(l => l.LanguageTag.StartsWith("zh-Hans", StringComparison.OrdinalIgnoreCase));
    }
}
