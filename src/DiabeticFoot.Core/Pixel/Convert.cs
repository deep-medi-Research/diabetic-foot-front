namespace DiabeticFoot.Core.Pixel;

/// <summary>열화상 원시값의 범위입니다. 온도가 아니라 센서 원시값입니다.</summary>
public readonly record struct Y16Range(ushort Low = 0, ushort High = 0);

/// <summary>한 화소의 색입니다.</summary>
public readonly record struct Rgb8(byte R = 0, byte G = 0, byte B = 0);

/// <summary>
/// 열화상 미리보기의 색상표입니다. 화면 표시에만 씁니다.
/// </summary>
public enum Palette : byte
{
    /// <summary>회색. R, G, B가 모두 Y16ToGray8의 결과와 같습니다.</summary>
    Gray,

    /// <summary>검정, 보라, 빨강, 주황, 노랑, 흰색 순서입니다. 낮은 값이 검정, 높은 값이 흰색입니다.</summary>
    Iron,

    /// <summary>남색, 파랑, 청록, 노랑, 빨강, 검붉은색 순서입니다(MATLAB jet와 같은 기준점).</summary>
    Rainbow,

    /// <summary>검정, 파랑, 청록, 초록, 노랑, 빨강, 자홍, 흰색 순서입니다. 한 단계당 색 변화가 가장 큽니다.</summary>
    HighContrast,
}
