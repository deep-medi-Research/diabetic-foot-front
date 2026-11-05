using System.Buffers.Binary;
using System.Text;

namespace DiabeticFoot.Platform;

/// <summary>
/// numpy 배열 하나의 모양과 값입니다. 값은 C 순서(마지막 축이 가장 빨리 변함)로 펼쳐 double로 바꿔 둡니다.
/// 0차원 배열은 Shape가 비었고 값이 1개입니다.
/// </summary>
public sealed record NpyArray(long[] Shape, double[] Values);

/// <summary>ReadNpz의 결과입니다. 항목 이름은 `.npy`를 뗀 이름(np.savez의 키)입니다.</summary>
public sealed class NpzContents
{
    /// <summary>읽은 배열입니다. 키는 항목 이름입니다.</summary>
    public Dictionary<string, NpyArray> Arrays { get; } = new(StringComparer.Ordinal);

    /// <summary>
    /// 읽지 않고 건너뛴 항목입니다. 값은 건너뛴 이유입니다(예: 지원하지 않는 dtype `&lt;U5`).
    /// 문자열 배열(sides, cuff_side)과 Fortran 순서 다차원 배열, `.npy`가 아닌 항목이 들어갑니다.
    /// </summary>
    public Dictionary<string, string> Skipped { get; } = new(StringComparer.Ordinal);
}

/// <summary>
/// 관류 분석 프로그램이 쓰는 `.npz`(무압축 ZIP 안의 `.npy`)를 읽습니다.
/// ZipArchive는 항목의 압축 방식을 알려 주지 않고 CRC를 확인하지 않아, 압축 항목을 거부하고 CRC를 확인하는 원래 동작을 위해 중앙 디렉터리를 직접 읽습니다.
/// </summary>
public static class NpyArchive
{
    private const uint LocalHeader = 0x04034b50;
    private const uint CentralHeader = 0x02014b50;
    private const uint EndOfCentral = 0x06054b50;
    private const uint Zip64Locator = 0x07064b50;
    private const uint Zip64EndOfCentral = 0x06064b50;
    private const ushort Zip64ExtraId = 0x0001;
    private const uint Full32 = 0xFFFFFFFFu;

    private static ReadOnlySpan<byte> NpyMagic => [0x93, (byte)'N', (byte)'U', (byte)'M', (byte)'P', (byte)'Y'];

    /// <summary>
    /// `.npy` 바이트 하나를 읽습니다. 머리 버전 1.0, 2.0, 3.0을 읽고, dtype은 리틀 엔디언 `&lt;f8`, `&lt;f4`, `&lt;i8`, `&lt;i4`만 값으로 바꿉니다.
    /// 형식은 맞지만 지원하지 않는 dtype이나 순서이면 skipReason에 이유를 쓰고, 형식이 틀리면 error에 이유를 쓰며 null을 돌려줍니다.
    /// </summary>
    public static NpyArray? ReadNpy(ReadOnlySpan<byte> bytes, out string skipReason, out string error)
    {
        skipReason = "";
        error = "";
        if (bytes.Length < 10 || !bytes[..6].SequenceEqual(NpyMagic))
        {
            error = ".npy 시작 바이트가 아닙니다";
            return null;
        }
        var major = bytes[6];
        if (major is < 1 or > 3)
        {
            error = $".npy 머리 버전 {major}은 읽지 않습니다";
            return null;
        }
        var lengthWidth = major == 1 ? 2 : 4;
        if (bytes.Length < 8 + lengthWidth)
        {
            error = ".npy 머리 길이가 잘렸습니다";
            return null;
        }
        var headerLength = LittleEndian(bytes, 8, lengthWidth);
        var dataStart = 8UL + (ulong)lengthWidth + headerLength;
        if (!Fits(bytes, 8UL + (ulong)lengthWidth, headerLength))
        {
            error = ".npy 머리가 잘렸습니다";
            return null;
        }
        var header = Encoding.Latin1.GetString(bytes.Slice(8 + lengthWidth, (int)headerLength));

        var descr = QuotedValue(header, "descr");
        if (descr is null)
        {
            // 구조체 dtype은 descr가 문자열이 아니라 목록입니다.
            if (ValueStart(header, "descr") >= 0)
            {
                skipReason = "구조체 dtype";
                return null;
            }
            error = ".npy 머리에 descr가 없습니다";
            return null;
        }
        var fortranAt = ValueStart(header, "fortran_order");
        if (fortranAt < 0)
        {
            error = ".npy 머리에 fortran_order가 없습니다";
            return null;
        }
        var fortran = string.CompareOrdinal(header, fortranAt, "True", 0, 4) == 0;
        if (!fortran && string.CompareOrdinal(header, fortranAt, "False", 0, 5) != 0)
        {
            error = ".npy fortran_order 값이 틀립니다";
            return null;
        }
        var shape = ShapeValue(header);
        if (shape is null)
        {
            error = ".npy 머리의 shape를 읽지 못했습니다";
            return null;
        }

        if (descr is not ("<f8" or "<f4" or "<i8" or "<i4"))
        {
            skipReason = "지원하지 않는 dtype " + descr;
            return null;
        }
        var isFloat = descr[1] == 'f';
        var width = descr[2] - '0';
        if (fortran && shape.Count > 1)
        {
            skipReason = "Fortran 순서 다차원 배열";
            return null;
        }

        var data = bytes[(int)dataStart..];
        ulong count = 1;
        foreach (var n in shape)
        {
            // 곱이 데이터 바이트 수를 넘으면 그 자리에서 멈춰 넘침을 막습니다.
            if (n != 0 && count > (ulong)data.Length / (ulong)n)
            {
                error = ".npy 데이터가 shape보다 짧습니다";
                return null;
            }
            count *= (ulong)n;
        }
        if (count * (ulong)width != (ulong)data.Length)
        {
            error = ".npy 데이터 길이가 shape와 다릅니다";
            return null;
        }
        var values = new double[count];
        for (var i = 0; i < values.Length; i++) values[i] = ElementAt(data, i * width, isFloat, width);
        return new NpyArray(shape.ToArray(), values);
    }

