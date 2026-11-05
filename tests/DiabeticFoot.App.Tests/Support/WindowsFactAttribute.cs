namespace DiabeticFoot.App.Tests.Support;

/// <summary>Windows에서만 도는 시험입니다. 다른 OS에서는 건너뜁니다(맥에서 개발하고 Windows CI에서 확인).</summary>
public sealed class WindowsFactAttribute : FactAttribute
{
    public WindowsFactAttribute()
    {
        if (!OperatingSystem.IsWindows()) Skip = "Windows에서만 도는 시험입니다";
    }
}
