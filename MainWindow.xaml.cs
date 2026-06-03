using System.Drawing;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Forms = System.Windows.Forms;

namespace SnapLingo;

public partial class MainWindow : Window
{
    private const int HOTKEY_ID = 9000;

    [DllImport("user32.dll")]
    private static extern bool RegisterHotKey(IntPtr hWnd, int id, uint fsModifiers, uint vk);

    [DllImport("user32.dll")]
    private static extern bool UnregisterHotKey(IntPtr hWnd, int id);

    private HwndSource? _hwndSource;
    private readonly OcrService _ocrService = new();
    private AppSettings _settings = new();
    private Forms.NotifyIcon? _notifyIcon;

    public MainWindow()
    {
        InitializeComponent();
        Loaded += OnLoaded;
        Closed += OnClosed;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        var handle = new WindowInteropHelper(this).Handle;
        _hwndSource = HwndSource.FromHwnd(handle);
        _hwndSource?.AddHook(WndProc);

        _settings = AppSettings.Load();
        RegisterCurrentHotkey();
        InitTrayIcon();

        var activeOcr = _settings.GetActiveOcr();
        if (activeOcr.Provider == OcrProvider.Windows && !_ocrService.IsAvailable)
        {
            TxtStatus.Text = "OCR 不可用 — 请安装中文语言包";
        }
        else if (_settings.Models.Count == 0)
        {
            TxtStatus.Text = "请先在设置中配置模型";
        }

        Hide();
    }


    private void InitTrayIcon()
    {
        var iconStream = Application.GetResourceStream(
            new Uri("pack://application:,,,/Assets/app.ico"))?.Stream;
        var icon = iconStream != null ? new Icon(iconStream) : SystemIcons.Application;

        _notifyIcon = new Forms.NotifyIcon
        {
            Icon = icon,
            Text = "SnapLingo",
            Visible = true
        };

        var menu = new Forms.ContextMenuStrip();
        menu.Items.Add("截图翻译", null, (_, _) => StartCapture());
        menu.Items.Add("设置", null, (_, _) => ShowSettingsFromTray());
        menu.Items.Add(new Forms.ToolStripSeparator());
        menu.Items.Add("退出", null, (_, _) => ExitApp());
        _notifyIcon.ContextMenuStrip = menu;
        _notifyIcon.DoubleClick += (_, _) => ShowAndActivate();
    }

    internal void RegisterCurrentHotkey()
    {
        var handle = new WindowInteropHelper(this).Handle;
        UnregisterHotKey(handle, HOTKEY_ID);

        if (!RegisterHotKey(handle, HOTKEY_ID, _settings.HotkeyModifiers, _settings.HotkeyKey))
        {
            TxtStatus.Text = "热键注册失败";
        }
    }

    internal void UnregisterCurrentHotkey()
    {
        var handle = new WindowInteropHelper(this).Handle;
        UnregisterHotKey(handle, HOTKEY_ID);
    }

    private void OnClosed(object? sender, EventArgs e)
    {
        var handle = new WindowInteropHelper(this).Handle;
        UnregisterHotKey(handle, HOTKEY_ID);
        _hwndSource?.RemoveHook(WndProc);
        if (_notifyIcon != null)
        {
            _notifyIcon.Visible = false;
            _notifyIcon.Dispose();
        }
    }

    private void ShowAndActivate()
    {
        Show();
        WindowState = WindowState.Normal;
        Activate();
    }

    protected override void OnDeactivated(EventArgs e)
    {
        base.OnDeactivated(e);
        Hide();
    }

    private void ShowSettingsFromTray()
    {
        ShowAndActivate();
        OpenSettings();
    }

    private void ExitApp()
    {
        Application.Current.Shutdown();
    }

