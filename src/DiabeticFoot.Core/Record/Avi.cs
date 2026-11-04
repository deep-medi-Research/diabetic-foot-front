namespace DiabeticFoot.Core.Record;

/// <summary>영상 파일의 화소 형식, 크기, fps입니다.</summary>
public readonly record struct VideoFormat(PixelFormat Format = PixelFormat.Uyvy, int Width = 0, int Height = 0, int FrameRate = 0);

/// <summary>이미 쓴 바이트를 고칠 자리입니다. Offset은 파일 처음부터 센 바이트 수입니다.</summary>
public sealed record FilePatch(ulong Offset, byte[] Bytes)
{
    public bool Equals(FilePatch? other) => other is not null && Offset == other.Offset && Bytes.AsSpan().SequenceEqual(other.Bytes);

    public override int GetHashCode() => HashCode.Combine(Offset, Bytes.Length);
}

/// <summary><see cref="AviBuilder"/>가 돌려주는 쓰기 작업입니다. 파일 끝에 Append를 덧붙인 뒤 Patches를 순서대로 덮어씁니다.</summary>
public sealed record AviOutput(byte[] Append, IReadOnlyList<FilePatch> Patches)
{
    public AviOutput() : this(Array.Empty<byte>(), Array.Empty<FilePatch>())
    {
    }
}

/// <summary>
/// 무압축 AVI를 OpenDML 형식으로 만듭니다.
/// </summary>
/// <remarks>
/// 1080p UYVY 900장(3.7GB)처럼 1GiB를 넘는 파일을 위해 RIFF를 나눕니다. 파일 입출력은 하지 않고 쓸 바이트와 위치만 돌려줍니다.
/// 호출 순서는 <see cref="Start"/>, (<see cref="BeginFrame"/> 뒤 <see cref="FrameBytes"/>만큼 화소 덧붙이기) 반복, <see cref="Finish"/>입니다.
/// 일찍 멈춰도(중지, 연결 끊김) Finish를 부르면 그때까지 쓴 프레임 수가 맞게 들어간 파일이 됩니다.
/// </remarks>
public sealed class AviBuilder
{
    /// <summary>1GiB를 넘는 RIFF를 읽지 못하는 도구가 있어, RIFF 하나를 이 크기 이하로 나눕니다.</summary>
    public const ulong DefaultRiffLimitBytes = 1UL << 30;

    // OpenDML 1.02 규격의 값입니다.
    private const uint AvifHasIndex = 0x10;
    private const uint AviifKeyframe = 0x10;
    private const byte IndexOfIndexes = 0x00;
    private const byte IndexOfChunks = 0x01;
    private const int AvihBytes = 56;
    private const int StrhBytes = 56;
    private const int StrfBytes = 40;
    private const int DmlhBytes = 248;
    private const int SuperIndexHeaderBytes = 24;
    private const int SuperIndexEntryBytes = 16;
    private const int ChunkIndexHeaderBytes = 24;
    private const int ChunkIndexEntryBytes = 8;
    private const int Idx1EntryBytes = 16;

    private struct Segment
    {
        public ulong RiffOffset;  // 'RIFF' 위치
        public ulong MoviOffset;  // 'LIST' 위치
        public long FirstFrame;
        public long Frames;
    }

    private readonly record struct IndexEntry(ulong Offset, uint Bytes, uint Frames);

    private readonly VideoFormat _format;
    private readonly long _maxFrames;
    private readonly ulong _riffLimitBytes;
    private ulong _frameBytes;
    private int _indexSlots;
    private int _headerBytes;
    private ulong _position;  // 지금까지 파일에 쓴 바이트 수
    private long _framesWritten;
    private readonly List<Segment> _segments = new();
    private readonly List<IndexEntry> _indexEntries = new();
    private bool _started;
    private bool _finished;

