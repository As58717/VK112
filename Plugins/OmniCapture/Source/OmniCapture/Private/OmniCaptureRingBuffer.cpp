#include "OmniCaptureRingBuffer.h"

#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Math/UnrealMathUtility.h"

class FOmniCaptureRingBufferWorker final : public FRunnable
{
public:
    FOmniCaptureRingBufferWorker(TQueue<TUniquePtr<FOmniCaptureFrame>, EQueueMode::Mpsc>& InQueue, FEvent* InEvent, const TFunction<void(TUniquePtr<FOmniCaptureFrame>&&)>& InConsumer, FCriticalSection& InQueueCS, TAtomic<bool>& InRunning, TAtomic<int32>& InPending)
        : Queue(InQueue)
        , DataEvent(InEvent)
        , Consumer(InConsumer)
        , QueueCS(InQueueCS)
        , bRunning(InRunning)
        , Pending(InPending)
    {
    }

    virtual uint32 Run() override
    {
        while (bRunning.Load())
        {
            DataEvent->Wait();

            if (!bRunning.Load())
            {
                break;
            }

            Drain();
        }

        Drain();

        return 0;
    }

private:
    void Drain()
    {
        if (!Consumer)
        {
            return;
        }

        for (;;)
        {
            TUniquePtr<FOmniCaptureFrame> Frame;
            {
                FScopeLock Lock(&QueueCS);
                if (!Queue.Dequeue(Frame))
                {
                    break;
                }
            }

            if (Frame.IsValid())
            {
                Consumer(MoveTemp(Frame));
                Pending.DecrementExchange();
            }
        }
    }

private:
    TQueue<TUniquePtr<FOmniCaptureFrame>, EQueueMode::Mpsc>& Queue;
    FEvent* DataEvent = nullptr;
    TFunction<void(TUniquePtr<FOmniCaptureFrame>&&)> Consumer;
    FCriticalSection& QueueCS;
    TAtomic<bool>& bRunning;
    TAtomic<int32>& Pending;
};

FOmniCaptureRingBuffer::FOmniCaptureRingBuffer()
{
    bRunning = false;
    PendingCount = 0;
    DroppedCount = 0;
    BlockedCount = 0;
}

FOmniCaptureRingBuffer::~FOmniCaptureRingBuffer()
{
    StopWorker();
    Flush();

    if (DataEvent)
    {
        FPlatformProcess::ReturnSynchEventToPool(DataEvent);
        DataEvent = nullptr;
    }
}

void FOmniCaptureRingBuffer::Initialize(const FOmniCaptureSettings& Settings, const TFunction<void(TUniquePtr<FOmniCaptureFrame>&&)>& InConsumer)
{
    Consumer = InConsumer;
    Capacity = FMath::Max(0, Settings.RingBufferCapacity);
    Policy = Settings.RingBufferPolicy;
    StartWorker();
}

void FOmniCaptureRingBuffer::Enqueue(TUniquePtr<FOmniCaptureFrame>&& Frame)
{
    if (!Consumer)
    {
        return;
    }

    if (Capacity > 0)
    {
        for (;;)
        {
            const int32 Current = PendingCount.Load();
            if (Current < Capacity)
            {
                break;
            }

            if (Policy == EOmniCaptureRingBufferPolicy::DropOldest)
            {
                TUniquePtr<FOmniCaptureFrame> Discarded;
                {
                    FScopeLock Lock(&QueueCriticalSection);
                    if (Queue.Dequeue(Discarded))
                    {
                        PendingCount.DecrementExchange();
                    }
                }
                DroppedCount.IncrementExchange();
                break;
            }
            else
            {
                BlockedCount.IncrementExchange();
                FPlatformProcess::Sleep(0.001f);
            }
        }
    }

    {
        FScopeLock Lock(&QueueCriticalSection);
        Queue.Enqueue(MoveTemp(Frame));
        PendingCount.IncrementExchange();
    }

    if (DataEvent)
    {
        DataEvent->Trigger();
    }
}

void FOmniCaptureRingBuffer::Flush()
{
    if (!Consumer)
    {
        return;
    }

    TUniquePtr<FOmniCaptureFrame> Frame;
    for (;;)
    {
        {
            FScopeLock Lock(&QueueCriticalSection);
            if (!Queue.Dequeue(Frame))
            {
                break;
            }
        }

        if (Frame.IsValid())
        {
            Consumer(MoveTemp(Frame));
            PendingCount.DecrementExchange();
        }
    }
}

void FOmniCaptureRingBuffer::StartWorker()
{
    if (WorkerThread.IsValid())
    {
        return;
    }

    DataEvent = FPlatformProcess::GetSynchEventFromPool();
    bRunning = true;

    Worker = new FOmniCaptureRingBufferWorker(Queue, DataEvent, Consumer, QueueCriticalSection, bRunning, PendingCount);
    WorkerThread.Reset(FRunnableThread::Create(Worker, TEXT("OmniCaptureRingBuffer")));
}

void FOmniCaptureRingBuffer::StopWorker()
{
    if (!WorkerThread.IsValid())
    {
        return;
    }

    bRunning = false;

    if (DataEvent)
    {
        DataEvent->Trigger();
    }

    WorkerThread->WaitForCompletion();
    WorkerThread.Reset();

    delete Worker;
    Worker = nullptr;
}

FOmniCaptureRingBufferStats FOmniCaptureRingBuffer::GetStats() const
{
    FOmniCaptureRingBufferStats Stats;
    Stats.PendingFrames = PendingCount.Load();
    Stats.DroppedFrames = DroppedCount.Load();
    Stats.BlockedPushes = BlockedCount.Load();
    return Stats;
}

