using DiabeticFoot.Core;
using DiabeticFoot.Core.Pixel;
using DiabeticFoot.Core.Record;

namespace DiabeticFoot.Platform;

/// <summary>열화상 카메라의 연결 상태입니다.</summary>
public enum ThermalConnection : byte
{
    /// <summary>열지 않았거나 닫음</summary>
    Closed,

    /// <summary>열었고 아직 첫 프레임 전</summary>
    Waiting,

    /// <summary>마지막 프레임이 끊김 판정 시간 안에 옴</summary>
    Receiving,

    /// <summary>정한 시간 동안 프레임이 오지 않음. 다시 열어야 함</summary>
    Lost,
}

/// <summary>
/// 열화상 연결 끊김을 판정하는 시간입니다. 기본값은 프레임 주기의 30배(1초)입니다.
/// </summary>
/// <remarks>
/// 열화상 SDK는 USB를 뽑아도 열린 상태를 유지하고 프레임만 주지 않으므로, 프레임이 없을 때만 시간으로 판정합니다.
/// TE-EV1 실측(2026-10-20, 9회 33,000장): 셔터 교정 중에도 SDK는 같은 프레임을 계속 주어 수신이 멈추지 않았고, 수신 간격
/// 최대는 199ms(프레임마다 SDK 온도 읽기를 더한 측정)와 36.9ms(더하지 않은 측정), Open이 반환된 뒤 첫 프레임은 최대
/// 48ms였습니다. 최대 간격의 5배를 넘깁니다.
/// </remarks>
/// <param name="FirstFrameUs">연 뒤 첫 프레임까지 기다리는 시간입니다.</param>
/// <param name="StallUs">받던 프레임이 이만큼 오지 않으면 끊김입니다.</param>
public sealed record ThermalLossRule(
    long FirstFrameUs = ThermalCamera.ThermalLossPeriods * ThermalCamera.ThermalFramePeriodUs,
    long StallUs = ThermalCamera.ThermalLossPeriods * ThermalCamera.ThermalFramePeriodUs);

/// <summary>열화상 렌즈 셰이딩 보정의 상태입니다(<see cref="IThermalCamera.LensShading"/>). 열화상 칸과 사이드카에 남깁니다.</summary>
/// <param name="Error">불러오기, 보정, 저장이 실패한 이유입니다. 실패가 없으면 빈 문자열입니다.</param>
public sealed record LensShadingStatus(LensShadingRecord Record = default, string Error = "");

/// <summary>
/// 열화상 카메라가 프레임과 함께 알려 주는 카메라 자신의 값입니다(<see cref="IThermalCamera.LastCameraTemperature"/>).
/// 장면(발)의 온도가 아닙니다. 녹화하면 원시값 파일에 쓴 프레임마다 카메라 온도 파일에 한 줄씩 남깁니다.
/// </summary>
/// <remarks>
/// TE-EV1: fpa는 SDK 문서에 단위가 없어 °C로 보지 않습니다(2026-10-28 켠 뒤 70분 실측 88.2~98.3, 0.061 간격).
/// 셔터 PT100 값은 셔터가 닫힐 때만 바뀌므로(SDK 문서) 셔터 교정 사이의 프레임은 같은 값입니다.
/// </remarks>
/// <param name="Fpa">FPA 값입니다. 단위를 모릅니다.</param>
/// <param name="ShutterRaw">셔터 PT100 원시값입니다.</param>
/// <param name="ShutterC">ShutterRaw를 옮긴 셔터 온도(°C)입니다.</param>
public readonly record struct CameraTemperature(float Fpa = 0, ushort ShutterRaw = 0, double ShutterC = 0);