    /// <param name="format">화소 형식과 크기. UYVY와 NV12만 받습니다</param>
    /// <param name="maxFrames">쓸 수 있는 최대 프레임 수. 헤더에 OpenDML 색인 자리를 잡는 데 씁니다</param>
    /// <param name="riffLimitBytes">RIFF 하나의 최대 크기</param>
    public AviBuilder(VideoFormat format, long maxFrames, ulong riffLimitBytes = DefaultRiffLimitBytes)
    {
        _format = format;
        _maxFrames = maxFrames;
        _riffLimitBytes = riffLimitBytes;
        ulong payload = PayloadBytes(format);
        if (payload == 0 || maxFrames <= 0)
        {
            return;
        }
        _frameBytes = payload;
        // 헤더 크기가 색인 자리 수에 따라 달라지고, 자리 수는 RIFF마다 담을 프레임 수에 따라 달라져 고정점까지 반복합니다.
        _indexSlots = 1;
        for (int i = 0; i < 64; i++)
        {
            _headerBytes = BuildHeader(0, 0).Length;
            long remaining = _maxFrames - CapacityOf(0);
            int slots = 1;
            while (remaining > 0)
            {
                long capacity = CapacityOf(1);
                if (capacity <= 0)
                {
                    _frameBytes = 0;
                    return;
                }
                remaining -= capacity;
                slots++;
            }
            if (CapacityOf(0) <= 0)
            {
                _frameBytes = 0;
                return;
            }
            if (slots == _indexSlots)
            {
                return;
            }
            _indexSlots = slots;
        }
        _frameBytes = 0;
    }

    /// <summary>형식이나 크기가 잘못되면 false이고, 이때 다른 함수는 빈 결과를 돌려줍니다.</summary>
    public bool IsValid => _frameBytes > 0;

    /// <summary>프레임 하나의 화소 바이트 수입니다. <see cref="BeginFrame"/> 뒤에 이만큼 덧붙입니다.</summary>
    public long FrameBytes => (long)_frameBytes;

    /// <summary><see cref="BeginFrame"/>으로 머리를 만든 프레임 수입니다.</summary>
    public long FramesWritten => _framesWritten;

    /// <summary>헤더와 첫 movi 목록의 머리를 만듭니다. 한 번만 부릅니다.</summary>
    public AviOutput Start()
    {
        if (!IsValid || _started)
        {
            return new AviOutput();
        }
        _started = true;
        var output = new Output();
        output.Append.AddRange(BuildHeader(0, 0));
        OpenSegment(output);
        _position += (ulong)output.Append.Count;
        return output.ToResult();
    }

    /// <summary>다음 프레임의 머리를 만듭니다. RIFF가 차면 색인을 닫고 다음 RIFF를 엽니다.</summary>
    /// <returns>덧붙일 바이트. maxFrames를 다 썼으면 null입니다</returns>
    public AviOutput? BeginFrame()
    {
        if (!IsValid || !_started || _finished || _framesWritten >= _maxFrames)
        {
            return null;
        }
        var output = new Output();
        if (_segments[^1].Frames >= CapacityOf(_segments.Count - 1))
        {
            CloseSegment(output);
            OpenSegment(output);
        }
        var w = new ByteWriter(output.Append);
        w.Tag("00db");
        w.U32((uint)_frameBytes);
        Segment last = _segments[^1];
        last.Frames++;
        _segments[^1] = last;
        _framesWritten++;
        _position += (ulong)output.Append.Count + _frameBytes;
        return output.ToResult();
    }

    /// <summary>색인을 닫고 헤더의 크기와 프레임 수를 고칩니다.</summary>
    public AviOutput Finish()
    {
        if (!IsValid || !_started || _finished)
        {
            return new AviOutput();
        }
        _finished = true;
        var output = new Output();
        CloseSegment(output);
        _position += (ulong)output.Append.Count;
        // 헤더 크기는 Start 때와 같으므로 RIFF 크기 자리만 남기고 덮어씁니다.
        byte[] header = BuildHeader(_framesWritten, _segments[0].Frames);
        output.Patches.Add(new FilePatch(8, header.AsSpan(8).ToArray()));
        return output.ToResult();
    }

    private sealed class Output
    {
        public List<byte> Append { get; } = new();

        public List<FilePatch> Patches { get; } = new();

        public AviOutput ToResult() => new(Append.ToArray(), Patches);
    }

    // AVI의 모든 수는 리틀 엔디언입니다. 호스트 바이트 순서와 관계없이 바이트 단위로 씁니다.
    private readonly struct ByteWriter
    {
        private readonly List<byte> _out;

        public ByteWriter(List<byte> output) => _out = output;

        public void U8(byte v) => _out.Add(v);

        public void U16(ushort v)
        {
            U8((byte)v);
            U8((byte)(v >> 8));
        }

        public void U32(uint v)
        {
            U16((ushort)v);
            U16((ushort)(v >> 16));
        }

        public void U64(ulong v)
        {
            U32((uint)v);
            U32((uint)(v >> 32));
        }

        public void Tag(string tag)
        {
            foreach (char c in tag)
            {
                _out.Add((byte)c);
            }
        }

        public void Zeros(int n)
        {
            for (int i = 0; i < n; i++)
            {
                _out.Add(0);
            }
        }
    }

