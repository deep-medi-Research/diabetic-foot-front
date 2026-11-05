using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using DiabeticFoot.Platform;

namespace DiabeticFoot.Platform.Tests;

/// <summary>시험용 가짜 관류 분석 프로그램(tests/DiabeticFoot.TestWorker)을 띄우는 값을 만듭니다.</summary>
internal static class FakeWorker
{
    private static readonly string FileName = OperatingSystem.IsWindows() ? "TestWorker.exe" : "TestWorker";

    public static string Path => System.IO.Path.Combine(AppContext.BaseDirectory, FileName);

    public static ProcessLaunch Launch(params string[] arguments) => new() { Program = Path, Arguments = arguments };

    /// <summary>실행 파일과 그 옆의 실행에 필요한 파일을 다른 폴더로 복사하고 복사한 실행 파일 경로를 돌려줍니다.</summary>
    public static string CopyTo(string folder)
    {
        foreach (var file in Directory.GetFiles(AppContext.BaseDirectory, "TestWorker*"))
        {
            if (file.EndsWith(".pdb", StringComparison.Ordinal)) continue;
            File.Copy(file, System.IO.Path.Combine(folder, System.IO.Path.GetFileName(file)), overwrite: true);
        }
        return System.IO.Path.Combine(folder, FileName);
    }

    // 실행 파일 런처는 DOTNET_ROOT가 없으면 전역 설치 위치에서만 런타임을 찾습니다. 시험을 돌리는 런타임을 그대로 쓰게 합니다.
    [ModuleInitializer]
    internal static void ExportDotnetRoot()
    {
        if (Environment.GetEnvironmentVariable("DOTNET_ROOT") is { Length: > 0 }) return;
        var runtimeDirectory = RuntimeEnvironment.GetRuntimeDirectory();
        Environment.SetEnvironmentVariable("DOTNET_ROOT", System.IO.Path.GetFullPath(System.IO.Path.Combine(runtimeDirectory, "..", "..", "..")));
    }
}
