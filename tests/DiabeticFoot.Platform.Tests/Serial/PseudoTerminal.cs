using System.Runtime.InteropServices;

namespace DiabeticFoot.Platform.Tests;

/// <summary>macOS와 Linux에서만 실행하는 시험입니다. Windows에는 가상 단말(pty)이 없습니다.</summary>
public sealed class UnixFactAttribute : FactAttribute
{
    public UnixFactAttribute()
    {
        if (!OperatingSystem.IsMacOS() && !OperatingSystem.IsLinux()) Skip = "가상 단말(pty)은 macOS와 Linux에만 있습니다";
    }
}

/// <summary>
/// 가상 단말 한 쌍입니다. 시험이 장치 쪽(master)을 쥐고, SlavePath를 직렬 포트처럼 엽니다.
/// 실제 USB 변환기 없이 System.IO.Ports로 읽고 쓰는 경로를 확인하는 데 씁니다.
/// </summary>
public sealed class PseudoTerminal : IDisposable
{
    const short PollIn = 1;
    int _master;

    public PseudoTerminal()
    {
        int noControllingTerminal = OperatingSystem.IsMacOS() ? 0x20000 : 0x100;
        _master = posix_openpt(2 | noControllingTerminal);
        if (_master < 0 || grantpt(_master) != 0 || unlockpt(_master) != 0)
        {
            throw new InvalidOperationException($"pty를 만들지 못했습니다: errno {Marshal.GetLastWin32Error()}");
        }
        SlavePath = Marshal.PtrToStringAnsi(ptsname(_master))!;
    }

    /// <summary>직렬 포트로 여는 경로입니다(예: /dev/ttys003).</summary>
    public string SlavePath { get; }

    /// <summary>장치가 보낸 것처럼 바이트를 넣습니다. 포트의 Read가 받습니다.</summary>
    public void Send(ReadOnlySpan<byte> bytes)
    {
        byte[] data = bytes.ToArray();
        int sent = 0;
        while (sent < data.Length)
        {
            nint n = write(_master, data.AsSpan(sent).ToArray(), data.Length - sent);
            if (n <= 0) throw new IOException($"pty에 쓰지 못했습니다: errno {Marshal.GetLastWin32Error()}");
            sent += (int)n;
        }
    }

    /// <summary>포트가 쓴 바이트를 count개가 모이거나 시간이 지날 때까지 읽습니다.</summary>
    public byte[] Receive(int count, int timeoutMs)
    {
        var all = new List<byte>();
        var chunk = new byte[256];
        var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (all.Count < count)
        {
            int wait = (int)Math.Max(0, (deadline - DateTime.UtcNow).TotalMilliseconds);
            var fd = new PollFd { Fd = _master, Events = PollIn };
            if (poll(ref fd, 1, wait) <= 0) break;
            nint n = read(_master, chunk, chunk.Length);
            if (n <= 0) break;
            all.AddRange(chunk.AsSpan(0, (int)n).ToArray());
        }
        return [.. all];
    }

    /// <summary>장치를 뽑은 것처럼 장치 쪽을 닫습니다.</summary>
    public void Dispose()
    {
        if (_master < 0) return;
        close(_master);
        _master = -1;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct PollFd
    {
        public int Fd;
        public short Events;
        public short Revents;
    }

    [DllImport("libc", SetLastError = true)]
    static extern int posix_openpt(int flags);

    [DllImport("libc", SetLastError = true)]
    static extern int grantpt(int fd);

    [DllImport("libc", SetLastError = true)]
    static extern int unlockpt(int fd);

    [DllImport("libc", SetLastError = true)]
    static extern IntPtr ptsname(int fd);

    [DllImport("libc", SetLastError = true)]
    static extern nint read(int fd, byte[] buffer, nint count);

    [DllImport("libc", SetLastError = true)]
    static extern nint write(int fd, byte[] buffer, nint count);

    [DllImport("libc", SetLastError = true)]
    static extern int poll(ref PollFd fds, uint count, int timeoutMs);

    [DllImport("libc", SetLastError = true)]
    static extern int close(int fd);
}
