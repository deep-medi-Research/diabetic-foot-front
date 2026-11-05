using DiabeticFoot.Core.Record;

namespace DiabeticFoot.Core.Tests.Record;

public class AviTests
{
    private static readonly VideoFormat SmallUyvy = new(PixelFormat.Uyvy, 8, 4, 30);

    // platform이 파일에 하는 일을 메모리에서 그대로 합니다.
    private sealed class MemoryFile
    {
        public List<byte> Bytes { get; } = new();

        public void Apply(AviOutput output)
        {
            Bytes.AddRange(output.Append);
            foreach (FilePatch patch in output.Patches)
            {
                Assert.True(patch.Offset + (ulong)patch.Bytes.Length <= (ulong)Bytes.Count);
                for (int i = 0; i < patch.Bytes.Length; i++)
                {
                    Bytes[(int)patch.Offset + i] = patch.Bytes[i];
                }
            }
        }

        // 프레임 i의 모든 바이트를 i로 채워, 색인이 가리키는 화소가 몇 번째 프레임인지 알 수 있게 합니다.
        public void AppendFrame(long frameBytes, long i)
        {
            for (long n = 0; n < frameBytes; n++)
            {
                Bytes.Add((byte)i);
            }
        }

        public uint U32(ulong at) =>
            (uint)(Bytes[(int)at] | (Bytes[(int)at + 1] << 8) | (Bytes[(int)at + 2] << 16) | (Bytes[(int)at + 3] << 24));

        public ulong U64(ulong at) => U32(at) | ((ulong)U32(at + 4) << 32);

        public string Tag(ulong at) => new(new[] { (char)Bytes[(int)at], (char)Bytes[(int)at + 1], (char)Bytes[(int)at + 2], (char)Bytes[(int)at + 3] });
    }

    private static MemoryFile Record(VideoFormat format, long maxFrames, long frames, ulong riffLimit)
    {
        var builder = new AviBuilder(format, maxFrames, riffLimit);
        var file = new MemoryFile();
        file.Apply(builder.Start());
        for (long i = 0; i < frames; i++)
        {
            AviOutput? output = builder.BeginFrame();
            if (output is null)
            {
                break;
            }
            file.Apply(output);
            file.AppendFrame(builder.FrameBytes, i);
        }
        file.Apply(builder.Finish());
        return file;
    }

    // 최상위 RIFF 묶음의 위치를 차례로 찾습니다.
    private static List<ulong> RiffOffsets(MemoryFile file)
    {
        var offsets = new List<ulong>();
        ulong at = 0;
        while (at + 8 <= (ulong)file.Bytes.Count)
        {
            Assert.Equal("RIFF", file.Tag(at));
            offsets.Add(at);
            at += 8 + file.U32(at + 4);
        }
        Assert.Equal((ulong)file.Bytes.Count, at);
        return offsets;
    }

    // 묶음 안에서 tag 청크의 위치를 찾습니다. LIST는 안으로 들어가 찾습니다.
    private static ulong? FindChunk(MemoryFile file, ulong begin, ulong end, string tag)
    {
        ulong at = begin;
        while (at + 8 <= end)
        {
            string id = file.Tag(at);
            uint size = file.U32(at + 4);
            if (id == tag)
            {
                return at;
            }
            if (id == "LIST")
            {
                ulong? inner = FindChunk(file, at + 12, at + 8 + size, tag);
                if (inner.HasValue)
                {
                    return inner;
                }
            }
            at += 8 + size + (size % 2);
        }
        return null;
    }

    // 헤더에 크기, 형식, 프레임 수가 맞게 들어갑니다.
    [Fact]
    public void WritesHeaderFields()
    {
        var file = Record(SmallUyvy, 10, 3, AviBuilder.DefaultRiffLimitBytes);

        Assert.Single(RiffOffsets(file));
        Assert.Equal("AVI ", file.Tag(8));
        ulong? avih = FindChunk(file, 12, (ulong)file.Bytes.Count, "avih");
        Assert.NotNull(avih);
        Assert.Equal(33333u, file.U32(avih.Value + 8));       // 프레임 간격(us)
        Assert.Equal(3u, file.U32(avih.Value + 8 + 16));      // 첫 RIFF 프레임 수
        Assert.Equal(8u, file.U32(avih.Value + 8 + 32));      // 폭
        Assert.Equal(4u, file.U32(avih.Value + 8 + 36));      // 높이
        ulong? strf = FindChunk(file, 12, (ulong)file.Bytes.Count, "strf");
        Assert.NotNull(strf);
        Assert.Equal("UYVY", file.Tag(strf.Value + 8 + 16));
        ulong? dmlh = FindChunk(file, 12, (ulong)file.Bytes.Count, "dmlh");
        Assert.NotNull(dmlh);
        Assert.Equal(3u, file.U32(dmlh.Value + 8));
    }

