using System.Text;
using DiabeticFoot.FakeAnalyzer;

// 앱과는 BOM 없는 UTF-8로 주고받습니다. Console의 기본 인코딩은 Windows에서 코드 페이지라 한글 경로가 깨집니다.
var utf8 = new UTF8Encoding(encoderShouldEmitUTF8Identifier: false);
using var stdout = new StreamWriter(Console.OpenStandardOutput(), utf8);
using var stderr = new StreamWriter(Console.OpenStandardError(), utf8) { AutoFlush = true };
using var stdin = new StreamReader(Console.OpenStandardInput(), utf8, detectEncodingFromByteOrderMarks: false);

WorkerOptions? options = WorkerOptions.Parse(args, Path.Combine(AppContext.BaseDirectory, "variants"), out string error, out int exitCode);
if (options is null)
{
    stderr.WriteLine(error);
    return exitCode;
}
return new Worker(options, stdin, new OutputLines(stdout), stderr).Run();
