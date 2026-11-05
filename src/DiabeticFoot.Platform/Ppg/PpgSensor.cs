using DiabeticFoot.Core;

namespace DiabeticFoot.Platform;

/// <summary>
/// 앱이 도는 동안 하나 두는 PPG 장치 스레드입니다.
/// </summary>
/// <remarks>
/// 포트, 해석기, 상태는 장치 스레드 하나가 <see cref="PpgLink"/>로 가집니다. 화면 스레드는 명령을 대기열에 넣기만 하고, 장치
/// 스레드는 상태가 바뀔 때만 스냅숏을 보냅니다. 명령이 오면 <see cref="ISerialPort.WakeRead"/>로 기다리던 읽기를 깨워 바로
/// 처리합니다. 촬영 기록은 촬영이 가지는 <see cref="PpgShotRecord"/>로 넘겨, 연결이 끊겨도 기록은 남고 촬영을 끝낼 때 장치 스레드를
/// 기다리지 않습니다.
/// 포트를 열면 정보 요청만 보내고, 장치가 답한 뒤에만 중지와 시작을 보냅니다(PPG 장치 확인, <see cref="PpgLink.Found"/>). 조명
/// 보드처럼 PPG 장치가 아닌 포트를 골라도 그 포트에는 정보 요청만 나가고, <c>PpgFindTimeoutUs</c> 뒤 포트를 닫고 Failed로 알립니다.
/// 녹화 중(BeginShot부터 EndShot까지)에 장치를 확인하면 녹화를 바꾸지 않도록 EndShot에서 시작합니다.
/// Dispose는 끝내기 명령을 넣고 <see cref="QuitWaitUs"/>만 기다립니다. 그 안에 끝나지 않으면 장치 스레드를 기다리지 않고
/// 돌아옵니다. 장치 스레드는 배경 스레드라 프로세스 종료를 막지 않고, 자기 상태만 참조하므로 이 객체를 버린 뒤에도 안전합니다.
/// 2026-10-20 기존 앱 실기처럼 직렬 드라이버가 멈추면 쓰기와 닫기가 돌아오지 않아 창을 닫아도 앱이 끝나지 않았기 때문입니다.
/// 드라이버가 끝내지 않는 입출력이 있으면 Windows가 그 스레드를 정리하지 못해, 놓아 둔 뒤에도 프로세스와 COM 포트가 남을 수
/// 있습니다. 놓아 두기는 창 닫기와 앱 종료 흐름을 막지 않게 할 뿐입니다.
/// </remarks>
public sealed class PpgSensor : IDisposable
{
    /// <summary>
    /// 장치 스레드가 부르는 함수들입니다. 장치 스레드에서 잠금을 쥔 채 부르므로 짧게 끝내고, 안에서 PpgSensor 함수를 부르지
    /// 않습니다. 화면에 알릴 때는 화면 스레드로 넘기기만 합니다(WPF의 Dispatcher.BeginInvoke).
    /// </summary>
    public sealed record Callbacks
    {
        /// <summary>상태가 바뀔 때마다 부릅니다. 처음에 Off를 한 번 부릅니다.</summary>
        public Action<PpgSnapshot>? State { get; init; }

        /// <summary>표본이 오는 동안 <see cref="WaveIntervalUs"/>마다 최근 5초 파형(<c>PpgWavePoints</c>)을, 표본이 멈추면 빈 목록을 한 번 부릅니다.</summary>
        public Action<IReadOnlyList<double>>? Wave { get; init; }

        /// <summary>RefreshPorts의 결과입니다.</summary>
        public Action<IReadOnlyList<SerialPortInfo>>? Ports { get; init; }
    }

    /// <summary>Dispose가 장치 스레드가 끝나기를 기다리는 최대 시간입니다.</summary>
    public const long QuitWaitUs = 1_000_000;

