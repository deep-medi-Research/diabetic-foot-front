using System.Collections.Concurrent;
using DiabeticFoot.Core.Record;

namespace DiabeticFoot.Platform;

public enum WriteMessageKind : byte
{
    Begin,
    Frame,
    Dropped,
    End,
    Quit,
}

/// <summary>수신 스레드가 쓰기 스레드에 보내는 메시지입니다. 순서대로 처리합니다.</summary>
/// <param name="Slot">Frame일 때 화소가 든 버퍼 번호입니다. Dropped는 버퍼 없이 시각만 넘깁니다.</param>
/// <param name="Record">Frame일 때 시각과 번호입니다. 밝기는 쓰기 스레드가 채웁니다.</param>
/// <param name="EndReason">End일 때 <c>EndReason</c> 값입니다.</param>
public readonly record struct WriteMessage(WriteMessageKind Kind, int Slot = -1, FrameRecord Record = default, int EndReason = 0);

/// <summary>
/// 수신 스레드와 쓰기 스레드 사이의 버퍼와 메시지 대기열입니다.
/// 크기가 같은 버퍼를 미리 잡아 두고 돌려 씁니다. 프레임마다 메모리를 새로 잡지 않기 위해서입니다.
/// </summary>
public sealed class FramePool
{
    readonly byte[][] _buffers;
    readonly object _slotGate = new();
    // 빈 버퍼 번호를 쌓아 둔 배열입니다. 앞 _freeCount개가 빈 버퍼이고 마지막 칸부터 빌려 줍니다.
    readonly int[] _free;
    int _freeCount;
    readonly BlockingCollection<WriteMessage> _messages = new(new ConcurrentQueue<WriteMessage>());

    /// <param name="slotBytes">버퍼 하나의 크기(프레임 하나)입니다.</param>
    /// <param name="slots">버퍼 수입니다. 쓰기가 밀릴 때 버틸 프레임 수입니다.</param>
    public FramePool(int slotBytes, int slots)
    {
        _buffers = new byte[slots][];
        _free = new int[slots];
        for (int i = 0; i < slots; ++i) _buffers[i] = new byte[slotBytes];
        for (int i = slots - 1; i >= 0; --i) _free[_freeCount++] = i;
    }

    /// <summary>
    /// 빈 버퍼 하나를 빌립니다. 빈 버퍼가 없으면 기다리지 않고 null입니다(수신 스레드를 멈추지 않기 위해서입니다).
    /// </summary>
    public int? TryAcquire()
    {
        lock (_slotGate)
        {
            if (_freeCount == 0) return null;
            return _free[--_freeCount];
        }
    }

    /// <summary>버퍼입니다.</summary>
    public byte[] Data(int slot) => _buffers[slot];

    /// <summary>다 쓴 버퍼를 돌려줍니다.</summary>
    public void Release(int slot)
    {
        lock (_slotGate) _free[_freeCount++] = slot;
    }

    /// <summary>메시지를 대기열 끝에 넣습니다.</summary>
    public void Push(WriteMessage message) => _messages.Add(message);

    /// <summary>메시지가 올 때까지 기다렸다가 맨 앞 메시지를 꺼냅니다.</summary>
    public WriteMessage Pop() => _messages.Take();

    /// <summary>빈 버퍼 수입니다. 쓰기 대기열 길이를 잴 때 씁니다.</summary>
    public int FreeSlots
    {
        get
        {
            lock (_slotGate) return _freeCount;
        }
    }
}
