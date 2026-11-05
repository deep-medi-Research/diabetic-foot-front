using DiabeticFoot.Core;
using DiabeticFoot.Core.Flow;

namespace DiabeticFoot.Platform;

/// <summary>가짜 카메라의 시나리오입니다. 끊김, 재연결, 누락, fps 저하, 장치 시계의 오프셋과 속도 차이를 재현합니다.</summary>
public sealed record FakeCameraScript
{
    public int Width { get; init; } = 64;

    public int Height { get; init; } = 32;

    /// <summary>열었을 때의 설정입니다.</summary>
    public CameraState Initial { get; init; }

    /// <summary>프레임 번호 앞에서 카메라가 빠뜨릴 장 수입니다.</summary>
    public IReadOnlyDictionary<long, int> Skips { get; init; } = new Dictionary<long, int>();

    /// <summary>이만큼 보낸 뒤 연결이 끊깁니다. -1이면 끊기지 않습니다.</summary>
    public long DisconnectAfter { get; init; } = -1;

    /// <summary>끊긴 뒤 Reopen이 실패하는 횟수입니다. -1이면 다시 연결되지 않습니다.</summary>
    public int ReopenFailures { get; init; } = -1;

    /// <summary>다시 연결되면 돌아가는 설정(실제 카메라의 공장 기본값)입니다.</summary>
    public CameraState Factory { get; init; }

    /// <summary>실제로 기다리는 간격입니다. 장치 시각과 따로 두어 테스트를 빨리 끝냅니다.</summary>
    public int WallPeriodUs { get; init; } = 1000;

    /// <summary>장치 시각 간격입니다. 0이면 SDK fps를 따릅니다. 카메라가 fps를 낮춘 경우를 재현합니다.</summary>
    public long DevicePeriodUs { get; init; }

    /// <summary>
    /// true이면 PC 시계로 촬영 시각과 수신 시각을 매깁니다. 프레임 간격은 WallPeriodUs이고 DevicePeriodUs와 SDK fps는 쓰지
    /// 않습니다. <see cref="FakeThermalScript.PcClock"/>과 함께 켜면 두 가짜 장치가 같은 시간축을 씁니다.
    /// </summary>
    public bool PcClock { get; init; }

    /// <summary>장치 시각 = DeviceOffsetUs + DeviceRate * 촬영 시각(반올림)입니다.</summary>
    public long DeviceOffsetUs { get; init; }

    /// <summary>장치 시계가 촬영 시각 1마이크로초 동안 세는 마이크로초입니다.</summary>
    public double DeviceRate { get; init; } = 1.0;

    /// <summary>
    /// PcClock일 때 수신 시각 = 촬영 시각 + HostLatencyUs + (0 이상 HostJitterUs 이하)입니다. OS 대기 시간과 관계없이
    /// 같은 시나리오에는 같은 수신 지연이 나오게 실제로 돌아온 시각 대신 이 값을 씁니다.
    /// </summary>
    public long HostLatencyUs { get; init; }

    public long HostJitterUs { get; init; }

    public uint HostJitterSeed { get; init; } = 1;

    /// <summary>이만큼 보낸 뒤 ReadFrame이 Interrupt 전까지 돌아오지 않습니다. -1이면 멈추지 않습니다. 프레임이 오지 않는 장치를 재현합니다.</summary>
    public long StallAfter { get; init; } = -1;

    /// <summary>true이면 화소 바이트마다 다른 값(<see cref="FakeRgbCamera.PatternByte"/>)을 냅니다. 영역 자르기를 바이트 단위로 확인할 때 씁니다.</summary>
    public bool VariedPixels { get; init; }
}

