using System.IO;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;
using System.Runtime.CompilerServices;

namespace SnapLingo;

public static class TranslationService
{
    private static readonly HttpClient HttpClient = new(new HttpClientHandler
    {
        AutomaticDecompression = DecompressionMethods.None
    });

    public static async IAsyncEnumerable<string> TranslateStreamAsync(ModelConfig config, string text, string? prompt = null,
        [EnumeratorCancellation] CancellationToken cancellationToken = default)
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

        var json = CreateRequestJson(config, userMessage);
        using var httpRequest = new HttpRequestMessage(HttpMethod.Post, config.Endpoint);
        httpRequest.Headers.Authorization = new AuthenticationHeaderValue("Bearer", config.ApiKey);
        httpRequest.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue("text/event-stream"));
        httpRequest.Content = new StringContent(json, Encoding.UTF8, "application/json");

        using var response = await HttpClient.SendAsync(httpRequest, HttpCompletionOption.ResponseHeadersRead, cancellationToken);

        if (!response.IsSuccessStatusCode)
        {
            var errorBody = await response.Content.ReadAsStringAsync(cancellationToken);
            yield return $"[翻译失败: {response.StatusCode} - {errorBody}]";
            yield break;
        }

        using var stream = await response.Content.ReadAsStreamAsync(cancellationToken);
        using var reader = new StreamReader(stream, Encoding.UTF8, leaveOpen: false, bufferSize: 256);

        while (true)
        {
            var line = await reader.ReadLineAsync(cancellationToken);
            if (line == null) break;
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

    internal static string CreateRequestJson(ModelConfig config, string userMessage)
    {
        var request = new Dictionary<string, object>
        {
            ["model"] = config.Model,
            ["stream"] = true,
            ["messages"] = new[] { new { role = "user", content = userMessage } }
        };
        // DeepSeek uses thinking.type, including when accessed through a compatible proxy.
        var isDeepSeek = config.Model.StartsWith("deepseek-", StringComparison.OrdinalIgnoreCase)
            || (Uri.TryCreate(config.Endpoint, UriKind.Absolute, out var endpoint)
                && endpoint.Host.Equals("api.deepseek.com", StringComparison.OrdinalIgnoreCase));
        if (isDeepSeek)
            request["thinking"] = new { type = config.EnableThinking ? "enabled" : "disabled" };
        else
            request["enable_thinking"] = config.EnableThinking;
        return JsonSerializer.Serialize(request);
    }
}
