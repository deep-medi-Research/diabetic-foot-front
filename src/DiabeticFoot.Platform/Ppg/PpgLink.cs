using System.Text;
using DiabeticFoot.Core;
using DiabeticFoot.Core.Record;

namespace DiabeticFoot.Platform;

/// <summary><see cref="PpgLink.Reading"/>의 결과입니다. 장치 스레드가 장치를 다시 시작할 때를 정하는 데 씁니다(<c>PpgNeedsRestart</c>).</summary>
public sealed record PpgReading
{
    /// <summary>포트가 열려 있으면 true입니다. 장치 오류로 닫히면 false입니다.</summary>
    public bool Open { get; init; }

    /// <summary>마지막 상태 메시지 값('v' 미리보기, 's' 측정 중, 'p' 측정 종료, 'u' 중지)입니다. 받은 적 없으면 0입니다.</summary>
    public byte Status { get; init; }

    /// <summary>파형 표본이 오는 중인지입니다. 마지막 측정 데이터를 받은 지 <see cref="PpgLink.StaleUs"/>가 지나지 않았으면 true입니다.</summary>
    public bool Streaming { get; init; }

    /// <summary>마지막 측정 데이터를 받은 뒤 흐른 시간(마이크로초)입니다. 시작 뒤 받은 적이 없으면 시작부터 셉니다. 시작 전이면 0입니다.</summary>
    public long QuietUs { get; init; }

    /// <summary>
    /// 장치가 마지막으로 보낸 0이 아닌 심박수(bpm)입니다. <see cref="PpgLink.HeartRateHoldUs"/> 동안 새 값이 없거나 시작 뒤 받은
    /// 적이 없으면 0입니다.
    /// </summary>
    /// <remarks>
    /// 장치는 박동을 잡은 측정 데이터에만 심박수를 채우고 나머지는 0으로 보냅니다. 측정 데이터 731개 중 59개(약 0.58초마다)에
    /// 심박수가 있었고 SpO2는 모두에 있었습니다(2026-10-21 실측). 마지막 측정 데이터의 값만 보이면 대부분 0이 됩니다.
    /// </remarks>
    public int HeartRate { get; init; }

    /// <summary>마지막 측정 데이터의 SpO2입니다.</summary>
    public int Spo2 { get; init; }

    /// <summary>마지막 측정 데이터의 측정 오류입니다.</summary>
    public byte MeasureError { get; init; }

    /// <summary>시작 뒤 받은 표본 수입니다.</summary>
    public long Samples { get; init; }

    /// <summary>장치가 보낸 오류 메시지('e') 값입니다. 받은 적 없으면 0입니다.</summary>
    public byte DeviceError { get; init; }

    /// <summary>명령 실패 이유입니다. 없으면 빈 문자열입니다.</summary>
    public string Problem { get; init; } = "";

    public PpgParserCounts Counts { get; init; }
}

/// <summary>PPG 장치의 연결 상태입니다. 장치 스레드가 정합니다.</summary>
public enum PpgState : byte
{
    /// <summary>포트를 열지 않음</summary>
    Off,

    /// <summary>포트를 여는 중</summary>
    Opening,

    /// <summary>포트를 열고 정보 요청을 보내며 장치가 답하기를 기다림(PPG 장치 확인). 연 뒤 <c>PpgPreparingAfterUs</c> 전</summary>
    Checking,

    /// <summary>장치 확인 중 연 뒤 <c>PpgPreparingAfterUs</c> 넘게 답이 없음. 전원을 켠 직후 장치는 초기화를 끝낸 뒤 답함</summary>
    Preparing,

    /// <summary>장치를 확인했고 측정 데이터를 아직 받지 못함</summary>
    Waiting,

    /// <summary>측정 데이터가 오는 중</summary>
    Streaming,

    /// <summary>측정 데이터가 오다가 <see cref="PpgLink.StaleUs"/> 넘게 오지 않음</summary>
    Stalled,

    /// <summary>포트를 열지 못했거나 읽기가 실패해 닫음(Problem에 이유)</summary>
    Failed,
}