    private static byte[] U32Bytes(uint v)
    {
        var bytes = new List<byte>();
        new ByteWriter(bytes).U32(v);
        return bytes.ToArray();
    }

    private static ulong PayloadBytes(VideoFormat f)
    {
        if (f.Width <= 0 || f.Height <= 0 || f.FrameRate <= 0)
        {
            return 0;
        }
        ulong pixels = (ulong)f.Width * (ulong)f.Height;
        switch (f.Format)
        {
            case PixelFormat.Uyvy:
                return pixels * 2;
            case PixelFormat.Nv12:
                // 색차가 2x2 화소마다 한 쌍이라 폭과 높이가 짝수여야 합니다.
                return (f.Width % 2 == 0 && f.Height % 2 == 0) ? pixels + pixels / 2 : 0;
            case PixelFormat.Y16:
                // 열화상 원시값은 AVI가 아니라 raw16 파일로 저장합니다.
                return 0;
            default:
                return 0;
        }
    }

    private static uint CompressionOf(PixelFormat format)
    {
        var tag = new List<byte>();
        var w = new ByteWriter(tag);
        w.Tag(format == PixelFormat.Nv12 ? "NV12" : "UYVY");
        return (uint)tag[0] | ((uint)tag[1] << 8) | ((uint)tag[2] << 16) | ((uint)tag[3] << 24);
    }

    private static ushort BitsPerPixel(PixelFormat format) => format == PixelFormat.Nv12 ? (ushort)12 : (ushort)16;

    private static ulong ChunkIndexBytes(long frames) => 8UL + ChunkIndexHeaderBytes + (ulong)ChunkIndexEntryBytes * (ulong)frames;

    private long CapacityOf(int segment)
    {
        ulong chunk = 8 + _frameBytes;
        ulong fixedBytes = 12 + 8 + ChunkIndexHeaderBytes;  // movi LIST, ix00 머리
        ulong perFrame = chunk + ChunkIndexEntryBytes;
        if (segment == 0)
        {
            fixedBytes += (ulong)_headerBytes + 8;  // 헤더(RIFF 머리 포함), idx1 머리
            perFrame += Idx1EntryBytes;
        }
        else
        {
            fixedBytes += 12;  // RIFF AVIX 머리
        }
        if (_riffLimitBytes <= fixedBytes)
        {
            return 0;
        }
        return (long)((_riffLimitBytes - fixedBytes) / perFrame);
    }

    private byte[] BuildHeader(long totalFrames, long firstRiffFrames)
    {
        int indxBytes = SuperIndexHeaderBytes + SuperIndexEntryBytes * _indexSlots;
        int strlBytes = 4 + (8 + StrhBytes) + (8 + StrfBytes) + (8 + indxBytes);
        int odmlBytes = 4 + 8 + DmlhBytes;
        int hdrlBytes = 4 + (8 + AvihBytes) + (8 + strlBytes) + (8 + odmlBytes);
        uint suggested = (uint)(_frameBytes + 8);
        uint usPerFrame = (uint)((1000000 + _format.FrameRate / 2) / _format.FrameRate);

        var output = new List<byte>();
        var w = new ByteWriter(output);
        w.Tag("RIFF");
        w.U32(0);  // Finish에서 채웁니다
        w.Tag("AVI ");
        w.Tag("LIST");
        w.U32((uint)hdrlBytes);
        w.Tag("hdrl");

        w.Tag("avih");
        w.U32(AvihBytes);
        w.U32(usPerFrame);
        w.U32((uint)Math.Min((ulong)suggested * (ulong)_format.FrameRate, uint.MaxValue));
        w.U32(0);
        w.U32(AvifHasIndex);
        w.U32((uint)firstRiffFrames);  // OpenDML 규격: 첫 RIFF의 프레임 수
        w.U32(0);
        w.U32(1);
        w.U32(suggested);
        w.U32((uint)_format.Width);
        w.U32((uint)_format.Height);
        w.Zeros(16);

        w.Tag("LIST");
        w.U32((uint)strlBytes);
        w.Tag("strl");
        w.Tag("strh");
        w.U32(StrhBytes);
        w.Tag("vids");
        w.U32(CompressionOf(_format.Format));
        w.U32(0);  // flags
        w.U16(0);  // priority
        w.U16(0);  // language
        w.U32(0);  // initial frames
        w.U32(1);  // scale
        w.U32((uint)_format.FrameRate);
        w.U32(0);  // start
        w.U32((uint)totalFrames);
        w.U32(suggested);
        w.U32(uint.MaxValue);  // quality: 기본값
        w.U32(0);              // sample size: 프레임마다 크기가 같아도 0을 씁니다
        w.U16(0);
        w.U16(0);
        w.U16((ushort)_format.Width);
        w.U16((ushort)_format.Height);

        w.Tag("strf");
        w.U32(StrfBytes);
        w.U32(StrfBytes);
        w.U32((uint)_format.Width);
        // YUV 형식은 높이가 양수여도 위쪽 행부터 저장합니다.
        w.U32((uint)_format.Height);
        w.U16(1);
        w.U16(BitsPerPixel(_format.Format));
        w.U32(CompressionOf(_format.Format));
        w.U32((uint)_frameBytes);
        w.Zeros(16);

        w.Tag("indx");
        w.U32((uint)indxBytes);
        w.U16(4);
        w.U8(0);
        w.U8(IndexOfIndexes);
        w.U32((uint)_indexEntries.Count);
        w.Tag("00db");
        w.Zeros(12);
        for (int i = 0; i < _indexSlots; i++)
        {
            if (i < _indexEntries.Count)
            {
                w.U64(_indexEntries[i].Offset);
                w.U32(_indexEntries[i].Bytes);
                w.U32(_indexEntries[i].Frames);
            }
            else
            {
                w.Zeros(SuperIndexEntryBytes);
            }
        }

        w.Tag("LIST");
        w.U32((uint)odmlBytes);
        w.Tag("odml");
        w.Tag("dmlh");
        w.U32(DmlhBytes);
        w.U32((uint)totalFrames);
        w.Zeros(DmlhBytes - 4);
        return output.ToArray();
    }

