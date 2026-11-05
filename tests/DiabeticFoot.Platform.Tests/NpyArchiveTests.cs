using System.Text;
using DiabeticFoot.Platform;

namespace DiabeticFoot.Platform.Tests;

public class NpyArchiveTests
{
    private static void PutLe(List<byte> output, ulong value, int width)
    {
        for (var i = 0; i < width; i++) output.Add((byte)((value >> (8 * i)) & 0xFF));
    }

    private static byte[] Ascii(string text) => Encoding.Latin1.GetBytes(text);

    private static T Ok<T>(T? value, string error) where T : class
    {
        Assert.True(value is not null, error);
        return value!;
    }

    // np.save와 같은 배치로 머리를 64바이트 경계까지 공백으로 채우고 줄바꿈으로 끝냅니다.
    private static byte[] Npy(string descr, string shape, byte[] data, int major = 1, bool fortran = false)
    {
        var header = "{'descr': '" + descr + "', 'fortran_order': " + (fortran ? "True" : "False") + ", 'shape': " + shape + ", }";
        var prefix = major == 1 ? 10 : 12;
        while ((prefix + header.Length + 1) % 64 != 0) header += ' ';
        header += '\n';
        var output = new List<byte>(Ascii("\u0093NUMPY")) { (byte)major, 0 };
        PutLe(output, (ulong)header.Length, major == 1 ? 2 : 4);
        output.AddRange(Ascii(header));
        output.AddRange(data);
        return output.ToArray();
    }

    private static byte[] F8(params double[] values)
    {
        var output = new List<byte>();
        foreach (var v in values) PutLe(output, (ulong)BitConverter.DoubleToInt64Bits(v), 8);
        return output.ToArray();
    }

    private static uint Crc32(byte[] bytes)
    {
        var crc = 0xFFFFFFFFu;
        foreach (var b in bytes)
        {
            crc ^= b;
            for (var k = 0; k < 8; k++) crc = (crc & 1) != 0 ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
        }
        return crc ^ 0xFFFFFFFFu;
    }

    private sealed record ZipItem(string Name, byte[] Data)
    {
        public ushort Method { get; init; }

        // 지역 머리의 CRC와 크기를 0으로 두고 데이터 뒤에 데이터 기술자를 붙입니다(스트림으로 쓴 ZIP).
        public bool Descriptor { get; init; }

        // 중앙 디렉터리의 크기와 위치를 0xFFFFFFFF로 두고 zip64 추가 필드에 적습니다.
        public bool Zip64 { get; init; }
    }

    private static byte[] Zip(IEnumerable<ZipItem> items, bool zip64End = false)
    {
        var all = items.ToList();
        var output = new List<byte>();
        var central = new List<byte>();
        foreach (var item in all)
        {
            var offset = (ulong)output.Count;
            var crc = Crc32(item.Data);
            ushort flags = item.Descriptor ? (ushort)0x0008 : (ushort)0;
            PutLe(output, 0x04034b50, 4);
            PutLe(output, 20, 2);
            PutLe(output, flags, 2);
            PutLe(output, item.Method, 2);
            PutLe(output, 0, 4);
            PutLe(output, item.Descriptor ? 0 : crc, 4);
            PutLe(output, item.Descriptor ? 0 : (ulong)item.Data.Length, 4);
            PutLe(output, item.Descriptor ? 0 : (ulong)item.Data.Length, 4);
            PutLe(output, (ulong)item.Name.Length, 2);
            PutLe(output, 0, 2);
            output.AddRange(Ascii(item.Name));
            output.AddRange(item.Data);
            if (item.Descriptor)
            {
                PutLe(output, 0x08074b50, 4);
                PutLe(output, crc, 4);
                PutLe(output, (ulong)item.Data.Length, 4);
                PutLe(output, (ulong)item.Data.Length, 4);
            }
            var extra = new List<byte>();
            if (item.Zip64)
            {
                PutLe(extra, 0x0001, 2);
                PutLe(extra, 24, 2);
                PutLe(extra, (ulong)item.Data.Length, 8);
                PutLe(extra, (ulong)item.Data.Length, 8);
                PutLe(extra, offset, 8);
            }
            const ulong small = 0xFFFFFFFFu;
            PutLe(central, 0x02014b50, 4);
            PutLe(central, 45, 2);
            PutLe(central, 45, 2);
            PutLe(central, flags, 2);
            PutLe(central, item.Method, 2);
            PutLe(central, 0, 4);
            PutLe(central, crc, 4);
            PutLe(central, item.Zip64 ? small : (ulong)item.Data.Length, 4);
            PutLe(central, item.Zip64 ? small : (ulong)item.Data.Length, 4);
            PutLe(central, (ulong)item.Name.Length, 2);
            PutLe(central, (ulong)extra.Count, 2);
            PutLe(central, 0, 2);
            PutLe(central, 0, 2);
            PutLe(central, 0, 2);
            PutLe(central, 0, 4);
            PutLe(central, item.Zip64 ? small : offset, 4);
            central.AddRange(Ascii(item.Name));
            central.AddRange(extra);
        }
        var dirOffset = (ulong)output.Count;
        output.AddRange(central);
        if (zip64End)
        {
            var record = (ulong)output.Count;
            PutLe(output, 0x06064b50, 4);
            PutLe(output, 44, 8);
            PutLe(output, 45, 2);
            PutLe(output, 45, 2);
            PutLe(output, 0, 4);
            PutLe(output, 0, 4);
            PutLe(output, (ulong)all.Count, 8);
            PutLe(output, (ulong)all.Count, 8);
            PutLe(output, (ulong)central.Count, 8);
            PutLe(output, dirOffset, 8);
            PutLe(output, 0x07064b50, 4);
            PutLe(output, 0, 4);
            PutLe(output, record, 8);
            PutLe(output, 1, 4);
        }
        PutLe(output, 0x06054b50, 4);
        PutLe(output, 0, 2);
        PutLe(output, 0, 2);
        PutLe(output, zip64End ? 0xFFFF : (ulong)all.Count, 2);
        PutLe(output, zip64End ? 0xFFFF : (ulong)all.Count, 2);
        PutLe(output, zip64End ? 0xFFFFFFFFu : (ulong)central.Count, 4);
        PutLe(output, zip64End ? 0xFFFFFFFFu : dirOffset, 4);
        PutLe(output, 0, 2);
        return output.ToArray();
    }

