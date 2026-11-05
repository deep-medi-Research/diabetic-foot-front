// ChildProcess와 Analyzer 시험에 쓰는 가짜 관류 분석 프로그램입니다. dfr_worker.py와 같은 꼴의 줄을 찍고, 첫 인자로 동작을 고릅니다.
//
//   progress        진행 줄을 찍고 0으로 끝남
//   worker [버전]   [dfr] ready(버전을 주면 뒤에 JSON) 뒤 표준 입력 요청마다 답함. 입력이 닫히면 0으로 끝남
//                   요청 이름이 fail이면 실패, hang이면 진행 줄 뒤 60초 출력 없음, crash이면 진행 줄 뒤 종료 코드 3,
//                   nosummary이면 요약 파일 없이 성공, slow로 시작하면 단계마다 0.4초 대기 뒤 성공.
//                   presegment 요청에는 발마다 [dfr] presegmented 한 줄로 답함.
//                   defer_figures가 true인 요청은 성공한 done 뒤에 [dfr] figures 한 줄을 더 찍음.
//                   그 밖의 이름은 <output>/<name>_summary.json을 쓰고(쓰지 못해도) 성공을 알립니다
//   <파일>.py ...   첫 인자가 .py로 끝나면 worker와 같습니다(AnalyzerLaunch가 만든 명령줄로 띄울 때)
//   exit <코드>     한 줄을 찍고 표준 오류에도 한 줄을 쓴 뒤 그 코드로 끝남
//   hang            한 줄을 찍고 입력을 읽지 않은 채 60초 기다림
//   args <인자...>  인자마다 arg:<값> 한 줄
//   cwd             작업 폴더
//   env <이름>      환경 변수 값
//   flood <바이트>  입력을 읽지 않고 그만큼 출력한 뒤 0으로 끝남
//   stderr          표준 출력과 표준 오류에 한 줄씩
//   priority        우선순위 클래스(normal, below_normal, idle, other)
//   spawn           자기 자신을 `sleep 60`으로 한 번 더 띄우고 child:<번호> 한 줄을 찍은 뒤 입력이 닫힐 때까지 기다림(하위 프로세스 시험)
//   sleep <초>      그만큼 기다림(spawn이 띄우는 하위 프로세스)

using System.Diagnostics;
using System.Text;
using System.Text.Json;

return Worker.Run(args);

internal static class Worker
{
    private static readonly UTF8Encoding Utf8 = new(false);
    private static readonly Stream Stdout = Console.OpenStandardOutput();
    private static readonly Stream Stderr = Console.OpenStandardError();
    private static readonly Stream Stdin = Console.OpenStandardInput();

    // 표준 출력과 표준 오류는 따로 읽히므로, 둘 사이의 순서를 가르는 시험은 표준 출력을 비운 뒤 잠시 쉬고 오류를 씁니다.
    private const int StreamGapMs = 50;

    private static void Out(string line)
    {
        Stdout.Write(Utf8.GetBytes(line + "\n"));
        Stdout.Flush();
    }

    private static void Err(string text)
    {
        Stderr.Write(Utf8.GetBytes(text));
        Stderr.Flush();
    }

    private static string? ReadInputLine()
    {
        var bytes = new List<byte>();
        int b;
        var one = new byte[1];
        while (Stdin.Read(one, 0, 1) == 1)
        {
            b = one[0];
            if (b == '\n') return Utf8.GetString(bytes.ToArray());
            bytes.Add((byte)b);
        }
        return bytes.Count > 0 ? Utf8.GetString(bytes.ToArray()) : null;
    }

    private static string RequestString(JsonElement request, string key) =>
        request.ValueKind == JsonValueKind.Object && request.TryGetProperty(key, out var value) && value.ValueKind == JsonValueKind.String
            ? value.GetString() ?? ""
            : "";

    private static void WriteSummary(string output, string name)
    {
        // 결과 창을 시험할 수 있게 실제 요약과 같은 키를 쓰고, 모델 이름으로 가짜 관류 분석 프로그램 결과임을 밝힙니다.
        const string text =
            "{\"fps\": 30.0, \"n_frames\": 900, \"window_s\": 10, \"window_stride_s\": 2, \"n_windows\": 11, " +
            "\"pi_left\": 0.08, \"pi_right\": 0.1, \"pi_ratio_left_right\": 0.8, " +
            "\"classification\": {\"kind\": \"fake analyzer\", \"p_pad\": 0.2, \"threshold\": 0.45, " +
            "\"decision\": \"normal\"}, " +
            "\"dfr\": {\"upstream_commit\": \"5e5178d\", \"worker_version\": \"fake-1\"}}\n";
        try
        {
            File.WriteAllBytes(output + "/" + name + "_summary.json", Utf8.GetBytes(text));
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
        }
    }

