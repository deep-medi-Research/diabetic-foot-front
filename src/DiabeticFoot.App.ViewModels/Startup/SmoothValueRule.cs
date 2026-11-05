namespace DiabeticFoot.App.Startup;

/// <summary>
/// 0.2초마다 바뀌는 값(촬영 장 수, 안정화 남은 프레임, 분석 진행 비율)을 화면에서 끊기지 않게 보이는 값(SmoothValue)의 규칙입니다.
/// 새 값이 오면 다음 값이 올 때까지 직선으로 따라가고, 정한 방향과 반대로 바뀌면(새 녹화로 장 수가 0이 됨 등) 따라가지 않고 바로 그 값이 됩니다.
/// 애니메이션은 화면(SmoothValue 컨트롤)이 하고, 어느 쪽으로 할지만 여기서 정합니다.
/// </summary>
public static class SmoothValueRule
{
    /// <summary>다음 값이 오는 간격입니다. 촬영과 분석이 상태를 읽는 간격(0.2초)과 같습니다.</summary>
    public static readonly TimeSpan DefaultDuration = TimeSpan.FromMilliseconds(200);

    /// <summary>따라가지 않고 바로 목표 값으로 가야 하면 true입니다.</summary>
    /// <param name="rising">값이 늘어나는 쪽이면 true(장 수, 진행 비율), 줄어드는 쪽이면 false(남은 프레임)</param>
    /// <param name="goal">새 목표</param>
    /// <param name="value">지금 화면에 쓰는 값</param>
    public static bool ShouldJump(bool rising, double goal, double value) => rising ? goal < value : goal > value;
}
