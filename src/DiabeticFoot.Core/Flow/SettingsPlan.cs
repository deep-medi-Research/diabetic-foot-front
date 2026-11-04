namespace DiabeticFoot.Core.Flow;

/// <summary>촬영 한 번에 쓸 설정값의 선언입니다. 화면이나 선언 파일이 이 값을 만듭니다.</summary>
public readonly record struct SettingsSpec
{
    public SettingMode Mode { get; init; }

    /// <summary>직접 지정일 때만 씁니다.</summary>
    public int ExposureStep { get; init; }

    public int Gain { get; init; }

    public int WhiteBalanceK { get; init; }

    public int SdkFrameRate { get; init; }
}

/// <summary>카메라에 보내는 설정 명령의 종류입니다.</summary>
public enum CommandKind : byte
{
    SdkFrameRate,
    Unlock,
    ExposureAuto,

    /// <summary>value: 노출 단계</summary>
    ExposureManual,
    Gain,

    /// <summary>value: K</summary>
    WhiteBalanceManual,
    WhiteBalanceAuto,
    Lock,
}

/// <summary>설정 명령 하나입니다. <see cref="Value"/>는 노출 단계, 게인, 색온도, fps처럼 값이 필요한 명령에만 씁니다.</summary>
public sealed record SettingCommand(CommandKind Kind = CommandKind.Gain, int Value = 0);

/// <summary>카메라 설정의 항목입니다. 목표와 다른 항목을 알릴 때 씁니다.</summary>
public enum SettingField : byte
{
    ExposureAuto,
    ExposureStep,
    Gain,
    WhiteBalanceAuto,
    WhiteBalanceK,
    SdkFrameRate,
    Locked,
}

public static partial class FlowFunctions
{
    /// <summary>
    /// 미리보기 동안 카메라가 가져야 할 목표 상태를 만듭니다.
    /// </summary>
    /// <param name="spec">설정값 선언</param>
    /// <returns>자동 맞춤은 펌웨어 자동 노출을 켜고 잠금을 풀며, 직접 지정은 입력한 값으로 잠급니다</returns>
    public static CameraState PreviewTarget(SettingsSpec spec)
    {
        var target = new CameraState
        {
            Gain = spec.Gain,
            WhiteBalanceAuto = false,
            WhiteBalanceK = spec.WhiteBalanceK,
            SdkFrameRate = spec.SdkFrameRate,
        };
        if (spec.Mode == SettingMode.Auto)
        {
            return target with { ExposureAuto = true, Locked = false };
        }
        return target with { ExposureAuto = false, ExposureStep = spec.ExposureStep, Locked = true };
    }

    /// <summary>
    /// 녹화 동안 카메라가 가져야 할 목표 상태를 만듭니다.
    /// </summary>
    /// <param name="spec">설정값 선언</param>
    /// <returns>미리보기 목표에 잠금을 더한 상태. 두 방식 모두 잠금입니다</returns>
    public static CameraState RecordingTarget(SettingsSpec spec) => PreviewTarget(spec) with { Locked = true };

