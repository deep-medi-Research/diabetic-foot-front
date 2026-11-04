namespace DiabeticFoot.Core;

/// <summary>
/// 장비 또는 드라이버가 프레임에 붙인 시각(마이크로초)입니다.
/// </summary>
/// <remarks>
/// 기준점은 스트림을 연 platform이 정합니다. 서로 다른 스트림의 시각을 비교하려면 같은 기준점이어야 합니다.
/// </remarks>
public readonly record struct Timestamp(long Us) : IComparable<Timestamp>
{
    public int CompareTo(Timestamp other) => Us.CompareTo(other.Us);

    public static bool operator <(Timestamp a, Timestamp b) => a.Us < b.Us;

    public static bool operator >(Timestamp a, Timestamp b) => a.Us > b.Us;

    public static bool operator <=(Timestamp a, Timestamp b) => a.Us <= b.Us;

    public static bool operator >=(Timestamp a, Timestamp b) => a.Us >= b.Us;

    /// <summary>두 시각 사이의 간격(마이크로초)입니다. <paramref name="to"/>가 <paramref name="from"/>보다 이르면 음수입니다.</summary>
    public static long IntervalUs(Timestamp from, Timestamp to) => to.Us - from.Us;
}
