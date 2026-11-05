using DiabeticFoot.Core.Record;

namespace DiabeticFoot.Platform.Tests;

// 원본에는 frame_pool 시험이 없습니다. 원본이 기대는 두 성질(빈 버퍼가 없으면 기다리지 않음, 프레임마다 메모리를 새로 잡지 않음)을
// C#에서 따로 확인합니다.
public class FramePoolTests
{
    // 빈 버퍼가 없으면 기다리지 않고 null이고, 돌려준 버퍼를 다시 빌립니다. 메시지는 넣은 순서대로 나옵니다.
    [Fact]
    public void LendsSlotsWithoutWaitingAndKeepsMessageOrder()
    {
        var pool = new FramePool(16, 2);
        int? first = pool.TryAcquire();
        int? second = pool.TryAcquire();
        Assert.NotNull(first);
        Assert.NotNull(second);
        Assert.NotEqual(first, second);
        Assert.Null(pool.TryAcquire());
        Assert.Equal(0, pool.FreeSlots);
        pool.Release(second.Value);
        Assert.Equal(second, pool.TryAcquire());

        pool.Push(new WriteMessage(WriteMessageKind.Begin));
        pool.Push(new WriteMessage(WriteMessageKind.Frame, first.Value, new FrameRecord(Index: 7)));
        pool.Push(new WriteMessage(WriteMessageKind.End, EndReason: 3));
        Assert.Equal(WriteMessageKind.Begin, pool.Pop().Kind);
        WriteMessage frame = pool.Pop();
        Assert.Equal(first.Value, frame.Slot);
        Assert.Equal(7UL, frame.Record.Index.Value);
        Assert.Equal(3, pool.Pop().EndReason);
    }

    // 수신 스레드와 쓰기 스레드가 프레임마다 도는 경로(빌리기, 넣기, 꺼내기, 돌려주기)는 준비가 끝난 뒤 메모리를 새로 잡지 않습니다.
    [Fact]
    public void FrameCycleDoesNotAllocate()
    {
        var pool = new FramePool(1920 * 1080 * 2, 4);
        void Cycle(int frames)
        {
            for (int i = 0; i < frames; ++i)
            {
                int slot = pool.TryAcquire()!.Value;
                pool.Data(slot)[0] = (byte)i;
                pool.Push(new WriteMessage(WriteMessageKind.Frame, slot, new FrameRecord(Index: (ulong)i)));
                WriteMessage message = pool.Pop();
                pool.Release(message.Slot);
            }
        }
        Cycle(1000);
        long before = GC.GetAllocatedBytesForCurrentThread();
        Cycle(10_000);
        Assert.Equal(0, GC.GetAllocatedBytesForCurrentThread() - before);
    }
}
