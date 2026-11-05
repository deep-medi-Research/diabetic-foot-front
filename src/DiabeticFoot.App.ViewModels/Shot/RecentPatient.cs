using System.Globalization;
using System.Text.RegularExpressions;

namespace DiabeticFoot.App.Shot;

/// <summary>저장 위치에 이미 촬영이 있는 환자 한 명입니다.</summary>
/// <param name="Name">환자 이름입니다.</param>
/// <param name="Birth">0000-00-00 형식의 생년월일입니다.</param>
/// <param name="Last">마지막 촬영 시각 문장("M/d HH:mm")입니다.</param>
public sealed record RecentPatient(string Name, string Birth, string Last)
{
    /// <summary>화면에 보이는 최근 환자 수입니다. 저장 위치에 촬영이 많아도 화면을 열 때 짧게 끝나게 합니다.</summary>
    public const int MaxCount = 8;

    /// <summary>이전 배치(날짜 폴더 아래 환자 폴더)에서 읽는 날짜 폴더 수입니다.</summary>
    public const int MaxDays = 60;

    private static readonly Regex DayPattern = new("^[0-9]{8}$");
    private static readonly Regex PatientPattern = new("^(.+)_([0-9]{8})$");

    /// <summary>
    /// 저장 위치의 환자 폴더(`이름_생년월일8자리`)와 이전 배치의 날짜 폴더(`yyyyMMdd/이름_생년월일`)에서 최근에 찍은 순서로 읽습니다.
    /// 이름에 '_'가 있어도 마지막 '_' 뒤 여덟 자리를 생년월일로 봅니다. 따로 저장하는 정보가 없습니다.
    /// </summary>
    /// <remarks>
    /// 환자 폴더의 수정 시각은 촬영 폴더를 만들 때 맞춥니다. 환자 폴더를 열지 않아 환자가 많아도 짧게 끝납니다.
    /// </remarks>
    public static IReadOnlyList<RecentPatient> Scan(string root)
    {
        var seen = new List<(string Name, string Birth, DateTime Last)>();

        void Add(Match match, DateTime last)
        {
            var name = match.Groups[1].Value;
            var digits = match.Groups[2].Value;
            var birth = $"{digits[..4]}-{digits.Substring(4, 2)}-{digits.Substring(6, 2)}";
            var index = seen.FindIndex(x => x.Name == name && x.Birth == birth);
            if (index < 0) seen.Add((name, birth, last));
            else if (last > seen[index].Last) seen[index] = (name, birth, last);
        }

        foreach (var patient in Folders(root))
        {
            var match = PatientPattern.Match(patient.Name);
            if (match.Success) Add(match, patient.LastWriteTime);
        }
        var days = Folders(root).Select(f => f.Name).Where(n => DayPattern.IsMatch(n)).OrderByDescending(n => n, StringComparer.Ordinal).Take(MaxDays);
        foreach (var day in days)
        {
            foreach (var patient in Folders(Path.Combine(root, day)))
            {
                var match = PatientPattern.Match(patient.Name);
                if (match.Success) Add(match, patient.LastWriteTime);
            }
        }
        return seen
            .OrderByDescending(x => x.Last)
            .Take(MaxCount)
            .Select(x => new RecentPatient(x.Name, x.Birth, x.Last.ToString("M/d HH:mm", CultureInfo.InvariantCulture)))
            .ToList();
    }

    private static IEnumerable<DirectoryInfo> Folders(string path)
    {
        try
        {
            return new DirectoryInfo(path).EnumerateDirectories().Where(d => !d.Attributes.HasFlag(FileAttributes.Hidden)).ToList();
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return [];
        }
    }
}
