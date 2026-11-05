namespace DiabeticFoot.App.Services;

/// <summary>사용자에게 폴더를 고르게 합니다. 화면 창을 여는 일이라 ViewModel은 이 약속으로만 부릅니다.</summary>
public interface IFolderPicker
{
    /// <param name="title">선택 창 제목</param>
    /// <param name="initialFolder">처음 보여 줄 폴더</param>
    /// <returns>고른 폴더. 취소하면 null입니다</returns>
    string? Pick(string title, string initialFolder);
}
