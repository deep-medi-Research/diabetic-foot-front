using System.Diagnostics;
using DiabeticFoot.Platform;

namespace DiabeticFoot.Platform.Tests;

public class ChildProcessTests
{
    private static ChildProcess StartFake(string[] arguments, ProcessLaunch? launch = null)
    {
        launch ??= new ProcessLaunch();
        launch = launch with { Program = launch.Program.Length == 0 ? FakeWorker.Path : launch.Program, Arguments = arguments };
        var process = ChildProcess.Start(launch, out var error);
        Assert.True(process is not null, error);
        return process!;
    }

    private static List<string> ReadUntilClosed(ChildProcess process)
    {
        var lines = new List<string>();
        var deadline = Stopwatch.StartNew();
        while (deadline.Elapsed < TimeSpan.FromSeconds(10))
        {
            var status = process.ReadLine(out var line, 1000);
            if (status == LineStatus.Closed) break;
            if (status == LineStatus.Line) lines.Add(line);
        }
        return lines;
    }

    // 원하는 종류의 관류 분석 프로그램 줄이 올 때까지 읽고, 그 사이의 줄을 함께 돌려줍니다.
    private static AnalyzerLine ReadUntil(ChildProcess process, AnalyzerLineKind kind, List<string>? seen = null)
    {
        var deadline = Stopwatch.StartNew();
        while (deadline.Elapsed < TimeSpan.FromSeconds(10))
        {
            var status = process.ReadLine(out var line, 1000);
            if (status == LineStatus.Closed) break;
            if (status != LineStatus.Line) continue;
            seen?.Add(line);
            var parsed = PerfusionAnalysis.ParseAnalyzerLine(line);
            if (parsed.Kind == kind) return parsed;
        }
        Assert.Fail("기다린 줄이 오지 않았습니다");
        return new AnalyzerLine();
    }

    private static string TempDir(string name)
    {
        var dir = Path.Combine(Path.GetTempPath(), "diabeticfoot_test_child_process", name);
        if (Directory.Exists(dir)) Directory.Delete(dir, recursive: true);
        Directory.CreateDirectory(dir);
        return dir;
    }

    // 진행 줄을 순서대로 읽고, 들여 쓴 하위 단계는 진행 단계로 세지 않습니다.
    [Fact]
    public void ReadsProgressLinesInOrder()
    {
        using var process = StartFake(["progress"]);
        var lines = ReadUntilClosed(process);
        var steps = new List<int>();
        var messages = new List<string>();
        foreach (var line in lines)
        {
            var parsed = PerfusionAnalysis.ParseAnalyzerLine(line);
            if (parsed.Kind != AnalyzerLineKind.Stage) continue;
            steps.Add(parsed.Step);
            messages.Add(parsed.Message);
            Assert.Equal(5, parsed.Total);
        }
        Assert.Equal(new[] { 0, 1, 2, 5 }, steps);
        Assert.Equal(4, messages.Count);
        Assert.Equal("Reading videos", messages[1]);
        Assert.Equal(5, lines.Count);
        Assert.Equal(0, process.Wait(5000));
    }

    // 상주 관류 분석 프로그램에 미리 올리기와 요청을 보내고, 요청 줄이 바이트 그대로 전달되며 완료 줄로 성공과 실패를 받습니다.
    [Fact]
    public void ExchangesRequestsWithWorker()
    {
        using var process = StartFake(["worker", "fake-1"]);
        var ready = ReadUntil(process, AnalyzerLineKind.Ready);
        Assert.Equal("fake-1", ready.WorkerVersion);
        Assert.Equal("5e5178d", ready.UpstreamCommit);

        Assert.True(process.WriteLine(PerfusionAnalysis.BuildPreloadLine()));
        ReadUntil(process, AnalyzerLineKind.Preloaded);

        var recordingDir = "C:\\촬영\\20261019\\홍길동_19701015\\40\\101500";
        var request = new PerfusionRequest
        {
            RecordingDir = recordingDir,
            OutputDir = recordingDir + "\\perfusion",
            Name = "20261019_홍길동_19701015_40_101500",
            LeftVideo = recordingDir + "\\101500_left.mkv",
            RightVideo = recordingDir + "\\101500_right.mkv",
            LeftFrames = 900,
            RightFrames = 900,
        };
        var sent = PerfusionAnalysis.BuildPerfusionRequestLine(request)!;
        Assert.True(process.WriteLine(sent));
        var seen = new List<string>();
        var done = ReadUntil(process, AnalyzerLineKind.Done, seen);
        Assert.True(done.Ok);
        Assert.Equal("", done.Error);
        Assert.NotEmpty(seen);
        Assert.Equal("[fake] request " + sent, seen[0]);
        Assert.Equal(3, seen.Count(line => PerfusionAnalysis.ParseAnalyzerLine(line).Kind == AnalyzerLineKind.Stage));

        request = request with { Name = "fail" };
        Assert.True(process.WriteLine(PerfusionAnalysis.BuildPerfusionRequestLine(request)!));
        var failed = ReadUntil(process, AnalyzerLineKind.Done);
        Assert.False(failed.Ok);
        Assert.Equal("left.avi 가 없습니다: C:\\촬영\\left.avi\n두 번째 줄", failed.Error);

        // 입력을 닫으면 관류 분석 프로그램이 스스로 0으로 끝납니다(앱이 닫힐 때의 경로).
        process.CloseInput();
        Assert.Equal(0, process.Wait(5000));
        Assert.False(process.WriteLine("{}"));
    }

