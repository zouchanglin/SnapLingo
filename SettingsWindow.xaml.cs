using System.Diagnostics;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using Microsoft.Win32;

namespace SnapLingo;

public partial class SettingsWindow : Window
{
    private uint _hotkeyModifiers;
    private uint _hotkeyKey;
    private bool _isRecordingHotkey;
    private readonly List<ModelConfig> _models;
    private readonly List<OcrServiceConfig> _ocrServices;
    private int _activeOcrIndex;
    private bool _suppressSync;

    public AppSettings? Result { get; private set; }

    public SettingsWindow(AppSettings settings)
    {
        InitializeComponent();

        _models = settings.Models.Select(m => new ModelConfig
        {
            Name = m.Name,
            Endpoint = m.Endpoint,
            ApiKey = m.ApiKey,
            Model = m.Model
        }).ToList();

        _ocrServices = settings.OcrServices.Select(o => new OcrServiceConfig
        {
            Name = o.Name,
            Provider = o.Provider,
            VolcengineAccessKeyId = o.VolcengineAccessKeyId,
            VolcengineSecretAccessKey = o.VolcengineSecretAccessKey,
            BaiduApiKey = o.BaiduApiKey,
            BaiduSecretKey = o.BaiduSecretKey,
            TencentSecretId = o.TencentSecretId,
            TencentSecretKey = o.TencentSecretKey
        }).ToList();
        _activeOcrIndex = settings.ActiveOcrIndex;

        _hotkeyModifiers = settings.HotkeyModifiers;
        _hotkeyKey = settings.HotkeyKey;
        TxtHotkey.Text = settings.GetHotkeyDisplayString();
        TxtPrompt.Text = settings.TranslationPrompt;
        ChkAutoStart.IsChecked = settings.AutoStart;

        RefreshModelList();
        if (_models.Count > 0)
            LstModels.SelectedIndex = 0;

        RefreshOcrList();
        if (_ocrServices.Count > 0)
            LstOcr.SelectedIndex = 0;
    }

    // ========== Model Management ==========

    private void RefreshModelList()
    {
        _suppressSync = true;
        var selectedIdx = LstModels.SelectedIndex;
        LstModels.Items.Clear();
        foreach (var m in _models)
        {
            LstModels.Items.Add(string.IsNullOrWhiteSpace(m.Name) ? "(未命名)" : m.Name);
        }
        if (selectedIdx >= 0 && selectedIdx < _models.Count)
            LstModels.SelectedIndex = selectedIdx;
        _suppressSync = false;
    }

