namespace DiabeticFoot.Core.Pixel;

/// <summary>프레임 안의 사각 영역입니다. 화소 단위이고 좌상단이 원점입니다.</summary>
public readonly record struct Rect(int X = 0, int Y = 0, int Width = 0, int Height = 0)
{
    /// <summary>폭이나 높이가 0 이하이면 true입니다.</summary>
    public bool IsEmpty => Width <= 0 || Height <= 0;

    /// <summary>비어 있지 않고 높이가 폭보다 크면 true입니다.</summary>
    /// <remarks>관류 분석은 발 하나씩 세로로 긴 영상만 받습니다. 좌우 분할 녹화 전에 두 영역을 이 속성으로 확인합니다.</remarks>
    public bool IsPortrait => !IsEmpty && Height > Width;
}

/// <summary>색차를 공유하는 화소 묶음의 크기입니다. 영역의 시작과 크기가 이 배수여야 성분이 밀리지 않습니다.</summary>
public sealed record ChromaGrid(int X = 1, int Y = 1);