/// <summary>화면에 보이는 PPG 장치 상태입니다. 장치 스레드가 바뀔 때만 보냅니다(<see cref="PpgSensor.Callbacks.State"/>).</summary>
public sealed record PpgSnapshot
{
    public PpgState State { get; init; }

    /// <summary>열었거나 여는 포트 이름입니다. Off이면 빈 문자열입니다.</summary>
    public string Port { get; init; } = "";

    /// <summary><see cref="PpgReading.HeartRate"/>와 같습니다(마지막 0이 아닌 값을 3초 동안).</summary>
    public int HeartRate { get; init; }

    public int Spo2 { get; init; }

    public byte MeasureError { get; init; }

    /// <summary>장치가 보낸 오류 메시지 값입니다. 다시 시작하면 0입니다.</summary>
    public byte DeviceError { get; init; }

    /// <summary>Failed의 이유나 명령 실패 이유입니다. 없으면 빈 문자열입니다.</summary>
    public string Problem { get; init; } = "";
}

/// <summary>
/// 직렬 포트 하나로 PPG 장치와 주고받는 일을 한 스레드에서 합니다. 장치 스레드(<see cref="PpgSensor"/>)가 가지고 돌립니다.
/// </summary>
/// <remarks>
/// 포트, 해석기, 상태, 화면 파형용 최근 표본, 붙인 촬영 기록을 이 객체 하나가 가집니다. 잠금이 없으므로 한 스레드에서만 부르고,
/// 다른 스레드는 Port의 WakeRead만 부릅니다. 실험 노트북(Intel xHCI)의 USB 포트에 변환기를 바로 꽂으면 측정 데이터가
/// 0.2~0.7초만 오고 멈춥니다. USB 허브를 거치면 정상입니다(2026-10-19~20 실측). 표본이 멈추면 Snapshot().State가 Stalled가 됩니다.
/// </remarks>
public sealed class PpgLink : IDisposable
{
    /// <summary>마지막 측정 데이터에서 이 시간이 지나면 표본이 멈춘 것으로 봅니다. 장치는 약 48ms마다 보냅니다(실측).</summary>
    public const long StaleUs = 500_000;

    /// <summary>마지막 0이 아닌 심박수를 보이는 시간입니다. 박동이 0.4~1.5초(40~150bpm)마다 오므로 그보다 깁니다.</summary>
    public const long HeartRateHoldUs = 3_000_000;

    /// <summary>
    /// 이웃한 측정 데이터 사이에서 PC 수신 시각이 장치 시각보다 이 시간 넘게 더 흐르면 수신 빈틈으로 보고 시계 구간을 나눕니다.
    /// </summary>
    /// <remarks>
    /// 허브를 거친 정상 수신에서 측정 데이터 간격은 최대 64~71ms(한 번 102.8ms)로, 장치 시각(약 48ms)보다 더 벌어진 시간은
    /// 대부분 약 23ms 이하였습니다. 중지 직후 다시 시작하면 13번 중 11번은 측정 데이터 1~2개가 먼저 온 뒤 143~176ms 동안 오지
    /// 않아 95~128ms 더 벌어졌습니다(2026-10-20 실측). 정상 수신을 잘못 나눠도 구간마다 오프셋만 다시 맞추므로 오차는 수신 지연의
    /// 흔들림 정도입니다.
    /// </remarks>
    public const long ReceiveGapUs = 50_000;

    /// <summary>화면 파형용으로 보관하는 최근 표본 수입니다(500Hz로 10초).</summary>
    public const int RecentCapacity = 5000;

    const int WriteTimeoutMs = 200;
    const int ReadBytes = 4096;

