using System.Globalization;
using DiabeticFoot.Core;
using DiabeticFoot.Core.Record;

namespace DiabeticFoot.Platform;

/// <summary>가짜 열화상 카메라가 만들 상황 목록입니다. 누락, 연결 끊김, 재연결, 수신 간격 변동을 만듭니다.</summary>
public sealed record FakeThermalScript
{
    public int Width { get; init; } = 640;

    public int Height { get; init; } = 480;

    /// <summary>보낸 프레임 수가 키와 같을 때 그 앞에서 놓칠 장 수입니다.</summary>
    public IReadOnlyDictionary<long, int> Skips { get; init; } = new Dictionary<long, int>();

    /// <summary>이만큼 보낸 뒤 프레임이 오지 않습니다(열린 상태는 유지). -1이면 끊기지 않습니다.</summary>
    public long DisconnectAfter { get; init; } = -1;

    /// <summary>끊긴 뒤 Open이 실패하는 횟수입니다. -1이면 다시 연결되지 않습니다.</summary>
    public int ReopenFailures { get; init; } = -1;

    /// <summary>장치가 프레임을 내는 간격(30Hz)입니다.</summary>
    public long DevicePeriodUs { get; init; } = 33_333;

    /// <summary>수신 지연의 최대값입니다. 프레임마다 0 이상 이 값 이하에서 고릅니다.</summary>
    public long JitterUs { get; init; }

    /// <summary>같은 값이면 같은 지연 순서가 나옵니다.</summary>
    public uint JitterSeed { get; init; } = 1;

    /// <summary>ReadFrame 한 번에 실제로 기다리는 간격입니다. 수신 시각과 따로 두어 테스트를 빨리 끝냅니다.</summary>
    public int WallPeriodUs { get; init; } = 1000;

    public ThermalLossRule LossRule { get; init; } = new();

    /// <summary>
    /// true이면 가상 시계 대신 PC 시계를 씁니다. ReadFrame은 프레임 수신 시각까지 실제로 기다리고 WallPeriodUs는 쓰지 않습니다.
    /// <see cref="FakeCameraScript.PcClock"/>과 함께 켜면 가짜 RGB와 같은 시간축이 됩니다.
    /// </summary>
    public bool PcClock { get; init; }

    /// <summary>false이면 CalibrateNow와 HoldAutoCalibration이 실패합니다(원래 설정을 모르는 장치).</summary>
    public bool CalibrationSupported { get; init; } = true;

    /// <summary>true이면 CorrectLensShading이 실패합니다.</summary>
    public bool LensShadingFails { get; init; }
}

/// <summary>
/// 장비 없이 열화상 저장과 끊김 처리를 시험하는 카메라입니다.
/// </summary>
/// <remarks>
/// 시각은 기본으로 시나리오가 정한 가상 시계(마이크로초, 0부터)이고, PcClock이면 PC 시계입니다. 장치 프레임은 연 시각부터
/// DevicePeriodUs 간격으로 나오고(누락된 장의 자리도 셈) 0~JitterUs의 수신 지연 뒤에 도착합니다. 화소 값은 PixelValue입니다.
/// 처음에는 닫혀 있으므로 Open을 먼저 부릅니다.
/// 렌즈 셰이딩 보정은 실제 구현과 같게 동작합니다. 보정 파일 폴더를 정하면 열 때마다 lens_shading_FakeThermal.cal이 있으면
/// 불러오고, CorrectLensShading이 그 파일을 씁니다. 파일 내용은 보정한 PC 시각(UTC 유닉스 초) 한 줄이며, 그 값을 FileUnixS로
/// 씁니다. 화소 값은 바꾸지 않습니다.
/// </remarks>
public sealed class FakeThermalCamera : IThermalCamera
{
    // DeviceName과 보정 파일 이름이 같은 이름을 쓰게 합니다(실제 구현은 장치 이름으로 파일 이름을 만듭니다).
    const string FakeDeviceName = "FakeThermal";

