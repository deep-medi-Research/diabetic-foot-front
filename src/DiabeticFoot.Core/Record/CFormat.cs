using System.Globalization;
using System.Numerics;
using System.Text;

namespace DiabeticFoot.Core.Record;

/// <summary>
/// 원본이 snprintf로 만든 숫자 문자열을 같은 바이트로 만드는 도우미입니다. 로캘과 플랫폼에 관계없이 같은 값에는 같은 결과를 냅니다.
/// </summary>
internal static class CFormat
{
    /// <summary>
    /// <c>%.{digits}f</c>입니다. 이진 값의 정확한 십진 값을 소수점 아래 digits자리로 반올림하며, 꼭 절반인 값은 짝수 쪽입니다
    /// (glibc와 Windows UCRT 2004 이후의 결과). 음수가 반올림해 0이 되어도 부호를 남깁니다("-0.000").
    /// </summary>
    public static string Fixed(double value, int digits)
    {
        if (double.IsNaN(value))
        {
            return double.IsNegative(value) ? "-nan" : "nan";
        }
        if (double.IsInfinity(value))
        {
            return value < 0 ? "-inf" : "inf";
        }
        long bits = BitConverter.DoubleToInt64Bits(value);
        bool negative = bits < 0;
        int exponent = (int)((bits >> 52) & 0x7FF);
        long fraction = bits & 0xFFFFFFFFFFFFFL;
        BigInteger mantissa = exponent == 0 ? fraction : (fraction | (1L << 52));
        int shift = (exponent == 0 ? 1 : exponent) - 1075;

        BigInteger scaled = mantissa * BigInteger.Pow(10, digits);
        BigInteger rounded;
        if (shift >= 0)
        {
            rounded = scaled << shift;
        }
        else
        {
            int right = -shift;
            BigInteger quotient = scaled >> right;
            BigInteger remainder = scaled - (quotient << right);
            BigInteger half = BigInteger.One << (right - 1);
            int compare = remainder.CompareTo(half);
            rounded = compare > 0 || (compare == 0 && !quotient.IsEven) ? quotient + 1 : quotient;
        }

        string text = rounded.ToString(CultureInfo.InvariantCulture).PadLeft(digits + 1, '0');
        var result = new StringBuilder();
        if (negative)
        {
            result.Append('-');
        }
        if (digits == 0)
        {
            return result.Append(text).ToString();
        }
        return result.Append(text, 0, text.Length - digits).Append('.').Append(text, text.Length - digits, digits).ToString();
    }

    /// <summary><c>%0{width}d</c>입니다. 부호도 자릿수에 셉니다(-1을 %03d로 쓰면 "-01").</summary>
    public static string ZeroPad(long value, int width)
    {
        string digits = Math.Abs((decimal)value).ToString(CultureInfo.InvariantCulture);
        if (value < 0)
        {
            return "-" + digits.PadLeft(Math.Max(width - 1, 0), '0');
        }
        return digits.PadLeft(width, '0');
    }

    /// <summary><c>\\u%04x</c> 같은 소문자 16진수입니다.</summary>
    public static string HexLower(uint value, int width) => value.ToString("x" + width.ToString(CultureInfo.InvariantCulture), CultureInfo.InvariantCulture);

    /// <summary><c>%04X</c> 같은 대문자 16진수입니다.</summary>
    public static string HexUpper(uint value, int width) => value.ToString("X" + width.ToString(CultureInfo.InvariantCulture), CultureInfo.InvariantCulture);

    public static string Int(long value) => value.ToString(CultureInfo.InvariantCulture);
}