    private IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        const int WM_HOTKEY = 0x0312;
        if (msg == WM_HOTKEY && wParam.ToInt32() == HOTKEY_ID)
        {
            StartCapture();
            handled = true;
        }
        return IntPtr.Zero;
    }

    private async void TxtOcrResult_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter)
        {
            e.Handled = true;
            var text = TxtOcrResult.Text.Trim();
            if (!string.IsNullOrWhiteSpace(text))
            {
                TxtStatus.Text = "正在翻译...";
                await RunAllTranslations(text);
            }
        }
    }

    private void TitleBar_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        DragMove();
    }

    private void BtnClose_Click(object sender, RoutedEventArgs e) => Hide();

    private SettingsWindow? _settingsWindow;

    private void OpenSettings()
    {
        if (_settingsWindow != null)
        {
            _settingsWindow.Activate();
            return;
        }

        _settingsWindow = new SettingsWindow(_settings);
        _settingsWindow.Closed += (_, _) =>
        {
            if (_settingsWindow.Result != null)
            {
                _settings = _settingsWindow.Result;
                RegisterCurrentHotkey();
                TxtStatus.Text = "设置已保存";
            }
            _settingsWindow = null;
        };
        _settingsWindow.Show();
    }

    private void BtnCopyOcr_Click(object sender, RoutedEventArgs e)
    {
        if (!string.IsNullOrEmpty(TxtOcrResult.Text))
        {
            Clipboard.SetText(TxtOcrResult.Text);
            TxtStatus.Text = "已复制";
        }
    }

    private async void StartCapture()
    {
        Hide();
        await Task.Delay(200);

        var captureWindow = new ScreenCaptureWindow();
        var result = captureWindow.ShowDialog();

        if (result == true && captureWindow.CapturedImage != null)
        {
            TxtStatus.Text = $"已截图 ({captureWindow.CapturedImage.PixelWidth}x{captureWindow.CapturedImage.PixelHeight})";

            var ocrText = await RunOcr(captureWindow.CapturedImage);

            ShowAndActivate();

            if (!string.IsNullOrWhiteSpace(ocrText))
            {
                await RunAllTranslations(ocrText);
            }
            return;
        }
    }

    private async Task<string> RunOcr(BitmapSource image)
    {
        TxtOcrResult.Text = "";

        try
        {
            var text = await _ocrService.RecognizeAsync(image, _settings);
            TxtOcrResult.Text = text;
            TxtStatus.Text = string.IsNullOrWhiteSpace(text)
                ? "未识别到文字"
                : "识别完成，正在翻译...";
            return text;
        }
        catch (Exception ex)
        {
            TxtOcrResult.Text = $"识别失败：{ex.Message}";
            TxtStatus.Text = "OCR 出错";
            return string.Empty;
        }
    }

    private async Task RunAllTranslations(string text)
    {
        TranslationCards.Items.Clear();

        if (_settings.Models.Count == 0)
        {
            TxtStatus.Text = "未配置翻译模型，请在设置中添加";
            return;
        }

        TxtStatus.Text = "正在翻译...";

        var tasks = new List<Task>();

        foreach (var model in _settings.Models)
        {
            var card = CreateTranslationCard(model.Name);
            TranslationCards.Items.Add(card);

            var textBox = (TextBox)((Grid)((Border)card).Child).Children
                .OfType<TextBox>().First();

            tasks.Add(RunSingleTranslation(model, text, textBox));
        }

        await Task.WhenAll(tasks);
        TxtStatus.Text = "就绪";
    }

    private Border CreateTranslationCard(string modelName)
    {
        var header = new DockPanel { Margin = new Thickness(14, 10, 10, 0) };

        var title = new TextBlock
        {
            Text = modelName,
            FontSize = 12,
            FontWeight = FontWeights.SemiBold,
            Foreground = (SolidColorBrush)FindResource("TextSecondaryBrush"),
            VerticalAlignment = VerticalAlignment.Center
        };
        header.Children.Add(title);

        var copyBtn = new Button
        {
            Content = "复制",
            Style = (Style)FindResource("GhostButton"),
            HorizontalAlignment = System.Windows.HorizontalAlignment.Right,
            FontSize = 11
        };
        DockPanel.SetDock(copyBtn, Dock.Right);
        header.Children.Add(copyBtn);

        var textBox = new TextBox
        {
            Style = (Style)FindResource("ModernTextBox"),
            IsReadOnly = true,
            TextWrapping = TextWrapping.Wrap,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            BorderThickness = new Thickness(0),
            Margin = new Thickness(6, 6, 6, 8)
        };

        copyBtn.Click += (_, _) =>
        {
            if (!string.IsNullOrEmpty(textBox.Text))
            {
                Clipboard.SetText(textBox.Text);
                TxtStatus.Text = "已复制";
            }
        };

        var grid = new Grid();
        grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        grid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        Grid.SetRow(header, 0);
        Grid.SetRow(textBox, 1);
        grid.Children.Add(header);
        grid.Children.Add(textBox);

        var border = new Border
        {
            CornerRadius = new CornerRadius(8),
            Background = (SolidColorBrush)FindResource("CardBrush"),
            BorderBrush = (SolidColorBrush)FindResource("BorderBrush"),
            BorderThickness = new Thickness(1),
            Margin = new Thickness(4),
            MinHeight = 120,
            Child = grid
        };

        return border;
    }

    private async Task RunSingleTranslation(ModelConfig config, string text, TextBox targetTextBox)
    {
        try
        {
            await foreach (var chunk in TranslationService.TranslateStreamAsync(config, text, _settings.TranslationPrompt).ConfigureAwait(false))
            {
                await Dispatcher.InvokeAsync(() =>
                {
                    targetTextBox.Text += chunk;
                    targetTextBox.ScrollToEnd();
                });
            }
        }
        catch (Exception ex)
        {
            await Dispatcher.InvokeAsync(() =>
            {
                targetTextBox.Text += $"\n[翻译出错：{ex.Message}]";
            });
        }
    }
}
