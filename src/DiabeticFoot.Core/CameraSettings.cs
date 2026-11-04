namespace DiabeticFoot.Core;

/// <summary>
/// 설정값의 종류입니다. 종류마다 새 값이 반영되는 프레임이 다릅니다(<c>SettleRule</c>).
/// </summary>
/// <remarks>
/// <see cref="Light"/>는 카메라 설정이 아니라 LED 조명 밝기입니다. 영상 밝기를 바꾸므로 카메라 설정 변경과 같은 사건으로 다루어
/// 안정화를 다시 기다리고, 녹화 중에 오면 녹화를 끝냅니다.
/// </remarks>
public enum SettingKind : byte
{
    Exposure,
    Gain,
    WhiteBalance,
    Light,
}

/// <summary>촬영 전 설정값을 정하는 방식입니다.</summary>
public enum SettingMode : byte
{
    /// <summary>펌웨어 자동 노출을 촬영 버튼에서 잠급니다.</summary>
    Auto,

    /// <summary>사용자가 입력한 값입니다.</summary>
    Manual,
}

/// <summary>
/// 카메라 설정 전체입니다.
/// </summary>
/// <remarks>
/// 두 가지로 씁니다. 카메라에서 다시 읽은 값으로 쓰면 빈 항목은 읽지 못한 값이고, 목표 상태로 쓰면 빈 항목은 바꾸지 않을 값입니다.
/// 펌웨어 자동 노출 중에는 실제 노출값을 읽을 방법이 없어 <see cref="ExposureStep"/>이 비어 있습니다(2026-10-16 실측).
/// </remarks>
public readonly record struct CameraState
{
    /// <summary>펌웨어 자동 노출입니다.</summary>
    public bool? ExposureAuto { get; init; }

    /// <summary>DirectShow 노출값입니다. 1 커지면 노출 시간이 2배입니다.</summary>
    public int? ExposureStep { get; init; }

    public int? Gain { get; init; }

    public bool? WhiteBalanceAuto { get; init; }

    public int? WhiteBalanceK { get; init; }

    /// <summary>제조사 SDK로 지정한 fps입니다.</summary>
    public int? SdkFrameRate { get; init; }

    /// <summary>확장 유닛 잠금입니다.</summary>
    public bool? Locked { get; init; }
}

public static class CameraSettings
{
    /// <summary>
    /// 노출 단계를 노출 시간(ms)으로 바꿉니다. 단계 n은 2ⁿ초입니다(Windows CameraControl_Exposure의 정의).
    /// </summary>
    /// <param name="step">노출 단계(<see cref="CameraState.ExposureStep"/>)</param>
    /// <returns>노출 시간(ms). -5는 31.25입니다</returns>
    /// <remarks>카메라가 실제로 이 시간만큼 노출하는지는 재지 않았습니다. 정의에 따른 명목값입니다.</remarks>
    public static double ExposureStepMs(int step) => Math.ScaleB(1000.0, step);
}