    readonly Func<long> _clock;
    readonly byte[] _buffer = new byte[ReadBytes];
    // 화면 파형용 최근 표본입니다. 크기를 넘으면 앞에서부터 버립니다.
    readonly Queue<ushort> _recent = new();
    ISerialPort? _port;
    PpgParser _parser = new();
    PpgReading _reading = new();
    bool _askedAfterFound;  // 장치를 확인한 뒤 정보 요청을 한 번 더 보냈음
    long _openUs;           // 포트를 연 PC 시각
    long _infoUs;           // 마지막으로 정보 요청을 보낸 PC 시각. 보낸 적 없으면 0
    long _nextIndex;        // 시작 뒤 다음 표본 번호
    long _startUs;          // 마지막 시작의 PC 시각
    long _restartUs;        // 마지막으로 다시 시작한 PC 시각
    long _lastMeasureUs;
    int _lastHeartRate;     // 마지막으로 받은 0이 아닌 심박수
    long _lastHeartRateUs;  // 그 값을 받은 PC 시각
    PpgShotRecord? _record;
    PpgParserCounts _recordCountsFrom;

    /// <param name="clock">PC 시각(마이크로초)입니다. 비우면 <see cref="SteadyClock"/>입니다. 촬영 파이프라인과 같은 시계를 넘깁니다.</param>
    public PpgLink(Func<long>? clock = null)
    {
        _clock = clock ?? SteadyClock.NowUs;
    }

    /// <summary>Close와 같습니다.</summary>
    public void Dispose() => Close();

    /// <summary>
    /// 포트를 엽니다. 열려 있던 포트는 먼저 닫습니다. 아직 명령은 보내지 않습니다. 장치 확인(Found)을 처음부터 다시 합니다.
    /// 열지 못하면 false이고 error에 이유를 씁니다.
    /// </summary>
    public bool Open(SerialPortOpener opener, string port, out string error)
    {
        Close();
        Failure = "";
        PortName = port;
        SerialOpenResult opened = opener(port, new SerialConfig(BaudRate: 115200));
        if (opened.Port is null)
        {
            Failure = opened.Error.Length == 0 ? "PPG 포트를 열지 못했습니다: " + port : opened.Error;
            error = Failure;
            return false;
        }
        _port = opened.Port;
        _parser = new PpgParser();
        _reading = new PpgReading { Open = true };
        Found = false;
        _askedAfterFound = false;
        DeviceInfo = "";
        _openUs = _clock();
        _infoUs = 0;
        error = "";
        return true;
    }

    /// <summary>
    /// 장치 정보 요청을 보냅니다. 장치를 확인하기 전이면 <c>PpgInfoIntervalUs</c>마다 보내고, 정보 응답이 아닌 메시지로 장치를
    /// 확인했으면 한 번만 더 보냅니다. 장치 정보를 받았으면 보내지 않습니다.
    /// </summary>
    /// <remarks>
    /// 정보 응답이 아닌 메시지로 확인하는 경우는 앞 연결이 장치를 측정 중으로 남긴 때입니다. 측정 중인 장치가 정보 요청에 답하는지는
    /// 확인하지 않았습니다.
    /// </remarks>
    public void AskDeviceInfo()
    {
        if (!IsOpen || DeviceInfo.Length != 0) return;
        long now = _clock();
        if (Found)
        {
            if (_askedAfterFound) return;
            _askedAfterFound = true;
        }
        else if (_infoUs != 0 && now - _infoUs < PpgFunctions.PpgInfoIntervalUs)
        {
            return;
        }
        _infoUs = now;
        Send(PpgWire.BuildInfoRequest(), out _);
    }

    /// <summary>
    /// 포트를 연 뒤 장치가 보낸 메시지(정보, 상태, 오류, 측정 데이터)를 하나라도 받았으면 true입니다(PPG 장치 확인).
    /// </summary>
    /// <remarks>
    /// 전원을 켠 장치는 초기화를 끝내면 묻지 않아도 장치 정보와 상태 'o'를 보냅니다. 실기(2026-10-19)에서 전원을 켜고 약 42초
    /// 뒤였습니다.
    /// </remarks>
    public bool Found { get; private set; }

    /// <summary>시작 명령을 보낸 뒤 닫지 않았으면 true입니다.</summary>
    public bool Started { get; private set; }

