using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Runtime.Versioning;

namespace DiabeticFoot.Platform;

/// <summary>
/// Windows 작업 개체입니다. 자식과 그 하위 프로세스를 한 번에 끝내고, 앱이 비정상 종료되어 핸들이 닫힐 때도 같이 끝냅니다.
/// 관류 분석 프로그램이 앱 없이 남아 모델 메모리(약 400MB)와 CPU를 쥐지 않게 하기 위함입니다.
/// </summary>
[SupportedOSPlatform("windows")]
internal sealed class WindowsJob : IDisposable
{
    private const int JobObjectExtendedLimitInformation = 9;
    private const uint KillOnJobClose = 0x2000;

    private IntPtr _handle;

    private WindowsJob(IntPtr handle) => _handle = handle;

    /// <summary>
    /// 프로세스를 새 작업 개체에 넣습니다. 만들지 못하면 null이고, 이때는 호출자가 Process.Kill로 물러납니다.
    /// Process.Start는 멈춘 채로 띄울 수 없어, 넣기 전에 하위 프로세스가 생기면 작업 개체 밖에 남습니다.
    /// </summary>
    public static WindowsJob? TryAssign(Process process)
    {
        var handle = CreateJobObjectW(IntPtr.Zero, null);
        if (handle == IntPtr.Zero) return null;
        var limits = new ExtendedLimitInformation();
        limits.Basic.LimitFlags = KillOnJobClose;
        try
        {
            if (SetInformationJobObject(handle, JobObjectExtendedLimitInformation, ref limits, (uint)Marshal.SizeOf<ExtendedLimitInformation>()) &&
                AssignProcessToJobObject(handle, process.Handle))
            {
                return new WindowsJob(handle);
            }
        }
        catch (InvalidOperationException)
        {
        }
        CloseHandle(handle);
        return null;
    }

    public bool Terminate(int exitCode) => TerminateJobObject(_handle, unchecked((uint)exitCode));

    public void Dispose()
    {
        if (_handle == IntPtr.Zero) return;
        CloseHandle(_handle);
        _handle = IntPtr.Zero;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct BasicLimitInformation
    {
        public long PerProcessUserTimeLimit;
        public long PerJobUserTimeLimit;
        public uint LimitFlags;
        public nuint MinimumWorkingSetSize;
        public nuint MaximumWorkingSetSize;
        public uint ActiveProcessLimit;
        public nuint Affinity;
        public uint PriorityClass;
        public uint SchedulingClass;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct IoCounters
    {
        public ulong ReadOperationCount;
        public ulong WriteOperationCount;
        public ulong OtherOperationCount;
        public ulong ReadTransferCount;
        public ulong WriteTransferCount;
        public ulong OtherTransferCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct ExtendedLimitInformation
    {
        public BasicLimitInformation Basic;
        public IoCounters Io;
        public nuint ProcessMemoryLimit;
        public nuint JobMemoryLimit;
        public nuint PeakProcessMemoryUsed;
        public nuint PeakJobMemoryUsed;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr CreateJobObjectW(IntPtr attributes, string? name);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetInformationJobObject(IntPtr job, int infoClass, ref ExtendedLimitInformation info, uint size);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool TerminateJobObject(IntPtr job, uint exitCode);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CloseHandle(IntPtr handle);
}