    // 기존 색인(idx1)의 각 항목이 해당 프레임의 화소를 가리킵니다.
    [Fact]
    public void Idx1PointsToEachFrame()
    {
        var file = Record(SmallUyvy, 10, 3, AviBuilder.DefaultRiffLimitBytes);
        ulong? movi = FindChunk(file, 12, (ulong)file.Bytes.Count, "LIST");  // hdrl
        ulong? idx1 = FindChunk(file, 12, (ulong)file.Bytes.Count, "idx1");
        Assert.True(movi.HasValue && idx1.HasValue);
        // movi 리스트는 hdrl 다음 LIST입니다.
        ulong moviList = movi.Value + 8 + file.U32(movi.Value + 4);
        Assert.Equal("movi", file.Tag(moviList + 8));

        Assert.Equal(3u * 16, file.U32(idx1.Value + 4));
        for (uint i = 0; i < 3; i++)
        {
            ulong entry = idx1.Value + 8 + i * 16;
            Assert.Equal("00db", file.Tag(entry));
            ulong chunk = moviList + 8 + file.U32(entry + 8);
            Assert.Equal("00db", file.Tag(chunk));
            Assert.Equal(i, file.Bytes[(int)chunk + 8]);
        }
    }

    // RIFF 크기 한도를 넘으면 AVIX로 나누고, 상위 색인이 모든 구간의 ix00을 가리킵니다.
    [Fact]
    public void SplitsIntoAvixAndIndexesEveryFrame()
    {
        // 프레임 하나가 64바이트라 한도 1000바이트에서 RIFF마다 몇 장만 들어갑니다.
        var file = Record(SmallUyvy, 40, 40, 1000);

        var riffs = RiffOffsets(file);
        Assert.True(riffs.Count > 2);
        for (int i = 0; i < riffs.Count; i++)
        {
            Assert.True(file.U32(riffs[i] + 4) + 8u <= 1000u);
            Assert.Equal(i == 0 ? "AVI " : "AVIX", file.Tag(riffs[i] + 8));
        }

        ulong? indx = FindChunk(file, 12, file.U32(4) + 8, "indx");
        Assert.NotNull(indx);
        uint inUse = file.U32(indx.Value + 8 + 4);
        Assert.Equal((uint)riffs.Count, inUse);
        long nextFrame = 0;
        for (uint s = 0; s < inUse; s++)
        {
            ulong entry = indx.Value + 8 + 24 + s * 16;
            ulong ix = file.U64(entry);
            Assert.Equal("ix00", file.Tag(ix));
            uint frames = file.U32(ix + 8 + 4);
            Assert.Equal(frames, file.U32(entry + 12));
            ulong baseOffset = file.U64(ix + 8 + 12);
            for (uint f = 0; f < frames; f++)
            {
                ulong data = baseOffset + file.U32(ix + 8 + 24 + f * 8);
                Assert.Equal((byte)nextFrame, file.Bytes[(int)data]);
                Assert.Equal("00db", file.Tag(data - 8));
                nextFrame++;
            }
        }
        Assert.Equal(40, nextFrame);
        ulong? dmlh = FindChunk(file, 12, file.U32(4) + 8, "dmlh");
        Assert.Equal(40u, file.U32(dmlh!.Value + 8));
    }

    // maxFrames를 다 쓰면 더 받지 않습니다.
    [Fact]
    public void RefusesFramesBeyondMax()
    {
        var builder = new AviBuilder(SmallUyvy, 2);
        builder.Start();

        Assert.NotNull(builder.BeginFrame());
        Assert.NotNull(builder.BeginFrame());
        Assert.Null(builder.BeginFrame());
        Assert.Equal(2, builder.FramesWritten);
    }

    // 목표보다 일찍 멈춰도(중지 버튼, 연결 끊김) 크기와 프레임 수가 맞는 파일이 됩니다.
    [Fact]
    public void StopsEarlyWithConsistentSizes()
    {
        var file = Record(SmallUyvy, 900, 5, 5000);

        Assert.NotEmpty(file.Bytes);
        RiffOffsets(file);
        ulong? dmlh = FindChunk(file, 12, file.U32(4) + 8, "dmlh");
        Assert.NotNull(dmlh);
        Assert.Equal(5u, file.U32(dmlh.Value + 8));
    }

    // 1080p UYVY 900장은 1GiB 한도에서 RIFF 4개로 나뉩니다.
    [Fact]
    public void PlansFullHdNineHundredFrames()
    {
        var builder = new AviBuilder(new VideoFormat(PixelFormat.Uyvy, 1920, 1080, 30), 900);

        Assert.True(builder.IsValid);
        Assert.Equal(1920L * 1080 * 2, builder.FrameBytes);
    }

    // 열화상 원시값과 잘못된 크기는 거부합니다.
    [Fact]
    public void RejectsUnsupportedFormats()
    {
        Assert.False(new AviBuilder(new VideoFormat(PixelFormat.Y16, 640, 480, 30), 900).IsValid);
        Assert.False(new AviBuilder(new VideoFormat(PixelFormat.Nv12, 7, 4, 30), 900).IsValid);
        Assert.False(new AviBuilder(new VideoFormat(PixelFormat.Uyvy, 8, 4, 0), 900).IsValid);
        Assert.False(new AviBuilder(SmallUyvy, 900, 100).IsValid);
        // 900장 분량의 색인 자리를 잡으면 헤더가 한도 1000바이트를 넘습니다.
        Assert.False(new AviBuilder(SmallUyvy, 900, 1000).IsValid);
    }
}
