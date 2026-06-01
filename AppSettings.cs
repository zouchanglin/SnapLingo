using System.IO;
using System.Text.Json;
using System.Windows.Input;

namespace SnapLingo;

public class ModelConfig
{
    public string Name { get; set; } = "";
    public string Endpoint { get; set; } = "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";
    public string ApiKey { get; set; } = "";
    public string Model { get; set; } = "qwen-plus";
}

public class OcrServiceConfig
{
    public string Name { get; set; } = "";
    public string Provider { get; set; } = OcrProvider.Windows;
    public string VolcengineAccessKeyId { get; set; } = "";
    public string VolcengineSecretAccessKey { get; set; } = "";
}

public static class OcrProvider
{
    public const string Windows = "Windows";
    public const string Volcengine = "Volcengine";
}

public class AppSettings
{
    public const string DefaultTranslationPrompt = """
        请根据以下规则翻译文本：
        1. 如果文本主要是中文，翻译成英文
        2. 如果文本主要是英文或其他非中文语言，翻译成中文
        3. 只输出翻译结果，不要添加任何解释、注释或额外内容
        4. 保持原文的格式和换行

        {text}
        """;

    public List<ModelConfig> Models { get; set; } = new();
    public List<OcrServiceConfig> OcrServices { get; set; } = new();
    public int ActiveOcrIndex { get; set; } = 0;
    public string TranslationPrompt { get; set; } = DefaultTranslationPrompt;
    public uint HotkeyModifiers { get; set; } = 0x0006; // Ctrl+Shift
    public uint HotkeyKey { get; set; } = 0x41; // A

    // Legacy fields for migration
    public string? ApiKey { get; set; }
    public string? Endpoint { get; set; }
    public string? Model { get; set; }
    public OcrProviderConfig? Ocr { get; set; }

    private static readonly string SettingsDir = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "SnapLingo");

    private static readonly string SettingsFile = Path.Combine(SettingsDir, "settings.json");

    public static AppSettings Load()
    {
        try
        {
            if (File.Exists(SettingsFile))
            {
                var json = File.ReadAllText(SettingsFile);
                var settings = JsonSerializer.Deserialize<AppSettings>(json) ?? new AppSettings();
                settings.MigrateLegacy();
                return settings;
            }
        }
        catch { }
        return new AppSettings();
    }

    private void MigrateLegacy()
    {
        if (!string.IsNullOrWhiteSpace(ApiKey) && Models.Count == 0)
        {
            Models.Add(new ModelConfig
            {
                Name = "默认模型",
                Endpoint = Endpoint ?? "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions",
                ApiKey = ApiKey,
                Model = Model ?? "qwen-plus"
            });
            ApiKey = null;
            Endpoint = null;
            Model = null;
        }

        if (Ocr != null && OcrServices.Count == 0)
        {
            if (Ocr.Provider == OcrProvider.Volcengine)
            {
                OcrServices.Add(new OcrServiceConfig
                {
                    Name = "火山引擎 OCR",
                    Provider = OcrProvider.Volcengine,
                    VolcengineAccessKeyId = Ocr.VolcengineAccessKeyId,
                    VolcengineSecretAccessKey = Ocr.VolcengineSecretAccessKey
                });
            }
            else
            {
                OcrServices.Add(new OcrServiceConfig
                {
                    Name = "Windows 本地 OCR",
                    Provider = OcrProvider.Windows
                });
            }
            Ocr = null;
        }

        if (OcrServices.Count == 0)
        {
            OcrServices.Add(new OcrServiceConfig
            {
                Name = "Windows 本地 OCR",
                Provider = OcrProvider.Windows
            });
        }

        Save();
    }

    public OcrServiceConfig GetActiveOcr()
    {
        if (ActiveOcrIndex >= 0 && ActiveOcrIndex < OcrServices.Count)
            return OcrServices[ActiveOcrIndex];
        return OcrServices.Count > 0 ? OcrServices[0] : new OcrServiceConfig();
    }

    public void Save()
    {
        Directory.CreateDirectory(SettingsDir);
        var options = new JsonSerializerOptions
        {
            WriteIndented = true,
            DefaultIgnoreCondition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull
        };
        var json = JsonSerializer.Serialize(this, options);
        File.WriteAllText(SettingsFile, json);
    }

    public string GetHotkeyDisplayString()
    {
        var parts = new List<string>();
        if ((HotkeyModifiers & 0x0002) != 0) parts.Add("Ctrl");
        if ((HotkeyModifiers & 0x0001) != 0) parts.Add("Alt");
        if ((HotkeyModifiers & 0x0004) != 0) parts.Add("Shift");
        if ((HotkeyModifiers & 0x0008) != 0) parts.Add("Win");

        var key = KeyInterop.KeyFromVirtualKey((int)HotkeyKey);
        parts.Add(key.ToString());
        return string.Join("+", parts);
    }
}

// Legacy class kept for deserialization migration
public class OcrProviderConfig
{
    public string Provider { get; set; } = OcrProvider.Windows;
    public string VolcengineAccessKeyId { get; set; } = "";
    public string VolcengineSecretAccessKey { get; set; } = "";
}
