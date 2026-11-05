using DiabeticFoot.Platform;

namespace DiabeticFoot.Platform.Tests;

// 시험 영상은 Data/make_ffv1_fixture.sh가 만든 64x32 UYVY 40장입니다. 화소마다, 장마다 값이 달라 번호가 어긋나면 바이트가 달라집니다.
public class Ffv1VideoReaderTests
{
    private const int FrameCount = 40;
    private const int Width = 64;
    private const int Height = 32;

    private static string Fixture => Path.Combine(AppContext.BaseDirectory, "Data", "ffv1_64x32_40frames.mkv");

    private static byte[] ExpectedFrame(int frame)
    {
        var bytes = new byte[Width * Height * 2];
        for (var i = 0; i < bytes.Length; i++) bytes[i] = (byte)((i >> 1) + frame * 17 + (i >> 7) * 13);
        return bytes;
    }

    private static IVideoReader OpenFixture(string? path = null)
    {
        var reader = FfmpegTestLibraries.Create();
        Assert.NotNull(reader);
        Assert.True(reader.Open(path ?? Fixture, out var error), error);
        return reader;
    }

    // 번호로 읽은 프레임이 쓴 UYVY와 바이트 단위로 같습니다. 순서대로, 거꾸로, 건너뛰며 읽어도 같습니다.
    [FfmpegFact]
    public void ReadsFrameByNumberByteExact()
    {
        Assert.NotEqual(ExpectedFrame(0), ExpectedFrame(1));
        using var reader = OpenFixture();
        Assert.Equal(Width, reader.Width);
        Assert.Equal(Height, reader.Height);
        Assert.Equal(FrameCount, reader.FrameCount);

        var order = new List<int>();
        order.AddRange(Enumerable.Range(0, FrameCount));
        order.AddRange(Enumerable.Range(0, FrameCount).Reverse());
        order.AddRange([17, 3, 39, 0, 20, 21, 22, 5, 38]);
        var frame = Array.Empty<byte>();
        foreach (var index in order)
        {
            Assert.True(reader.ReadFrame(index, ref frame), $"frame {index}");
            Assert.True(ExpectedFrame(index).AsSpan().SequenceEqual(frame), $"frame {index}");
        }
    }

    // 범위 밖 번호는 읽지 않고, 그 뒤에도 정상 번호를 읽습니다.
    [FfmpegFact]
    public void RejectsOutOfRangeIndex()
    {
        using var reader = OpenFixture();
        var frame = Array.Empty<byte>();
        Assert.False(reader.ReadFrame(-1, ref frame));
        Assert.False(reader.ReadFrame(FrameCount, ref frame));
        Assert.True(reader.ReadFrame(FrameCount - 1, ref frame));
        Assert.Equal(ExpectedFrame(FrameCount - 1), frame);
    }

    // 없는 파일은 열지 못하고 이유를 남깁니다.
    [FfmpegFact]
    public void FailsOnMissingFile()
    {
        using var reader = FfmpegTestLibraries.Create()!;
        Assert.False(reader.Open(Path.Combine(Path.GetTempPath(), "diabeticfoot_missing.mkv"), out var error));
        Assert.NotEmpty(error);
        var frame = Array.Empty<byte>();
        Assert.False(reader.ReadFrame(0, ref frame));
    }

    // 한글 폴더와 파일 이름의 영상도 엽니다.
    [FfmpegFact]
    public void OpensKoreanPath()
    {
        var folder = Path.Combine(Path.GetTempPath(), "diabeticfoot_test_video_reader", "홍길동_19701015");
        Directory.CreateDirectory(folder);
        var copied = Path.Combine(folder, "101500_left.mkv");
        File.Copy(Fixture, copied, overwrite: true);
        using var reader = OpenFixture(copied);
        var frame = Array.Empty<byte>();
        Assert.True(reader.ReadFrame(7, ref frame));
        Assert.Equal(ExpectedFrame(7), frame);
    }

    // 4:2:2가 아닌 영상은 열지 않고 이유를 남깁니다.
    [FfmpegFact]
    public void RejectsNon422Video()
    {
        using var reader = FfmpegTestLibraries.Create()!;
        Assert.False(reader.Open(Path.Combine(AppContext.BaseDirectory, "Data", "ffv1_yuv420p.mkv"), out var error));
        Assert.Contains("4:2:2", error);
        Assert.Equal(0, reader.Width);
    }
}