    public static NpyArray? ReadNpy(ReadOnlySpan<byte> bytes) => ReadNpy(bytes, out _, out _);

    /// <summary>
    /// 메모리에 있는 `.npz`(ZIP) 바이트를 읽습니다. 중앙 디렉터리에서 항목의 크기와 위치를 읽으므로 데이터 기술자가 붙은 항목도 읽고,
    /// 크기나 위치가 32비트를 넘으면 zip64 레코드와 추가 필드를 읽습니다. 항목마다 CRC32를 확인합니다.
    /// ZIP 구조가 틀리거나, 압축(deflate)되었거나 암호화된 항목이 있거나, CRC가 다르거나, `.npy` 형식이 틀린 항목이 있으면 null입니다.
    /// np.savez는 무압축(stored)으로 쓰고, np.savez_compressed가 쓴 파일은 오류로 돌려줍니다.
    /// </summary>
    public static NpzContents? ReadNpzBytes(ReadOnlySpan<byte> bytes, out string error)
    {
        var entries = CentralDirectory(bytes, out error);
        if (entries is null) return null;
        var contents = new NpzContents();
        foreach (var e in entries)
        {
            if ((e.Flags & 1) != 0)
            {
                error = "암호화된 항목은 읽지 않습니다: " + e.Name;
                return null;
            }
            if (e.Method != 0)
            {
                error = $"압축된 항목은 읽지 않습니다(방식 {e.Method}): {e.Name}";
                return null;
            }
            if (!Fits(bytes, e.LocalOffset, 30) || LittleEndian(bytes, e.LocalOffset, 4) != LocalHeader)
            {
                error = "항목 머리가 틀립니다: " + e.Name;
                return null;
            }
            // 지역 머리의 크기 칸은 데이터 기술자나 zip64에서 비어 있을 수 있어 중앙 디렉터리 값을 씁니다.
            var start = e.LocalOffset + 30 + LittleEndian(bytes, e.LocalOffset + 26, 2) + LittleEndian(bytes, e.LocalOffset + 28, 2);
            if (!Fits(bytes, start, e.Size))
            {
                error = "항목 데이터가 파일 밖으로 나갑니다: " + e.Name;
                return null;
            }
            var data = bytes.Slice((int)start, (int)e.Size);
            if (Crc32(data) != e.Crc)
            {
                error = "항목 CRC가 다릅니다: " + e.Name;
                return null;
            }

            const string suffix = ".npy";
            if (e.Name.Length <= suffix.Length || !e.Name.EndsWith(suffix, StringComparison.Ordinal))
            {
                contents.Skipped[e.Name] = ".npy 항목이 아님";
                continue;
            }
            var key = e.Name[..^suffix.Length];
            var array = ReadNpy(data, out var skipReason, out var npyError);
            if (array is not null)
            {
                contents.Arrays[key] = array;
            }
            else if (skipReason.Length > 0)
            {
                contents.Skipped[key] = skipReason;
            }
            else
            {
                error = e.Name + ": " + npyError;
                return null;
            }
        }
        return contents;
    }

