using System.IO;
using System.Windows.Media.Imaging;
using Windows.Graphics.Imaging;
using Windows.Media.Ocr;
using Windows.Storage.Streams;
using BitmapDecoder = Windows.Graphics.Imaging.BitmapDecoder;

namespace SnapLingo;

public class OcrService
{
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

    public async Task<string> RecognizeAsync(BitmapSource bitmapSource)
    {
        if (_engine == null)
            return "[OCR 引擎不可用，请安装语言包]";

        var softwareBitmap = await ConvertToSoftwareBitmap(bitmapSource);
        var result = await _engine.RecognizeAsync(softwareBitmap);

        return string.Join(Environment.NewLine, result.Lines.Select(l => l.Text));
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
