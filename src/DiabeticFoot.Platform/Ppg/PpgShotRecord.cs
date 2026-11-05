using DiabeticFoot.Core;
using DiabeticFoot.Core.Record;
using DiabeticFoot.Core.Timing;

namespace DiabeticFoot.Platform;

/// <summary>
/// 촬영 하나 동안 받은 PPG 파형입니다(<see cref="PpgShotRecord.Finish"/>).
/// </summary>
/// <remarks>
/// 표본 i의 장치 시각은 시작 뒤 표본 번호 / 500Hz이고, 측정 데이터 메시지마다 (마지막 표본의 장치 시각, PC 수신 시각) 쌍을 남겨
/// 공통 시간축으로 옮깁니다. 수신 빈틈(<see cref="PpgLink.ReceiveGapUs"/>)에서 시계 구간을 나눕니다.
/// </remarks>
public sealed record PpgRecord
{
    /// <summary>파형 표본 값입니다.</summary>
    public IReadOnlyList<ushort> Values { get; init; } = Array.Empty<ushort>();

    /// <summary>표본마다 시작 뒤 장치 시각(표본 번호 / 500Hz, 마이크로초)입니다.</summary>
    public IReadOnlyList<Timestamp> DeviceTime { get; init; } = Array.Empty<Timestamp>();

    /// <summary>표본마다 공통 시각(PC 시계)입니다. 그 표본이 든 Clock의 시계 구간 식으로 DeviceTime을 옮긴 값입니다.</summary>
    public IReadOnlyList<Timestamp> CommonTime { get; init; } = Array.Empty<Timestamp>();

    /// <summary>표본마다 그 표본이 든 메시지의 측정 오류(0 정상, 'f' 손가락 빠짐 등)입니다.</summary>
    public IReadOnlyList<byte> MeasureError { get; init; } = Array.Empty<byte>();

    /// <summary>표본마다 그 표본이 든 메시지의 장치 심박수(bpm)입니다.</summary>
    public IReadOnlyList<byte> HeartRate { get; init; } = Array.Empty<byte>();

    /// <summary>표본마다 그 표본이 든 메시지의 장치 SpO2(%)입니다.</summary>
    public IReadOnlyList<byte> Spo2 { get; init; } = Array.Empty<byte>();

    /// <summary>측정 데이터 메시지마다 (마지막 표본 장치 시각, PC 수신 시각)입니다.</summary>
    public IReadOnlyList<ClockSample> Anchors { get; init; } = Array.Empty<ClockSample>();

    /// <summary>Anchors를 수신 빈틈에서 나눠 맞춘 시계 구간입니다. 장치를 다시 시작한 직후 빈틈이 있으면 둘 이상입니다.</summary>
    public IReadOnlyList<ClockSegment> Clock { get; init; } = Array.Empty<ClockSegment>();

    /// <summary>촬영 버튼을 누른 PC 시각입니다.</summary>
    public Timestamp BeginTime { get; init; }

    /// <summary>촬영을 끝낸 PC 시각입니다.</summary>
    public Timestamp EndTime { get; init; }

    /// <summary>기록하는 동안 해석기가 센 메시지 수와 버린 바이트입니다.</summary>
    public PpgParserCounts Counts { get; init; }

    /// <summary>기록하는 동안 받은 상태 메시지와 오류 메시지입니다. 받은 순서입니다.</summary>
    public IReadOnlyList<PpgEvent> Events { get; init; } = Array.Empty<PpgEvent>();

    /// <summary>기록 중 포트가 닫힌 PC 시각입니다. 끊기지 않았으면 비웁니다. 끊기기 전까지 받은 표본은 그대로 남습니다.</summary>
    public Timestamp? DisconnectedAt { get; init; }

    /// <summary>포트가 닫힌 이유입니다. 끊기지 않았으면 빈 문자열입니다.</summary>
    public string DisconnectReason { get; init; } = "";

    /// <summary>장치 정보 응답의 데이터입니다(<see cref="PpgLink.DeviceInfo"/>). 받지 못했으면 빈 문자열입니다.</summary>
    public string DeviceInfo { get; init; } = "";
}

/// <summary>
/// 촬영 하나의 PPG 기록입니다(PPG 촬영 기록). 촬영이 만들어 가지고, 장치 스레드는 기록하는 동안 표본과 상태 메시지를 더합니다.
/// </summary>
/// <remarks>
/// 기록의 수명이 포트 연결과 따로라서, 녹화 중 연결이 끊겨도 그때까지 받은 표본은 남고 끊긴 시각만 적힙니다. 촬영을 끝내는 쪽은
/// Close만 부르고 장치 스레드를 기다리지 않습니다. 모든 함수는 여러 스레드에서 불러도 됩니다.
/// </remarks>
public sealed class PpgShotRecord
{
    const long SampleUs = 1_000_000 / PpgWire.SampleHz;

