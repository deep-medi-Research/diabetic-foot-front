using System.Diagnostics;

namespace DiabeticFoot.Platform;

/// <summary>
/// 장치들이 함께 쓰는 PC 시계(마이크로초)입니다. 원본의 <c>std::chrono::steady_clock</c>과 같은 단조 시계라, 가짜 RGB 카메라,
/// 가짜 열화상 카메라, PPG 연결이 이 시계를 쓰면 세 장치의 PC 수신 시각을 바로 비교할 수 있습니다.
/// </summary>
internal static class SteadyClock
{
    public static long NowUs() => (long)((Int128)Stopwatch.GetTimestamp() * 1_000_000 / Stopwatch.Frequency);

    /// <summary>시계가 dueUs에 닿을 때까지 기다립니다. 이미 지났으면 바로 돌아옵니다.</summary>
    public static void SleepUntilUs(long dueUs)
    {
        for (long remaining = dueUs - NowUs(); remaining > 0; remaining = dueUs - NowUs())
        {
            // Thread.Sleep은 밀리초 아래를 버리므로 올림해 정한 시각보다 일찍 깨지 않게 합니다.
            Thread.Sleep((int)Math.Min((remaining + 999) / 1000, int.MaxValue));
        }
    }
}
