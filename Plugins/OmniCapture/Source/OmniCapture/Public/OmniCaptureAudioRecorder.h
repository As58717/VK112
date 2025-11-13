#pragma once

#include "CoreMinimal.h"
#include "OmniCaptureTypes.h"
#include "Templates/Atomic.h"

class UWorld;
class USoundWave;
class USoundSubmix;
namespace Audio
{
    class FMixerDevice;
}

class OMNICAPTURE_API FOmniCaptureAudioRecorder
{
public:
    FOmniCaptureAudioRecorder();

    bool Initialize(UWorld* InWorld, const FOmniCaptureSettings& Settings);
    void Start();
    void Stop(const FString& OutputDirectory, const FString& BaseFileName);

    void GatherAudio(double FrameTimestamp, TArray<FOmniAudioPacket>& OutPackets);
    FString GetDebugStatus() const;
    int32 GetPendingPacketCount() const;

    void SetPaused(bool bInPaused);
    bool IsPaused() const { return bPaused.Load(); }

    bool IsRecording() const { return bIsRecording; }
    FString GetOutputFilePath() const { return OutputFilePath; }

private:
    void RegisterListener();
    void UnregisterListener();
    void HandleSubmixBuffer(const float* AudioData, int32 NumSamples, int32 NumChannels, int32 SampleRate, double AudioClock);

    TWeakObjectPtr<UWorld> WorldPtr;
    bool bIsRecording = false;
    float Gain = 1.0f;
    FString OutputFilePath;

    mutable FCriticalSection PacketCS;
    TQueue<FOmniAudioPacket, EQueueMode::Mpsc> PendingPackets;
    TWeakObjectPtr<USoundSubmix> TargetSubmix;
    class FOmniCaptureSubmixListener* SubmixListener = nullptr;
    class Audio::FMixerDevice* MixerDevice = nullptr;
    double AudioClockOrigin = -1.0;
    double AudioStartTime = 0.0;
    int32 CachedSampleRate = 48000;
    TAtomic<int32> PendingPacketCount = 0;
    TAtomic<int32> DroppedPacketCount = 0;
    TAtomic<bool> bPaused = false;
    TAtomic<bool> bLoggedOverflowWarning = false;
};