/// <summary>
/// 열화상 카메라 하나입니다.
/// </summary>
/// <remarks>
/// 프레임은 Y16(화소마다 16비트 원시값, 리틀 엔디언)이고 View.Stream은 <see cref="StreamKind.Thermal"/>입니다.
/// 장치가 프레임에 시각 정보를 주지 않으므로 View.Time과 HostTime 모두 PC 수신 시각입니다. 끊긴 뒤 다시 연결하려면 Close 뒤
/// Open을 부릅니다. 열화상 SDK는 프레임을 보관하지 않으므로, 다음 프레임이 도착하기 전에 읽지 않은 프레임은 받을 수 없습니다.
/// ReadFrame을 부르는 수신 스레드가 디스크 쓰기나 화면 변환을 하면 프레임이 빠집니다(기존 앱 실측 20~25fps).
/// 실제 카메라를 만드는 함수(원본 <c>MakeDeviceThermalCamera</c>)는 OS별 구현(Platform.Windows)에 있습니다.
/// 기본 구현이 있는 멤버는 원본에서 기본 구현이 있던 가상 함수이고, 그 기능이 없는 구현은 그대로 둡니다.
/// </remarks>
public interface IThermalCamera : IDisposable
{
    /// <summary>
    /// 장치를 찾아 엽니다. 장치가 없거나 다른 프로그램이 쓰고 있으면 false이고 error에 이유를 씁니다.
    /// </summary>
    /// <remarks>
    /// 수 초가 걸릴 수 있습니다(장치 탐색, 셔터 교정). 화면 스레드에서 부르지 않습니다. 셔터 교정을 하는 구현은 교정 구간과 버릴
    /// 프레임이 끝난 뒤 반환하며, 그 판정을 LastCalibration에 남깁니다. 보정 파일 폴더를 정한 구현은 셔터 교정 뒤에 이 카메라의
    /// 렌즈 셰이딩 보정 파일이 있으면 불러오고 결과를 LensShading에 남깁니다. 끊긴 뒤 다시 열 때도 같습니다. 불러오기가 실패해도
    /// 열기는 실패로 보지 않습니다.
    /// </remarks>
    bool Open(out string error);

    /// <summary>장치를 닫습니다. 열려 있지 않으면 아무것도 하지 않습니다.</summary>
    void Close();

    /// <summary>장치 이름입니다. 사이드카와 화면에 남깁니다. 열기 전에는 빈 문자열입니다.</summary>
    string DeviceName { get; }

    /// <summary>프레임 폭입니다. 열기 전에는 0입니다.</summary>
    int Width { get; }

    /// <summary>프레임 높이입니다. 열기 전에는 0입니다.</summary>
    int Height { get; }

    /// <summary>
    /// 프레임 하나를 기다려 받습니다. 시간 안에 오지 않았거나 열려 있지 않으면 null입니다. null이 연결 끊김을 뜻하지는 않으며,
    /// 끊김은 Connection으로 판정합니다. 수신 스레드에서만 부릅니다. 화소는 다음 ReadFrame 호출 전까지만 유효합니다.
    /// </summary>
    /// <param name="timeoutUs">기다릴 최대 시간입니다.</param>
    ReceivedFrame? ReadFrame(long timeoutUs);

    /// <summary>지금의 연결 상태입니다. <see cref="ThermalConnection.Lost"/>이면 Close 뒤 Open으로 다시 엽니다.</summary>
    ThermalConnection Connection { get; }

    /// <summary>
    /// 셔터를 닫아 화소별 편차를 지금 다시 잽니다(셔터 교정). 교정 구간이 보였고 버릴 프레임까지 끝났으면 true입니다. 열려 있지
    /// 않거나, 교정이 없거나, 교정 구간이 보이지 않았거나, 기다리는 동안 닫히거나 끊겼으면 false입니다.
    /// </summary>
    /// <remarks>
    /// 수신 스레드가 ReadFrame을 부르는 동안 다른 스레드에서 부릅니다. 끝은 시간이 아니라 ReadFrame이 받은 프레임의 내용으로
    /// 판정합니다(<c>PixelFunctions.AfterCalibrationFrame</c>). 교정 구간의 같은 프레임도 ReadFrame으로 나갑니다. 판정 결과는
    /// LastCalibration에 남습니다.
    /// </remarks>
    bool CalibrateNow(out string error)
    {
        error = "이 열화상 구현은 셔터 교정을 지원하지 않습니다";
        return false;
    }

    /// <summary>
    /// 셔터 교정 한 번(CalibrateNow가 반환할 때까지)에 걸리는 시간의 실측값(마이크로초)입니다. 모르면 0입니다.
    /// 촬영 흐름이 PPG 안정 대기와 함께 교정이 끝나도록 요청 시점을 정할 때 씁니다(<c>FlowRule.ThermalCalibrationFrames</c>).
    /// </summary>
    long CalibrationUs => 0;