    readonly FakeThermalScript _script;
    readonly byte[] _pixels;
    readonly object _gate = new();
    // 아래는 모두 _gate 아래에서 읽고 씁니다.
    long _disconnectAfter;
    bool _open;
    bool _connected = true;  // 장치가 USB에 붙어 있는지. 열린 상태와 따로 둡니다
    int _reopenAttempts;
    long _sent;
    long _deviceFrame;  // 다음에 낼 장치 프레임 번호
    long _lastDeviceFrame = -1;
    long _nowUs;
    long _nextIdealUs;  // 다음 장치 프레임이 나오는 시각(지연 전)
    long? _pendingArrivalUs;  // 누락된 장을 반영하고 지연을 더한 다음 프레임의 수신 시각
    Timestamp _openedAt;
    Timestamp? _lastFrame;
    uint _jitterState;
    int _calibrations;
    bool _held;
    string _lensShadingFolder = "";  // 비면 보정 기능이 없는 장치
    LensShadingStatus? _lensShading;  // 지금 연결의 보정 상태
    long _nextDueUs;  // 수신 스레드만 씁니다. 0이면 아직 정하지 않음

    public FakeThermalCamera(FakeThermalScript script)
    {
        _script = script;
        _pixels = new byte[script.Width * script.Height * 2];
        _disconnectAfter = script.DisconnectAfter;
        _jitterState = script.JitterSeed;
    }

    /// <summary>
    /// 장치 프레임의 화소 값입니다. 행마다, 프레임마다 값이 달라 바이트 순서나 행 어긋남을 찾을 수 있습니다.
    /// </summary>
    /// <param name="deviceFrame">누락된 장까지 센 장치 프레임 번호입니다.</param>
    /// <returns>7000 이상 8999 이하의 원시값입니다.</returns>
    public static ushort PixelValue(long deviceFrame, int x, int y) => (ushort)(7000 + (x + 2 * (long)y + 5 * deviceFrame) % 2000);

    public bool Open(out string error)
    {
        lock (_gate)
        {
            _open = false;
            if (!_connected)
            {
                if (_script.ReopenFailures < 0 || _reopenAttempts++ < _script.ReopenFailures)
                {
                    error = "열화상 카메라를 찾지 못했습니다";
                    return false;
                }
                _connected = true;
                _disconnectAfter = -1;
            }
            if (_script.PcClock) _nowUs = SteadyClock.NowUs();
            _open = true;
            _lensShading = _lensShadingFolder.Length == 0 ? null : LoadLensShadingLocked();
            _openedAt = new Timestamp(_nowUs);
            _lastFrame = null;
            _nextIdealUs = _nowUs + _script.DevicePeriodUs;
            _pendingArrivalUs = null;
            _nextDueUs = 0;
            error = "";
            return true;
        }
    }

    public void Close()
    {
        lock (_gate)
        {
            _open = false;
            _held = false;
        }
    }

    public string DeviceName
    {
        get
        {
            lock (_gate) return _open ? FakeDeviceName : "";
        }
    }

    public int Width
    {
        get
        {
            lock (_gate) return _open ? _script.Width : 0;
        }
    }

    public int Height
    {
        get
        {
            lock (_gate) return _open ? _script.Height : 0;
        }
    }

    public ReceivedFrame? ReadFrame(long timeoutUs)
    {
        if (_script.PcClock) return ReadFrameOnPcClock(timeoutUs);
        // 실제 수신 스레드처럼 한 번 부를 때마다 조금 기다립니다. 정해진 시각에 맞춰 만드는 시간까지 간격에 넣습니다.
        if (_script.WallPeriodUs > 0)
        {
            long now = SteadyClock.NowUs();
            if (_nextDueUs == 0 || _nextDueUs < now) _nextDueUs = now;
            _nextDueUs += _script.WallPeriodUs;
            SteadyClock.SleepUntilUs(_nextDueUs);
        }
        lock (_gate)
        {
            if (!_open) return null;
            if (_disconnectAfter >= 0 && _sent >= _disconnectAfter) _connected = false;
            if (!_connected)
            {
                // 실제 SDK는 뽑아도 열린 상태로 남고 프레임만 주지 않으므로 시간만 흐릅니다.
                _nowUs += timeoutUs;
                return null;
            }
            long arrival = PlanNextArrivalLocked();
            if (arrival - _nowUs > timeoutUs)
            {
                _nowUs += timeoutUs;
                return null;
            }
            _nowUs = Math.Max(_nowUs, arrival);
            _pendingArrivalUs = null;
            return EmitLocked();
        }
    }