    // 0이 아닌 종료 코드를 그대로 받고, 표준 오류의 줄도 같은 대기열로 읽습니다.
    [Fact]
    public void ReportsNonZeroExitCode()
    {
        using var process = StartFake(["exit", "3"]);
        var lines = ReadUntilClosed(process);
        Assert.Equal(new[] { "exiting", "Traceback (most recent call last)" }, lines);
        Assert.Equal(3, process.Wait(5000));
    }

    // 응답 없는 관류 분석 프로그램은 제한 시간이 지나면 강제로 끝내고, 종료 코드는 KilledExitCode입니다.
    [Fact]
    public void KillsHungProcessAfterTimeout()
    {
        using var process = StartFake(["hang"]);
        Assert.Equal(LineStatus.Line, process.ReadLine(out var line, 5000));
        Assert.Equal(AnalyzerLineKind.Stage, PerfusionAnalysis.ParseAnalyzerLine(line).Kind);

        var before = Stopwatch.StartNew();
        Assert.Equal(LineStatus.Timeout, process.ReadLine(out line, 300));
        Assert.Null(process.Wait(300));
        // Windows 대기 단위가 약 15ms라 하한에 여유를 둡니다.
        Assert.True(before.Elapsed >= TimeSpan.FromMilliseconds(500), before.Elapsed.ToString());

        process.Kill();
        Assert.Equal(ChildProcess.KilledExitCode, process.Wait(5000));
        Assert.True(before.Elapsed < TimeSpan.FromSeconds(5), before.Elapsed.ToString());
        Assert.Equal(LineStatus.Closed, process.ReadLine(out line, 5000));
        process.Kill();
    }

    // 공백, 따옴표, 역슬래시, 빈 인자, 한글 경로 인자가 그대로 자식에 도착합니다.
    [Fact]
    public void PassesQuotedAndKoreanArguments()
    {
        string[] values =
        [
            "C:\\촬영 폴더\\홍길동_19701015\\",
            "따옴표 \"안\" 글자",
            "",
            "a\\\\\"b",
            "탭\t있음",
            "plain",
            "끝 역슬래시 둘\\\\",
        ];
        using var process = StartFake(["args", .. values]);
        var lines = ReadUntilClosed(process);
        Assert.Equal(values.Select(value => "arg:" + value), lines);
        Assert.Equal(0, process.Wait(5000));
    }

    // 한글 폴더에 있는 실행 파일을 한글 작업 폴더에서 띄우고, 더한 환경 변수가 자식에 보입니다.
    [Fact]
    public void RunsFromKoreanFolderWithWorkingDirAndEnv()
    {
        var binDir = TempDir("관류 분석 프로그램 폴더 한글");
        var workDir = TempDir("작업 폴더 홍길동");
        var copied = FakeWorker.CopyTo(binDir);
        File.WriteAllText(Path.Combine(workDir, "여기.txt"), "marker");

        using (var cwd = StartFake(["cwd"], new ProcessLaunch { Program = copied, WorkingDir = workDir }))
        {
            var lines = ReadUntilClosed(cwd);
            var reported = Assert.Single(lines);
            // 임시 폴더가 심볼릭 링크 아래에 있으면 경로 글자가 달라지므로 같은 폴더인지 안의 파일로 확인합니다.
            Assert.True(File.Exists(Path.Combine(reported, "여기.txt")), reported);
            Assert.Equal(0, cwd.Wait(5000));
        }

        var launch = new ProcessLaunch
        {
            Program = copied,
            ExtraEnv = [("DIABETICFOOT_TEST_VALUE", "값 한글 C:\\경로"), ("PYTHONUTF8", "1")],
        };
        using (var env = StartFake(["env", "DIABETICFOOT_TEST_VALUE"], launch))
        {
            Assert.Equal(new[] { "값 한글 C:\\경로" }, ReadUntilClosed(env));
            Assert.Equal(0, env.Wait(5000));
        }
    }