    /// <summary>
    /// 지금 상태에서 목표 상태로 가는 명령을 실행 순서대로 만듭니다.
    /// </summary>
    /// <param name="current">카메라에서 다시 읽은 지금 상태. 읽지 못한 항목은 다르다고 봅니다</param>
    /// <param name="target">목표 상태. 빈 항목은 바꾸지 않습니다</param>
    /// <returns>명령 목록. 이미 목표에 있으면 빈 목록</returns>
    /// <remarks>
    /// 같은 항목은 명령을 만들지 않습니다. 순서는 fps, 잠금 풀기, 노출, 게인, 색온도, 잠금입니다.
    /// 잠금 중에는 펌웨어가 노출을 바꾸지 않으므로 풀기를 노출보다 먼저 둡니다.
    /// 잠긴 카메라를 잠금 목표로 두고 노출, 게인, 색온도를 바꿀 때도 풀고 쓴 뒤 다시 잠급니다. 잠긴 채 쓴 값은 영상에
    /// 반영되지 않았기 때문입니다(2026-10-27 실기, 색온도).
    /// </remarks>
    public static IReadOnlyList<SettingCommand> PlanCommands(CameraState current, CameraState target)
    {
        var commands = new List<SettingCommand>();
        if (Differs(current.SdkFrameRate, target.SdkFrameRate))
        {
            commands.Add(new SettingCommand(CommandKind.SdkFrameRate, target.SdkFrameRate!.Value));
        }
        if (target.Locked == false && current.Locked != false)
        {
            commands.Add(new SettingCommand(CommandKind.Unlock));
        }
        if (target.ExposureAuto == true)
        {
            if (current.ExposureAuto != true)
            {
                commands.Add(new SettingCommand(CommandKind.ExposureAuto));
            }
        }
        else if (target.ExposureAuto == false || target.ExposureStep.HasValue)
        {
            // 수동 노출은 값과 함께 설정합니다. 목표 값이 없으면 지금 값을 그대로 수동으로 둡니다.
            int? step = target.ExposureStep.HasValue ? target.ExposureStep : current.ExposureStep;
            if (step.HasValue && (current.ExposureAuto != false || current.ExposureStep != step))
            {
                commands.Add(new SettingCommand(CommandKind.ExposureManual, step.Value));
            }
        }
        if (Differs(current.Gain, target.Gain))
        {
            commands.Add(new SettingCommand(CommandKind.Gain, target.Gain!.Value));
        }
        if (target.WhiteBalanceAuto == true)
        {
            if (current.WhiteBalanceAuto != true)
            {
                commands.Add(new SettingCommand(CommandKind.WhiteBalanceAuto));
            }
        }
        else if (target.WhiteBalanceK.HasValue &&
                 (current.WhiteBalanceAuto != false || current.WhiteBalanceK != target.WhiteBalanceK))
        {
            commands.Add(new SettingCommand(CommandKind.WhiteBalanceManual, target.WhiteBalanceK.Value));
        }
        if (target.Locked == true && current.Locked != true)
        {
            commands.Add(new SettingCommand(CommandKind.Lock));
        }
        // 잠긴 카메라에 쓴 색온도는 다시 읽으면 새 값인데 영상에는 반영되지 않았습니다(2026-10-27 실기, 재연결하면 반영).
        // 잠금을 유지하는 목표에서 노출, 게인, 색온도를 바꾸면 풀고 쓴 뒤 다시 잠급니다.
        bool changesValue = commands.Exists(c =>
            c.Kind != CommandKind.SdkFrameRate && c.Kind != CommandKind.Unlock && c.Kind != CommandKind.Lock);
        if (target.Locked == true && current.Locked == true && changesValue)
        {
            int firstValue = commands.FindIndex(c => c.Kind != CommandKind.SdkFrameRate);
            commands.Insert(firstValue, new SettingCommand(CommandKind.Unlock));
            commands.Add(new SettingCommand(CommandKind.Lock));
        }
        return commands;
    }

    /// <summary>
    /// 다시 읽은 값이 목표와 다른 항목을 찾습니다.
    /// </summary>
    /// <param name="readback">명령을 실행한 뒤 다시 읽은 값</param>
    /// <param name="target">목표 상태</param>
    /// <returns>다른 항목 목록. 목표가 비어 있는 항목과, 목표가 자동인 노출과 색온도의 값은 보지 않습니다</returns>
    /// <remarks>
    /// 노출 단계는 범위 밖 값을 넣으면 카메라가 성공을 돌려주고 다른 값으로 설정하므로, 이 함수로 확인해야 합니다.
    /// </remarks>
    public static IReadOnlyList<SettingField> FindMismatches(CameraState readback, CameraState target) =>
        Mismatches(readback, target, compareAutoValues: false);

