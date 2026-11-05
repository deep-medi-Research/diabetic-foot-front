using DiabeticFoot.Core;
using DiabeticFoot.Core.Flow;

namespace DiabeticFoot.Platform;

/// <summary>카메라에서 받은 프레임 하나입니다.</summary>
/// <param name="View">화소는 다음 ReadFrame 호출 전까지만 유효합니다.</param>
/// <param name="HostTime">PC가 받은 시각입니다.</param>
/// <param name="StreamGap">드라이버가 이 프레임 앞에서 스트림이 끊겼다고 알렸습니다.</param>
public readonly record struct ReceivedFrame(FrameView View, Timestamp HostTime, bool StreamGap = false);

/// <summary>
/// RGB 카메라 하나입니다. 설정은 core(<c>FlowFunctions.PlanCommands</c>)가 만든 명령으로만 바꾸며, 보통 직접 부르지 않고
/// <see cref="RgbCamera.Reconcile"/>를 씁니다.
/// </summary>
/// <remarks>
/// 장치를 찾아 무압축 UYVY로 여는 함수(원본 <c>RgbCamera::Open</c>)는 OS별 구현(Platform.Windows)에 있습니다.
/// 다시 열린 카메라는 공장 기본 설정이므로 부른 쪽이 목표 상태로 다시 맞춰야 합니다.
/// </remarks>
public interface IRgbCamera : IDisposable
{
    /// <summary>장치 이름입니다. 사이드카와 화면에 남깁니다.</summary>
    string DeviceName { get; }

    /// <summary>
    /// 프레임 하나를 기다려 받습니다. 연결이 끊기거나 스트림이 끝나면 null입니다.
    /// 수신 스레드에서만 부릅니다. 화소는 다음 ReadFrame 호출 전까지만 유효합니다.
    /// </summary>
    ReceivedFrame? ReadFrame();

    /// <summary>
    /// 끊긴 장치를 다시 찾아 같은 형식으로 엽니다. ReadFrame이 끊김을 알린 뒤 수신 스레드에서 부릅니다.
    /// 다시 열었으면 true, 장치가 아직 없으면 false입니다.
    /// </summary>
    bool Reopen();

    /// <summary>
    /// 장치를 찾았지만 촬영 선언의 형식으로 열지 못한 이유입니다(예: USB 2 포트라 UYVY 1920x1200이 최대 6fps).
    /// 열었거나 장치를 찾지 못했으면 빈 문자열입니다. 어느 스레드에서 불러도 됩니다.
    /// </summary>
    string OpenProblem => "";

    /// <summary>
    /// 카메라 설정을 다시 읽습니다. 읽지 못한 항목과 자동 노출 중의 노출값은 비어 있습니다.
    /// 제어 스레드에서만 부릅니다. 수백 ms 걸릴 수 있어 수신 스레드에서 부르면 프레임이 빠집니다.
    /// </summary>
    CameraState ReadState();

    /// <summary>
    /// 카메라 설정을 저장값까지 다시 읽습니다. 실험 전 상태를 읽어 두고 되돌릴 때 씁니다. ReadState와 같지만 자동 노출 중에도
    /// 카메라에 저장된 노출값(마지막 수동 값)을 채웁니다. 이 노출값은 자동 노출이 실제로 쓰는 값이 아니므로 사이드카에 남기지
    /// 않습니다. 제어 스레드에서만 부릅니다.
    /// </summary>
    CameraState ReadStoredState();

    /// <summary>
    /// 설정 명령 하나를 실행합니다. 카메라가 성공을 돌려주면 true이며, 실제로 그 값이 되었는지는 ReadState로 확인해야 합니다.
    /// 제어 스레드에서만 부릅니다.
    /// </summary>
    bool Execute(SettingCommand command);