    /// <summary>촬영 기록을 붙여 두었으면 true입니다.</summary>
    public bool Attached => _record is not null;

    /// <summary>포트를 연 뒤 흐른 시간(마이크로초)입니다. 포트가 닫혀 있으면 0입니다.</summary>
    public long OpenUs => IsOpen ? _clock() - _openUs : 0;

    /// <summary>
    /// 장치 정보 응답의 데이터입니다. ASCII 15바이트로 MP-SDK 버전 6자리, HRV 기준 6자리, APG 기준 3자리입니다(예: 실기
    /// "100002280280320"은 MP-SDK 1.0.0.0.0.2, HRV 기준 2.8.0.2.8.0, APG 기준 3.2.0). 받지 못했으면 빈 문자열입니다.
    /// </summary>
    public string DeviceInfo { get; private set; } = "";

    /// <summary>
    /// 시작했으면 중지 명령을 보내고 포트를 닫습니다. 촬영 기록을 붙여 두었으면 끊긴 시각을 적습니다.
    /// 드라이버가 멈추면 이 함수가 돌아오지 않을 수 있습니다. 그래서 장치 스레드에서만 부릅니다.
    /// </summary>
    /// <param name="why">촬영 기록에 적을 이유입니다.</param>
    public void Close(string why = "PPG 연결을 끊었습니다")
    {
        if (_port is null) return;
        // 장치는 중지 명령을 받지 않으면 미리보기와 측정을 끝까지 이어가므로 닫기 전에 멈춥니다.
        if (Started && _port.IsOpen) _port.Write(PpgWire.BuildStop(), WriteTimeoutMs);
        _record?.MarkDisconnected(new Timestamp(_clock()), why);
        _port.Close();
        _port = null;
        Started = false;
        _reading = new PpgReading();
        _recent.Clear();
    }

    /// <inheritdoc cref="Restart(out string, PpgStartRequest?)"/>
    public bool Restart() => Restart(out _);

    /// <summary>
    /// 중지 명령 뒤 시작 명령을 보냅니다. 표본 번호를 0부터 다시 셉니다. 두 명령을 보냈으면 true입니다. 쓰기에 실패하면 false이고
    /// error와 Snapshot().Problem에 이유를 씁니다.
    /// </summary>
    /// <param name="request">
    /// 시작 명령 값입니다. 비우면 미리보기 60초, 측정 60초, 센서 'P'입니다. 장치는 미리보기 동안에도 파형을 보내고, PPG 안정 대기
    /// (<c>PpgSettleUs</c>)와 녹화 30초가 미리보기 60초 안에 들어갑니다.
    /// </param>
    /// <remarks>
    /// 앞 연결이나 다른 프로그램이 장치를 측정 중으로 남기면 중지 없이 보낸 시작 명령은 오류 's'로 거부되고 장치는 앞 측정을
    /// 이어갑니다(2026-10-20 실측). 장치는 측정 중이었으면 상태 'u'로 답합니다.
    /// </remarks>
    public bool Restart(out string error, PpgStartRequest? request = null)
    {
        if (!IsOpen)
        {
            error = "PPG 포트가 닫혀 있습니다";
            return false;
        }
        if (!PpgWire.TryBuildStart(request ?? new PpgStartRequest(), out byte[]? start, out string? why))
        {
            error = why;
            return false;
        }
        long now = _clock();
        Started = true;
        _nextIndex = 0;
        _startUs = now;
        _restartUs = now;
        _lastMeasureUs = 0;
        _lastHeartRate = 0;
        _lastHeartRateUs = 0;
        _recent.Clear();
        _reading = _reading with { Samples = 0, Status = 0, DeviceError = 0, Problem = "" };
        // 다른 연결이 장치를 측정 중으로 남겼으면 중지 없는 시작은 거부되므로 중지를 늘 먼저 보냅니다.
        if (!Send(PpgWire.BuildStop(), out error)) return false;
        return Send(start, out error);
    }

