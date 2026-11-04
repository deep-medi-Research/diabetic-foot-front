namespace DiabeticFoot.Core.Pixel;

/// <summary>영역 안 모든 화소의 밝기 합계입니다. 밝기는 UYVY와 NV12에서 Y, Y16에서 원시값입니다.</summary>
/// <param name="PixelCount">잰 화소 수입니다.</param>
/// <param name="Sum">밝기 합계입니다.</param>
/// <param name="ClippedPixels">밝기가 clip_level 이상인 화소 수입니다.</param>
public readonly record struct Brightness(long PixelCount = 0, long Sum = 0, long ClippedPixels = 0);

/// <summary>
/// 녹화한 프레임들의 영역 밝기가 프레임마다 얼마나 흔들리는지 요약한 값입니다.
/// </summary>
/// <remarks>
/// LED를 PWM으로 조절하면 PWM 주기와 노출 시간이 맞지 않을 때 노출 하나에 들어가는 켜짐 시간이 프레임마다 달라지고,
/// 프레임 밝기가 PWM 주파수와 fps의 차이로 정해지는 느린 주기(맥놀이)로 오르내립니다. 이 값으로 그 주기를 찾습니다.
/// 밝기 단위는 화소 하나의 밝기(UYVY와 NV12의 Y)입니다.
/// </remarks>
public readonly record struct Flicker
{
    public long FrameCount { get; init; }

    /// <summary>프레임 평균 밝기들의 평균입니다.</summary>
    public double Mean { get; init; }

    /// <summary>프레임 평균 밝기들의 표준편차(모집단)입니다.</summary>
    public double Stddev { get; init; }

    /// <summary>가장 밝은 프레임과 가장 어두운 프레임의 평균 밝기 차입니다.</summary>
    public double PeakToPeak { get; init; }

    /// <summary>가장 큰 주기 성분이 흔들림(직선 추세를 뺀 분산)에서 차지하는 비율입니다. 0에서 1 사이입니다.</summary>
    public double PeriodicFraction { get; init; }

    /// <summary>주된 주기(프레임 수, 소수)입니다. 주기 성분이 기준보다 약하면 비어 있습니다.</summary>
    public double? DominantPeriodFrames { get; init; }
}

/// <summary>LED 변경이 영상 밝기에 나타났는지 판정한 결과입니다. 사이드카 light.response에 남깁니다.</summary>
public enum LightResponse : byte
{
    /// <summary>판정하지 않음(기준값 없음, 프레임 부족, 보낸 밝기 변화가 0이거나 모름)입니다.</summary>
    Unknown,

    /// <summary>보낸 방향으로 기준 이상 바뀌었습니다.</summary>
    Applied,

    /// <summary>기준 이상 바뀌지 않았거나 반대 방향으로 바뀌었습니다.</summary>
    NotSeen,
}

/// <summary>
/// LED 변경의 반영 판정 기준입니다. LED 보드와 카메라 조합의 실측값이라 platform이 넘깁니다.
/// </summary>
/// <remarks>실측하지 않았습니다. WindowFrames나 MinMeanChange가 0 이하이면 판정하지 않습니다(<see cref="LightResponse.Unknown"/>).</remarks>
public readonly record struct LightResponseRule
{
    /// <summary>LED 명령 뒤 새 밝기로 보는 첫 프레임입니다(명령 뒤 받은 프레임을 1부터 셈).</summary>
    public int AppliedFrame { get; init; }

    /// <summary>명령 전과 반영 뒤에 각각 평균할 프레임 수입니다.</summary>
    public int WindowFrames { get; init; }

    /// <summary>반영으로 보는 평균 밝기(화소 하나의 Y) 변화의 최솟값입니다.</summary>
    public double MinMeanChange { get; init; }
}