    /// <summary>
    /// 수신을 끝냅니다. 기다리던 ReadFrame은 null로 돌아오고, 이후 ReadFrame과 Reopen은 바로 실패합니다.
    /// 카메라가 프레임을 보내지 않으면 ReadFrame이 돌아오지 않아 종료가 끝나지 않았습니다(2026-10-27 노트북). 종료할 때
    /// 수신 스레드가 정한 시간 안에 끝나지 않으면 부릅니다. 어느 스레드에서 불러도 됩니다. 장치 스트림을 닫으므로 이후 설정
    /// 명령(노출, 게인, 색온도)은 실패할 수 있습니다.
    /// </summary>
    void Interrupt();
}

/// <summary>목표 상태로 맞춘 결과입니다.</summary>
public sealed record ReconcileResult
{
    public IReadOnlyList<SettingCommand> Executed { get; init; } = Array.Empty<SettingCommand>();

    public IReadOnlyList<SettingCommand> Failed { get; init; } = Array.Empty<SettingCommand>();

    /// <summary>실행 뒤 다시 읽은 값입니다.</summary>
    public CameraState Readback { get; init; }

    /// <summary>다시 읽은 값이 목표와 다른 항목입니다.</summary>
    public IReadOnlyList<SettingField> Mismatches { get; init; } = Array.Empty<SettingField>();

    /// <summary>Restore에서 실행 전 값을 몰라 되돌리지도 비교하지도 않은 항목(<c>FlowFunctions.FindUnknownSaved</c>)입니다. Reconcile은 비웁니다.</summary>
    public IReadOnlyList<SettingField> UnknownSaved { get; init; } = Array.Empty<SettingField>();
}

public static class RgbCamera
{
    /// <summary>
    /// 카메라를 목표 상태로 맞춥니다. 미리보기 설정, 잠금, 실험 뒤 복원, 재연결 뒤 재설정이 모두 이 함수입니다.
    /// 지금 상태를 읽고, 목표와 다른 항목만 바꾸고(<c>PlanCommands</c>), 다시 읽어 확인합니다(<c>FindMismatches</c>).
    /// 제어 스레드에서만 부릅니다.
    /// </summary>
    /// <param name="target">목표 상태입니다. 빈 항목은 바꾸지 않습니다.</param>
    public static ReconcileResult Reconcile(IRgbCamera camera, CameraState target)
    {
        var executed = new List<SettingCommand>();
        var failed = new List<SettingCommand>();
        foreach (SettingCommand command in FlowFunctions.PlanCommands(camera.ReadState(), target))
        {
            (camera.Execute(command) ? executed : failed).Add(command);
        }
        CameraState readback = camera.ReadState();
        return new ReconcileResult
        {
            Executed = executed,
            Failed = failed,
            Readback = readback,
            Mismatches = FlowFunctions.FindMismatches(readback, target),
        };
    }

    /// <summary>
    /// 카메라를 실험 전에 읽어 둔 상태로 되돌립니다. 저장값까지 읽고(<see cref="IRgbCamera.ReadStoredState"/>),
    /// <c>PlanRestore</c>의 명령을 실행하고, 다시 읽어 모드와 저장값을 모두 비교합니다(<c>FindRestoreMismatches</c>).
    /// 제어 스레드에서만 부릅니다.
    /// </summary>
    /// <param name="saved">실험 전에 ReadStoredState로 읽은 상태입니다.</param>
    public static ReconcileResult Restore(IRgbCamera camera, CameraState saved)
    {
        var executed = new List<SettingCommand>();
        var failed = new List<SettingCommand>();
        foreach (SettingCommand command in FlowFunctions.PlanRestore(camera.ReadStoredState(), saved))
        {
            (camera.Execute(command) ? executed : failed).Add(command);
        }
        CameraState readback = camera.ReadStoredState();
        return new ReconcileResult
        {
            Executed = executed,
            Failed = failed,
            Readback = readback,
            Mismatches = FlowFunctions.FindRestoreMismatches(readback, saved),
            UnknownSaved = FlowFunctions.FindUnknownSaved(saved),
        };
    }
}
