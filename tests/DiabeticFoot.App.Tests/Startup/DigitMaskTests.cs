using DiabeticFoot.App.Startup;

namespace DiabeticFoot.App.Tests.Startup;

public sealed class DigitMaskTests
{
    // 숫자만 뽑아 패턴에 맞추고, 입력하는 중에는 다음 숫자가 올 때까지 구분 글자를 붙이지 않습니다.
    [Theory]
    [InlineData("0000-00-00", "19600101", "1960-01-01")]
    [InlineData("0000-00-00", "1960", "1960")]
    [InlineData("0000-00-00", "19601", "1960-1")]
    [InlineData("0000-00-00", "1960-0", "1960-0")]
    [InlineData("0000-00-00", "1960-01-0", "1960-01-0")]
    [InlineData("0000-00-00", "1960-  -  ", "1960")]
    [InlineData("0000-00-00", "a1b9c6d0", "1960")]
    [InlineData("0000-00-00", "196001019999", "1960-01-01")]
    [InlineData("0000-00-00", "", "")]
    [InlineData("0000-00-00", "---", "")]
    [InlineData("000", "12 ", "12")]
    [InlineData("000", "1a2b3c4", "123")]
    [InlineData("000", "٣٤", "")]
    public void FormatsDigitsIntoPattern(string pattern, string text, string expected)
    {
        Assert.Equal(expected, DigitMask.Format(pattern, text));
    }

    // 칸이 글자를 다시 쓴 뒤에도 캐럿은 같은 숫자 개수 뒤에 있습니다.
    [Theory]
    [InlineData("1960-01-01", 4, 4)]
    [InlineData("1960-01-01", 5, 4)]
    [InlineData("19601", 5, 6)]
    [InlineData("1960-01-01", 0, 0)]
    [InlineData("1960-01-01", 10, 10)]
    public void KeepsCaretAfterSameNumberOfDigits(string typed, int caret, int expectedAfterFormat)
    {
        var formatted = DigitMask.Format("0000-00-00", typed);

        var digits = DigitMask.DigitsBefore(typed, caret);

        Assert.Equal(expectedAfterFormat, DigitMask.CaretAfterDigits(formatted, digits));
    }

    [Fact]
    public void CaretAfterDigitsHandlesEdges()
    {
        Assert.Equal(0, DigitMask.CaretAfterDigits("1960-01", 0));
        Assert.Equal(4, DigitMask.CaretAfterDigits("1960-01", 4));
        Assert.Equal(7, DigitMask.CaretAfterDigits("1960-01", 6));
        Assert.Equal(7, DigitMask.CaretAfterDigits("1960-01", 99));
        Assert.Equal(1, DigitMask.DigitsBefore("a1b2", 3));
        Assert.Equal(2, DigitMask.DigitsBefore("a1b2", 99));
    }
}
