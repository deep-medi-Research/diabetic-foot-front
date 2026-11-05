using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;

namespace DiabeticFoot.App.Camera;

/// <summary>카메라에서 다시 읽은 값 한 줄입니다. 목표와 다르면 <paramref name="Mismatch"/>가 true이고 목표 값을 함께 보입니다.</summary>
public sealed record ReadbackRow(string Name, string Value, bool Mismatch = false, string Target = "");

/// <summary>
/// 카메라에서 다시 읽은 설정값 목록입니다. 화면의 '다시 읽은 값' 표가 씁니다.
/// <see cref="Update"/>는 행 수가 같으면 바뀐 행만 화면에 알려, 0.2초마다 갱신해도 나머지 행이 다시 그려지지 않습니다.
/// </summary>
public sealed class ReadbackModel : ObservableCollection<ReadbackRow>
{
    /// <summary>행을 바꿉니다. 행 수가 같으면 바뀐 행만 알리고, 다르면 목록 전체를 다시 알립니다.</summary>
    public void Update(IReadOnlyList<ReadbackRow> rows)
    {
        if (rows.Count != Count)
        {
            CheckReentrancy();
            Items.Clear();
            foreach (var row in rows) Items.Add(row);
            OnPropertyChanged(new PropertyChangedEventArgs(nameof(Count)));
            OnPropertyChanged(new PropertyChangedEventArgs("Item[]"));
            OnCollectionChanged(new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Reset));
            return;
        }
        for (var i = 0; i < rows.Count; i++)
        {
            if (rows[i] != this[i]) this[i] = rows[i];
        }
    }
}
