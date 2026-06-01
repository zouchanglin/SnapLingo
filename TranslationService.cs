using System.IO;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;

namespace SnapLingo;

public static class TranslationService
{
    private static readonly HttpClient HttpClient = new(new HttpClientHandler
    {
        AutomaticDecompression = DecompressionMethods.None
    });

    public static async IAsyncEnumerable<string> TranslateStreamAsync(ModelConfig config, string text, string? prompt = null)
    {
        if (string.IsNullOrWhiteSpace(text))
            yield break;

        if (string.IsNullOrWhiteSpace(config.ApiKey))
        {
            yield return "[请先在设置中配置 API Key]";
            yield break;
        }

        var effectivePrompt = prompt ?? AppSettings.DefaultTranslationPrompt;
        var userMessage = effectivePrompt.Replace("{text}", text);

        var request = new
        {
            model = config.Model,
            stream = true,
            enable_thinking = false,
            messages = new[]
            {
                new { role = "user", content = userMessage }
            }
        };

        var json = JsonSerializer.Serialize(request);
        using var httpRequest = new HttpRequestMessage(HttpMethod.Post, config.Endpoint);
        httpRequest.Headers.Authorization = new AuthenticationHeaderValue("Bearer", config.ApiKey);
        httpRequest.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue("text/event-stream"));
        httpRequest.Content = new StringContent(json, Encoding.UTF8, "application/json");

        using var response = await HttpClient.SendAsync(httpRequest, HttpCompletionOption.ResponseHeadersRead);

        if (!response.IsSuccessStatusCode)
        {
            var errorBody = await response.Content.ReadAsStringAsync();
            yield return $"[翻译失败: {response.StatusCode} - {errorBody}]";
            yield break;
        }

        using var stream = await response.Content.ReadAsStreamAsync();
        using var reader = new StreamReader(stream, Encoding.UTF8, leaveOpen: false, bufferSize: 256);

        while (!reader.EndOfStream)
        {
            var line = await reader.ReadLineAsync();
            if (string.IsNullOrEmpty(line)) continue;
            if (!line.StartsWith("data: ")) continue;

            var data = line["data: ".Length..];
            if (data == "[DONE]") break;

            string? content = null;
            try
            {
                using var doc = JsonDocument.Parse(data);
                content = doc.RootElement
                    .GetProperty("choices")[0]
                    .GetProperty("delta")
                    .GetProperty("content")
                    .GetString();
            }
            catch
            {
            }

            if (!string.IsNullOrEmpty(content))
                yield return content;
        }
    }
}
