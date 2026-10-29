using System.Reflection;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using SnapLingo;
using System.Net;
using System.Net.Sockets;
using System.Text;

internal static class Program
{
    [STAThread]
    private static int Main()
    {
        try
        {
            var app = new Application();
            app.Resources.MergedDictionaries.Add(new ResourceDictionary
            {
                Source = new Uri("pack://application:,,,/SnapLingo;component/Styles.xaml")
            });
            var settings = new AppSettings
            {
                Models = new() { new() { Name = "A" }, new() { Name = "B" } },
                OcrServices = new() { new() { Name = "OCR A" }, new() { Name = "OCR B" } }
            };
            var window = new SettingsWindow(settings);
            var thinking = (CheckBox)window.FindName("ChkEnableThinking");
            thinking.IsChecked = true;
            var models = (ListBox)window.FindName("LstModels");
            models.SelectedIndex = 1;
            Assert(thinking.IsChecked == false, "Thinking mode is independent per model");
            models.SelectedIndex = 0;
            Assert(thinking.IsChecked == true, "Thinking mode survives switching models");
            Assert(!settings.Models[0].EnableThinking, "Thinking edits do not change original configuration");
            CheckThinkingRequests();
            Invoke(window, "BtnRemoveModel_Click");
            Assert(((TextBox)window.FindName("TxtModelName")).Text == "B", "Deleting first model refreshes editor");
            ((TextBox)window.FindName("TxtModelName")).Text = "Edited B";
            Assert(settings.Models[0].Name == "A", "Editing settings leaves original snapshot unchanged");
            Invoke(window, "BtnRemoveOcr_Click");
            Assert(((TextBox)window.FindName("TxtOcrName")).Text == "OCR B", "Deleting first OCR refreshes editor");
            Invoke(window, "BtnRemoveModel_Click");
            Assert(((TextBox)window.FindName("TxtModelName")).Text == "", "Deleting last model clears editor");
            var snapshot = settings.Clone();
            settings.Models[0].Name = "Changed";
            settings.OcrServices[0].Name = "Changed";
            Assert(snapshot.Models[0].Name == "A" && snapshot.OcrServices[0].Name == "OCR A", "Saved snapshot owns independent nested objects");

            var ocr = new OcrService();
            typeof(OcrService).GetField("_engine", BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(ocr, null);
            var bitmap = BitmapSource.Create(1, 1, 96, 96, PixelFormats.Bgra32, null, new byte[4], 4);
            try
            {
                ocr.RecognizeAsync(bitmap).GetAwaiter().GetResult();
                throw new Exception("Unavailable OCR must fail instead of returning text");
            }
            catch (InvalidOperationException)
            {
                Console.WriteLine("PASS: Unavailable OCR reports an error");
            }
            CheckCrop();
            Task.Run(CheckStreamCancellation).GetAwaiter().GetResult();
            Console.WriteLine("All regression checks passed.");
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine(ex);
            return 1;
        }
    }

    private static void Invoke(object target, string name) =>
        target.GetType().GetMethod(name, BindingFlags.Instance | BindingFlags.NonPublic)!
            .Invoke(target, new object[] { target, new RoutedEventArgs() });

    private static void CheckCrop()
    {
        var capture = new ScreenCaptureWindow();
        var canvas = (Canvas)capture.FindName("OverlayCanvas");
        canvas.Measure(new Size(100, 50));
        canvas.Arrange(new Rect(0, 0, 100, 50));
        var pixels = new byte[200 * 100 * 4];
        for (var i = 0; i < pixels.Length; i += 4) { pixels[i] = 123; pixels[i + 3] = 255; }
        var snapshot = BitmapSource.Create(200, 100, 96, 96, PixelFormats.Bgra32, null, pixels, 800);
        typeof(ScreenCaptureWindow).GetField("_screenSnapshot", BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(capture, snapshot);
        typeof(ScreenCaptureWindow).GetMethod("CaptureScreen", BindingFlags.Instance | BindingFlags.NonPublic)!
            .Invoke(capture, new object[] { -5d, 10d, 25d, 100d });
        var result = capture.CapturedImage!;
        Assert(result.PixelWidth == 40 && result.PixelHeight == 80, "Crop scales coordinates and clamps selection to desktop");
        var output = new byte[40 * 80 * 4];
        result.CopyPixels(output, 160, 0);
        Assert(output[0] == 123, "Crop uses original snapshot pixels");
    }

    private static void CheckThinkingRequests()
    {
        var build = typeof(TranslationService).GetMethod("CreateRequestJson", BindingFlags.Static | BindingFlags.NonPublic)!;
        foreach (var enabled in new[] { false, true })
        {
            var config = new ModelConfig { Model = "deepseek-flash", EnableThinking = enabled };
            var restored = System.Text.Json.JsonSerializer.Deserialize<ModelConfig>(System.Text.Json.JsonSerializer.Serialize(config))!;
            Assert(restored.EnableThinking == enabled, $"Thinking setting persists: {enabled}");
            using var deepseek = System.Text.Json.JsonDocument.Parse((string)build.Invoke(null, new object[] { restored, "test" })!);
            Assert(deepseek.RootElement.GetProperty("thinking").GetProperty("type").GetString() == (enabled ? "enabled" : "disabled")
                && !deepseek.RootElement.TryGetProperty("enable_thinking", out _), $"DeepSeek request uses thinking.type: {enabled}");
            config.Model = "qwen-plus";
            using var compatible = System.Text.Json.JsonDocument.Parse((string)build.Invoke(null, new object[] { config, "test" })!);
            Assert(compatible.RootElement.GetProperty("enable_thinking").GetBoolean() == enabled
                && !compatible.RootElement.TryGetProperty("thinking", out _), $"Compatible request uses enable_thinking: {enabled}");
        }
        Assert(!System.Text.Json.JsonSerializer.Deserialize<ModelConfig>("{}")!.EnableThinking, "Existing configuration defaults to thinking disabled");
    }

    private static async Task CheckStreamCancellation()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var port = ((IPEndPoint)listener.LocalEndpoint).Port;
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        var server = Task.Run(async () =>
        {
            using var client = await listener.AcceptTcpClientAsync(timeout.Token);
            var stream = client.GetStream();
            var buffer = new byte[8192];
            await stream.ReadAsync(buffer, timeout.Token);
            var response = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: close\r\n\r\ndata: {\"choices\":[{\"delta\":{\"content\":\"hello\"}}]}\n\n";
            await stream.WriteAsync(Encoding.UTF8.GetBytes(response), timeout.Token);
            try { await Task.Delay(Timeout.Infinite, timeout.Token); }
            catch (OperationCanceledException) { }
        });
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(timeout.Token);
        await using var enumerator = TranslationService.TranslateStreamAsync(new ModelConfig
        {
            Endpoint = $"http://127.0.0.1:{port}/", ApiKey = "local-test"
        }, "test", cancellationToken: cancellation.Token).GetAsyncEnumerator();
        try
        {
            Assert(await enumerator.MoveNextAsync() && enumerator.Current == "hello", "Streaming translation yields response content");
            cancellation.CancelAfter(100);
            try
            {
                await enumerator.MoveNextAsync();
                throw new Exception("Stalled stream should be cancelled");
            }
            catch (OperationCanceledException)
            {
                Assert(!timeout.IsCancellationRequested, "Cancellation interrupts stalled stream promptly");
            }
        }
        finally
        {
            timeout.Cancel();
            await server;
        }
    }

    private static void Assert(bool condition, string name)
    {
        if (!condition) throw new Exception(name);
        Console.WriteLine($"PASS: {name}");
    }
}