    /// <summary>
    /// 장치가 스스로 하는 셔터 교정을 멈추거나(hold가 true) 연 때 읽은 원래 설정으로 되돌립니다(false). 바꿨으면 true이고,
    /// 원래 설정을 모르면 바꾸지 않고 false입니다.
    /// </summary>
    /// <remarks>
    /// 실행 중에만 바꾸고 장치 저장 영역(플래시)에는 쓰지 않습니다. 멈춘 채로 Close를 부르면 Close가 되돌립니다. 멈춘 동안에도
    /// CalibrateNow는 동작합니다(TE-EV1 실측). 다시 연결해 열면 멈춤은 해제된 상태입니다.
    /// </remarks>
    bool HoldAutoCalibration(bool hold, out string error)
    {
        error = "이 열화상 구현은 자동 셔터 교정 제어를 지원하지 않습니다";
        return false;
    }

    /// <summary>
    /// 장치의 자동 셔터 교정 설정을 사람이 읽을 글로 돌려줍니다(예: "temperature, time=0, temp=0, held"). 사이드카와 로그에
    /// 남깁니다. 열기 전이거나 모르면 빈 문자열입니다.
    /// </summary>
    string AutoCalibrationSetting => "";

    /// <summary>
    /// 마지막 셔터 교정(Open 안의 교정 포함)의 판정 결과입니다. 사이드카와 로그에 남깁니다. 교정한 적이 없거나 이 구현에 교정이
    /// 없으면 null입니다.
    /// </summary>
    CalibrationWatch? LastCalibration => null;

    /// <summary>
    /// 렌즈 셰이딩 보정 파일을 두는 폴더를 정합니다. Open 전에 부릅니다. 폴더가 없으면 CorrectLensShading이 만듭니다.
    /// 폴더를 정하지 않은 카메라는 보정 기능이 없는 것으로 보고 LensShading이 null입니다.
    /// </summary>
    void SetLensShadingFolder(string folder)
    {
    }

    /// <summary>
    /// 지금 열린 연결의 렌즈 셰이딩 보정 상태입니다. 열화상 칸과 사이드카에 남깁니다. 열기 전이거나 닫은 뒤, 보정 기능이
    /// 없으면(폴더를 정하지 않음) null입니다. 화면 스레드와 제어 스레드에서도 부를 수 있습니다.
    /// </summary>
    LensShadingStatus? LensShading => null;

    /// <summary>
    /// 렌즈 셰이딩 보정을 하고 이 카메라의 보정 파일로 저장합니다. 다음에 열 때 그 파일을 불러옵니다. 보정했으면 true입니다.
    /// 보정했지만 저장하지 못하면 true이고 LensShading의 FileUnixS가 비며 error에 이유가 남습니다. 열려 있지 않거나 보정 기능이
    /// 없거나 보정이 실패하면 false입니다.
    /// </summary>
    /// <remarks>
    /// 보정할 때 장치가 고른 장면(실온의 고른 면으로 가린 렌즈)을 보고 있어야 합니다(SDK 문서). 손을 둔 채 보정하면 보정하지 않았을
    /// 때보다 영상이 고르지 않게 됩니다(설계 문서 표 10). 수신 스레드가 ReadFrame을 부르는 동안 다른 스레드에서 부르며, 보정하는
    /// 동안(TE-EV1 실측 1.5~1.8초) ReadFrame은 기다립니다.
    /// </remarks>
    bool CorrectLensShading(out string error)
    {
        error = "이 열화상 구현은 렌즈 셰이딩 보정을 지원하지 않습니다";
        return false;
    }

    /// <summary>
    /// 마지막으로 ReadFrame이 돌려준 프레임과 함께 읽은 카메라 온도입니다. 이 구현에 카메라 온도가 없거나 연 뒤 받은 프레임이
    /// 없으면 null입니다.
    /// </summary>
    /// <remarks>
    /// 수신 스레드에서 ReadFrame 뒤에만 부릅니다. 구현은 장치에 명령을 보내지 않고 프레임을 받을 때 읽어 둔 값을 돌려주어야
    /// 합니다. 셔터를 닫거나 기다리는 호출은 프레임을 빠뜨립니다.
    /// </remarks>
    CameraTemperature? LastCameraTemperature => null;
}