    public static NpzContents? ReadNpzBytes(ReadOnlySpan<byte> bytes) => ReadNpzBytes(bytes, out _);

    /// <summary>
    /// `.npz` 파일을 읽습니다. 파일 전체를 메모리로 읽으므로 화면 스레드에서 부르지 않습니다.
    /// 파일을 열지 못하면 null입니다.
    /// </summary>
    public static NpzContents? ReadNpz(string path, out string error)
    {
        byte[] bytes;
        try
        {
            bytes = File.ReadAllBytes(path);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            error = "파일을 열지 못했습니다: " + path;
            return null;
        }
        return ReadNpzBytes(bytes, out error);
    }

    public static NpzContents? ReadNpz(string path) => ReadNpz(path, out _);

    private sealed record Entry(string Name, ushort Flags, ushort Method, uint Crc, ulong Size, ulong LocalOffset);

    private static ulong LittleEndian(ReadOnlySpan<byte> bytes, ulong at, int width)
    {
        ulong value = 0;
        for (var i = 0; i < width; i++) value |= (ulong)bytes[(int)at + i] << (8 * i);
        return value;
    }

    private static bool Fits(ReadOnlySpan<byte> bytes, ulong at, ulong length) =>
        at <= (ulong)bytes.Length && length <= (ulong)bytes.Length - at;

    private static readonly uint[] CrcTable = BuildCrcTable();