    /// <summary>
    /// 실험 뒤 카메라를 시작 전 상태로 되돌리는 명령을 실행 순서대로 만듭니다.
    /// </summary>
    /// <param name="current">자동 모드에서도 저장값을 채워 읽은 지금 상태(<c>RgbCamera.ReadStoredState</c>)</param>
    /// <param name="savedState">
    /// 시작 전에 같은 방법으로 읽어 둔 상태. 빈 항목과 0 이하의 <see cref="CameraState.SdkFrameRate"/>는 실행 전 값을 모르는 것으로 보고
    /// 바꾸지 않습니다(<see cref="FindUnknownSaved"/>)
    /// </param>
    /// <returns>명령 목록. 이미 시작 전 상태이면 빈 목록</returns>
    /// <remarks>
    /// <see cref="PlanCommands"/>와 같지만 자동 모드인 노출과 색온도도 저장값을 되돌립니다. 자동 모드에서는 값을 쓸 명령이 없으므로,
    /// 저장값이 다르면 수동 명령으로 값을 쓴 뒤 자동 명령으로 모드를 돌립니다.
    /// 2026-10-19 실기에서 자동 명령만 보내면 모드는 돌아왔지만 저장값은 녹화 때 수동 값(-5, 4600K)으로 남았습니다.
    /// USB 재연결 직후 읽은 sdk_frame_rate가 0인 경우가 있어, 0을 그대로 되돌리면 카메라에 fps 0 명령을 보내게 됩니다.
    /// </remarks>
    public static IReadOnlyList<SettingCommand> PlanRestore(CameraState current, CameraState savedState)
    {
        CameraState saved = KnownPart(savedState);
        // 자동 모드 항목은 목표에서 빼고 PlanCommands에 맡긴 뒤, 저장값 쓰기와 자동 전환을 그 자리에 넣습니다.
        CameraState rest = saved;
        bool exposureAuto = saved.ExposureAuto == true;
        bool balanceAuto = saved.WhiteBalanceAuto == true;
        if (exposureAuto)
        {
            rest = rest with { ExposureAuto = null, ExposureStep = null };
        }
        if (balanceAuto)
        {
            rest = rest with { WhiteBalanceAuto = null, WhiteBalanceK = null };
        }
        IReadOnlyList<SettingCommand> planned = PlanCommands(current, rest);
        var exposure = new List<SettingCommand>();
        if (exposureAuto)
        {
            bool valueDiffers = saved.ExposureStep.HasValue && current.ExposureStep != saved.ExposureStep;
            if (valueDiffers)
            {
                exposure.Add(new SettingCommand(CommandKind.ExposureManual, saved.ExposureStep!.Value));
            }
            if (valueDiffers || current.ExposureAuto != true)
            {
                exposure.Add(new SettingCommand(CommandKind.ExposureAuto));
            }
        }
        var balance = new List<SettingCommand>();
        if (balanceAuto)
        {
            bool valueDiffers = saved.WhiteBalanceK.HasValue && current.WhiteBalanceK != saved.WhiteBalanceK;
            if (valueDiffers)
            {
                balance.Add(new SettingCommand(CommandKind.WhiteBalanceManual, saved.WhiteBalanceK!.Value));
            }
            if (valueDiffers || current.WhiteBalanceAuto != true)
            {
                balance.Add(new SettingCommand(CommandKind.WhiteBalanceAuto));
            }
        }
        // PlanCommands의 순서(fps, 잠금 풀기, 노출, 게인, 색온도, 잠금)를 유지합니다.
        var commands = new List<SettingCommand>();
        int next = 0;
        while (next < planned.Count && planned[next].Kind is CommandKind.SdkFrameRate or CommandKind.Unlock)
        {
            commands.Add(planned[next++]);
        }
        commands.AddRange(exposure);
        while (next < planned.Count &&
               planned[next].Kind is CommandKind.ExposureAuto or CommandKind.ExposureManual or CommandKind.Gain)
        {
            commands.Add(planned[next++]);
        }
        commands.AddRange(balance);
        while (next < planned.Count)
        {
            commands.Add(planned[next++]);
        }
        return commands;
    }

    /// <summary>
    /// 되돌린 뒤 다시 읽은 값이 시작 전 상태와 다른 항목을 찾습니다.
    /// </summary>
    /// <param name="readback">되돌린 뒤 저장값을 채워 다시 읽은 값</param>
    /// <param name="saved">시작 전에 읽어 둔 상태</param>
    /// <returns>다른 항목 목록. 시작 전에 읽지 못한 항목(<see cref="FindUnknownSaved"/>)은 보지 않습니다</returns>
    /// <remarks><see cref="FindMismatches"/>와 달리 자동 모드인 노출과 색온도의 저장값도 비교합니다.</remarks>
    public static IReadOnlyList<SettingField> FindRestoreMismatches(CameraState readback, CameraState saved) =>
        Mismatches(readback, KnownPart(saved), compareAutoValues: true);

    /// <summary>
    /// 시작 전에 읽어 둔 상태에서 값을 모르는 항목을 찾습니다. 이 항목은 되돌리지도 비교하지도 않으므로 따로 알립니다.
    /// </summary>
    /// <param name="saved">시작 전에 읽어 둔 상태</param>
    /// <returns>빈 항목과, sdk_frame_rate가 0 이하이면 그 항목. 순서는 <see cref="SettingField"/> 선언 순서입니다</returns>
    /// <remarks>
    /// sdk_frame_rate 0은 카메라가 실제로 0 fps라는 뜻이 아니라 USB 재연결 직후 SDK가 값을 채우지 않은 것입니다.
    /// </remarks>
    public static IReadOnlyList<SettingField> FindUnknownSaved(CameraState saved)
    {
        var fields = new List<SettingField>();
        if (!saved.ExposureAuto.HasValue)
        {
            fields.Add(SettingField.ExposureAuto);
        }
        if (!saved.ExposureStep.HasValue)
        {
            fields.Add(SettingField.ExposureStep);
        }
        if (!saved.Gain.HasValue)
        {
            fields.Add(SettingField.Gain);
        }
        if (!saved.WhiteBalanceAuto.HasValue)
        {
            fields.Add(SettingField.WhiteBalanceAuto);
        }
        if (!saved.WhiteBalanceK.HasValue)
        {
            fields.Add(SettingField.WhiteBalanceK);
        }
        if (!KnownFrameRate(saved.SdkFrameRate))
        {
            fields.Add(SettingField.SdkFrameRate);
        }
        if (!saved.Locked.HasValue)
        {
            fields.Add(SettingField.Locked);
        }
        return fields;
    }