    /// <summary>
    /// 받은 바이트를 최대 timeoutMs 기다려 읽고 해석합니다. 다른 스레드가 WakeRead를 부르면 일찍 돌아옵니다.
    /// 포트가 열려 있으면 true입니다. 읽기가 실패하면 포트를 닫고 촬영 기록에 끊긴 시각을 적은 뒤 false입니다.
    /// </summary>
    public bool Pump(int timeoutMs)
    {
        if (_port is null) return false;
        int? n = null;
        if (_port.IsOpen) n = _port.Read(_buffer, timeoutMs);
        if (n is not { } count)
        {
            Failure = _reading.Problem.Length == 0 ? "PPG 포트를 읽지 못해 닫았습니다" : _reading.Problem;
            _record?.MarkDisconnected(new Timestamp(_clock()), Failure);
            _port.Close();
            Started = false;
            _reading = _reading with { Open = false };
            return false;
        }
        if (count == 0) return true;
        long now = _clock();
        foreach (PpgMessage m in _parser.Feed(_buffer.AsSpan(0, count), now)) Handle(m);
        _reading = _reading with { Counts = _parser.Counts };
        _record?.SetCounts(Minus(_parser.Counts, _recordCountsFrom));
        return true;
    }

    /// <summary>시작한 뒤 촬영 기록을 붙이지 않았고 <c>PpgNeedsRestart</c>가 true이면 다시 시작합니다. 다시 시작했으면 true입니다.</summary>
    public bool RestartIfStopped()
    {
        if (_record is not null || !IsOpen || !Started) return false;
        if (!PpgFunctions.PpgNeedsRestart(Reading(), _clock() - _restartUs)) return false;
        // 손가락 없이 미리보기가 끝나면 장치가 멈추고(오류 'f') 시작 명령을 다시 받아야 돌아옵니다.
        Restart();
        return true;
    }

    /// <summary>지금부터 받는 표본과 메시지를 record에 더합니다. 붙어 있던 기록은 뗍니다. 받아 둔 장치 정보를 record에 적습니다.</summary>
    public void Attach(PpgShotRecord? record)
    {
        _record = record;
        _recordCountsFrom = _parser.Counts;
        if (_record is not null && DeviceInfo.Length != 0) _record.SetDeviceInfo(DeviceInfo);
    }

    /// <summary>붙인 촬영 기록을 뗍니다.</summary>
    public void Detach() => _record = null;

    /// <summary>포트가 열려 있으면 true입니다.</summary>
    public bool IsOpen => _port is { IsOpen: true };

    /// <summary>연 포트입니다. 다른 스레드가 WakeRead를 부를 때 씁니다. 연 적이 없거나 Close 뒤이면 null입니다.</summary>
    public ISerialPort? Port => _port;

    /// <summary>열었거나 마지막으로 연 포트 이름입니다.</summary>
    public string PortName { get; private set; } = "";

    /// <summary>포트를 열지 못했거나 읽기가 실패해 닫았을 때의 이유입니다. Open하면 지웁니다. 없으면 빈 문자열입니다.</summary>
    public string Failure { get; private set; } = "";

    /// <summary>지금 상태입니다.</summary>
    public PpgReading Reading()
    {
        long now = _clock();
        bool fresh = _lastHeartRateUs > 0 && now - _lastHeartRateUs <= HeartRateHoldUs;
        return _reading with
        {
            Open = IsOpen,
            Streaming = Started && _lastMeasureUs > 0 && now - _lastMeasureUs <= StaleUs,
            QuietUs = Started ? now - (_lastMeasureUs > 0 ? _lastMeasureUs : _startUs) : _reading.QuietUs,
            HeartRate = fresh ? _lastHeartRate : 0,
        };
    }