    /// <summary>포트가 열려 있을 때 한 번에 읽기를 기다리는 최대 시간입니다. 명령은 WakeRead로 이보다 일찍 처리합니다.</summary>
    public const int ReadTimeoutMs = 100;

    /// <summary>화면 파형을 보내는 간격입니다.</summary>
    public const long WaveIntervalUs = 100_000;

    readonly Shared _shared;
    readonly Thread _thread;
    bool _disposed;

    /// <summary>장치 스레드를 시작합니다. 포트는 아직 열지 않습니다.</summary>
    /// <param name="opener">포트를 여는 함수(SystemSerialPort, 가짜 포트)입니다.</param>
    /// <param name="listPorts">포트 목록을 읽는 함수입니다. 비우면 RefreshPorts가 아무것도 하지 않습니다.</param>
    /// <param name="clock">PC 시각(마이크로초)입니다. 비우면 <see cref="SteadyClock"/>입니다.</param>
    public PpgSensor(
        SerialPortOpener opener, Func<IReadOnlyList<SerialPortInfo>>? listPorts, Callbacks callbacks, Func<long>? clock = null)
    {
        _shared = new Shared(opener, listPorts, callbacks, clock ?? SteadyClock.NowUs);
        Shared shared = _shared;
        _thread = new Thread(() => shared.Run()) { IsBackground = true, Name = "PPG 장치" };
        _thread.Start();
    }