    private void LstModels_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_suppressSync) return;
        var idx = LstModels.SelectedIndex;
        if (idx < 0 || idx >= _models.Count)
        {
            ClearModelFields();
            return;
        }

        _suppressSync = true;
        var m = _models[idx];
        TxtModelName.Text = m.Name;
        TxtModelEndpoint.Text = m.Endpoint;
        PwdModelApiKey.Password = m.ApiKey;
        TxtModelApiKey.Text = m.ApiKey;
        TxtModelModel.Text = m.Model;
        _suppressSync = false;
    }

    private void ClearModelFields()
    {
        _suppressSync = true;
        TxtModelName.Text = "";
        TxtModelEndpoint.Text = "";
        PwdModelApiKey.Password = "";
        TxtModelApiKey.Text = "";
        TxtModelModel.Text = "";
        _suppressSync = false;
    }

    private void ModelField_TextChanged(object sender, TextChangedEventArgs e)
    {
        if (_suppressSync) return;
        var idx = LstModels.SelectedIndex;
        if (idx < 0 || idx >= _models.Count) return;

        var m = _models[idx];
        m.Name = TxtModelName.Text.Trim();
        m.Endpoint = TxtModelEndpoint.Text.Trim();
        m.ApiKey = TxtModelApiKey.Text.Trim();
        m.Model = TxtModelModel.Text.Trim();

        _suppressSync = true;
        LstModels.Items[idx] = string.IsNullOrWhiteSpace(m.Name) ? "(未命名)" : m.Name;
        _suppressSync = false;
    }

    private void PwdModelApiKey_Changed(object sender, RoutedEventArgs e)
    {
        if (_suppressSync) return;
        var idx = LstModels.SelectedIndex;
        if (idx < 0 || idx >= _models.Count) return;
        _models[idx].ApiKey = PwdModelApiKey.Password;
        _suppressSync = true;
        TxtModelApiKey.Text = PwdModelApiKey.Password;
        _suppressSync = false;
    }

    private void ShowModelApiKey_Down(object sender, MouseButtonEventArgs e)
    {
        TxtModelApiKey.Text = PwdModelApiKey.Password;
        PwdModelApiKey.Visibility = Visibility.Collapsed;
        TxtModelApiKey.Visibility = Visibility.Visible;
    }

    private void ShowModelApiKey_Up(object sender, MouseButtonEventArgs e)
    {
        PwdModelApiKey.Visibility = Visibility.Visible;
        TxtModelApiKey.Visibility = Visibility.Collapsed;
    }

    private void BtnAddModel_Click(object sender, RoutedEventArgs e)
    {
        _models.Add(new ModelConfig { Name = $"模型 {_models.Count + 1}" });
        RefreshModelList();
        LstModels.SelectedIndex = _models.Count - 1;
    }

    private void BtnRemoveModel_Click(object sender, RoutedEventArgs e)
    {
        var idx = LstModels.SelectedIndex;
        if (idx < 0 || idx >= _models.Count) return;

        _models.RemoveAt(idx);
        RefreshModelList();
        if (_models.Count > 0)
            LstModels.SelectedIndex = Math.Min(idx, _models.Count - 1);
        else
            ClearModelFields();
    }

    // ========== OCR Management ==========

    private void RefreshOcrList()
    {
        _suppressSync = true;
        var selectedIdx = LstOcr.SelectedIndex;
        LstOcr.Items.Clear();
        for (int i = 0; i < _ocrServices.Count; i++)
        {
            var o = _ocrServices[i];
            var name = string.IsNullOrWhiteSpace(o.Name) ? "(未命名)" : o.Name;
            LstOcr.Items.Add(i == _activeOcrIndex ? $"★ {name}" : name);
        }
        if (selectedIdx >= 0 && selectedIdx < _ocrServices.Count)
            LstOcr.SelectedIndex = selectedIdx;
        _suppressSync = false;
    }

    private void LstOcr_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_suppressSync) return;
        var idx = LstOcr.SelectedIndex;
        if (idx < 0 || idx >= _ocrServices.Count)
        {
            ClearOcrFields();
            return;
        }

        _suppressSync = true;
        var o = _ocrServices[idx];
        TxtOcrName.Text = o.Name;
        SelectOcrProvider(o.Provider);
        PwdOcrAccessKeyId.Password = o.VolcengineAccessKeyId;
        TxtOcrAccessKeyId.Text = o.VolcengineAccessKeyId;
        PwdOcrSecretKey.Password = o.VolcengineSecretAccessKey;
        TxtOcrSecretKey.Text = o.VolcengineSecretAccessKey;
        PwdBaiduApiKey.Password = o.BaiduApiKey;
        TxtBaiduApiKey.Text = o.BaiduApiKey;
        PwdBaiduSecretKey.Password = o.BaiduSecretKey;
        TxtBaiduSecretKey.Text = o.BaiduSecretKey;
        PwdTencentSecretId.Password = o.TencentSecretId;
        TxtTencentSecretId.Text = o.TencentSecretId;
        PwdTencentSecretKey.Password = o.TencentSecretKey;
        TxtTencentSecretKey.Text = o.TencentSecretKey;
        _suppressSync = false;
        UpdateOcrProviderVisibility();
    }

    private void ClearOcrFields()
    {
        _suppressSync = true;
        TxtOcrName.Text = "";
        CmbOcrProvider.SelectedIndex = 0;
        PwdOcrAccessKeyId.Password = "";
        TxtOcrAccessKeyId.Text = "";
        PwdOcrSecretKey.Password = "";
        TxtOcrSecretKey.Text = "";
        PwdBaiduApiKey.Password = "";
        TxtBaiduApiKey.Text = "";
        PwdBaiduSecretKey.Password = "";
        TxtBaiduSecretKey.Text = "";
        PwdTencentSecretId.Password = "";
        TxtTencentSecretId.Text = "";
        PwdTencentSecretKey.Password = "";
        TxtTencentSecretKey.Text = "";
        _suppressSync = false;
        UpdateOcrProviderVisibility();
    }

    private void OcrField_TextChanged(object sender, TextChangedEventArgs e)
    {
        if (_suppressSync) return;
        var idx = LstOcr.SelectedIndex;
        if (idx < 0 || idx >= _ocrServices.Count) return;

        var o = _ocrServices[idx];
        o.Name = TxtOcrName.Text.Trim();
        o.VolcengineAccessKeyId = TxtOcrAccessKeyId.Text.Trim();
        o.VolcengineSecretAccessKey = TxtOcrSecretKey.Text.Trim();
        o.BaiduApiKey = TxtBaiduApiKey.Text.Trim();
        o.BaiduSecretKey = TxtBaiduSecretKey.Text.Trim();
        o.TencentSecretId = TxtTencentSecretId.Text.Trim();
        o.TencentSecretKey = TxtTencentSecretKey.Text.Trim();

        if (sender == TxtOcrName)
        {
            _suppressSync = true;
            var selectedIdx = LstOcr.SelectedIndex;
            var name = string.IsNullOrWhiteSpace(o.Name) ? "(未命名)" : o.Name;
            LstOcr.Items[idx] = idx == _activeOcrIndex ? $"★ {name}" : name;
            LstOcr.SelectedIndex = selectedIdx;
            _suppressSync = false;
        }
    }

    private void PwdOcrAccessKeyId_Changed(object sender, RoutedEventArgs e)
    {
        if (_suppressSync) return;
        var idx = LstOcr.SelectedIndex;
        if (idx < 0 || idx >= _ocrServices.Count) return;
        _ocrServices[idx].VolcengineAccessKeyId = PwdOcrAccessKeyId.Password;
        _suppressSync = true;
        TxtOcrAccessKeyId.Text = PwdOcrAccessKeyId.Password;
        _suppressSync = false;
    }

    private void PwdOcrSecretKey_Changed(object sender, RoutedEventArgs e)
    {
        if (_suppressSync) return;
        var idx = LstOcr.SelectedIndex;
        if (idx < 0 || idx >= _ocrServices.Count) return;
        _ocrServices[idx].VolcengineSecretAccessKey = PwdOcrSecretKey.Password;
        _suppressSync = true;
        TxtOcrSecretKey.Text = PwdOcrSecretKey.Password;
        _suppressSync = false;
    }

    private void ShowOcrAccessKeyId_Down(object sender, MouseButtonEventArgs e)
    {
        TxtOcrAccessKeyId.Text = PwdOcrAccessKeyId.Password;
        PwdOcrAccessKeyId.Visibility = Visibility.Collapsed;
        TxtOcrAccessKeyId.Visibility = Visibility.Visible;
    }

    private void ShowOcrAccessKeyId_Up(object sender, MouseButtonEventArgs e)
    {
        PwdOcrAccessKeyId.Visibility = Visibility.Visible;
        TxtOcrAccessKeyId.Visibility = Visibility.Collapsed;
    }

    private void ShowOcrSecretKey_Down(object sender, MouseButtonEventArgs e)
    {
        TxtOcrSecretKey.Text = PwdOcrSecretKey.Password;
        PwdOcrSecretKey.Visibility = Visibility.Collapsed;
        TxtOcrSecretKey.Visibility = Visibility.Visible;
    }

    private void ShowOcrSecretKey_Up(object sender, MouseButtonEventArgs e)
    {
        PwdOcrSecretKey.Visibility = Visibility.Visible;
        TxtOcrSecretKey.Visibility = Visibility.Collapsed;
    }

    // Baidu
    private void PwdBaiduApiKey_Changed(object sender, RoutedEventArgs e)
    {
        if (_suppressSync) return;
        var idx = LstOcr.SelectedIndex;
        if (idx < 0 || idx >= _ocrServices.Count) return;
        _ocrServices[idx].BaiduApiKey = PwdBaiduApiKey.Password;
        _suppressSync = true;
        TxtBaiduApiKey.Text = PwdBaiduApiKey.Password;
        _suppressSync = false;
    }

    private void PwdBaiduSecretKey_Changed(object sender, RoutedEventArgs e)
    {
        if (_suppressSync) return;
        var idx = LstOcr.SelectedIndex;
        if (idx < 0 || idx >= _ocrServices.Count) return;
        _ocrServices[idx].BaiduSecretKey = PwdBaiduSecretKey.Password;
        _suppressSync = true;
        TxtBaiduSecretKey.Text = PwdBaiduSecretKey.Password;
        _suppressSync = false;
    }

    private void ShowBaiduApiKey_Down(object sender, MouseButtonEventArgs e)
    {
        TxtBaiduApiKey.Text = PwdBaiduApiKey.Password;
        PwdBaiduApiKey.Visibility = Visibility.Collapsed;
        TxtBaiduApiKey.Visibility = Visibility.Visible;
    }

    private void ShowBaiduApiKey_Up(object sender, MouseButtonEventArgs e)
    {
        PwdBaiduApiKey.Visibility = Visibility.Visible;
        TxtBaiduApiKey.Visibility = Visibility.Collapsed;
    }

    private void ShowBaiduSecretKey_Down(object sender, MouseButtonEventArgs e)
    {
        TxtBaiduSecretKey.Text = PwdBaiduSecretKey.Password;
        PwdBaiduSecretKey.Visibility = Visibility.Collapsed;
        TxtBaiduSecretKey.Visibility = Visibility.Visible;
    }

    private void ShowBaiduSecretKey_Up(object sender, MouseButtonEventArgs e)
    {
        PwdBaiduSecretKey.Visibility = Visibility.Visible;
        TxtBaiduSecretKey.Visibility = Visibility.Collapsed;
    }

    // Tencent
    private void PwdTencentSecretId_Changed(object sender, RoutedEventArgs e)
    {
        if (_suppressSync) return;
        var idx = LstOcr.SelectedIndex;
        if (idx < 0 || idx >= _ocrServices.Count) return;
        _ocrServices[idx].TencentSecretId = PwdTencentSecretId.Password;
        _suppressSync = true;
        TxtTencentSecretId.Text = PwdTencentSecretId.Password;
        _suppressSync = false;
    }

    private void PwdTencentSecretKey_Changed(object sender, RoutedEventArgs e)
    {
        if (_suppressSync) return;
        var idx = LstOcr.SelectedIndex;
        if (idx < 0 || idx >= _ocrServices.Count) return;
        _ocrServices[idx].TencentSecretKey = PwdTencentSecretKey.Password;
        _suppressSync = true;
        TxtTencentSecretKey.Text = PwdTencentSecretKey.Password;
        _suppressSync = false;
    }

    private void ShowTencentSecretId_Down(object sender, MouseButtonEventArgs e)
    {
        TxtTencentSecretId.Text = PwdTencentSecretId.Password;
        PwdTencentSecretId.Visibility = Visibility.Collapsed;
        TxtTencentSecretId.Visibility = Visibility.Visible;
    }

    private void ShowTencentSecretId_Up(object sender, MouseButtonEventArgs e)
    {
        PwdTencentSecretId.Visibility = Visibility.Visible;
        TxtTencentSecretId.Visibility = Visibility.Collapsed;
    }

    private void ShowTencentSecretKey_Down(object sender, MouseButtonEventArgs e)
    {
        TxtTencentSecretKey.Text = PwdTencentSecretKey.Password;
        PwdTencentSecretKey.Visibility = Visibility.Collapsed;
        TxtTencentSecretKey.Visibility = Visibility.Visible;
    }

    private void ShowTencentSecretKey_Up(object sender, MouseButtonEventArgs e)
    {
        PwdTencentSecretKey.Visibility = Visibility.Visible;
        TxtTencentSecretKey.Visibility = Visibility.Collapsed;
    }

    private void SelectOcrProvider(string provider)
    {
        foreach (ComboBoxItem item in CmbOcrProvider.Items)
        {
            if (string.Equals(item.Tag?.ToString(), provider, StringComparison.OrdinalIgnoreCase))
            {
                CmbOcrProvider.SelectedItem = item;
                return;
            }
        }
        CmbOcrProvider.SelectedIndex = 0;
    }

    private void CmbOcrProvider_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_suppressSync) return;
        var idx = LstOcr.SelectedIndex;
        if (idx >= 0 && idx < _ocrServices.Count)
        {
            var provider = (CmbOcrProvider.SelectedItem as ComboBoxItem)?.Tag?.ToString() ?? OcrProvider.Windows;
            _ocrServices[idx].Provider = provider;
        }
        UpdateOcrProviderVisibility();
    }

    private void UpdateOcrProviderVisibility()
    {
        if (PanelVolcengineOcr == null) return;
        var provider = (CmbOcrProvider.SelectedItem as ComboBoxItem)?.Tag?.ToString();
        PanelVolcengineOcr.Visibility = provider == OcrProvider.Volcengine ? Visibility.Visible : Visibility.Collapsed;
        PanelBaiduOcr.Visibility = provider == OcrProvider.Baidu ? Visibility.Visible : Visibility.Collapsed;
        PanelTencentOcr.Visibility = provider == OcrProvider.Tencent ? Visibility.Visible : Visibility.Collapsed;
    }

    private void BtnAddOcr_Click(object sender, RoutedEventArgs e)
    {
        _ocrServices.Add(new OcrServiceConfig { Name = $"OCR {_ocrServices.Count + 1}" });
        RefreshOcrList();
        LstOcr.SelectedIndex = _ocrServices.Count - 1;
    }

    private void BtnRemoveOcr_Click(object sender, RoutedEventArgs e)
    {
        var idx = LstOcr.SelectedIndex;
        if (idx < 0 || idx >= _ocrServices.Count) return;

        _ocrServices.RemoveAt(idx);
        if (_activeOcrIndex >= _ocrServices.Count)
            _activeOcrIndex = Math.Max(0, _ocrServices.Count - 1);
        else if (idx < _activeOcrIndex)
            _activeOcrIndex--;

        RefreshOcrList();
        if (_ocrServices.Count > 0)
            LstOcr.SelectedIndex = Math.Min(idx, _ocrServices.Count - 1);
        else
            ClearOcrFields();
    }

    private void BtnSetActiveOcr_Click(object sender, RoutedEventArgs e)
    {
        var idx = LstOcr.SelectedIndex;
        if (idx < 0 || idx >= _ocrServices.Count) return;
        _activeOcrIndex = idx;
        RefreshOcrList();
        LstOcr.SelectedIndex = idx;
    }

    private void LinkVolcengine_Click(object sender, RoutedEventArgs e)
    {
        Process.Start(new ProcessStartInfo("https://console.volcengine.com/ai/ability/info/78") { UseShellExecute = true });
    }

    private void LinkBaidu_Click(object sender, RoutedEventArgs e)
    {
        Process.Start(new ProcessStartInfo("https://console.bce.baidu.com/ai/#/ai/ocr/overview/index") { UseShellExecute = true });
    }

    private void LinkTencent_Click(object sender, RoutedEventArgs e)
    {
        Process.Start(new ProcessStartInfo("https://console.cloud.tencent.com/cam/capi") { UseShellExecute = true });
    }

    // ========== Hotkey ==========

    private void TxtHotkey_MouseDown(object sender, MouseButtonEventArgs e)
    {
        _isRecordingHotkey = true;
        if (Application.Current.MainWindow is MainWindow main) main.UnregisterCurrentHotkey();
        TxtHotkey.Text = "请按下快捷键组合...";
        TxtHotkey.Focus();
        e.Handled = true;
    }

    private void TxtHotkey_LostFocus(object sender, RoutedEventArgs e)
    {
        _isRecordingHotkey = false;
        if (Application.Current.MainWindow is MainWindow main) main.RegisterCurrentHotkey();
        var s = new AppSettings { HotkeyModifiers = _hotkeyModifiers, HotkeyKey = _hotkeyKey };
        TxtHotkey.Text = s.GetHotkeyDisplayString();
    }

    private void TxtHotkey_PreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (!_isRecordingHotkey) return;
        e.Handled = true;

        var key = e.Key == Key.System ? e.SystemKey : e.Key;

        if (key == Key.LeftCtrl || key == Key.RightCtrl ||
            key == Key.LeftAlt || key == Key.RightAlt ||
            key == Key.LeftShift || key == Key.RightShift ||
            key == Key.LWin || key == Key.RWin)
            return;

        uint modifiers = 0;
        if (Keyboard.Modifiers.HasFlag(ModifierKeys.Control)) modifiers |= 0x0002;
        if (Keyboard.Modifiers.HasFlag(ModifierKeys.Alt)) modifiers |= 0x0001;
        if (Keyboard.Modifiers.HasFlag(ModifierKeys.Shift)) modifiers |= 0x0004;
        if (Keyboard.Modifiers.HasFlag(ModifierKeys.Windows)) modifiers |= 0x0008;

        if (modifiers == 0) return;

        _hotkeyModifiers = modifiers;
        _hotkeyKey = (uint)KeyInterop.VirtualKeyFromKey(key);

        var s = new AppSettings { HotkeyModifiers = _hotkeyModifiers, HotkeyKey = _hotkeyKey };
        TxtHotkey.Text = s.GetHotkeyDisplayString();
        _isRecordingHotkey = false;
        Keyboard.ClearFocus();
    }

    // ========== Auto Start ==========

    private static void ApplyAutoStart(bool enable)
    {
        const string appName = "SnapLingo";
        using var key = Registry.CurrentUser.OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Run", true);
        if (key == null) return;

        if (enable)
        {
            var exePath = Environment.ProcessPath ?? "";
            key.SetValue(appName, $"\"{exePath}\"");
        }
        else
        {
            key.DeleteValue(appName, false);
        }
    }

    // ========== Prompt ==========

    private void BtnResetPrompt_Click(object sender, RoutedEventArgs e)
    {
        TxtPrompt.Text = AppSettings.DefaultTranslationPrompt;
    }

    // ========== About ==========

    private void LinkAuthor_Click(object sender, RoutedEventArgs e)
    {
        Process.Start(new ProcessStartInfo("https://github.com/zouchanglin/SnapLingo") { UseShellExecute = true });
    }

    // ========== Navigation ==========

    private void Nav_Checked(object sender, RoutedEventArgs e)
    {
        if (PageModels == null) return;
        PageModels.Visibility = Visibility.Collapsed;
        PageOcr.Visibility = Visibility.Collapsed;
        PagePrompt.Visibility = Visibility.Collapsed;
        PageHotkey.Visibility = Visibility.Collapsed;
        PageAbout.Visibility = Visibility.Collapsed;

        if (sender == NavModels) PageModels.Visibility = Visibility.Visible;
        else if (sender == NavOcr) PageOcr.Visibility = Visibility.Visible;
        else if (sender == NavPrompt) PagePrompt.Visibility = Visibility.Visible;
        else if (sender == NavHotkey) PageHotkey.Visibility = Visibility.Visible;
        else if (sender == NavAbout) PageAbout.Visibility = Visibility.Visible;
    }

    // ========== Title bar ==========

    private void TitleBar_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        DragMove();
    }

    // ========== Save / Cancel ==========

    private void BtnSave_Click(object sender, RoutedEventArgs e)
    {
        Result = new AppSettings
        {
            Models = _models,
            OcrServices = _ocrServices,
            ActiveOcrIndex = _activeOcrIndex,
            TranslationPrompt = TxtPrompt.Text,
            HotkeyModifiers = _hotkeyModifiers,
            HotkeyKey = _hotkeyKey,
            AutoStart = ChkAutoStart.IsChecked == true
        };
        Result.Save();
        ApplyAutoStart(Result.AutoStart);
        TxtSaveStatus.Text = "已保存 ✓";
        _ = ClearSaveStatusAsync();
    }

    private async Task ClearSaveStatusAsync()
    {
        await Task.Delay(2000);
        TxtSaveStatus.Text = "";
    }

    private void BtnCancel_Click(object sender, RoutedEventArgs e)
    {
        Close();
    }
}