    private static byte[] FloatBytes(float value)
    {
        var output = new List<byte>();
        PutLe(output, (uint)BitConverter.SingleToInt32Bits(value), 4);
        return output.ToArray();
    }

    [Fact]
    public void ReadsFloat64Vector()
    {
        var array = Ok(NpyArchive.ReadNpy(Npy("<f8", "(3,)", F8(1.5, -2.0, 7.25)), out _, out var error), error);
        Assert.Equal(new long[] { 3 }, array.Shape);
        Assert.Equal(new[] { 1.5, -2.0, 7.25 }, array.Values);
    }

    [Fact]
    public void ReadsZeroDimensionalScalar()
    {
        var array = NpyArchive.ReadNpy(Npy("<f8", "()", F8(30.0)));
        Assert.NotNull(array);
        Assert.Empty(array.Shape);
        Assert.Equal(new[] { 30.0 }, array.Values);
    }

    [Fact]
    public void ReadsMultiDimensionalInCOrder()
    {
        var array = NpyArchive.ReadNpy(Npy("<f8", "(2, 3)", F8(0, 1, 2, 10, 11, 12)));
        Assert.NotNull(array);
        Assert.Equal(new long[] { 2, 3 }, array.Shape);
        Assert.Equal(11.0, array.Values[4]);
    }

    [Fact]
    public void ConvertsIntegersAndFloat32()
    {
        var i8 = new List<byte>();
        PutLe(i8, unchecked((ulong)-3L), 8);
        PutLe(i8, 6, 8);
        var array = NpyArchive.ReadNpy(Npy("<i8", "(2,)", i8.ToArray()));
        Assert.NotNull(array);
        Assert.Equal(new[] { -3.0, 6.0 }, array.Values);

        var i4 = new List<byte>();
        PutLe(i4, unchecked((uint)-7), 4);
        array = NpyArchive.ReadNpy(Npy("<i4", "(1,)", i4.ToArray()));
        Assert.NotNull(array);
        Assert.Equal(new[] { -7.0 }, array.Values);

        array = NpyArchive.ReadNpy(Npy("<f4", "(1,)", FloatBytes(0.5f)));
        Assert.NotNull(array);
        Assert.Equal(new[] { 0.5 }, array.Values);
    }

    [Theory]
    [InlineData(2)]
    [InlineData(3)]
    public void ReadsHeaderVersion2And3(int major)
    {
        var array = NpyArchive.ReadNpy(Npy("<f8", "(2,)", F8(4.0, 5.0), major));
        Assert.NotNull(array);
        Assert.Equal(new[] { 4.0, 5.0 }, array.Values);
    }