/// <summary>
/// 장비 없이 촬영 파이프라인을 시험하는 카메라입니다.
/// </summary>
/// <remarks>
/// 프레임 i의 모든 화소 Y는 (i % 200) + 20이고 U와 V는 128입니다(VariedPixels이면 PatternByte). 설정 명령은 실제 카메라처럼
/// 상태에 반영합니다. 촬영 시각은 PcClock이 false이면 0부터 세는 가상 시각이고, true이면 PC 시계로 프레임을 내보내기로 정한
/// 시각입니다. View.Time은 촬영 시각을 DeviceOffsetUs와 DeviceRate로 옮긴 장치 시각입니다. HostTime은 PcClock이 false이면
/// 촬영 시각과 같고, true이면 촬영 시각에 시나리오의 수신 지연을 더한 PC 시각입니다.
/// </remarks>
public sealed class FakeRgbCamera : IRgbCamera
{
    readonly FakeCameraScript _script;
    readonly byte[] _pixels;
    readonly object _gate = new();
    // _gate 아래에서 읽고 씁니다.
    CameraState _state;
    readonly List<SettingCommand> _executed = [];
    bool _interrupted;
    int _interrupts;
    // 수신 스레드에서만 읽고 씁니다.
    long _disconnectAfter;
    long _sent;
    bool _connected = true;
    int _reopenAttempts;
    long _nextDueUs;  // 다음 프레임을 내보낼 SteadyClock 시각. 0이면 아직 정하지 않음
    long _deviceFrame;  // 빠뜨린 장까지 센 카메라 프레임 번호
    uint _jitterState;

    public FakeRgbCamera(FakeCameraScript script)
    {
        _script = script;
        _pixels = new byte[script.Width * script.Height * 2];
        _state = script.Initial;
        _disconnectAfter = script.DisconnectAfter;
        _jitterState = script.HostJitterSeed;
    }

    public string DeviceName => "FakeCamera";

    public ReceivedFrame? ReadFrame()
    {
        if (_script.StallAfter >= 0 && _sent >= _script.StallAfter)
        {
            lock (_gate)
            {
                while (!_interrupted) Monitor.Wait(_gate);
            }
        }
        lock (_gate)
        {
            if (_interrupted) return null;
        }
        if (_disconnectAfter >= 0 && _sent >= _disconnectAfter) _connected = false;
        if (!_connected) return null;
        // 프레임을 만드는 시간까지 간격 안에 들어가도록 정해진 시각에 맞춰 내보냅니다.
        // 기다린 뒤 만들면 간격이 (기다림 + 만들기)로 늘어나 30fps가 20fps로 떨어졌습니다.
        long now = SteadyClock.NowUs();
        if (_nextDueUs == 0) _nextDueUs = now;
        int skipped = _script.Skips.TryGetValue(_sent, out int skip) ? skip : 0;
        // PC 시계를 쓰면 빠진 장의 시간도 실제로 지나야 다른 가짜 장치와 시각이 맞습니다.
        if (_script.PcClock) _nextDueUs += (long)_script.WallPeriodUs * skipped;
        _nextDueUs += _script.WallPeriodUs;
        SteadyClock.SleepUntilUs(_nextDueUs);
        _deviceFrame += skipped;
        int fps = 30;
        lock (_gate)
        {
            if (_state.SdkFrameRate is > 0 and int rate) fps = rate;
        }
        if (_script.VariedPixels)
        {
            int rowBytes = _script.Width * 2;
            for (int r = 0; r < _script.Height; ++r)
            {
                for (int b = 0; b < rowBytes; ++b) _pixels[r * rowBytes + b] = PatternByte(_sent, b, r);
            }
        }
        else
        {
            byte y = (byte)(_sent % 200 + 20);
            for (int i = 0; i < _pixels.Length; i += 2)
            {
                _pixels[i] = 128;
                _pixels[i + 1] = y;
            }
        }
        long captureUs;
        if (_script.PcClock)
        {
            captureUs = _nextDueUs;
        }
        else
        {
            captureUs = _script.DevicePeriodUs > 0 ? _deviceFrame * _script.DevicePeriodUs : _deviceFrame * 1_000_000 / fps;
        }
        var view = new FrameView
        {
            Format = PixelFormat.Uyvy,
            Width = _script.Width,
            Height = _script.Height,
            Plane0 = _pixels,
            RowStride0 = _script.Width * 2,
            Index = (ulong)_sent,
            Time = new Timestamp(_script.DeviceOffsetUs + (long)Math.Round(_script.DeviceRate * captureUs, MidpointRounding.AwayFromZero)),
        };
        long latencyUs = 0;
        if (_script.PcClock)
        {
            _jitterState = _jitterState * 1664525u + 1013904223u;
            latencyUs = _script.HostLatencyUs + (_script.HostJitterUs > 0 ? (_jitterState >> 8) % (_script.HostJitterUs + 1) : 0);
        }
        ++_sent;
        ++_deviceFrame;
        return new ReceivedFrame(view, new Timestamp(captureUs + latencyUs));
    }

