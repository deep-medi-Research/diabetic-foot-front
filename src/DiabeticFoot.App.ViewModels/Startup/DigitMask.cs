namespace DiabeticFoot.App.Startup;

/// <summary>
/// 숫자 입력 칸의 모양(마스크)입니다. 패턴의 '0'은 숫자 한 자리이고 나머지 글자는 그대로 들어가는 구분 글자입니다
/// (생년월일은 "0000-00-00", 혈압은 "000"). 입력 중에는 칸이 비어 있어도 구분 글자를 미리 채우지 않습니다
/// ("1960"까지 쓰면 "1960", 다음 숫자를 쓰면 "1960-1").
/// </summary>
public static class DigitMask
{
    /// <summary>입력 글자에서 숫자만 뽑아 패턴에 맞춥니다. 패턴보다 많은 숫자는 버립니다.</summary>
    public static string Format(string pattern, string text)
    {
        var result = new System.Text.StringBuilder();
        var next = 0;
        var pendingLiteral = new System.Text.StringBuilder();
        foreach (var slot in pattern)
        {
            if (slot != '0')
            {
                pendingLiteral.Append(slot);
                continue;
            }
            while (next < text.Length && !char.IsAsciiDigit(text[next])) next++;
            if (next >= text.Length) break;
            result.Append(pendingLiteral).Append(text[next++]);
            pendingLiteral.Clear();
        }
        return result.ToString();
    }

    /// <summary>
    /// 편집한 글자에서 캐럿 앞의 숫자 개수를 셉니다. 칸이 글자를 다시 쓴 뒤 같은 숫자 뒤로 캐럿을 되돌리는 데 씁니다.
    /// </summary>
    public static int DigitsBefore(string text, int caret) => text.Take(Math.Clamp(caret, 0, text.Length)).Count(char.IsAsciiDigit);

    /// <summary>다듬은 글자에서 숫자 <paramref name="digits"/>개 바로 뒤의 위치입니다. 0이면 맨 앞입니다.</summary>
    public static int CaretAfterDigits(string formatted, int digits)
    {
        if (digits <= 0) return 0;
        var seen = 0;
        for (var i = 0; i < formatted.Length; i++)
        {
            if (char.IsAsciiDigit(formatted[i]) && ++seen == digits) return i + 1;
        }
        return formatted.Length;
    }
}
