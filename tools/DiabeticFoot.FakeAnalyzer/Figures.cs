using static DiabeticFoot.FakeAnalyzer.FixedResult;

namespace DiabeticFoot.FakeAnalyzer;

internal static class Figures
{
    const int FootHeight = 140;
    const int Margin = 10;
    const int Gap = 12;
    const int BarHeight = 12;

    // 아래 막대 길이가 (창 번호 + 1) / 창 수라, 결과 창이 어느 장을 보이는지 눈으로 알 수 있습니다.
    public static IndexedImage MapFrame(int window)
    {
        IndexedImage image = Feet((foot, row, col) => MapValid(row, col) ? Palette.Ramp(Map(foot, window, row, col), MapTop) : null, BarHeight);
        int inner = image.Width - 2 * Margin;
        int top = Margin + FootHeight + 4;
        image.FillRect(Margin, top, Margin + inner * (window + 1) / Windows, top + BarHeight - 6, Palette.White);
        return image;
    }

    public static IndexedImage Segmentation() => Feet((_, _, _) => Palette.White);

    public static IndexedImage TemplateCells() => Feet((_, row, col) => (byte)((row / 2 + col / 3) % 2 == 0 ? 4 : 8));

    public static IndexedImage MeanMap() => Feet((foot, row, col) =>
        MapValid(row, col) ? Palette.Ramp(Enumerable.Range(0, Windows).Average(k => Map(foot, k, row, col)), MapTop) : null);

    // 두 발을 표준 발 틀 모양으로 그립니다. color가 지도 칸(행, 열)의 색을 정하고, null이면 계산하지 않는 칸의 색입니다.
    static IndexedImage Feet(Func<int, int, int, byte?> color, int extraHeight = 0)
    {
        double widest = Enumerable.Range(0, OutlinePoints).Max(i => Outline(i).Right);
        int footWidth = (int)Math.Ceiling(2 * widest * (FootHeight - 1)) + 1;
        var image = new IndexedImage(2 * Margin + 2 * footWidth + Gap, 2 * Margin + FootHeight + extraHeight, Palette.Background);
        for (int foot = 0; foot < 2; foot++)
        {
            int left = Margin + foot * (footWidth + Gap);
            for (int y = 0; y < FootHeight; y++)
            {
                double u = (double)y / (FootHeight - 1);
                double half = HalfWidth(u);
                for (int x = 0; x < footWidth; x++)
                {
                    double across = -widest + (double)x / (FootHeight - 1);
                    if (Math.Abs(across) > half) continue;
                    int row = Math.Min(MapRows - 1, (int)(u * MapRows));
                    int col = Math.Min(MapCols - 1, (int)((across + half) / (2 * half) * MapCols));
                    image.Set(left + x, Margin + y, color(foot, row, col) ?? Palette.Gap);
                }
            }
        }
        return image;
    }
}