    ReceivedFrame? ReadFrameOnPcClock(long timeoutUs)
    {
        long? arrival = null;
        long waitUs;
        lock (_gate)
        {
            _nowUs = SteadyClock.NowUs();
            if (!_open) return null;
            if (_disconnectAfter >= 0 && _sent >= _disconnectAfter) _connected = false;
            if (_connected) arrival = PlanNextArrivalLocked();
            waitUs = arrival is { } due ? due - _nowUs : timeoutUs + 1;
        }
        // 다른 스레드의 Connection, Now 호출을 막지 않도록 기다리는 동안 잠금을 풉니다.
        SteadyClock.SleepUntilUs(SteadyClock.NowUs() + Math.Clamp(waitUs, 0, timeoutUs));
        lock (_gate)
        {
            _nowUs = SteadyClock.NowUs();
            if (!_open || arrival is not { } due || waitUs > timeoutUs) return null;
            _pendingArrivalUs = null;
            // Windows의 대기 단위(약 15ms)만큼 늦게 반환되어도 프레임 시각은 예정 도착 시각으로 두어 간격이 대기 단위에 좌우되지 않게 합니다.
            _nowUs = Math.Min(_nowUs, due);
            return EmitLocked();
        }
    }

    long PlanNextArrivalLocked()
    {
        if (_pendingArrivalUs is { } pending) return pending;
        if (_script.Skips.TryGetValue(_sent, out int skipped))
        {
            _deviceFrame += skipped;
            _nextIdealUs += skipped * _script.DevicePeriodUs;
        }
        _jitterState = _jitterState * 1664525u + 1013904223u;
        long delay = _script.JitterUs > 0 ? (_jitterState >> 8) % (_script.JitterUs + 1) : 0;
        long floor = _lastFrame is { } last ? last.Us + 1 : 0;
        long arrival = Math.Max(_nextIdealUs + delay, floor);
        _pendingArrivalUs = arrival;
        return arrival;
    }

    ReceivedFrame EmitLocked()
    {
        int width = _script.Width;
        for (int y = 0; y < _script.Height; ++y)
        {
            int row = y * width * 2;
            for (int x = 0; x < width; ++x)
            {
                ushort v = PixelValue(_deviceFrame, x, y);
                _pixels[row + x * 2] = (byte)(v & 0xFF);
                _pixels[row + x * 2 + 1] = (byte)(v >> 8);
            }
        }
        var time = new Timestamp(_nowUs);
        var view = new FrameView
        {
            Format = PixelFormat.Y16,
            Width = width,
            Height = _script.Height,
            Plane0 = _pixels,
            RowStride0 = width * 2,
            Stream = StreamKind.Thermal,
            Index = (ulong)_sent,
            Time = time,
        };
        _lastFrame = time;
        _lastDeviceFrame = _deviceFrame;
        ++_sent;
        ++_deviceFrame;
        _nextIdealUs += _script.DevicePeriodUs;
        return new ReceivedFrame(view, time);
    }

    long ClockNowLocked() => _script.PcClock ? SteadyClock.NowUs() : _nowUs;

    public ThermalConnection Connection
    {
        get
        {
            lock (_gate)
            {
                return ThermalCamera.JudgeThermalConnection(
                    _open, _openedAt, _lastFrame, new Timestamp(ClockNowLocked()), _script.LossRule);
            }
        }
    }

    public bool CalibrateNow(out string error)
    {
        lock (_gate)
        {
            if (!_open || !_script.CalibrationSupported)
            {
                error = _open ? "셔터 교정을 지원하지 않습니다" : "열려 있지 않습니다";
                return false;
            }
            ++_calibrations;
            error = "";
            return true;
        }
    }

