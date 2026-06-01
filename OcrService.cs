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
        if (ocr.Provider == OcrProvider.Volcengine)
            return await RecognizeWithVolcengineAsync(bitmapSource, ocr);

        return await RecognizeWithWindowsAsync(bitmapSource);
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
