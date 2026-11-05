using static DiabeticFoot.Core.Timing.TimingFunctions;

namespace DiabeticFoot.Core.Tests.Timing;

public class WindowIndexTests
{
    private const long SecondUs = 1_000_000;
    private const long WindowUs = 10 * SecondUs;
    private const long StrideUs = 2 * SecondUs;

    // 관류 분석 프로그램 기본값(창 10초, 간격 2초, 창 11개)에서 첫 창의 가운데(5초)까지는 창 0입니다.
    [Fact]
    public void BeforeFirstCentreIsFirstWindow()
    {
        Assert.Equal(0, WindowIndexAt(-SecondUs, 11, WindowUs, StrideUs));
        Assert.Equal(0, WindowIndexAt(0, 11, WindowUs, StrideUs));
        Assert.Equal(0, WindowIndexAt(5 * SecondUs, 11, WindowUs, StrideUs));
    }

    // 두 창 가운데의 정확히 중간(6초)은 앞 창, 그보다 1마이크로초 뒤는 다음 창입니다.
    [Fact]
    public void TieGoesToEarlierWindow()
    {
        Assert.Equal(0, WindowIndexAt(6 * SecondUs - 1, 11, WindowUs, StrideUs));
        Assert.Equal(0, WindowIndexAt(6 * SecondUs, 11, WindowUs, StrideUs));
        Assert.Equal(1, WindowIndexAt(6 * SecondUs + 1, 11, WindowUs, StrideUs));
        Assert.Equal(1, WindowIndexAt(7 * SecondUs, 11, WindowUs, StrideUs));
    }

    // 마지막 창의 가운데(25초) 뒤와 녹화 끝(30초)은 마지막 창입니다.
    [Fact]
    public void AfterLastCentreIsLastWindow()
    {
        Assert.Equal(10, WindowIndexAt(25 * SecondUs, 11, WindowUs, StrideUs));
        Assert.Equal(10, WindowIndexAt(30 * SecondUs, 11, WindowUs, StrideUs));
        Assert.Equal(10, WindowIndexAt(1000 * SecondUs, 11, WindowUs, StrideUs));
    }

    // 창 길이가 홀수 마이크로초여도 가운데를 반올림하지 않고 비교합니다.
    [Fact]
    public void OddWindowLengthKeepsHalfMicrosecond()
    {
        // 창 3us, 간격 2us: 가운데 1.5, 3.5. 2.5는 같은 거리라 앞 창, 2.5보다 뒤인 3은 다음 창입니다.
        Assert.Equal(0, WindowIndexAt(2, 2, 3, 2));
        Assert.Equal(1, WindowIndexAt(3, 2, 3, 2));
    }

    // 창이 하나이면 항상 0, 창 수나 길이나 간격이 맞지 않으면 값이 없습니다.
    [Fact]
    public void InvalidInputsHaveNoIndex()
    {
        Assert.Equal(0, WindowIndexAt(123, 1, 0, 0));
        Assert.Null(WindowIndexAt(0, 0, WindowUs, StrideUs));
        Assert.Null(WindowIndexAt(0, -1, WindowUs, StrideUs));
        Assert.Null(WindowIndexAt(0, 11, 0, StrideUs));
        Assert.Null(WindowIndexAt(0, 11, WindowUs, 0));
    }
}