    private void OpenSegment(Output output)
    {
        var w = new ByteWriter(output.Append);
        var segment = new Segment { FirstFrame = _framesWritten };
        if (_segments.Count > 0)
        {
            segment.RiffOffset = _position + (ulong)output.Append.Count;
            w.Tag("RIFF");
            w.U32(0);
            w.Tag("AVIX");
        }
        segment.MoviOffset = _position + (ulong)output.Append.Count;
        w.Tag("LIST");
        w.U32(0);
        w.Tag("movi");
        _segments.Add(segment);
    }

    private void CloseSegment(Output output)
    {
        Segment segment = _segments[^1];
        ulong chunk = 8 + _frameBytes;
        ulong moviDataOffset = segment.MoviOffset + 8;  // 'movi' 태그 위치
        var w = new ByteWriter(output.Append);

        // 표준 색인(ix00)은 movi 안 마지막에 둡니다. 화소 위치는 기준 위치에서 센 바이트 수입니다.
        ulong ixOffset = _position + (ulong)output.Append.Count;
        ulong baseOffset = segment.MoviOffset;
        w.Tag("ix00");
        w.U32((uint)(ChunkIndexBytes(segment.Frames) - 8));
        w.U16(2);
        w.U8(0);
        w.U8(IndexOfChunks);
        w.U32((uint)segment.Frames);
        w.Tag("00db");
        w.U64(baseOffset);
        w.U32(0);
        for (long i = 0; i < segment.Frames; i++)
        {
            ulong data = moviDataOffset + 4 + (ulong)i * chunk + 8;
            w.U32((uint)(data - baseOffset));
            w.U32((uint)_frameBytes);  // 최상위 비트 0: 키 프레임
        }
        _indexEntries.Add(new IndexEntry(ixOffset, (uint)ChunkIndexBytes(segment.Frames), (uint)segment.Frames));

        ulong moviEnd = _position + (ulong)output.Append.Count;
        output.Patches.Add(new FilePatch(segment.MoviOffset + 4, U32Bytes((uint)(moviEnd - segment.MoviOffset - 8))));

        if (_segments.Count == 1)
        {
            // OpenDML을 모르는 도구를 위해 첫 RIFF에 기존 색인(idx1)도 둡니다. 위치는 'movi' 태그에서 센 값입니다.
            w.Tag("idx1");
            w.U32((uint)((ulong)Idx1EntryBytes * (ulong)segment.Frames));
            for (long i = 0; i < segment.Frames; i++)
            {
                w.Tag("00db");
                w.U32(AviifKeyframe);
                w.U32((uint)(4 + (ulong)i * chunk));
                w.U32((uint)_frameBytes);
            }
        }
        ulong riffEnd = _position + (ulong)output.Append.Count;
        output.Patches.Add(new FilePatch(segment.RiffOffset + 4, U32Bytes((uint)(riffEnd - segment.RiffOffset - 8))));
    }
}