    // 호출자가 줄을 읽지 않아도 자식이 출력 파이프에서 막히지 않고 끝납니다.
    [Fact]
    public void DrainsOutputWhileCallerDoesNotRead()
    {
        using var process = StartFake(["flood", "4000000"]);
        Assert.Equal(0, process.Wait(20000));
        var lines = ReadUntilClosed(process);
        Assert.NotEmpty(lines);
        Assert.Equal("flood-end", lines[^1]);
        Assert.Equal(40001, lines.Count);
    }

    // 표준 출력과 표준 오류를 하나의 대기열로 받습니다.
    [Fact]
    public void MergesStandardError()
    {
        using var process = StartFake(["stderr"]);
        Assert.Equal(new[] { "to-stdout", "to-stderr" }, ReadUntilClosed(process));
        Assert.Equal(0, process.Wait(5000));
    }

    // 없는 실행 파일과 없는 작업 폴더는 띄우지 않고 이유를 돌려줍니다.
    [Fact]
    public void FailsToStartMissingProgramOrFolder()
    {
        var launch = new ProcessLaunch { Program = Path.Combine(Path.GetTempPath(), "diabeticfoot_no_such_analyzer") };
        Assert.Null(ChildProcess.Start(launch, out var error));
        Assert.NotEmpty(error);

        launch = FakeWorker.Launch("cwd") with { WorkingDir = Path.Combine(Path.GetTempPath(), "diabeticfoot_no_such_folder_없음") };
        Assert.Null(ChildProcess.Start(launch, out error));
        Assert.NotEmpty(error);
    }

    // 끝난 자식에 쓰면 false를 돌려주고 앱이 끝나지 않습니다.
    [Fact]
    public void WriteAfterExitFails()
    {
        using var process = StartFake(["progress"]);
        ReadUntilClosed(process);
        Assert.Equal(0, process.Wait(5000));
        var ok = true;
        // 파이프 버퍼에 들어가는 첫 쓰기는 성공할 수 있어 여러 번 씁니다.
        for (var i = 0; i < 100 && ok; i++) ok = process.WriteLine(new string('y', 4096));
        Assert.False(ok);
    }

    // 낮은 우선순위로 띄우면 자식의 우선순위 클래스가 보통보다 낮습니다.
    [Fact]
    public void StartsWithLowPriority()
    {
        using var normal = StartFake(["priority"]);
        var normalLines = ReadUntilClosed(normal);
        using var low = StartFake(["priority"], new ProcessLaunch { LowPriority = true });
        var lowLines = ReadUntilClosed(low);
        Assert.Equal(new[] { "normal" }, normalLines);
        Assert.Equal(new[] { "below_normal" }, lowLines);
        Assert.Equal(0, normal.Wait(5000));
        Assert.Equal(0, low.Wait(5000));
    }

    // Dispose가 응답 없는 자식을 끝내고 거둡니다.
    [Fact]
    public void DisposeKillsRunningChild()
    {
        var before = Stopwatch.StartNew();
        using (var process = StartFake(["hang"]))
        {
            Assert.Equal(LineStatus.Line, process.ReadLine(out _, 5000));
        }
        Assert.True(before.Elapsed < TimeSpan.FromSeconds(10), before.Elapsed.ToString());
    }

    // Kill은 자식이 띄운 하위 프로세스까지 끝냅니다. 하위 프로세스가 남아 있는지는 프로세스 번호로 확인합니다.
    [Fact]
    public void KillEndsDescendants()
    {
        using var process = StartFake(["spawn"]);
        Assert.Equal(LineStatus.Line, process.ReadLine(out var line, 10000));
        Assert.StartsWith("child:", line);
        var grandchildId = int.Parse(line["child:".Length..]);
        Assert.True(IsRunning(grandchildId));

        process.Kill();
        Assert.Equal(ChildProcess.KilledExitCode, process.Wait(5000));
        Assert.Equal(LineStatus.Closed, process.ReadLine(out _, 5000));
        var gone = SpinWait.SpinUntil(() => !IsRunning(grandchildId), TimeSpan.FromSeconds(5));
        Assert.True(gone, "하위 프로세스가 남아 있습니다");
    }

    private static bool IsRunning(int id)
    {
        try
        {
            using var process = Process.GetProcessById(id);
            return !process.HasExited;
        }
        catch (ArgumentException)
        {
            return false;
        }
    }
}
