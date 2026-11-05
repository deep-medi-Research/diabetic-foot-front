using System.Text;

namespace DiabeticFoot.Platform;

/// <summary>
/// 장비 없이 PPG 센서 사용자를 시험하는 PPG 장치입니다. FakeSerialLine의 장치 쪽에서 명령을 읽고 메시지를 넣습니다.
/// 시간을 스스로 흘려보내지 않습니다. 부르는 쪽이 HandleCommands, EmitMeasurement, EmitStatus를 불러 한 단계씩 진행합니다.
/// 맥파는 표본 번호로 정해지는 사인파라 같은 호출 순서에는 같은 바이트를 냅니다. 한 스레드에서만 부릅니다.
/// </summary>
public sealed class FakePpgDevice
{
    readonly FakeSerialLine _line;
    readonly PpgParser _commands = new();
    int _handledBytes;
    long _nextSample;
    bool _booting;

    public FakePpgDevice(FakeSerialLine line)
    {
        _line = line;
    }

    /// <summary>시작 명령을 받은 뒤 중지 명령이나 상태 'p'가 없었으면 true입니다.</summary>
    public bool Running { get; private set; }

    /// <summary>받아들인 시작 명령 수입니다. 측정 중에 받아 오류로 답한 시작 명령은 세지 않습니다.</summary>
    public int StartCount { get; private set; }

    public int StopCount { get; private set; }

    /// <summary>받은 정보 요청 수입니다. 초기화 중에 받아 답하지 않은 요청도 셉니다.</summary>
    public int InfoRequestCount { get; private set; }

    /// <summary>마지막으로 받아들인 시작 명령의 데이터(미리보기 초, 측정 초, 센서, 성별, 나이, 결과 기준)입니다.</summary>
    public byte[] LastStartData { get; private set; } = [];

    /// <summary>
    /// 지난 호출 뒤 포트에 쓴 명령에 답합니다.
    /// 측정 중이 아닐 때 시작 명령을 받으면 상태 'v'를 넣고 표본 번호를 0부터 셉니다. 측정 중에 시작 명령을 받으면
    /// 오류 's'만 넣고 측정을 이어갑니다. 측정 중에 중지 명령을 받으면 상태 'u'를 넣습니다. 정보 요청에는 장치 정보
    /// 15바이트를 넣습니다. 초기화 중(StartBooting부터 FinishBooting까지)에는 어떤 명령에도 답하지 않습니다.
    /// </summary>
    /// <returns>이번에 받은 명령입니다. 받은 순서입니다.</returns>
    /// <remarks>
    /// 측정 중 시작 명령의 오류 's'는 2026-10-20 실측(중지 없이 시작하면 8ms 뒤 오류 's', 측정 데이터는 이어짐)입니다.
    /// </remarks>
    public IReadOnlyList<PpgMessage> HandleCommands()
    {
        byte[] written = _line.Written();
        ReadOnlySpan<byte> fresh = written.AsSpan(_handledBytes);
        _handledBytes = written.Length;
        IReadOnlyList<PpgMessage> commands = _commands.Feed(fresh, 0);
        foreach (PpgMessage command in commands)
        {
            if (command.Source != PpgSource.Pc) continue;
            if (command.Code == (byte)'I') ++InfoRequestCount;
            if (_booting) continue;
            switch (command.Code)
            {
                case (byte)'D':
                    if (Running)
                    {
                        Emit(PpgCode.Error, [(byte)'s']);
                        break;
                    }
                    Running = true;
                    ++StartCount;
                    _nextSample = 0;
                    LastStartData = command.Data;
                    EmitStatus(PpgStatus.Preview);
                    break;
                case (byte)'P':
                    ++StopCount;
                    if (Running) EmitStatus(PpgStatus.Stopped);
                    Running = false;
                    break;
                case (byte)'I':
                    EmitInfo();
                    break;
            }
        }
        return commands;
    }

    /// <summary>측정 데이터 메시지 하나를 넣습니다. 측정 중이 아니어도 넣습니다.</summary>
    /// <param name="heartRate">장치 심박수(bpm)입니다. 계산 전이면 0입니다.</param>
    /// <param name="spo2">장치 SpO2(%)입니다. 계산 전이면 0입니다.</param>
    public void EmitMeasurement(int samples, byte heartRate, byte spo2, byte error = 0)
    {
        var data = new byte[samples * 2 + 3];
        for (int i = 0; i < samples; ++i)
        {
            ushort value = WaveAt(_nextSample++);
            data[2 * i] = (byte)(value >> 8);
            data[2 * i + 1] = (byte)(value & 0xFF);
        }
        data[^3] = heartRate;
        data[^2] = spo2;
        data[^1] = error;
        Emit(PpgCode.Measure, data);
    }

    /// <summary>상태 메시지 하나를 넣습니다. 'p'(장치 측정 끝)이면 측정을 끝낸 것으로 봅니다.</summary>
    public void EmitStatus(byte status)
    {
        if (status == PpgStatus.Finished) Running = false;
        Emit(PpgCode.Status, [status]);
    }

    /// <summary>
    /// 오류 메시지 하나를 넣습니다. 'f'와 'm'이면 측정을 끝낸 것으로 봅니다.
    /// </summary>
    /// <remarks>
    /// 손가락 없이 장치 미리보기가 끝나면 장치는 오류 'f'를 보내고 측정 데이터를 멈춥니다(2026-10-21 실측). 측정 구간에서
    /// 손가락이 빠지면 오류 'm'으로 측정을 끝낸다고 사양서가 적습니다.
    /// </remarks>
    public void EmitError(byte error)
    {
        if (error is (byte)'f' or (byte)'m') Running = false;
        Emit(PpgCode.Error, [error]);
    }

    /// <summary>전원을 켠 직후처럼 초기화를 시작합니다. FinishBooting까지 HandleCommands는 명령을 읽기만 하고 답하지 않습니다.</summary>
    public void StartBooting()
    {
        _booting = true;
        Running = false;
    }

    /// <summary>
    /// 초기화를 끝냅니다. 묻지 않아도 장치 정보와 상태 'o'를 넣고, 이후 명령에 답합니다.
    /// </summary>
    /// <remarks>
    /// 실기(2026-10-19)에서 전원을 켜고 약 42초 뒤 장치 정보와 상태 'o'가 왔습니다. 그 전에는 측정할 수 없습니다(사양서).
    /// </remarks>
    public void FinishBooting()
    {
        _booting = false;
        EmitInfo();
        EmitStatus(PpgStatus.Boot);
    }

    /// <summary>표본 번호의 맥파 값입니다. 2048을 중심으로 진폭 400, 1.2Hz(500Hz 기준) 사인파입니다.</summary>
    /// <param name="sample">시작 뒤 표본 번호입니다.</param>
    public static ushort WaveAt(long sample)
    {
        double t = (double)sample / PpgWire.SampleHz;
        // C++ std::lround와 같게 반올림합니다. 값이 모두 양수라 0에서 먼 쪽이 곧 .5 올림입니다.
        return (ushort)Math.Round(2048.0 + 400.0 * Math.Sin(2.0 * Math.PI * 1.2 * t), MidpointRounding.AwayFromZero);
    }

    void EmitInfo()
    {
        // 사양서 장치 정보 예제와 같은 15바이트입니다.
        Emit(PpgCode.Info, Encoding.ASCII.GetBytes("200000270270320"));
    }

    void Emit(byte code, byte[] data) => _line.Feed(PpgWire.BuildMessage(PpgSource.Device, code, data));
}