    private static int RunWorker(string version)
    {
        Out(version.Length == 0
            ? "[dfr] ready"
            : "[dfr] ready {\"worker_version\": \"" + version + "\", \"upstream_commit\": \"5e5178d\"}");
        while (ReadInputLine() is { } raw)
        {
            var line = raw.TrimEnd('\r');
            if (line.Length == 0) continue;
            using var document = JsonDocument.Parse(line);
            var request = document.RootElement;
            if (request.TryGetProperty("preload", out _))
            {
                Out("[dfr] preloaded");
                continue;
            }
            if (request.TryGetProperty("presegment", out _))
            {
                Out("[dfr] presegmented {\"video\": \"left\", \"ok\": true}");
                Out("[dfr] presegmented {\"video\": \"right\", \"ok\": true}");
                continue;
            }
            var name = RequestString(request, "name");
            var pauseMs = name.StartsWith("slow", StringComparison.Ordinal) ? 400 : 0;
            Out("[fake] request " + line);
            Out("[0/5] Loading segmentation model");
            Thread.Sleep(pauseMs);
            Out("[1/5] Reading videos [2 s]");
            Thread.Sleep(pauseMs);
            Out("  [1/7] thermal sub-step");
            if (name == "hang")
            {
                Thread.Sleep(60_000);
                continue;
            }
            if (name == "crash")
            {
                Err("Traceback (most recent call last)\nMemoryError\n");
                return 3;
            }
            Thread.Sleep(pauseMs);
            Out("[5/5] Writing outputs");
            if (name == "fail")
            {
                Out("[dfr] done {\"ok\": false, \"error\": \"left.avi 가 없습니다: C:\\\\촬영\\\\left.avi\\n두 번째 줄\"}");
            }
            else
            {
                // 결과 폴더가 없으면 요약을 쓰지 못한 채 성공을 알립니다. 앱 쪽이 요약 파일로 성공을 다시 확인하는지 시험합니다.
                if (name != "nosummary") WriteSummary(RequestString(request, "output"), name);
                Out("[dfr] done {\"ok\": true, \"error\": \"\"}");
                if (request.TryGetProperty("defer_figures", out var defer) && defer.ValueKind == JsonValueKind.True)
                {
                    Out("[dfr] figures {\"ok\": true, \"seconds\": 1.5}");
                }
            }
        }
        return 0;
    }

    private static string PriorityName()
    {
        using var self = Process.GetCurrentProcess();
        return self.PriorityClass switch
        {
            ProcessPriorityClass.BelowNormal => "below_normal",
            ProcessPriorityClass.Normal => "normal",
            ProcessPriorityClass.Idle => "idle",
            _ => "other",
        };
    }

    public static int Run(string[] args)
    {
        var mode = args.Length > 0 ? args[0] : "";
        switch (mode)
        {
            case "progress":
                Out("[0/5] Loading segmentation model [0 s]");
                Out("[1/5] Reading videos [1 s]");
                Out("  [1/7] thermal sub-step");
                Out("[2/5] Pulse template [3 s]");
                Out("[5/5] Done [9 s]");
                return 0;
            case "worker":
                return RunWorker(args.Length > 1 ? args[1] : "");
            case "priority":
                Out(PriorityName());
                return 0;
            case "exit" when args.Length > 1:
                Out("exiting");
                Thread.Sleep(StreamGapMs);
                Err("Traceback (most recent call last)\n");
                return int.Parse(args[1]);
            case "hang":
                Out("[0/5] Loading segmentation model");
                Thread.Sleep(60_000);
                return 0;
            case "args":
                foreach (var value in args.Skip(1)) Out("arg:" + value);
                return 0;
            case "cwd":
                Out(Directory.GetCurrentDirectory());
                return 0;
            case "env" when args.Length > 1:
                Out(Environment.GetEnvironmentVariable(args[1]) ?? "(none)");
                return 0;
            case "flood" when args.Length > 1:
                var total = long.Parse(args[1]);
                var row = Utf8.GetBytes(new string('x', 99) + "\n");
                var buffered = new BufferedStream(Stdout, 1 << 16);
                for (long sent = 0; sent < total; sent += 100) buffered.Write(row);
                buffered.Flush();
                Out("flood-end");
                return 0;
            case "stderr":
                Out("to-stdout");
                Thread.Sleep(StreamGapMs);
                Err("to-stderr\n");
                return 0;
            case "spawn":
                using (var child = Process.Start(new ProcessStartInfo(Environment.ProcessPath!, "sleep 60") { UseShellExecute = false }))
                {
                    Out("child:" + child!.Id);
                    while (ReadInputLine() is not null)
                    {
                    }
                }
                return 0;
            case "sleep" when args.Length > 1:
                Thread.Sleep(TimeSpan.FromSeconds(double.Parse(args[1])));
                return 0;
        }
        if (mode.EndsWith(".py", StringComparison.Ordinal)) return RunWorker("");
        Err("unknown mode\n");
        return 2;
    }
}
