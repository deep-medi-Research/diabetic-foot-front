using System.Globalization;
using System.Text;
using System.Text.Json;

namespace DiabeticFoot.Platform;

/// <summary>JSON 스칼라 값의 종류입니다.</summary>
internal enum JsonKind
{
    String,
    Number,
    True,
    False,
    Null,
    /// <summary>파이썬 json.dump가 쓰는 NaN, Infinity, -Infinity입니다.</summary>
    NonFinite,
}

/// <summary>JSON 스칼라 값 하나입니다. Text는 문자열이면 이스케이프를 푼 값, 숫자이면 원문입니다.</summary>
internal readonly record struct JsonScalar(JsonKind Kind, string Text);

/// <summary>
/// 관류 분석 프로그램 출력 줄, 요약 JSON, 매니페스트에서 값 몇 개만 꺼내는 읽기입니다.
/// 문법이 틀린 문서는 어디를 찾든 읽지 못한 것으로 봅니다.
/// </summary>
internal sealed class JsonReader
{
    // 파이썬 json.dump가 찍는 NaN과 무한대는 JSON 문법이 아니라서, 문자열 밖에서만 이 표식 문자열로 바꿔 읽습니다.
    private const string NonFiniteMarker = "non-finite";

    private static readonly JsonDocumentOptions Options = new()
    {
        AllowTrailingCommas = false,
        CommentHandling = JsonCommentHandling.Disallow,
        MaxDepth = 64,
    };

    private readonly string _text;

    public JsonReader(string text) => _text = text;

    /// <summary>값이 모두 스칼라인 JSON 객체의 키와 값(참거짓은 "true"/"false", null과 NaN은 빈 문자열)을 읽습니다.</summary>
    public Dictionary<string, string>? Read()
    {
        return Parse(root =>
        {
            if (root.ValueKind != JsonValueKind.Object) return null;
            var members = new Dictionary<string, string>(StringComparer.Ordinal);
            foreach (var property in root.EnumerateObject())
            {
                if (!TryScalar(property.Value, out var scalar)) return null;
                members[property.Name] = scalar.Text;
            }
            return members;
        });
    }

    /// <summary>문서 전체를 읽으며 키 경로가 가리키는 문자열 값을 찾습니다.</summary>
    public string? FindString(params string[] path)
    {
        var found = FindScalar(path);
        return found is { Kind: JsonKind.String } scalar ? scalar.Text : null;
    }

    /// <summary>
    /// 문서 전체를 읽으며 키 경로가 가리키는 스칼라 값을 찾습니다. 경로가 배열을 지나면 "0", "1" 같은 번호로 원소를 고릅니다.
    /// 문법이 틀리거나 경로가 객체, 배열을 가리키거나 없으면 null입니다.
    /// </summary>
    public JsonScalar? FindScalar(params string[] path)
    {
        return Parse(root =>
        {
            var node = Descend(root, path);
            return node is { } element && TryScalar(element, out var scalar) ? scalar : (JsonScalar?)null;
        });
    }

    /// <summary>문서 전체를 읽으며 키 경로가 가리키는 배열의 스칼라 원소를 찾습니다. 원소에 객체나 배열이 있으면 null입니다.</summary>
    public List<JsonScalar>? FindArray(params string[] path)
    {
        return Parse(root =>
        {
            if (Descend(root, path) is not { ValueKind: JsonValueKind.Array } array) return null;
            var items = new List<JsonScalar>();
            foreach (var element in array.EnumerateArray())
            {
                if (!TryScalar(element, out var scalar)) return null;
                items.Add(scalar);
            }
            return items;
        });
    }

    private T? Parse<T>(Func<JsonElement, T?> read)
    {
        try
        {
            using var document = JsonDocument.Parse(QuoteNonFinite(_text), Options);
            return read(document.RootElement);
        }
        catch (Exception e) when (e is JsonException or InvalidOperationException or ArgumentException)
        {
            return default;
        }
    }

    // 같은 이름이 되풀이되면 마지막 값을 씁니다.
    private static JsonElement? Descend(JsonElement node, string[] path)
    {
        foreach (var step in path)
        {
            switch (node.ValueKind)
            {
                case JsonValueKind.Object:
                    JsonElement? last = null;
                    foreach (var property in node.EnumerateObject())
                    {
                        if (property.NameEquals(step)) last = property.Value;
                    }
                    if (last is null) return null;
                    node = last.Value;
                    break;
                case JsonValueKind.Array:
                    if (!int.TryParse(step, NumberStyles.None, CultureInfo.InvariantCulture, out var index) ||
                        index.ToString(CultureInfo.InvariantCulture) != step ||
                        index >= node.GetArrayLength())
                    {
                        return null;
                    }
                    node = node[index];
                    break;
                default:
                    return null;
            }
        }
        return node;
    }

    private static bool TryScalar(JsonElement element, out JsonScalar scalar)
    {
        switch (element.ValueKind)
        {
            case JsonValueKind.String:
                var text = element.GetString() ?? "";
                scalar = text == NonFiniteMarker ? new JsonScalar(JsonKind.NonFinite, "") : new JsonScalar(JsonKind.String, text);
                return true;
            case JsonValueKind.Number:
                scalar = new JsonScalar(JsonKind.Number, element.GetRawText());
                return true;
            case JsonValueKind.True:
                scalar = new JsonScalar(JsonKind.True, "true");
                return true;
            case JsonValueKind.False:
                scalar = new JsonScalar(JsonKind.False, "false");
                return true;
            case JsonValueKind.Null:
                scalar = new JsonScalar(JsonKind.Null, "");
                return true;
            default:
                scalar = default;
                return false;
        }
    }

    private static string QuoteNonFinite(string text)
    {
        if (!text.Contains("NaN", StringComparison.Ordinal) && !text.Contains("Infinity", StringComparison.Ordinal)) return text;
        var quoted = new StringBuilder(text.Length + 16);
        var inString = false;
        for (var i = 0; i < text.Length; i++)
        {
            var c = text[i];
            if (inString)
            {
                quoted.Append(c);
                if (c == '\\' && i + 1 < text.Length) quoted.Append(text[++i]);
                else if (c == '"') inString = false;
                continue;
            }
            if (c == '"')
            {
                inString = true;
                quoted.Append(c);
                continue;
            }
            var word = c switch
            {
                'N' when At(text, i, "NaN") => "NaN",
                'I' when At(text, i, "Infinity") => "Infinity",
                '-' when At(text, i, "-Infinity") => "-Infinity",
                _ => null,
            };
            if (word is null)
            {
                quoted.Append(c);
                continue;
            }
            quoted.Append('"').Append(NonFiniteMarker).Append('"');
            i += word.Length - 1;
        }
        return quoted.ToString();
    }

    private static bool At(string text, int index, string word) =>
        string.CompareOrdinal(text, index, word, 0, word.Length) == 0;
}
