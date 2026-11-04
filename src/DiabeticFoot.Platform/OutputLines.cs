using System.Diagnostics;
using System.Text;

namespace DiabeticFoot.Platform;

/// <summary>
/// 자식 출력 바이트를 줄로 나눠 보관하는 대기열입니다. 읽기 스레드가 넣고 ReadLine이 꺼냅니다.
/// 표준 출력과 표준 오류는 따로 읽히므로 출처마다 아직 줄이 끝나지 않은 조각을 따로 둡니다.
/// </summary>
internal sealed class OutputLines
{
    private readonly object _gate = new();
    private readonly Queue<string> _lines = new();
    private readonly MemoryStream[] _partial;
    private readonly bool[] _sourceClosed;
    private int _openSources;

    public OutputLines(int sources)
    {
        _partial = Enumerable.Range(0, sources).Select(_ => new MemoryStream()).ToArray();
        _sourceClosed = new bool[sources];
        _openSources = sources;
    }

    /// <summary>읽은 바이트를 붙입니다. '\n'이 나올 때마다 한 줄을 대기열에 넣습니다.</summary>
    public void Append(int source, ReadOnlySpan<byte> bytes)
    {
        lock (_gate)
        {
            var partial = _partial[source];
            while (!bytes.IsEmpty)
            {
                var newline = bytes.IndexOf((byte)'\n');
                if (newline < 0)
                {
                    partial.Write(bytes);
                    break;
                }
                partial.Write(bytes[..newline]);
                PushLocked(partial);
                bytes = bytes[(newline + 1)..];
            }
            Monitor.PulseAll(_gate);
        }
    }

    /// <summary>한 출처의 출력이 닫혔음을 알립니다. '\n' 없이 남은 조각은 그 출처의 마지막 줄이 됩니다. 모든 출처가 닫히면 대기열이 닫힙니다.</summary>
    public void CloseSource(int source)
    {
        lock (_gate)
        {
            if (_sourceClosed[source]) return;
            _sourceClosed[source] = true;
            if (_partial[source].Length > 0) PushLocked(_partial[source]);
            _openSources--;
            Monitor.PulseAll(_gate);
        }
    }

    /// <summary>한 줄을 꺼냅니다. ChildProcess.ReadLine과 같은 규칙입니다.</summary>
    public LineStatus Read(out string line, long timeoutMs)
    {
        lock (_gate)
        {
            var deadline = Stopwatch.GetTimestamp() + (long)(Math.Max(timeoutMs, 0) / 1000.0 * Stopwatch.Frequency);
            while (_lines.Count == 0 && _openSources > 0)
            {
                var remainingMs = (deadline - Stopwatch.GetTimestamp()) * 1000.0 / Stopwatch.Frequency;
                if (remainingMs <= 0 || !Monitor.Wait(_gate, (int)Math.Min(Math.Ceiling(remainingMs), int.MaxValue - 1))) break;
            }
            if (_lines.Count > 0)
            {
                line = _lines.Dequeue();
                return LineStatus.Line;
            }
            line = "";
            return _openSources == 0 ? LineStatus.Closed : LineStatus.Timeout;
        }
    }

    private void PushLocked(MemoryStream partial)
    {
        var buffer = partial.GetBuffer();
        var length = (int)partial.Length;
        if (length > 0 && buffer[length - 1] == '\r') length--;
        _lines.Enqueue(Encoding.UTF8.GetString(buffer, 0, length));
        partial.SetLength(0);
    }
}
