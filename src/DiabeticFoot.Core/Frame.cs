namespace DiabeticFoot.Core;

/// <summary>프레임 화소의 형식입니다.</summary>
public enum PixelFormat : byte
{
    /// <summary>RGB 카메라 무압축. 화소 2개가 4바이트(U Y0 V Y1)입니다.</summary>
    Uyvy,

    /// <summary>Y 평면 뒤에 UV 평면입니다.</summary>
    Nv12,

    /// <summary>열화상 원시값. 화소 하나가 2바이트(리틀 엔디언)입니다.</summary>
    Y16,
}

/// <summary>
/// 프레임 한 장의 화소와 시각을 가리킵니다.
/// </summary>
/// <remarks>
/// 화소를 소유하지 않습니다. <see cref="Plane0"/>과 <see cref="RowStride0"/>, <see cref="Plane1"/>과 <see cref="RowStride1"/>은 형식에 따라 씁니다.
/// UYVY와 Y16은 0번 평면만, NV12는 0번(Y)과 1번(UV) 평면을 씁니다.
/// 평면의 메모리는 이 뷰를 받은 함수가 반환할 때까지만 유효합니다. 더 오래 쓰려면 복사해야 합니다.
/// </remarks>
public readonly record struct FrameView
{
    public PixelFormat Format { get; init; }

    public int Width { get; init; }

    public int Height { get; init; }

    public ReadOnlyMemory<byte> Plane0 { get; init; }

    public ReadOnlyMemory<byte> Plane1 { get; init; }

    public int RowStride0 { get; init; }

    public int RowStride1 { get; init; }

    public StreamKind Stream { get; init; }

    public FrameIndex Index { get; init; }

    public Timestamp Time { get; init; }
}