    /// <summary>VariedPixels일 때 프레임의 바이트 값입니다. 위치와 프레임마다 다른 값입니다.</summary>
    /// <param name="index">프레임 번호(View.Index)입니다.</param>
    /// <param name="byteX">행 안의 바이트 위치입니다. UYVY이므로 화소 x의 바이트는 2x와 2x+1입니다.</param>
    /// <param name="y">행입니다.</param>
    public static byte PatternByte(long index, int byteX, int y) => (byte)(((long)byteX * 7 + (long)y * 13 + index * 3) % 251);

    public bool Reopen()
    {
        lock (_gate)
        {
            if (_interrupted) return false;
        }
        if (_connected) return true;
        if (_script.ReopenFailures < 0 || _reopenAttempts++ < _script.ReopenFailures) return false;
        lock (_gate)
        {
            _connected = true;
            _disconnectAfter = -1;
            _state = _script.Factory;
            _nextDueUs = 0;
        }
        return true;
    }

    public CameraState ReadState()
    {
        lock (_gate)
        {
            // 실제 카메라처럼 펌웨어 자동 노출 중에는 노출값을 읽지 못합니다.
            return _state.ExposureAuto == true ? _state with { ExposureStep = null } : _state;
        }
    }

    public CameraState ReadStoredState()
    {
        lock (_gate) return _state;
    }

    public bool Execute(SettingCommand command)
    {
        lock (_gate)
        {
            _executed.Add(command);
            _state = command.Kind switch
            {
                CommandKind.SdkFrameRate => _state with { SdkFrameRate = command.Value },
                CommandKind.Unlock => _state with { Locked = false },
                CommandKind.Lock => _state with { Locked = true },
                CommandKind.ExposureAuto => _state with { ExposureAuto = true },
                CommandKind.ExposureManual => _state with { ExposureAuto = false, ExposureStep = command.Value },
                CommandKind.Gain => _state with { Gain = command.Value },
                CommandKind.WhiteBalanceAuto => _state with { WhiteBalanceAuto = true },
                CommandKind.WhiteBalanceManual => _state with { WhiteBalanceAuto = false, WhiteBalanceK = command.Value },
                _ => _state,
            };
        }
        return true;
    }

    /// <summary>지금까지 실행한 설정 명령입니다. 테스트에서 순서를 확인할 때 씁니다.</summary>
    public IReadOnlyList<SettingCommand> ExecutedCommands()
    {
        lock (_gate) return _executed.ToArray();
    }

    /// <summary>지금 설정입니다. ReadState와 달리 자동 노출 중에도 노출값을 감추지 않습니다.</summary>
    public CameraState State()
    {
        lock (_gate) return _state;
    }

    public void Interrupt()
    {
        lock (_gate)
        {
            _interrupted = true;
            ++_interrupts;
            Monitor.PulseAll(_gate);
        }
    }

    /// <summary>Interrupt가 불린 횟수입니다.</summary>
    public int Interrupts()
    {
        lock (_gate) return _interrupts;
    }

    public void Dispose()
    {
    }
}