    readonly object _gate = new();
    // 아래는 모두 _gate 아래에서 읽고 씁니다.
    readonly List<ushort> _values = [];
    readonly List<Timestamp> _deviceTime = [];
    readonly List<byte> _measureError = [];
    readonly List<byte> _heartRate = [];
    readonly List<byte> _spo2 = [];
    readonly List<ClockSample> _anchors = [];
    readonly List<PpgEvent> _events = [];
    readonly Timestamp _beginTime;
    Timestamp _endTime;
    PpgParserCounts _counts;
    Timestamp? _disconnectedAt;
    string _disconnectReason = "";
    string _deviceInfo = "";
    bool _closed;

    /// <param name="begin">촬영 버튼을 누른 PC 시각입니다.</param>
    public PpgShotRecord(Timestamp begin)
    {
        _beginTime = begin;
    }

    /// <summary>촬영을 끝냅니다. 이 뒤로 더하는 표본과 메시지는 버립니다. 두 번째부터는 아무것도 하지 않습니다.</summary>
    public void Close(Timestamp end)
    {
        lock (_gate)
        {
            if (_closed) return;
            _closed = true;
            _endTime = end;
        }
    }

    /// <summary>Close를 불렀으면 true입니다.</summary>
    public bool Closed
    {
        get
        {
            lock (_gate) return _closed;
        }
    }

    /// <summary>
    /// 지금까지 더한 표본을 시계 구간으로 맞춰 돌려줍니다. 부른 스레드에서 계산합니다. 표본마다 공통 시각을 채우고, 측정 데이터
    /// 사이 수신 빈틈에서 시계 구간을 나눕니다.
    /// </summary>
    public PpgRecord Finish()
    {
        PpgRecord copy;
        lock (_gate)
        {
            copy = new PpgRecord
            {
                Values = _values.ToArray(),
                DeviceTime = _deviceTime.ToArray(),
                MeasureError = _measureError.ToArray(),
                HeartRate = _heartRate.ToArray(),
                Spo2 = _spo2.ToArray(),
                Anchors = _anchors.ToArray(),
                BeginTime = _beginTime,
                EndTime = _endTime,
                Counts = _counts,
                Events = _events.ToArray(),
                DisconnectedAt = _disconnectedAt,
                DisconnectReason = _disconnectReason,
                DeviceInfo = _deviceInfo,
            };
        }
        // 다시 시작 직후 먼저 온 측정 데이터까지 한 식으로 맞추면 빈틈 뒤 표본의 공통 시각이 빈틈만큼 이르게 나옵니다.
        IReadOnlyList<ClockSegment> clock = TimingFunctions.FitClockSegments(copy.Anchors, PpgLink.ReceiveGapUs);
        var common = new Timestamp[copy.DeviceTime.Count];
        for (int i = 0; i < common.Length; ++i) common[i] = TimingFunctions.ToCommonTime(clock, copy.DeviceTime[i]);
        return copy with { Clock = clock, CommonTime = common };
    }

    /// <summary>측정 데이터 하나의 표본을 더합니다. 장치 스레드가 부릅니다.</summary>
    /// <param name="firstIndex">이 메시지 첫 표본의 시작 뒤 표본 번호입니다.</param>
    /// <param name="hostTime">메시지를 받은 PC 시각입니다.</param>
    public void AddMeasure(PpgMeasure measure, long firstIndex, Timestamp hostTime)
    {
        int count = measure.Samples.Length;
        if (count == 0) return;
        lock (_gate)
        {
            if (_closed) return;
            for (int i = 0; i < count; ++i)
            {
                _values.Add(measure.Samples[i]);
                _deviceTime.Add(new Timestamp((firstIndex + i) * SampleUs));
                _measureError.Add(measure.Error);
                _heartRate.Add((byte)measure.HeartRate);
                _spo2.Add((byte)measure.Spo2);
            }
            // 메시지의 마지막 표본이 PC에 닿은 시각이 수신 시각입니다. 앞 표본들은 시계 맞춤식으로 시각을 정합니다.
            _anchors.Add(new ClockSample(new Timestamp((firstIndex + count - 1) * SampleUs), hostTime));
        }
    }

    /// <summary>상태 메시지나 오류 메시지 하나를 더합니다. 장치 스레드가 부릅니다.</summary>
    public void AddEvent(PpgEvent value)
    {
        lock (_gate)
        {
            if (!_closed) _events.Add(value);
        }
    }

    /// <summary>기록하는 동안의 해석기 집계를 바꿉니다. 장치 스레드가 부릅니다.</summary>
    public void SetCounts(PpgParserCounts counts)
    {
        lock (_gate)
        {
            if (!_closed) _counts = counts;
        }
    }

    /// <summary>포트가 닫힌 시각과 이유를 적습니다. 처음 한 번만 적고, Close 뒤에는 적지 않습니다. 장치 스레드가 부릅니다.</summary>
    public void MarkDisconnected(Timestamp at, string why)
    {
        lock (_gate)
        {
            if (_closed || _disconnectedAt.HasValue) return;
            _disconnectedAt = at;
            _disconnectReason = why;
        }
    }

    /// <summary>장치 정보 응답의 데이터를 적습니다. Close 뒤에는 적지 않습니다. 장치 스레드가 부릅니다.</summary>
    public void SetDeviceInfo(string info)
    {
        lock (_gate)
        {
            if (!_closed) _deviceInfo = info;
        }
    }
}