public static class ThermalCamera
{
    /// <summary>TE-EV1의 프레임 주기(30Hz)입니다. 실제 수신 간격은 실기에서 따로 잽니다.</summary>
    public const long ThermalFramePeriodUs = 33_333;

    /// <summary>끊김 판정 시간을 정하는 프레임 주기 배수입니다.</summary>
    public const long ThermalLossPeriods = 30;

    /// <summary>
    /// TE-EV1의 셔터 교정 판정 수치입니다(설계 문서 표 5).
    /// </summary>
    /// <remarks>
    /// 실측(2026-10-20, 교정 명령 14회, 열기 5회, 교정 밖 20,108장):
    /// FreezeRepeats 3은 교정 구간이 19회 모두 앞 프레임과 같은 프레임 42장이 이어졌고 교정 밖 반복은 22회 모두 1장이었기
    /// 때문입니다. FreezeWaitFrames 15는 명령 뒤 교정 구간까지 19회 모두 0장이었기 때문입니다. FreezeMaxFrames 130은 교정 구간
    /// 최대 42장의 약 3배입니다. DiscardFrames 105는 교정 뒤 14회 평균 편차(0.5초 평균과 5~15초 뒤 추세의 차)가 교정 없는 구간
    /// 평균 0.61 count의 1.2배 이하로 처음 내려온 15장 묶음의 시작입니다. 열기 5회는 첫 새 프레임부터 교정 없는 구간 수준이라
    /// Open은 버리지 않습니다.
    /// </remarks>
    public static readonly CalibrationRule TeEv1CalibrationRule = new()
    {
        FreezeRepeats = 3,
        FreezeWaitFrames = 15,
        FreezeMaxFrames = 130,
        DiscardFrames = 105,
    };

    /// <summary>
    /// TE-EV1의 셔터 교정 한 번에 걸리는 시간입니다(<see cref="IThermalCamera.CalibrationUs"/>).
    /// </summary>
    /// <remarks>
    /// 교정 구간의 같은 프레임 42장과 교정 뒤 버리는 105장(<see cref="TeEv1CalibrationRule"/>)을 장치 30Hz로 받는 시간입니다.
    /// 실기(2026-10-27, 직접 지정 3회)에서 PPG 안정 대기 9.5초에 요청한 교정 뒤 녹화가 PPG를 다시 시작한 뒤 14.35~14.37초에
    /// 시작했습니다.
    /// </remarks>
    public const long TeEv1CalibrationUs = 4_900_000;

    /// <summary>연결 상태를 판정합니다.</summary>
    /// <param name="open">열려 있는지입니다.</param>
    /// <param name="openedAt">연 시각입니다.</param>
    /// <param name="lastFrame">마지막 프레임의 수신 시각입니다. 연 뒤 받은 프레임이 없으면 null입니다.</param>
    /// <param name="now">지금 시각입니다. openedAt, lastFrame과 같은 시계입니다.</param>
    public static ThermalConnection JudgeThermalConnection(
        bool open, Timestamp openedAt, Timestamp? lastFrame, Timestamp now, ThermalLossRule rule)
    {
        if (!open) return ThermalConnection.Closed;
        if (lastFrame is not { } last)
        {
            // 연 직후의 셔터 교정 동안 프레임이 없으므로 받던 중의 끊김보다 길게 기다립니다.
            return Timestamp.IntervalUs(openedAt, now) < rule.FirstFrameUs ? ThermalConnection.Waiting : ThermalConnection.Lost;
        }
        return Timestamp.IntervalUs(last, now) < rule.StallUs ? ThermalConnection.Receiving : ThermalConnection.Lost;
    }

    /// <summary>
    /// 렌즈 셰이딩 보정 파일 이름입니다(예: lens_shading_TE-EV1-001234.cal). 렌즈와 센서마다 보정값이 다르므로 장치 이름(모델과
    /// 코어 번호)을 넣습니다.
    /// </summary>
    /// <param name="deviceName"><see cref="IThermalCamera.DeviceName"/>입니다.</param>
    public static string LensShadingFileName(string deviceName) => "lens_shading_" + deviceName + ".cal";
}