    /// <summary>
    /// 화면에 보일 상태입니다. 포트가 닫혀 있으면 Off이고, 실패 여부는 부른 쪽이 Failure로 정합니다. 장치를 확인하기 전이면 연 뒤
    /// 시간에 따라 Checking이나 Preparing입니다.
    /// </summary>
    public PpgSnapshot Snapshot()
    {
        if (!IsOpen) return new PpgSnapshot();
        PpgReading r = Reading();
        PpgState state;
        if (!Found)
        {
            state = OpenUs < PpgFunctions.PpgPreparingAfterUs ? PpgState.Checking : PpgState.Preparing;
        }
        else if (r.Streaming)
        {
            state = PpgState.Streaming;
        }
        else
        {
            state = Started && _lastMeasureUs > 0 ? PpgState.Stalled : PpgState.Waiting;
        }
        return new PpgSnapshot
        {
            State = state,
            Port = PortName,
            HeartRate = r.HeartRate,
            Spo2 = r.Spo2,
            MeasureError = r.MeasureError,
            DeviceError = r.DeviceError,
            Problem = r.Problem,
        };
    }

    /// <summary>
    /// 최근에 받은 표본 값을 받은 순서대로 돌려줍니다. 화면 파형용입니다. 기록과 상관없이 시작 뒤 받은 측정 데이터의 표본을
    /// <see cref="RecentCapacity"/>개까지 보관하고, 시작하면 비웁니다.
    /// </summary>
    /// <param name="count">돌려받을 최대 표본 수입니다. 가장 최근 표본부터 셉니다.</param>
    /// <returns>오래된 표본부터 최근 표본까지입니다. 받은 표본이 없으면 빈 목록입니다.</returns>
    public IReadOnlyList<ushort> RecentValues(int count)
    {
        int n = Math.Min(count, _recent.Count);
        return _recent.Skip(_recent.Count - n).ToArray();
    }

    void Handle(PpgMessage m)
    {
        if (m.Source != PpgSource.Device) return;
        // CRC까지 맞는 장치 메시지라 PPG 장치로 봅니다. 기존 앱도 정보 응답, 상태, 측정 데이터 중 하나를 받으면 시작했습니다.
        Found = true;
        switch (m.Code)
        {
            case PpgCode.Info:
                DeviceInfo = Encoding.Latin1.GetString(m.Data);
                _record?.SetDeviceInfo(DeviceInfo);
                break;
            case PpgCode.Status:
                if (m.Data.Length == 0) break;
                _reading = _reading with { Status = m.Data[0] };
                _record?.AddEvent(new PpgEvent(new Timestamp(m.HostTimeUs), m.Code, m.Data[0]));
                break;
            case PpgCode.Error:
                if (m.Data.Length == 0) break;
                _reading = _reading with { DeviceError = m.Data[0] };
                _record?.AddEvent(new PpgEvent(new Timestamp(m.HostTimeUs), m.Code, m.Data[0]));
                break;
            case PpgCode.Measure:
            {
                PpgMeasure? measure = PpgWire.DecodeMeasure(m.Data);
                if (measure is null || !Started) break;
                if (measure.HeartRate > 0)
                {
                    _lastHeartRate = measure.HeartRate;
                    _lastHeartRateUs = m.HostTimeUs;
                }
                _lastMeasureUs = m.HostTimeUs;
                foreach (ushort sample in measure.Samples) _recent.Enqueue(sample);
                while (_recent.Count > RecentCapacity) _recent.Dequeue();
                _record?.AddMeasure(measure, _nextIndex, new Timestamp(m.HostTimeUs));
                _nextIndex += measure.Samples.Length;
                _reading = _reading with
                {
                    Spo2 = measure.Spo2,
                    MeasureError = measure.Error,
                    Samples = _nextIndex,
                };
                break;
            }
        }
    }

    bool Send(byte[] bytes, out string error)
    {
        if (_port is not null && _port.Write(bytes, WriteTimeoutMs))
        {
            error = "";
            return true;
        }
        error = "PPG 장치에 명령을 보내지 못했습니다";
        _reading = _reading with { Problem = error };
        return false;
    }

    static PpgParserCounts Minus(PpgParserCounts a, PpgParserCounts b) => new(
        a.CrcFailures - b.CrcFailures,
        a.BadLengths - b.BadLengths,
        a.Implausible - b.Implausible,
        a.SkippedBytes - b.SkippedBytes,
        a.Messages - b.Messages);
}
