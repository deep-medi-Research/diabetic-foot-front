using System.Text;

namespace DiabeticFoot.Core.Record;

/// <summary>
/// 들여쓰기 2칸의 JSON을 씁니다. 사람이 열어 확인하는 파일이라 한 줄에 값 하나를 둡니다. 키 순서는 호출 순서입니다.
/// </summary>
internal sealed class JsonWriter
{
    private readonly StringBuilder _out = new();
    private int _depth;
    private bool _first = true;

    public void Open(string? key, char bracket)
    {
        Key(key);
        _out.Append(bracket);
        _first = true;
        _depth++;
    }

    public void Close(char bracket)
    {
        _depth--;
        if (_first)
        {
            _out.Append(bracket);
            _first = false;
            return;
        }
        _out.Append('\n');
        Indent();
        _out.Append(bracket);
        _first = false;
    }

    public void String(string key, string value)
    {
        Key(key);
        AppendEscaped(_out, value);
    }

    public void Int(string key, long value)
    {
        Key(key);
        _out.Append(CFormat.Int(value));
    }

    public void Null(string key)
    {
        Key(key);
        _out.Append("null");
    }

    public void OptionalBool(string key, bool? value)
    {
        Key(key);
        _out.Append(value.HasValue ? (value.Value ? "true" : "false") : "null");
    }

    public void OptionalInt(string key, int? value)
    {
        Key(key);
        _out.Append(value.HasValue ? CFormat.Int(value.Value) : "null");
    }

    // 소수점 아래 자릿수를 고정해 같은 값에는 플랫폼과 관계없이 같은 바이트를 냅니다.
    public void Fixed(string key, double value, int digits)
    {
        Key(key);
        _out.Append(CFormat.Fixed(value, digits));
    }

    // 숫자 배열은 한 줄에 씁니다. 900개 값을 줄마다 나누면 파일을 사람이 읽기 어렵습니다.
    public void IntArray(string key, int count, Func<int, long> get)
    {
        Key(key);
        _out.Append('[');
        for (int i = 0; i < count; i++)
        {
            if (i > 0)
            {
                _out.Append(", ");
            }
            _out.Append(CFormat.Int(get(i)));
        }
        _out.Append(']');
    }

    public string Take()
    {
        _out.Append('\n');
        return _out.ToString();
    }

    private void Key(string? key)
    {
        if (_depth > 0)
        {
            _out.Append(_first ? "\n" : ",\n");
            Indent();
        }
        _first = false;
        if (key != null)
        {
            AppendEscaped(_out, key);
            _out.Append(": ");
        }
    }

    private void Indent() => _out.Append(' ', _depth * 2);

    private static void AppendEscaped(StringBuilder output, string text)
    {
        output.Append('"');
        foreach (char c in text)
        {
            switch (c)
            {
                case '"':
                    output.Append("\\\"");
                    break;
                case '\\':
                    output.Append("\\\\");
                    break;
                case '\n':
                    output.Append("\\n");
                    break;
                case '\r':
                    output.Append("\\r");
                    break;
                case '\t':
                    output.Append("\\t");
                    break;
                default:
                    if (c < 0x20)
                    {
                        output.Append("\\u").Append(CFormat.HexLower(c, 4));
                    }
                    else
                    {
                        // 한글 파일 이름을 이스케이프 없이 읽을 수 있게 하기 위해 UTF-8 문자는 그대로 둡니다.
                        output.Append(c);
                    }
                    break;
            }
        }
        output.Append('"');
    }
}