    public bool HoldAutoCalibration(bool hold, out string error)
    {
        lock (_gate)
        {
            if (!_open || !_script.CalibrationSupported)
            {
                error = _open ? "원래 셔터 모드를 모릅니다" : "열려 있지 않습니다";
                return false;
            }
            _held = hold;
            error = "";
            return true;
        }
    }

    public string AutoCalibrationSetting
    {
        get
        {
            lock (_gate)
            {
                if (!_open) return "";
                return _held ? "fake, held" : "fake";
            }
        }
    }

    public void SetLensShadingFolder(string folder)
    {
        lock (_gate) _lensShadingFolder = folder;
    }

    public LensShadingStatus? LensShading
    {
        get
        {
            lock (_gate) return _open ? _lensShading : null;
        }
    }

    public bool CorrectLensShading(out string error)
    {
        lock (_gate)
        {
            if (!_open || _lensShadingFolder.Length == 0)
            {
                error = _open ? "렌즈 셰이딩 보정 파일 폴더가 정해지지 않았습니다" : "열려 있지 않습니다";
                return false;
            }
            if (_script.LensShadingFails)
            {
                _lensShading = new LensShadingStatus(new LensShadingRecord(LensShadingSource.Failed), "렌즈 셰이딩 보정이 실패했습니다");
                error = _lensShading.Error;
                return false;
            }
            long nowS = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
            string path = LensShadingPathLocked();
            bool saved;
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(path)!);
                File.WriteAllText(path, nowS.ToString(CultureInfo.InvariantCulture) + "\n");
                saved = true;
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                saved = false;
            }
            _lensShading = saved
                ? new LensShadingStatus(new LensShadingRecord(LensShadingSource.Corrected, nowS))
                : new LensShadingStatus(new LensShadingRecord(LensShadingSource.Corrected), "보정은 적용했지만 보정 파일을 저장하지 못했습니다");
            error = "";
            return true;
        }
    }

    string LensShadingPathLocked() => Path.Combine(_lensShadingFolder, ThermalCamera.LensShadingFileName(FakeDeviceName));

    LensShadingStatus LoadLensShadingLocked()
    {
        string text;
        try
        {
            text = File.ReadAllText(LensShadingPathLocked());
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return new LensShadingStatus();
        }
        // 원본은 istream >> int64_t로 읽어 앞 공백을 건너뛰고 첫 정수만 봅니다.
        ReadOnlySpan<char> rest = text.AsSpan().TrimStart();
        int end = rest.Length > 0 && (rest[0] == '+' || rest[0] == '-') ? 1 : 0;
        while (end < rest.Length && char.IsAsciiDigit(rest[end])) ++end;
        if (!long.TryParse(rest[..end], NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out long savedS))
        {
            return new LensShadingStatus(new LensShadingRecord(LensShadingSource.Failed), "보정 파일을 불러오지 못했습니다");
        }
        return new LensShadingStatus(new LensShadingRecord(LensShadingSource.Loaded, savedS));
    }

    /// <summary>CalibrateNow가 성공한 횟수입니다.</summary>
    public int Calibrations
    {
        get
        {
            lock (_gate) return _calibrations;
        }
    }

    /// <summary>지금 자동 셔터 교정을 멈춘 상태인지입니다. Close가 되돌리면 false입니다.</summary>
    public bool AutoCalibrationHeld
    {
        get
        {
            lock (_gate) return _held;
        }
    }

    /// <summary>이 카메라 시계(가상 시계 또는 PC 시계)의 지금 시각입니다.</summary>
    public Timestamp Now()
    {
        lock (_gate) return new Timestamp(ClockNowLocked());
    }

    /// <summary>방금 받은 프레임의 장치 프레임 번호입니다. 받은 프레임이 없으면 -1입니다.</summary>
    public long LastDeviceFrame
    {
        get
        {
            lock (_gate) return _lastDeviceFrame;
        }
    }

    public void Dispose() => Close();
}
