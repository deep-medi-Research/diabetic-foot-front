namespace DiabeticFoot.Core.Pixel;

/// <summary>발 하나가 따뜻한지의 판정입니다.</summary>
public enum Warmth : byte
{
    /// <summary>아직 <see cref="FootWarmthCheck.WarmFrames"/>장이 쌓이지 않았습니다.</summary>
    Checking,

    /// <summary>최근 <see cref="FootWarmthCheck.WarmFrames"/>장이 모두 <see cref="FootWarmthCheck.WarmContrastMin"/> 이상입니다.</summary>
    Warm,

    /// <summary>최근 <see cref="FootWarmthCheck.WarmFrames"/>장 중 <see cref="FootWarmthCheck.WarmContrastMin"/> 미만이 있습니다.</summary>
    Cold,
}

/// <summary>
/// 미리보기 열화상의 두 발 대비를 장마다 받아 발이 차가운지 판정합니다.
/// </summary>
/// <remarks>
/// 발 하나는 최근 <see cref="WarmFrames"/>장이 모두 <see cref="WarmContrastMin"/> 이상이면 따뜻함, 하나라도 미만이면 차가움입니다.
/// 장이 모자라면 확인 중입니다. 기준 근처에서 판정이 장마다 바뀌지 않게 하려는 것이고, 데우는 중인 발은 기준을 넘긴 상태가
/// 이어져야 따뜻함이 됩니다.
/// </remarks>
public sealed class FootWarmthCheck
{
    /// <summary>발이 따뜻하다고 보는 열화상 대비의 아래 끝(원시값)입니다.</summary>
    /// <remarks>
    /// 관류 분석 프로그램 v8 3a9d830 <c>pad_clf/quality.py</c>의 WARM_COUNTS와 같은 값입니다. 녹화 146건에서 이 값보다 작으면
    /// 맥박 진폭이 급격히 줄었습니다. 열화상 장치(TE-EV1)의 원시값이라 섭씨로 바꿀 수 없고(#49), 렌즈 셰이딩이 틀어지면(#39)
    /// 대비도 흔들려 잠정값입니다.
    /// </remarks>
    public const int WarmContrastMin = 60;

    /// <summary>판정에 쓰는 미리보기 장 수입니다.</summary>
    /// <remarks>
    /// 관류 분석 프로그램은 녹화 첫 2초(9장/초 녹화의 18장) 평균 영상으로 대비를 구합니다. 미리보기는 약 10장/초라
    /// 같은 2초인 20장을 봅니다. 노트북 미리보기의 흔들림을 재서 바꿀 수 있습니다.
    /// </remarks>
    public const int WarmFrames = 20;

    private readonly Queue<(int Left, int Right)> _recent = new();

    /// <summary>쌓은 장을 지웁니다. 발 상자가 바뀌거나 열화상이 끊기면 부릅니다.</summary>
    public void Reset() => _recent.Clear();

    /// <summary>한 장의 두 발 대비를 넣습니다.</summary>
    /// <param name="left">화소 왼쪽 상자의 <see cref="PixelFunctions.ThermalContrast"/></param>
    /// <param name="right">화소 오른쪽 상자의 <see cref="PixelFunctions.ThermalContrast"/></param>
    public void Push(int left, int right)
    {
        _recent.Enqueue((left, right));
        while (_recent.Count > WarmFrames) _recent.Dequeue();
    }

    /// <summary>화소 왼쪽(0), 오른쪽(1) 상자의 판정입니다.</summary>
    public Warmth State(int side)
    {
        if (_recent.Count < WarmFrames) return Warmth.Checking;
        return Lowest(side) >= WarmContrastMin ? Warmth.Warm : Warmth.Cold;
    }

    /// <summary>화소 왼쪽(0), 오른쪽(1) 상자의 최근 <see cref="WarmFrames"/>장 중 가장 작은 대비입니다. 장이 없으면 0입니다.</summary>
    public int Lowest(int side)
    {
        if (_recent.Count == 0 || side < 0 || side > 1) return 0;
        int lowest = Contrast(_recent.Peek(), side);
        foreach (var c in _recent) lowest = Math.Min(lowest, Contrast(c, side));
        return lowest;
    }

    private static int Contrast((int Left, int Right) frame, int side) => side == 0 ? frame.Left : frame.Right;
}

public static partial class PixelFunctions
{
    /// <summary>발 상자 안 열화상 대비입니다. 상위 10%(발바닥)와 하위 25%(배경)의 원시값 차이입니다.</summary>
    /// <param name="y16">Y16 열화상 프레임</param>
    /// <param name="rect">발 상자(프레임 화소)</param>
    /// <returns>대비(원시값). 상자가 비었거나 형식이 Y16이 아니면 0입니다</returns>
    /// <remarks>
    /// 관류 분석 프로그램 <c>quality.thermal_contrast</c>와 같은 식입니다. 발을 분할하지 않고, 분할한 발 - 배경 대비와 같은 값이라고
    /// 연구 쪽이 보였습니다(녹화 33건, r 0.995). 상자를 발에 바짝 줄이면 배경이 빠져 값이 작아집니다.
    /// </remarks>
    public static int ThermalContrast(FrameView y16, Rect rect)
    {
        Y16Range range = FindPercentileRange(y16, rect, 250, 900);
        return (int)range.High - (int)range.Low;
    }
}
