using System.Runtime.InteropServices;

namespace DiabeticFoot.Core;

internal static class Sequences
{
    /// <summary>배열과 List는 복사 없이, 그 밖의 목록은 한 번 복사해 span으로 봅니다.</summary>
    public static ReadOnlySpan<T> AsSpan<T>(IReadOnlyList<T> list) => list switch
    {
        T[] array => array,
        List<T> items => CollectionsMarshal.AsSpan(items),
        _ => list.ToArray(),
    };
}
