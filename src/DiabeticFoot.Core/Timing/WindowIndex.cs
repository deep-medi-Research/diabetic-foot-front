namespace DiabeticFoot.Core.Timing;

public static partial class TimingFunctions
{
    /// <summary>
    /// 녹화 시작에서 <paramref name="tUs"/>만큼 지난 시각에 보여 줄 분석 창의 번호를 고릅니다.
    /// </summary>
    /// <param name="tUs">녹화 첫 프레임에서 지난 시간(마이크로초). 음수이면 창 0입니다</param>
    /// <param name="count">창 수</param>
    /// <param name="windowUs">창 길이(마이크로초)</param>
    /// <param name="strideUs">창 시작 간격(마이크로초)</param>
    /// <returns>창 번호. count가 1이면 항상 0입니다. count가 1보다 작거나 windowUs나 strideUs가 0 이하이면 null입니다</returns>
    /// <remarks>
    /// 창 k는 k * strideUs에서 시작해 windowUs 동안이고, 그 가운데 시각은 k * strideUs + windowUs / 2입니다.
    /// 가운데 시각이 tUs에 가장 가까운 창을 고르며, 거리가 같으면 앞 창을 고릅니다. 결과는 0부터 count - 1까지로 제한합니다.
    /// 관류 분석 프로그램은 창마다 관류 지도 GIF를 한 장 만들고 그 장의 시각으로 창의 가운데 시각을 적습니다
    /// (창 10초, 간격 2초, 30초 녹화에서 창 11개, 가운데 5초에서 25초).
    /// </remarks>
    public static int? WindowIndexAt(long tUs, int count, long windowUs, long strideUs)
    {
        if (count == 1)
        {
            return 0;
        }
        if (count < 1 || windowUs <= 0 || strideUs <= 0)
        {
            return null;
        }
        // 창 길이가 홀수 마이크로초여도 가운데를 정수로 다루려고 모든 값을 두 배로 셉니다.
        long d = 2 * tUs - windowUs;
        long s = 2 * strideUs;
        long k = FloorDiv(d, s);
        if (d - k * s > (k + 1) * s - d)
        {
            k++;
        }
        if (k < 0)
        {
            k = 0;
        }
        if (k > count - 1)
        {
            k = count - 1;
        }
        return (int)k;
    }

    // 음수에서도 아래로 내림하는 정수 나눗셈입니다.
    private static long FloorDiv(long a, long b)
    {
        long q = a / b;
        return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
    }
}
