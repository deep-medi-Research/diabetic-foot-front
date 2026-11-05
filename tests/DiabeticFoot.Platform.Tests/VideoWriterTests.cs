using DiabeticFoot.Core;
using DiabeticFoot.Core.Flow;
using DiabeticFoot.Core.Record;
using DiabeticFoot.Platform;

namespace DiabeticFoot.Platform.Tests;

// 영상 쓰기 시험입니다. FFV1은 쓴 파일을 FFV1 읽기(Ffv1Reader)로 다시 읽어 바이트가 같은지 봅니다.
// 원본은 가짜 카메라의 프레임을 썼고(test_video_reader), 여기서는 Data/make_ffv1_fixture.sh와 같은 식으로 화소마다 장마다 다른 값을 만듭니다.
public class VideoWriterTests
{
    private const int FrameCount = 40;
    private const int Width = 64;
    private const int Height = 32;

    private static readonly VideoFormat Format = new(PixelFormat.Uyvy, Width, Height, 30);

    private static string FreshFolder(string name)
    {
        var folder = Path.Combine(Path.GetTempPath(), "diabeticfoot_test_video_writer", name);
        if (Directory.Exists(folder)) Directory.Delete(folder, recursive: true);
        Directory.CreateDirectory(folder);
        return folder;
    }

    private static byte[] Frame(int frame)
    {
        var bytes = new byte[Width * Height * 2];
        for (var i = 0; i < bytes.Length; i++) bytes[i] = (byte)((i >> 1) + frame * 17 + (i >> 7) * 13);
        return bytes;
    }

    private static IVideoWriter NewFfv1Writer()
    {
        FfmpegTestLibraries.Create()?.Dispose();
        var writer = VideoWriters.Create(VideoCodec.Ffv1, out var error);
        Assert.True(writer is not null, error);
        return writer!;
    }

    private static void WriteFrames(IVideoWriter writer, int count)
    {
        for (var i = 0; i < count; i++) Assert.True(writer.WriteFrame(Frame(i)), $"frame {i}");
    }