    private static uint[] BuildCrcTable()
    {
        var table = new uint[256];
        for (uint n = 0; n < 256; n++)
        {
            var c = n;
            for (var k = 0; k < 8; k++) c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        return table;
    }

    private static uint Crc32(ReadOnlySpan<byte> bytes)
    {
        var crc = Full32;
        foreach (var b in bytes) crc = CrcTable[(crc ^ b) & 0xFF] ^ (crc >> 8);
        return crc ^ Full32;
    }

    // numpy 머리는 파이썬 dict 문자 표기입니다. 이 형식에서 키 하나의 값 시작 위치만 찾습니다.
    private static int ValueStart(string header, string key)
    {
        foreach (var quote in new[] { '\'', '"' })
        {
            var quoted = quote + key + quote;
            var at = header.IndexOf(quoted, StringComparison.Ordinal);
            if (at < 0) continue;
            at = header.IndexOf(':', at + quoted.Length);
            if (at < 0) return -1;
            for (var i = at + 1; i < header.Length; i++)
            {
                if (header[i] is not (' ' or '\t')) return i;
            }
            return -1;
        }
        return -1;
    }

    private static string? QuotedValue(string header, string key)
    {
        var at = ValueStart(header, key);
        if (at < 0 || header[at] is not ('\'' or '"')) return null;
        var end = header.IndexOf(header[at], at + 1);
        return end < 0 ? null : header.Substring(at + 1, end - at - 1);
    }

    private static List<long>? ShapeValue(string header)
    {
        var at = ValueStart(header, "shape");
        if (at < 0 || header[at] != '(') return null;
        var end = header.IndexOf(')', at);
        if (end < 0) return null;
        var shape = new List<long>();
        long value = 0;
        var digits = false;
        for (var i = at + 1; i <= end; i++)
        {
            var c = header[i];
            if (c is >= '0' and <= '9')
            {
                if (value > (long.MaxValue - 9) / 10) return null;
                value = value * 10 + (c - '0');
                digits = true;
            }
            else if (c is ',' or ')')
            {
                if (digits) shape.Add(value);
                else if (c == ',') return null;
                value = 0;
                digits = false;
            }
            else if (c is not (' ' or 'L'))
            {
                return null;
            }
        }
        return shape;
    }

    private static double ElementAt(ReadOnlySpan<byte> data, int offset, bool isFloat, int width)
    {
        var item = data.Slice(offset, width);
        if (isFloat) return width == 8 ? BinaryPrimitives.ReadDoubleLittleEndian(item) : BinaryPrimitives.ReadSingleLittleEndian(item);
        return width == 8 ? BinaryPrimitives.ReadInt64LittleEndian(item) : BinaryPrimitives.ReadInt32LittleEndian(item);
    }

    private static List<Entry>? CentralDirectory(ReadOnlySpan<byte> bytes, out string error)
    {
        error = "";
        // 끝 레코드 뒤에 주석이 최대 65535바이트까지 붙을 수 있어 뒤에서부터 찾습니다.
        if (bytes.Length < 22)
        {
            error = "ZIP 끝 레코드가 없습니다";
            return null;
        }
        var end = -1;
        var lowest = bytes.Length > 22 + 0xFFFF ? bytes.Length - 22 - 0xFFFF : 0;
        for (var at = bytes.Length - 22; at >= lowest; at--)
        {
            if (LittleEndian(bytes, (ulong)at, 4) == EndOfCentral)
            {
                end = at;
                break;
            }
        }
        if (end < 0)
        {
            error = "ZIP 끝 레코드가 없습니다";
            return null;
        }
        var count = LittleEndian(bytes, (ulong)end + 10, 2);
        var dirSize = LittleEndian(bytes, (ulong)end + 12, 4);
        var dirOffset = LittleEndian(bytes, (ulong)end + 16, 4);
        if (count == 0xFFFF || dirSize == Full32 || dirOffset == Full32)
        {
            if (end < 20 || LittleEndian(bytes, (ulong)end - 20, 4) != Zip64Locator)
            {
                error = "zip64 위치 레코드가 없습니다";
                return null;
            }
            var record = LittleEndian(bytes, (ulong)end - 20 + 8, 8);
            if (!Fits(bytes, record, 56) || LittleEndian(bytes, record, 4) != Zip64EndOfCentral)
            {
                error = "zip64 끝 레코드가 없습니다";
                return null;
            }
            count = LittleEndian(bytes, record + 32, 8);
            dirSize = LittleEndian(bytes, record + 40, 8);
            dirOffset = LittleEndian(bytes, record + 48, 8);
        }
        if (!Fits(bytes, dirOffset, dirSize))
        {
            error = "중앙 디렉터리가 파일 밖을 가리킵니다";
            return null;
        }
        var entries = new List<Entry>();
        var at2 = dirOffset;
        for (ulong i = 0; i < count; i++)
        {
            if (!Fits(bytes, at2, 46) || LittleEndian(bytes, at2, 4) != CentralHeader)
            {
                error = "중앙 디렉터리 항목 머리가 틀립니다";
                return null;
            }
            var flags = (ushort)LittleEndian(bytes, at2 + 8, 2);
            var method = (ushort)LittleEndian(bytes, at2 + 10, 2);
            var crc = (uint)LittleEndian(bytes, at2 + 16, 4);
            var packed = LittleEndian(bytes, at2 + 20, 4);
            var unpacked = LittleEndian(bytes, at2 + 24, 4);
            var nameLength = LittleEndian(bytes, at2 + 28, 2);
            var extraLength = LittleEndian(bytes, at2 + 30, 2);
            var commentLength = LittleEndian(bytes, at2 + 32, 2);
            var localOffset = LittleEndian(bytes, at2 + 42, 4);
            if (!Fits(bytes, at2 + 46, nameLength + extraLength + commentLength))
            {
                error = "중앙 디렉터리 항목이 파일 밖으로 나갑니다";
                return null;
            }
            var name = Encoding.UTF8.GetString(bytes.Slice((int)at2 + 46, (int)nameLength));
            // zip64 추가 필드에는 32비트 칸이 0xFFFFFFFF인 값만 이 순서로 들어 있습니다.
            var extraEnd = at2 + 46 + nameLength + extraLength;
            for (var x = at2 + 46 + nameLength; x + 4 <= extraEnd;)
            {
                var id = LittleEndian(bytes, x, 2);
                var length = LittleEndian(bytes, x + 2, 2);
                if (x + 4 + length > extraEnd) break;
                if (id == Zip64ExtraId)
                {
                    var field = x + 4;
                    for (var slot = 0; slot < 3; slot++)
                    {
                        var current = slot == 0 ? unpacked : slot == 1 ? packed : localOffset;
                        if (current != Full32) continue;
                        if (field + 8 > x + 4 + length)
                        {
                            error = "zip64 추가 필드가 짧습니다: " + name;
                            return null;
                        }
                        var wide = LittleEndian(bytes, field, 8);
                        if (slot == 0) unpacked = wide;
                        else if (slot == 1) packed = wide;
                        else localOffset = wide;
                        field += 8;
                    }
                }
                x += 4 + length;
            }
            if (method == 0 && packed != unpacked)
            {
                error = "무압축 항목의 크기가 서로 다릅니다: " + name;
                return null;
            }
            entries.Add(new Entry(name, flags, method, crc, packed, localOffset));
            at2 += 46 + nameLength + extraLength + commentLength;
        }
        return entries;
    }
}
