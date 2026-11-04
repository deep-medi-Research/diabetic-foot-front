namespace DiabeticFoot.Core.Pixel;

/// <summary>셔터 교정 구간을 프레임 내용으로 판정하는 장치별 수치입니다. 장치마다 실측한 값을 platform이 넘깁니다.</summary>
public readonly record struct CalibrationRule
{
    /// <summary>앞 프레임과 같은 프레임이 이만큼 이어지면 교정 중으로 봅니다.</summary>
    public int FreezeRepeats { get; init; }

    /// <summary>교정 명령 뒤 이만큼 받도록 교정 구간이 보이지 않으면 교정 없음으로 끝냅니다.</summary>
    public int FreezeWaitFrames { get; init; }

    /// <summary>같은 프레임이 이만큼을 넘으면 교정이 끝나지 않는 것으로 보고 끝냅니다.</summary>
    public int FreezeMaxFrames { get; init; }

    /// <summary>교정 뒤 첫 새 프레임을 1로 세어 이 장 수까지 버립니다.</summary>
    public int DiscardFrames { get; init; }
}

/// <summary>셔터 교정 판정의 단계입니다.</summary>
public enum CalibrationPhase : byte
{
    /// <summary>명령 뒤 같은 프레임 반복을 기다립니다.</summary>
    WaitingFreeze,

    /// <summary>같은 프레임이 이어집니다(교정 중).</summary>
    Frozen,

    /// <summary>교정이 끝난 뒤 새 프레임을 버리는 중입니다.</summary>
    Discarding,

    /// <summary>교정과 버리기가 끝났습니다. 다음 프레임부터 씁니다.</summary>
    Done,

    /// <summary><see cref="CalibrationRule.FreezeWaitFrames"/> 안에 교정 구간이 보이지 않았습니다.</summary>
    NotSeen,

    /// <summary>같은 프레임이 <see cref="CalibrationRule.FreezeMaxFrames"/>를 넘었습니다.</summary>
    Stuck,
}

/// <summary>셔터 교정 판정 상태입니다. 값으로 주고받으며 함수 밖에 상태를 두지 않습니다. 명령을 보낼 때 기본값으로 시작합니다.</summary>
public readonly record struct CalibrationWatch
{
    public CalibrationPhase Phase { get; init; }

    public bool HasLast { get; init; }

    public ulong LastSignature { get; init; }

    /// <summary>지금 이어지는 반복 수(앞 프레임과 같은 장 수)입니다.</summary>
    public int Repeats { get; init; }

    /// <summary>명령 뒤 받은 장 수입니다.</summary>
    public int Received { get; init; }

    /// <summary>교정 구간의 첫 장 전에 받은 장 수입니다.</summary>
    public int WaitedFrames { get; init; }

    /// <summary>교정 구간의 같은 프레임 장 수입니다(반복 시작 장 포함).</summary>
    public int FrozenFrames { get; init; }

    /// <summary>교정 뒤 버린 새 프레임 수입니다.</summary>
    public int Discarded { get; init; }

    /// <summary>판정이 끝났으면 true입니다. <see cref="CalibrationPhase.Done"/>, <see cref="CalibrationPhase.NotSeen"/>, <see cref="CalibrationPhase.Stuck"/>이면 true입니다.</summary>
    public bool IsCalibrationFinished =>
        Phase == CalibrationPhase.Done || Phase == CalibrationPhase.NotSeen || Phase == CalibrationPhase.Stuck;
}