    /// <summary>
    /// 설정 항목의 이름입니다. 복원에 실패한 항목을 출력할 때 씁니다.
    /// </summary>
    /// <param name="field">항목</param>
    /// <returns>사이드카의 키와 같은 영문 이름(예: "exposure_step")</returns>
    public static string NameOf(SettingField field) => field switch
    {
        SettingField.ExposureAuto => "exposure_auto",
        SettingField.ExposureStep => "exposure_step",
        SettingField.Gain => "gain",
        SettingField.WhiteBalanceAuto => "white_balance_auto",
        SettingField.WhiteBalanceK => "white_balance_k",
        SettingField.SdkFrameRate => "sdk_frame_rate",
        SettingField.Locked => "locked",
        _ => "",
    };

    /// <summary>
    /// 설정 명령 종류의 이름입니다. 실패한 명령을 출력할 때 씁니다.
    /// </summary>
    /// <param name="kind">명령 종류</param>
    /// <returns>영문 이름(예: "exposure_manual")</returns>
    public static string NameOf(CommandKind kind) => kind switch
    {
        CommandKind.SdkFrameRate => "sdk_frame_rate",
        CommandKind.Unlock => "unlock",
        CommandKind.ExposureAuto => "exposure_auto",
        CommandKind.ExposureManual => "exposure_manual",
        CommandKind.Gain => "gain",
        CommandKind.WhiteBalanceManual => "white_balance_manual",
        CommandKind.WhiteBalanceAuto => "white_balance_auto",
        CommandKind.Lock => "lock",
        _ => "",
    };

    private static bool Differs<T>(T? current, T? target) where T : struct =>
        target.HasValue && !Nullable.Equals(current, target);

    // 재연결 직후 SDK가 fps를 0으로 돌려준 실측이 있어, 0 이하는 실행 전 값을 읽지 못한 것으로 봅니다.
    private static bool KnownFrameRate(int? fps) => fps.HasValue && fps.Value > 0;

    // 복원 목표에서 실행 전 값을 모르는 항목을 비웁니다. 빈 항목은 명령도 비교도 하지 않습니다.
    private static CameraState KnownPart(CameraState saved) =>
        KnownFrameRate(saved.SdkFrameRate) ? saved : saved with { SdkFrameRate = null };

    private static List<SettingField> Mismatches(CameraState readback, CameraState target, bool compareAutoValues)
    {
        var fields = new List<SettingField>();
        if (Differs(readback.ExposureAuto, target.ExposureAuto))
        {
            fields.Add(SettingField.ExposureAuto);
        }
        // 목표가 자동인 항목은 카메라가 값을 스스로 바꾸므로 값을 비교하지 않습니다. 복원은 저장값을 비교합니다.
        if ((compareAutoValues || target.ExposureAuto != true) && Differs(readback.ExposureStep, target.ExposureStep))
        {
            fields.Add(SettingField.ExposureStep);
        }
        if (Differs(readback.Gain, target.Gain))
        {
            fields.Add(SettingField.Gain);
        }
        if (Differs(readback.WhiteBalanceAuto, target.WhiteBalanceAuto))
        {
            fields.Add(SettingField.WhiteBalanceAuto);
        }
        if ((compareAutoValues || target.WhiteBalanceAuto != true) &&
            Differs(readback.WhiteBalanceK, target.WhiteBalanceK))
        {
            fields.Add(SettingField.WhiteBalanceK);
        }
        if (Differs(readback.SdkFrameRate, target.SdkFrameRate))
        {
            fields.Add(SettingField.SdkFrameRate);
        }
        if (Differs(readback.Locked, target.Locked))
        {
            fields.Add(SettingField.Locked);
        }
        return fields;
    }
}
