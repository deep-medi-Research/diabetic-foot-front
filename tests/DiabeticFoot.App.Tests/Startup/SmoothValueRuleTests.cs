using DiabeticFoot.App.Startup;

namespace DiabeticFoot.App.Tests.Startup;

public sealed class SmoothValueRuleTests
{
    // 늘어나는 값(장 수, 진행 비율)은 줄어드는 방향의 새 값(새 녹화로 0이 됨)을 따라가지 않고 바로 그 값이 됩니다.
    [Theory]
    [InlineData(true, 0.0, 300.0, true)]
    [InlineData(true, 306.0, 300.0, false)]
    [InlineData(true, 300.0, 300.0, false)]
    // 줄어드는 값(안정화 남은 프레임)은 반대로, 늘어나는 방향의 새 값(새 안정화로 75가 됨)에서 바로 그 값이 됩니다.
    [InlineData(false, 75.0, 0.0, true)]
    [InlineData(false, 30.0, 36.0, false)]
    [InlineData(false, 36.0, 36.0, false)]
    public void JumpsWhenValueMovesAgainstItsDirection(bool rising, double goal, double value, bool jump)
    {
        Assert.Equal(jump, SmoothValueRule.ShouldJump(rising, goal, value));
    }

    [Fact]
    public void DefaultDurationMatchesPollInterval()
    {
        Assert.Equal(TimeSpan.FromMilliseconds(200), SmoothValueRule.DefaultDuration);
    }
}
