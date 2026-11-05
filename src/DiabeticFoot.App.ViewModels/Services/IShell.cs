namespace DiabeticFoot.App.Services;

/// <summary>운영체제 셸에 맡기는 일입니다.</summary>
public interface IShell
{
    /// <summary>폴더를 파일 탐색기로 엽니다.</summary>
    void OpenFolder(string path);
}