    [Fact]
    public void SkipsUnicodeAndFortranArrays()
    {
        Assert.Null(NpyArchive.ReadNpy(Npy("<U4", "()", new byte[16]), out var skip, out var error));
        Assert.Contains("<U4", skip);
        Assert.Empty(error);

        Assert.Null(NpyArchive.ReadNpy(Npy("<f8", "(2, 2)", F8(1, 2, 3, 4), 1, true), out skip, out error));
        Assert.NotEmpty(skip);
        Assert.Empty(error);
    }

    [Fact]
    public void RejectsBadBytes()
    {
        Assert.Null(NpyArchive.ReadNpy(Ascii("not numpy"), out var skip, out var error));
        Assert.NotEmpty(error);
        // shape보다 데이터가 짧습니다.
        Assert.Null(NpyArchive.ReadNpy(Npy("<f8", "(4,)", F8(1, 2)), out skip, out error));
        Assert.NotEmpty(error);
        // 곱이 넘치는 shape도 길이 검사에서 거부합니다.
        Assert.Null(NpyArchive.ReadNpy(Npy("<f8", "(4294967296, 4294967296, 16)", F8(1)), out skip, out error));
        Assert.NotEmpty(error);
        Assert.Empty(skip);
    }

    [Fact]
    public void ReadsStoredEntriesAndListsSkipped()
    {
        var zip = Zip(new[]
        {
            new ZipItem("time_s.npy", Npy("<f8", "(2,)", F8(5.0, 7.0))),
            new ZipItem("sides.npy", Npy("<U5", "(2,)", new byte[40])),
            new ZipItem("notes.txt", Ascii("hello")),
        });
        var contents = Ok(NpyArchive.ReadNpzBytes(zip, out var error), error);
        Assert.Equal(new[] { 5.0, 7.0 }, contents.Arrays["time_s"].Values);
        Assert.True(contents.Skipped.ContainsKey("sides"));
        Assert.True(contents.Skipped.ContainsKey("notes.txt"));
    }

    [Fact]
    public void ReadsDataDescriptorAndZip64()
    {
        var item = new ZipItem("a.npy", Npy("<f8", "(1,)", F8(3.0))) { Descriptor = true, Zip64 = true };
        var plain = new ZipItem("b.npy", Npy("<f8", "()", F8(9.0)));
        var contents = Ok(NpyArchive.ReadNpzBytes(Zip(new[] { item, plain }, true), out var error), error);
        Assert.Equal(new[] { 3.0 }, contents.Arrays["a"].Values);
        Assert.Equal(new[] { 9.0 }, contents.Arrays["b"].Values);
    }

    [Fact]
    public void RejectsDeflatedEntry()
    {
        var item = new ZipItem("a.npy", Npy("<f8", "(1,)", F8(3.0))) { Method = 8 };
        Assert.Null(NpyArchive.ReadNpzBytes(Zip(new[] { item }), out var error));
        Assert.Contains("압축", error);
    }

    [Fact]
    public void RejectsCorruptArchive()
    {
        var zip = Zip(new[] { new ZipItem("a.npy", Npy("<f8", "(1,)", F8(3.0))) });
        Assert.Null(NpyArchive.ReadNpzBytes(zip.AsSpan(0, zip.Length / 2), out var error));
        Assert.NotEmpty(error);

        // 데이터 한 바이트를 바꾸면 CRC가 맞지 않습니다.
        var magicAt = zip.AsSpan().IndexOf(Ascii("\u0093NUMPY").AsSpan());
        zip[magicAt + 130] ^= 1;
        Assert.Null(NpyArchive.ReadNpzBytes(zip, out error));
        Assert.Contains("CRC", error);
        Assert.Null(NpyArchive.ReadNpzBytes(ReadOnlySpan<byte>.Empty, out error));
    }

    [Fact]
    public void ReadsAnalyzerOutputFile()
    {
        var path = Path.Combine(AppContext.BaseDirectory, "Data", "154007_perfusion.npz");
        var contents = Ok(NpyArchive.ReadNpz(path, out var error), error);
        Assert.Equal(25, contents.Arrays.Count);
        Assert.Equal(new long[] { 2, 11, 32, 16 }, contents.Arrays["maps"].Shape);
        Assert.Equal(Enumerable.Repeat(6.0, 11), contents.Arrays["n_light_ref"].Values);
        Assert.Equal(new[] { 30.0 }, contents.Arrays["fps"].Values);
        Assert.Equal(2, contents.Skipped.Count);
        Assert.True(contents.Skipped.ContainsKey("sides"));
        Assert.True(contents.Skipped.ContainsKey("cuff_side"));
        Assert.Null(NpyArchive.ReadNpz(Path.Combine(AppContext.BaseDirectory, "Data", "없는_파일.npz"), out error));
        Assert.NotEmpty(error);
    }
}