    // 번호로 읽은 프레임이 쓴 UYVY와 바이트 단위로 같습니다. 순서대로, 거꾸로, 건너뛰며 읽어도 같습니다.
    [FfmpegFact]
    public void Ffv1WriterRoundTripsByteExact()
    {
        var path = Path.Combine(FreshFolder("roundtrip"), "fake.mkv");
        using (var writer = NewFfv1Writer())
        {
            Assert.True(writer.Open(path, Format, FrameCount));
            WriteFrames(writer, FrameCount);
            Assert.Equal(FrameCount, writer.FramesWritten);
            Assert.True(writer.Close());
        }

        using var reader = FfmpegTestLibraries.Create()!;
        Assert.True(reader.Open(path, out var error), error);
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
            Assert.True(Frame(index).AsSpan().SequenceEqual(frame), $"frame {index}");
        }
    }

    // 최대 장 수보다 일찍 닫아도 그때까지 쓴 프레임으로 읽을 수 있는 파일이 됩니다.
    [FfmpegFact]
    public void Ffv1WriterClosedEarlyKeepsWrittenFrames()
    {
        var path = Path.Combine(FreshFolder("early"), "early.mkv");
        using (var writer = NewFfv1Writer())
        {
            Assert.True(writer.Open(path, Format, 900));
            WriteFrames(writer, 7);
            Assert.True(writer.Close());
            Assert.False(writer.Close());
        }

        using var reader = FfmpegTestLibraries.Create()!;
        Assert.True(reader.Open(path, out var error), error);
        Assert.Equal(7, reader.FrameCount);
        var frame = Array.Empty<byte>();
        Assert.True(reader.ReadFrame(6, ref frame));
        Assert.True(Frame(6).AsSpan().SequenceEqual(frame));
    }

    // 닫지 않고 버려도(Dispose) 파일이 읽을 수 있게 마무리됩니다.
    [FfmpegFact]
    public void Ffv1WriterDisposeFinishesFile()
    {
        var path = Path.Combine(FreshFolder("dispose"), "dispose.mkv");
        using (var writer = NewFfv1Writer())
        {
            Assert.True(writer.Open(path, Format, 10));
            WriteFrames(writer, 3);
        }

        using var reader = FfmpegTestLibraries.Create()!;
        Assert.True(reader.Open(path, out var error), error);
        Assert.Equal(3, reader.FrameCount);
    }

    // UYVY가 아니거나 폭이 홀수이거나 fps가 0 이하이거나 폴더가 없으면 열지 않고, 열기 전에는 쓰지도 닫지도 못합니다.
    [FfmpegFact]
    public void Ffv1WriterRejectsBadFormatAndMissingFolder()
    {
        var folder = FreshFolder("reject");
        using var writer = NewFfv1Writer();
        Assert.False(writer.WriteFrame(Frame(0)));
        Assert.False(writer.Close());
        Assert.False(writer.Open(Path.Combine(folder, "a.mkv"), new VideoFormat(PixelFormat.Nv12, Width, Height, 30), 10));
        Assert.False(writer.Open(Path.Combine(folder, "b.mkv"), new VideoFormat(PixelFormat.Uyvy, Width + 1, Height, 30), 10));
        Assert.False(writer.Open(Path.Combine(folder, "c.mkv"), new VideoFormat(PixelFormat.Uyvy, Width, Height, 0), 10));
        Assert.False(writer.Open(Path.Combine(folder, "없는 폴더", "d.mkv"), Format, 10));
        Assert.False(writer.Close());
    }

    // 프레임 바이트 수보다 짧은 입력은 쓰지 않습니다.
    [FfmpegFact]
    public void Ffv1WriterRejectsShortFrame()
    {
        var path = Path.Combine(FreshFolder("short"), "short.mkv");
        using var writer = NewFfv1Writer();
        Assert.True(writer.Open(path, Format, 10));
        Assert.False(writer.WriteFrame(new byte[Width * Height * 2 - 1]));
        Assert.Equal(0, writer.FramesWritten);
        Assert.True(writer.WriteFrame(Frame(0)));
        Assert.Equal(1, writer.FramesWritten);
        Assert.True(writer.Close());
    }

    // 한글 폴더와 파일 이름에도 씁니다. 파일 옆에 쓰기 대기열 기록(.queue.txt)을 남깁니다.
    [FfmpegFact]
    public void Ffv1WriterWritesKoreanPathAndQueueReport()
    {
        var path = Path.Combine(FreshFolder("홍길동_19701015"), "101500_left.mkv");
        using (var writer = NewFfv1Writer())
        {
            Assert.True(writer.Open(path, Format, 10));
            WriteFrames(writer, 4);
            Assert.True(writer.Close());
        }

        Assert.True(File.Exists(path));
        var report = File.ReadAllText(path + ".queue.txt");
        Assert.StartsWith("frames=4 limit_mb=768 ", report);
        Assert.EndsWith("mux_failed=0\n", report);
    }

    [Fact]
    public void CreatesAviWriterForUncompressedWithoutFfmpeg()
    {
        using var writer = VideoWriters.Create(VideoCodec.Uncompressed, out var error);
        Assert.NotNull(writer);
        Assert.IsType<AviFile>(writer);
        Assert.Equal("", error);
        Assert.Null(VideoWriters.Create((VideoCodec)99, out error));
        Assert.Equal("알 수 없는 코덱", error);
    }

    // AviFile은 AviBuilder가 돌려준 덧붙임과 덮어쓰기를 순서대로 파일에 적습니다. 같은 입력으로 직접 만든 바이트와 같습니다.
    [Fact]
    public void AviFileWritesBuilderOutputByteForByte()
    {
        const int frames = 3;
        var path = Path.Combine(FreshFolder("avi"), "plain.avi");
        using (var avi = new AviFile())
        {
            Assert.Equal(0, avi.FrameBytes);
            Assert.True(avi.Open(path, Format, 10));
            Assert.Equal(Width * Height * 2, avi.FrameBytes);
            for (var i = 0; i < frames; i++) Assert.True(avi.WriteFrame(Frame(i)));
            Assert.Equal(frames, avi.FramesWritten);
            Assert.True(avi.Close());
            Assert.False(avi.Close());
        }

        var builder = new AviBuilder(Format, 10);
        var expected = new List<byte>();
        void Apply(AviOutput output)
        {
            expected.AddRange(output.Append);
            foreach (var patch in output.Patches)
            {
                for (var i = 0; i < patch.Bytes.Length; i++) expected[(int)patch.Offset + i] = patch.Bytes[i];
            }
        }
        Apply(builder.Start());
        for (var i = 0; i < frames; i++)
        {
            Apply(builder.BeginFrame()!);
            expected.AddRange(Frame(i));
        }
        Apply(builder.Finish());
        Assert.Equal(expected.ToArray(), File.ReadAllBytes(path));
    }

    // 형식이 잘못되었거나 최대 장 수를 넘기거나 짧은 프레임이면 쓰지 않습니다.
    [Fact]
    public void AviFileRejectsBadFormatExtraAndShortFrames()
    {
        var folder = FreshFolder("avi_reject");
        using var avi = new AviFile();
        Assert.False(avi.WriteFrame(Frame(0)));
        Assert.False(avi.Open(Path.Combine(folder, "zero.avi"), new VideoFormat(PixelFormat.Uyvy, 0, Height, 30), 10));
        Assert.False(avi.Open(Path.Combine(folder, "none.avi"), Format, 0));
        Assert.False(avi.Open(Path.Combine(folder, "없는 폴더", "x.avi"), Format, 10));
        Assert.True(avi.Open(Path.Combine(folder, "한글.avi"), Format, 2));
        Assert.False(avi.WriteFrame(new byte[10]));
        Assert.True(avi.WriteFrame(Frame(0)));
        Assert.True(avi.WriteFrame(Frame(1)));
        Assert.False(avi.WriteFrame(Frame(2)));
        Assert.Equal(2, avi.FramesWritten);
        Assert.True(avi.Close());
    }
}
