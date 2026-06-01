using System.Diagnostics;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;

namespace SnapLingo;

public partial class SettingsWindow : Window
{
    private uint _hotkeyModifiers;
    private uint _hotkeyKey;
    private bool _isRecordingHotkey;
    private readonly List<ModelConfig> _models;
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

        _hotkeyModifiers = settings.HotkeyModifiers;
        _hotkeyKey = settings.HotkeyKey;
        TxtHotkey.Text = settings.GetHotkeyDisplayString();
        TxtPrompt.Text = settings.TranslationPrompt;

        RefreshModelList();
        if (_models.Count > 0)
            LstModels.SelectedIndex = 0;
    }

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
        TxtModelApiKey.Text = m.ApiKey;
        TxtModelModel.Text = m.Model;
        _suppressSync = false;
    }

    private void ClearModelFields()
    {
        _suppressSync = true;
        TxtModelName.Text = "";
        TxtModelEndpoint.Text = "";
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

    // Hotkey
    private void TxtHotkey_MouseDown(object sender, MouseButtonEventArgs e)
    {
        _isRecordingHotkey = true;
        TxtHotkey.Text = "请按下快捷键组合...";
        TxtHotkey.Focus();
        e.Handled = true;
    }

    private void TxtHotkey_LostFocus(object sender, RoutedEventArgs e)
    {
        _isRecordingHotkey = false;
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

    // About
    private void LinkAuthor_Click(object sender, RoutedEventArgs e)
    {
        Process.Start(new ProcessStartInfo("https://github.com/zouchanglin") { UseShellExecute = true });
    }

    // Prompt
    private void BtnResetPrompt_Click(object sender, RoutedEventArgs e)
    {
        TxtPrompt.Text = AppSettings.DefaultTranslationPrompt;
    }

    // Navigation
    private void Nav_Checked(object sender, RoutedEventArgs e)
    {
        if (PageModels == null) return;
        PageModels.Visibility = Visibility.Collapsed;
        PagePrompt.Visibility = Visibility.Collapsed;
        PageHotkey.Visibility = Visibility.Collapsed;
        PageAbout.Visibility = Visibility.Collapsed;

        if (sender == NavModels) PageModels.Visibility = Visibility.Visible;
        else if (sender == NavPrompt) PagePrompt.Visibility = Visibility.Visible;
        else if (sender == NavHotkey) PageHotkey.Visibility = Visibility.Visible;
        else if (sender == NavAbout) PageAbout.Visibility = Visibility.Visible;
    }

    // Title bar
    private void TitleBar_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        DragMove();
    }

    // Save / Cancel
    private void BtnSave_Click(object sender, RoutedEventArgs e)
    {
        Result = new AppSettings
        {
            Models = _models,
            TranslationPrompt = TxtPrompt.Text,
            HotkeyModifiers = _hotkeyModifiers,
            HotkeyKey = _hotkeyKey
        };
        Result.Save();
        DialogResult = true;
        Close();
    }

    private void BtnCancel_Click(object sender, RoutedEventArgs e)
    {
        DialogResult = false;
        Close();
    }
}
