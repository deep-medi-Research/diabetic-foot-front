using System.Collections.Specialized;
using DiabeticFoot.App.Camera;

namespace DiabeticFoot.App.Tests.Camera;

public sealed class ReadbackModelTests
{
    private static ReadbackRow[] Rows(string exposure = "-9") =>
    [
        new("노출 단계", exposure),
        new("게인", "4"),
        new("색온도", "4600 K"),
    ];

    // 행 수가 다르면 목록 전체를 다시 알립니다.
    [Fact]
    public void ResetsWhenRowCountChanges()
    {
        var model = new ReadbackModel();
        var events = new List<NotifyCollectionChangedAction>();
        model.CollectionChanged += (_, e) => events.Add(e.Action);

        model.Update(Rows());
        Assert.Equal(3, model.Count);
        model.Update(Rows()[..2]);
        Assert.Equal(2, model.Count);

        Assert.Equal([NotifyCollectionChangedAction.Reset, NotifyCollectionChangedAction.Reset], events);
    }

    // 행 수가 같으면 바뀐 행만 알리고, 같은 값은 알리지 않습니다.
    [Fact]
    public void ReplacesOnlyChangedRows()
    {
        var model = new ReadbackModel();
        model.Update(Rows());
        var events = new List<NotifyCollectionChangedEventArgs>();
        model.CollectionChanged += (_, e) => events.Add(e);

        model.Update(Rows());
        Assert.Empty(events);

        model.Update(Rows(exposure: "-6"));

        var replaced = Assert.Single(events);
        Assert.Equal(NotifyCollectionChangedAction.Replace, replaced.Action);
        Assert.Equal(0, replaced.NewStartingIndex);
        Assert.Equal("-6", model[0].Value);
    }

    // 목표와 다른 행은 불일치와 목표 값을 함께 가집니다.
    [Fact]
    public void RowKeepsMismatchAndTarget()
    {
        var model = new ReadbackModel();

        model.Update([new ReadbackRow("노출 단계", "-6", Mismatch: true, Target: "-9")]);

        Assert.True(model[0].Mismatch);
        Assert.Equal("-9", model[0].Target);
    }
}