public static partial class PixelFunctions
{
    /// <summary>프레임 화소 내용의 서명입니다. 같은 화소의 프레임이면 같은 값입니다.</summary>
    /// <param name="frame">서명할 프레임. Plane0만 읽습니다</param>
    /// <returns>서명. 폭이나 높이가 0 이하면 FNV-1a 초기값입니다</returns>
    /// <remarks>
    /// 행 끝의 여백(RowStride0가 폭보다 긴 부분)은 넣지 않고 화소 바이트만 FNV-1a 64비트로 묶습니다.
    /// 열화상 SDK는 셔터 교정 동안 같은 화소의 프레임을 계속 주므로(TE-EV1 실측) 이 값의 반복으로 교정 구간을 찾습니다.
    /// </remarks>
    public static ulong FrameSignature(FrameView frame)
    {
        ulong hash = 1469598103934665603UL;
        if (frame.Width <= 0 || frame.Height <= 0 || frame.Plane0.IsEmpty) return hash;
        int bytesPerPixel = frame.Format == PixelFormat.Y16 || frame.Format == PixelFormat.Uyvy ? 2 : 1;
        int rowBytes = frame.Width * bytesPerPixel;
        ReadOnlySpan<byte> plane = frame.Plane0.Span;
        for (int y = 0; y < frame.Height; ++y)
        {
            ReadOnlySpan<byte> row = plane[(y * frame.RowStride0)..];
            for (int i = 0; i < rowBytes; ++i)
            {
                hash ^= row[i];
                hash *= 1099511628211UL;
            }
        }
        return hash;
    }

    /// <summary>받은 프레임 하나로 판정을 진행합니다.</summary>
    /// <param name="watch">지금 상태</param>
    /// <param name="signature">받은 프레임의 <see cref="FrameSignature"/></param>
    /// <param name="rule">장치별 수치</param>
    /// <returns>다음 상태. <see cref="CalibrationWatch.IsCalibrationFinished"/>가 true가 된 뒤의 프레임은 상태를 바꾸지 않습니다</returns>
    public static CalibrationWatch AfterCalibrationFrame(CalibrationWatch watch, ulong signature, CalibrationRule rule)
    {
        if (watch.IsCalibrationFinished) return watch;
        bool same = watch.HasLast && watch.LastSignature == signature;
        watch = watch with { HasLast = true, LastSignature = signature, Received = watch.Received + 1 };
        switch (watch.Phase)
        {
            case CalibrationPhase.WaitingFreeze:
                watch = watch with { Repeats = same ? watch.Repeats + 1 : 0 };
                if (watch.Repeats >= rule.FreezeRepeats && rule.FreezeRepeats > 0)
                {
                    int frozenFrames = watch.Repeats + 1;
                    watch = watch with
                    {
                        Phase = CalibrationPhase.Frozen,
                        FrozenFrames = frozenFrames,
                        WaitedFrames = watch.Received - frozenFrames,
                    };
                }
                else if (watch.Received >= rule.FreezeWaitFrames)
                {
                    watch = watch with { Phase = CalibrationPhase.NotSeen };
                }
                break;
            case CalibrationPhase.Frozen:
                if (same)
                {
                    watch = watch with { Repeats = watch.Repeats + 1, FrozenFrames = watch.FrozenFrames + 1 };
                    if (watch.FrozenFrames > rule.FreezeMaxFrames) watch = watch with { Phase = CalibrationPhase.Stuck };
                    break;
                }
                // 반복이 끊긴 이 프레임이 교정 뒤 첫 새 프레임입니다.
                watch = watch with { Repeats = 0 };
                if (rule.DiscardFrames <= 0)
                {
                    watch = watch with { Phase = CalibrationPhase.Done };
                    break;
                }
                watch = watch with { Discarded = 1 };
                watch = watch with
                {
                    Phase = watch.Discarded >= rule.DiscardFrames ? CalibrationPhase.Done : CalibrationPhase.Discarding,
                };
                break;
            case CalibrationPhase.Discarding:
                watch = watch with { Discarded = watch.Discarded + 1 };
                if (watch.Discarded >= rule.DiscardFrames) watch = watch with { Phase = CalibrationPhase.Done };
                break;
            case CalibrationPhase.Done:
            case CalibrationPhase.NotSeen:
            case CalibrationPhase.Stuck:
                break;
        }
        return watch;
    }
}