    /// <summary>콜백을 지우고 끝내기 명령(장치 중지, 포트 닫기)을 넣은 뒤 <see cref="QuitWaitUs"/>만 기다립니다.</summary>
    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        lock (_shared.CallbackGate) _shared.Handlers = new Callbacks();
        _shared.Post(new Command(CommandKind.Quit));
        bool done;
        lock (_shared.Gate)
        {
            var deadline = TimeSpan.FromTicks(QuitWaitUs * TimeSpan.TicksPerMicrosecond);
            long start = SteadyClock.NowUs();
            while (!_shared.Finished)
            {
                TimeSpan remaining = deadline - TimeSpan.FromTicks((SteadyClock.NowUs() - start) * TimeSpan.TicksPerMicrosecond);
                if (remaining <= TimeSpan.Zero) break;
                Monitor.Wait(_shared.Gate, remaining);
            }
            done = _shared.Finished;
        }
        // 직렬 드라이버가 멈춰 장치 스레드가 돌아오지 않아도 앱이 끝나도록 기다리지 않습니다. 장치 스레드는 _shared를 함께 가집니다.
        if (done) _thread.Join();
    }

    /// <summary>포트를 열고 장치를 확인합니다. 장치가 답하면 시작합니다. 열려 있던 포트는 먼저 닫습니다.</summary>
    public void Connect(string port) => _shared.Post(new Command(CommandKind.Connect, port));

    /// <summary>장치를 멈추고 포트를 닫습니다.</summary>
    public void Disconnect() => _shared.Post(new Command(CommandKind.Disconnect));

    /// <summary>포트 목록을 읽어 <see cref="Callbacks.Ports"/>로 보냅니다.</summary>
    public void RefreshPorts() => _shared.Post(new Command(CommandKind.RefreshPorts));

    /// <summary>
    /// 장치를 다시 시작하고 record에 기록을 시작합니다. 포트가 닫혀 있으면 record에 끊긴 시각만 적습니다. 장치를 확인하기 전이면
    /// 시작하지 않고 기록만 붙입니다. 미리보기 60초를 이 촬영에 쓰도록 다시 시작합니다. PPG 안정 대기(<c>PpgSettleUs</c>)와
    /// 녹화 30초가 미리보기 60초 안에 들어갑니다.
    /// </summary>
    public void BeginShot(PpgShotRecord? record) => _shared.Post(new Command(CommandKind.BeginShot, Record: record));

    /// <summary>
    /// 촬영 기록을 떼고 다음 촬영까지 파형이 오도록 장치를 다시 시작합니다. 장치를 확인하기 전이면 시작하지 않습니다.
    /// 촬영 기록의 Close는 부른 쪽이 합니다.
    /// </summary>
    public void EndShot() => _shared.Post(new Command(CommandKind.EndShot));

    /// <summary>장치 스레드가 마지막으로 한 바퀴 돈 PC 시각입니다. 포트가 열려 있는 동안 <see cref="ReadTimeoutMs"/>마다 바뀝니다.</summary>
    public long LastLoopUs => Interlocked.Read(ref _shared.LoopUs);

    /// <summary>아직 처리하지 않은 가장 오래된 명령을 넣은 PC 시각입니다. 없으면 0입니다.</summary>
    public long PendingSinceUs
    {
        get
        {
            lock (_shared.Gate) return _shared.Commands.Count == 0 ? 0 : _shared.Commands.Peek().PostedUs;
        }
    }

    enum CommandKind
    {
        Connect,
        Disconnect,
        RefreshPorts,
        BeginShot,
        EndShot,
        Quit,
    }

    sealed record Command(CommandKind Kind, string Port = "", PpgShotRecord? Record = null)
    {
        public long PostedUs { get; set; }
    }

    const string NotFound = "이 포트에서 PPG 장치를 찾지 못했습니다";

    // 장치 스레드와 PpgSensor가 함께 가지는 상태입니다. PpgSensor를 버린 뒤에도 놓아 둔 장치 스레드가 이 객체만 참조합니다.
    sealed class Shared
    {
        readonly SerialPortOpener _opener;
        readonly Func<IReadOnlyList<SerialPortInfo>>? _listPorts;
        readonly Func<long> _clock;

        // 명령이 들어옴, 장치 스레드가 끝남을 Gate로 알립니다.
        public readonly object Gate = new();
        public readonly Queue<Command> Commands = new();
        // 장치 스레드가 연 포트입니다. 명령을 넣는 쪽이 Gate 아래에서 WakeRead를 부릅니다.
        ISerialPort? _port;
        public bool Finished;

        // Dispose가 콜백을 지운 뒤로는 부르지 않게 합니다.
        public readonly object CallbackGate = new();
        public Callbacks Handlers;

        public long LoopUs;

        public Shared(SerialPortOpener opener, Func<IReadOnlyList<SerialPortInfo>>? listPorts, Callbacks callbacks, Func<long> clock)
        {
            _opener = opener;
            _listPorts = listPorts;
            Handlers = callbacks;
            _clock = clock;
            LoopUs = clock();
        }

        public void Post(Command command)
        {
            command.PostedUs = _clock();
            lock (Gate)
            {
                Commands.Enqueue(command);
                _port?.WakeRead();
                Monitor.PulseAll(Gate);
            }
        }

        void SetPort(ISerialPort? opened)
        {
            lock (Gate) _port = opened;
        }

        void Call(Action<Callbacks> call)
        {
            lock (CallbackGate) call(Handlers);
        }

        void StoreLoop() => Interlocked.Exchange(ref LoopUs, _clock());

        public void Run()
        {
            var link = new PpgLink(_clock);
            string failure = "";  // 포트를 열지 못했거나 읽기가 실패한 이유. 연결과 끊기 명령에서 지움
            string failedPort = "";
            PpgSnapshot? sent = null;
            long waveUs = 0;
            long waveSamples = -1;
            bool waveShown = false;

            void Send(PpgSnapshot snapshot)
            {
                if (sent is not null && sent == snapshot) return;
                sent = snapshot;
                Call(c => c.State?.Invoke(snapshot));
            }

            void Publish()
            {
                PpgSnapshot snapshot = link.Snapshot();
                if (!link.IsOpen && failure.Length != 0)
                {
                    snapshot = snapshot with { State = PpgState.Failed, Port = failedPort, Problem = failure };
                }
                Send(snapshot);
            }

            // 명령을 넣는 쪽이 닫힌 포트의 WakeRead를 부르지 않도록 포트를 닫기 전에 알려 둔 포트를 지웁니다.
            void Close(string why)
            {
                SetPort(null);
                link.Close(why);
            }

            Publish();
            for (;;)
            {
                Command[] todo;
                lock (Gate)
                {
                    if (!link.IsOpen)
                    {
                        while (Commands.Count == 0) Monitor.Wait(Gate);
                    }
                    todo = Commands.ToArray();
                    Commands.Clear();
                }
                StoreLoop();
                foreach (Command c in todo)
                {
                    switch (c.Kind)
                    {
                        case CommandKind.Quit:
                            Close("앱을 닫았습니다");
                            lock (Gate)
                            {
                                Finished = true;
                                Monitor.PulseAll(Gate);
                            }
                            return;
                        case CommandKind.Connect:
                        {
                            Close("PPG 포트를 바꿨습니다");
                            failure = "";
                            Send(new PpgSnapshot { State = PpgState.Opening, Port = c.Port });
                            if (link.Open(_opener, c.Port, out string why))
                            {
                                SetPort(link.Port);
                                link.AskDeviceInfo();
                                // 준비된 장치는 첫 읽기에서 바로 답하므로 읽기 전에 확인 중을 보내야 화면이 그 단계를 거칩니다.
                                Publish();
                            }
                            else
                            {
                                failure = why;
                                failedPort = c.Port;
                            }
                            break;
                        }
                        case CommandKind.Disconnect:
                            Close("PPG 연결을 끊었습니다");
                            failure = "";
                            break;
                        case CommandKind.RefreshPorts:
                            if (_listPorts is not null)
                            {
                                IReadOnlyList<SerialPortInfo> infos = _listPorts();
                                Call(h => h.Ports?.Invoke(infos));
                            }
                            break;
                        case CommandKind.BeginShot:
                            if (c.Record is null) break;
                            if (!link.IsOpen)
                            {
                                c.Record.MarkDisconnected(new Timestamp(_clock()), failure.Length == 0 ? "PPG 포트가 닫혀 있었습니다" : failure);
                                break;
                            }
                            if (link.Found) link.Restart();
                            link.Attach(c.Record);
                            break;
                        case CommandKind.EndShot:
                            link.Detach();
                            if (link.IsOpen && link.Found) link.Restart();
                            break;
                    }
                    StoreLoop();
                }
                if (link.IsOpen)
                {
                    if (!link.Pump(ReadTimeoutMs))
                    {
                        failure = link.Failure;
                        failedPort = link.PortName;
                        Close(failure);
                    }
                    else if (!link.Found && link.OpenUs >= PpgFunctions.PpgFindTimeoutUs)
                    {
                        failure = NotFound;
                        failedPort = link.PortName;
                        Close(failure);
                    }
                    else
                    {
                        link.AskDeviceInfo();
                        // 녹화 중에 장치를 확인하면 기록 중인 파형을 바꾸지 않도록 촬영을 끝낼 때(EndShot) 시작합니다.
                        if (link.Found && !link.Started && !link.Attached) link.Restart();
                        link.RestartIfStopped();
                    }
                }
                StoreLoop();
                Publish();

                PpgReading reading = link.Reading();
                long now = _clock();
                if (reading.Streaming)
                {
                    if (reading.Samples != waveSamples && now - waveUs >= WaveIntervalUs)
                    {
                        IReadOnlyList<double> points = PpgFunctions.PpgWavePoints(link.RecentValues(PpgFunctions.PpgWaveSamples));
                        waveUs = now;
                        waveSamples = reading.Samples;
                        waveShown = true;
                        Call(h => h.Wave?.Invoke(points));
                    }
                }
                else if (waveShown)
                {
                    waveShown = false;
                    waveSamples = -1;
                    Call(h => h.Wave?.Invoke(Array.Empty<double>()));
                }
            }
        }
    }
}
