namespace DiabeticFoot.Core;

/// <summary>프레임을 내는 장비의 종류입니다. 모든 프레임에 붙어 RGB와 열화상을 시각으로 맞추는 데 씁니다.</summary>
public enum StreamKind : byte
{
    Rgb,
    Thermal,
}

/// <summary>
/// 스트림을 연 뒤 받은 순서입니다.
/// </summary>
/// <remarks>
/// 카메라가 빠뜨린 프레임에는 번호가 붙지 않으므로 번호로는 카메라 단 누락을 알 수 없습니다.
/// 누락은 프레임 시각으로 판정합니다(<c>FrameGaps.FindFrameGaps</c>).
/// </remarks>
public readonly record struct FrameIndex(ulong Value) : IComparable<FrameIndex>
{
    public int CompareTo(FrameIndex other) => Value.CompareTo(other.Value);

    public static implicit operator FrameIndex(ulong value) => new(value);

    public static explicit operator ulong(FrameIndex index) => index.Value;
}
