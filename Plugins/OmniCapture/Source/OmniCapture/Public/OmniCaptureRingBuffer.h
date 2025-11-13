#pragma once

#include "CoreMinimal.h"
#include "OmniCaptureTypes.h"

class FRunnableThread;
class FOmniCaptureRingBufferWorker;

class OMNICAPTURE_API FOmniCaptureRingBuffer
{
public:
    FOmniCaptureRingBuffer();
    ~FOmniCaptureRingBuffer();

    void Initialize(const FOmniCaptureSettings& Settings, const TFunction<void(TUniquePtr<FOmniCaptureFrame>&&)>& InConsumer);
    void Enqueue(TUniquePtr<FOmniCaptureFrame>&& Frame);
    void Flush();
    FOmniCaptureRingBufferStats GetStats() const;

private:
    void StartWorker();
    void StopWorker();

    TQueue<TUniquePtr<FOmniCaptureFrame>, EQueueMode::Mpsc> Queue;
    TFunction<void(TUniquePtr<FOmniCaptureFrame>&&)> Consumer;

    TUniquePtr<FRunnableThread> WorkerThread;
    FOmniCaptureRingBufferWorker* Worker = nullptr;
    FEvent* DataEvent = nullptr;
    FCriticalSection QueueCriticalSection;
    TAtomic<bool> bRunning;
    TAtomic<int32> PendingCount;
    TAtomic<int32> DroppedCount;
    TAtomic<int32> BlockedCount;
    int32 Capacity = 0;
    EOmniCaptureRingBufferPolicy Policy = EOmniCaptureRingBufferPolicy::DropOldest;
};

