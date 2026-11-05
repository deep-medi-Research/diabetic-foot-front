using System.Text;
using DiabeticFoot.Core;
using DiabeticFoot.Core.Pixel;
using DiabeticFoot.Platform;

namespace DiabeticFoot.Platform.Tests;

// 원본 test_thermal.cc 중 열화상 파일 쓰기(ThermalFiles) 시험입니다. 원본은 프레임을 가짜 열화상 카메라에서 받았고,
// 여기서는 같은 화소 식(FakeThermalCamera::PixelValue)으로 시험 안에서 프레임을 만듭니다. 가짜 열화상 카메라를 옮기면 그것으로 바꿉니다.
public class ThermalWriterTests : IDisposable
{
    private const int Width = 640;
    private const int Height = 480;
    private const int FrameBytes = Width * Height * 2;

    private readonly string _dir;

    public ThermalWriterTests()
    {
        _dir = Path.Combine(Path.GetTempPath(), "diabeticfoot_test_thermal_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_dir);
    }

    public void Dispose() => Directory.Delete(_dir, recursive: true);

    private string PathOf(string name) => Path.Combine(_dir, name);

    // 장치 프레임 번호, 열, 행으로 정해지는 화소 값입니다. 원본 가짜 열화상 카메라와 같은 식입니다.
    private static ushort PixelValue(long deviceFrame, int x, int y) => (ushort)(7000 + (x + 2L * y + 5 * deviceFrame) % 2000);

    private static byte[] Pixels(long deviceFrame)
    {
        var bytes = new byte[FrameBytes];
        for (var y = 0; y < Height; ++y)
        {
            for (var x = 0; x < Width; ++x)
            {
                var v = PixelValue(deviceFrame, x, y);
                bytes[(y * Width + x) * 2] = (byte)(v & 0xFF);
                bytes[(y * Width + x) * 2 + 1] = (byte)(v >> 8);
            }
        }
        return bytes;
    }

    private static FrameView View(byte[] pixels, int width = Width, int height = Height, int rowStride = Width * 2) => new()
    {
        Format = PixelFormat.Y16,
        Width = width,
        Height = height,
        Plane0 = pixels,
        RowStride0 = rowStride,
        Stream = StreamKind.Thermal,
    };

    // frames장을 원시값 파일로 쓰고, 쓴 바이트를 이어 붙여 돌려줍니다.
    private byte[] RecordRaw(string path, int frames)
    {
        using var raw = new Raw16File();
        Assert.True(raw.Open(path, Width, Height));
        var received = new List<byte>();
        for (var i = 0; i < frames; ++i)
        {
            var pixels = Pixels(i);
            received.AddRange(pixels);
            Assert.True(raw.WriteFrame(View(pixels)));
        }
        Assert.True(raw.Close());
        return received.ToArray();
    }

    private static uint U32(byte[] b, long at) => b[at] | ((uint)b[at + 1] << 8) | ((uint)b[at + 2] << 16) | ((uint)b[at + 3] << 24);

    private static string Tag(byte[] b, long at) => Encoding.ASCII.GetString(b, (int)at, 4);

    // 묶음 안에서 tag 청크의 위치를 찾습니다. LIST는 안으로 들어가 찾습니다.
    private static long? FindChunk(byte[] b, long begin, long end, string tag)
    {
        var at = begin;
        while (at + 8 <= end)
        {
            var size = U32(b, at + 4);
            if (Tag(b, at) == tag) return at;
            if (Tag(b, at) == "LIST" && FindChunk(b, at + 12, at + 8 + size, tag) is { } inner) return inner;
            at += 8 + size + (size % 2);
        }
        return null;
    }

    private static ushort Y16At(byte[] pixels, int x, int y) => (ushort)(pixels[(y * Width + x) * 2] | (pixels[(y * Width + x) * 2 + 1] << 8));

    // PixelFunctions.Y16ToGray8과 같은 식을 따로 적어 변환 결과를 대조합니다.
    private static byte ExpectedGray(ushort v, Y16Range range)
    {
        int span = range.High - range.Low;
        if (span <= 0) return 128;
        var c = Math.Clamp((int)v, range.Low, range.High) - range.Low;
        return (byte)((c * 255 + span / 2) / span);
    }

    // 640x480 Y16을 원시값 파일로 쓰고 다시 읽으면 받은 바이트와 같습니다.
    [Fact]
    public void Raw16RoundTripMatchesReceivedBytes()
    {
        var path = PathOf("thermal.raw16");
        var received = RecordRaw(path, 5);
        Assert.Equal(5 * FrameBytes, received.Length);
        var stored = File.ReadAllBytes(path);
        Assert.Equal(received, stored);
        // 리틀 엔디언으로 저장했는지 화소 값으로 확인합니다.
        var v = PixelValue(2, 7, 3);
        var at = 2 * FrameBytes + (3 * Width + 7) * 2;
        Assert.Equal(v & 0xFF, stored[at]);
        Assert.Equal(v >> 8, stored[at + 1]);
    }

    // 행 사이에 빈 바이트가 있는 프레임은 빈 바이트를 빼고 저장합니다. 크기가 다른 프레임은 거부합니다.
    [Fact]
    public void Raw16DropsRowPaddingAndRejectsOtherSize()
    {
        var padded = new byte[2 * 8];  // 폭 2, 높이 2, 행 간격 8
        Array.Fill(padded, (byte)0xEE);
        new byte[] { 1, 2, 3, 4 }.CopyTo(padded, 0);
        new byte[] { 5, 6, 7, 8 }.CopyTo(padded, 8);
        var frame = View(padded, 2, 2, 8);

        var path = PathOf("padded.raw16");
        using var raw = new Raw16File();
        Assert.True(raw.Open(path, 2, 2));
        Assert.True(raw.WriteFrame(frame));
        Assert.False(raw.WriteFrame(frame with { Width = 4 }));
        Assert.Equal(1, raw.FramesWritten);
        Assert.True(raw.Close());
        Assert.Equal(new byte[] { 1, 2, 3, 4, 5, 6, 7, 8 }, File.ReadAllBytes(path));
    }

    // 열기 전과 닫은 뒤에는 쓰지 못하고, 닫기는 한 번만 성공합니다. 프레임이 0장이어도 파일은 남습니다.
    [Fact]
    public void Raw16RejectsUseOutsideOpenAndKeepsEmptyFile()
    {
        using var raw = new Raw16File();
        Assert.Equal(0, raw.FrameBytes);
        Assert.False(raw.Close());
        Assert.False(raw.WriteFrame(View(Pixels(0))));
        Assert.False(raw.Open(PathOf("zero.raw16"), 0, 480));
        Assert.False(raw.Open(PathOf("없는 폴더/a.raw16"), Width, Height));
        var path = PathOf("empty.raw16");
        Assert.True(raw.Open(path, Width, Height));
        Assert.False(raw.Open(PathOf("again.raw16"), Width, Height));
        Assert.Equal(FrameBytes, raw.FrameBytes);
        Assert.True(raw.Close());
        Assert.False(raw.WriteFrame(View(Pixels(0))));
        Assert.True(File.Exists(path));
        Assert.Empty(File.ReadAllBytes(path));
    }

    // 확인용 AVI는 쓴 프레임 수를 갖고, 화소는 첫 프레임에서 정한 범위로 편 회색입니다. 범위는 녹화 동안 바뀌지 않습니다.
    [Fact]
    public void PreviewAviKeepsFirstFrameRange()
    {
        const int frames = 5;
        var path = PathOf("thermal.avi");
        using var preview = new ThermalPreviewFile();
        Assert.True(preview.Open(path, Width, Height, 30, 100, null));
        Assert.Null(preview.Range);
        var values = new List<byte[]>();
        Y16Range? firstRange = null;
        for (var i = 0; i < frames; ++i)
        {
            var pixels = Pixels(i);
            if (i == 0) firstRange = PixelFunctions.FindRange(View(pixels), new Rect(0, 0, Width, Height));
            values.Add(pixels);
            Assert.True(preview.WriteFrame(View(pixels)));
        }
        Assert.True(preview.Close());
        Assert.NotNull(preview.Range);
        Assert.Equal(firstRange!.Value.Low, preview.Range!.Value.Low);
        Assert.Equal(firstRange.Value.High, preview.Range.Value.High);
        Assert.Equal(frames, preview.FramesWritten);

        var avi = File.ReadAllBytes(path);
        Assert.True(avi.Length > frames * FrameBytes);
        var avih = FindChunk(avi, 12, avi.Length, "avih");
        Assert.NotNull(avih);
        Assert.Equal((uint)frames, U32(avi, avih!.Value + 8 + 16));  // 프레임 수
        Assert.Equal((uint)Width, U32(avi, avih.Value + 8 + 32));
        Assert.Equal((uint)Height, U32(avi, avih.Value + 8 + 36));

        var hdrl = FindChunk(avi, 12, avi.Length, "LIST");
        var idx1 = FindChunk(avi, 12, avi.Length, "idx1");
        Assert.True(hdrl.HasValue && idx1.HasValue);
        var movi = hdrl!.Value + 8 + U32(avi, hdrl.Value + 4);
        Assert.Equal("movi", Tag(avi, movi + 8));
        Assert.Equal((uint)frames * 16, U32(avi, idx1!.Value + 4));
        var sawClipped = false;
        for (var i = 0; i < frames; ++i)
        {
            var chunk = movi + 8 + U32(avi, idx1.Value + 8 + i * 16 + 8);
            Assert.Equal((uint)FrameBytes, U32(avi, chunk + 4));  // UYVY도 화소당 2바이트
            var start = chunk + 8;
            for (var y = 0; y < Height; ++y)
            {
                for (var x = 0; x < Width; ++x)
                {
                    var p = y * Width + x;
                    var v = Y16At(values[i], x, y);
                    Assert.True(avi[start + p * 2] == 128, $"frame {i} pixel {p}");
                    Assert.True(avi[start + p * 2 + 1] == ExpectedGray(v, firstRange.Value), $"frame {i} pixel {p}");
                    sawClipped = sawClipped || v > firstRange.Value.High;
                }
            }
        }
        // 뒤 프레임에 첫 범위를 넘는 값이 있어야 범위를 고정했는지 확인됩니다.
        Assert.True(sawClipped);
    }

    // 표시 범위를 주면 그 값을 그대로 씁니다. 폭이 홀수이면 열지 않습니다.
    [Fact]
    public void PreviewUsesGivenRangeAndRejectsOddWidth()
    {
        using var preview = new ThermalPreviewFile();
        Assert.True(preview.Open(PathOf("given.avi"), Width, Height, 30, 10, new Y16Range(7500, 8000)));
        Assert.NotNull(preview.Range);
        Assert.Equal(7500, preview.Range!.Value.Low);
        Assert.Equal(8000, preview.Range.Value.High);
        Assert.True(preview.Close());
        using var odd = new ThermalPreviewFile();
        Assert.False(odd.Open(PathOf("odd.avi"), 641, Height, 30, 10, null));
    }

    // .npy는 NumPy 1.0 머리글(64바이트 정렬, '<u2', 모양) 뒤에 원시값 파일과 같은 바이트가 옵니다.
    [Fact]
    public void NpyHasHeaderAndRawBytes()
    {
        var rawPath = PathOf("thermal.raw16");
        var received = RecordRaw(rawPath, 3);
        var frames = ThermalWriter.WriteNpy(rawPath, Width, Height, new Rect(0, 0, Width, Height), PathOf("thermal.npy"), out var error);
        Assert.True(frames.HasValue, error);
        Assert.Equal(3, frames!.Value);

        var npy = File.ReadAllBytes(PathOf("thermal.npy"));
        Assert.True(npy.Length > 10);
        Assert.Equal(new byte[] { 0x93, (byte)'N', (byte)'U', (byte)'M', (byte)'P', (byte)'Y', 1, 0 }, npy[..8]);
        var headerBytes = npy[8] | (npy[9] << 8);
        var dataAt = 10 + headerBytes;
        Assert.Equal(0, dataAt % 64);
        var dict = Encoding.ASCII.GetString(npy, 10, headerBytes);
        Assert.StartsWith("{'descr': '<u2', 'fortran_order': False, 'shape': (3, 480, 640), }", dict);
        Assert.Equal('\n', dict[^1]);
        Assert.Equal(received.Length, npy.Length - dataAt);
        Assert.Equal(received, npy[dataAt..]);
    }

    // 영역을 자른 .npy와 원시값 파일은 영역 크기의 모양이고 화소는 원본 영역과 같습니다.
    [Fact]
    public void CropWritesRegionToNpyAndRaw16()
    {
        var rawPath = PathOf("thermal.raw16");
        RecordRaw(rawPath, 2);
        var left = new Rect(10, 20, 100, 50);
        Assert.True(ThermalWriter.WriteNpy(rawPath, Width, Height, left, PathOf("thermal_left.npy"), out var error).HasValue, error);
        Assert.True(ThermalWriter.WriteRaw16Crop(rawPath, Width, Height, left, PathOf("thermal_left.raw16"), out error).HasValue, error);

        var npy = File.ReadAllBytes(PathOf("thermal_left.npy"));
        var dataAt = 10 + (npy[8] | (npy[9] << 8));
        var dict = Encoding.ASCII.GetString(npy, 10, dataAt - 10);
        Assert.Contains("'shape': (2, 50, 100)", dict);
        var crop = npy[dataAt..];
        Assert.Equal(File.ReadAllBytes(PathOf("thermal_left.raw16")), crop);
        Assert.Equal(2 * 100 * 50 * 2, crop.Length);
        for (var f = 0; f < 2; ++f)
        {
            for (var y = 0; y < left.Height; ++y)
            {
                for (var x = 0; x < left.Width; ++x)
                {
                    var at = (((long)f * left.Height + y) * left.Width + x) * 2;
                    var v = (ushort)(crop[at] | (crop[at + 1] << 8));
                    Assert.True(v == PixelValue(f, left.X + x, left.Y + y), $"frame {f} x {x} y {y}");
                }
            }
        }
    }

    // 원시값 파일 크기가 프레임의 배수가 아니거나 영역이 프레임 밖이면 쓰지 않습니다.
    [Fact]
    public void NpyRejectsBadSizeAndRegion()
    {
        var rawPath = PathOf("short.raw16");
        File.WriteAllBytes(rawPath, new byte[FrameBytes + 2]);
        var full = new Rect(0, 0, Width, Height);
        Assert.Null(ThermalWriter.WriteNpy(rawPath, Width, Height, full, PathOf("a.npy"), out var error));
        Assert.NotEmpty(error);
        Assert.Null(ThermalWriter.WriteNpy(rawPath, Width, Height, new Rect(600, 0, 100, 10), PathOf("b.npy"), out error));
        Assert.Null(ThermalWriter.WriteNpy(PathOf("missing.raw16"), Width, Height, full, PathOf("c.npy"), out error));
        Assert.NotEmpty(error);
    }
}
