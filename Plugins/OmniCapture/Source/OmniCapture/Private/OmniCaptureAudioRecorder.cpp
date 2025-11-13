#include "OmniCaptureAudioRecorder.h"

#include "AudioMixerBlueprintLibrary.h"
#include "AudioDevice.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundSubmix.h"
#include "Misc/ScopeLock.h"

#if WITH_AUDIOMIXER
#include "AudioMixerDevice.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogOmniCaptureAudio, Log, All);

namespace
{
    constexpr int32 GMaxPendingAudioPackets = 256;

#if WITH_AUDIOMIXER
    class FOmniCaptureSubmixListener final : public Audio::ISubmixBufferListener
    {
    public:
        explicit FOmniCaptureSubmixListener(FOmniCaptureAudioRecorder& InOwner)
            : Owner(InOwner)
        {
        }

        virtual void OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock) override
        {
            Owner.HandleSubmixBuffer(AudioData, NumSamples, NumChannels, SampleRate, AudioClock);
        }

    private:
        FOmniCaptureAudioRecorder& Owner;
    };
#endif
}

FOmniCaptureAudioRecorder::FOmniCaptureAudioRecorder()
{
}

bool FOmniCaptureAudioRecorder::Initialize(UWorld* InWorld, const FOmniCaptureSettings& Settings)
{
    WorldPtr = InWorld;
    Gain = Settings.AudioGain;
    TargetSubmix = Settings.SubmixToRecord.Get();
    if (!TargetSubmix.IsValid() && Settings.SubmixToRecord.ToSoftObjectPath().IsValid())
    {
        if (USoundSubmix* LoadedSubmix = Cast<USoundSubmix>(Settings.SubmixToRecord.LoadSynchronous()))
        {
            TargetSubmix = LoadedSubmix;
        }
    }
    PendingPacketCount = 0;
    DroppedPacketCount = 0;
    bLoggedOverflowWarning = false;
    AudioClockOrigin = -1.0;
    AudioStartTime = 0.0;
    bPaused.Store(false);

#if WITH_AUDIOMIXER
    MixerDevice = nullptr;
    if (WorldPtr.IsValid())
    {
        if (FAudioDevice* AudioDevice = WorldPtr->GetAudioDeviceRaw())
        {
            if (AudioDevice->IsAudioMixerEnabled())
            {
                MixerDevice = static_cast<Audio::FMixerDevice*>(AudioDevice);
            }
        }
    }
#endif

    return WorldPtr.IsValid();
}

void FOmniCaptureAudioRecorder::Start()
{
    if (!WorldPtr.IsValid() || bIsRecording)
    {
        return;
    }

    DroppedPacketCount = 0;
    bLoggedOverflowWarning = false;

    RegisterListener();
    AudioStartTime = FPlatformTime::Seconds();

    USoundSubmix* Submix = TargetSubmix.IsValid() ? TargetSubmix.Get() : nullptr;
    UAudioMixerBlueprintLibrary::StartRecordingOutput(WorldPtr.Get(), 0.0f, Submix);
    bIsRecording = true;
    bPaused.Store(false);
}

void FOmniCaptureAudioRecorder::Stop(const FString& OutputDirectory, const FString& BaseFileName)
{
    if (!WorldPtr.IsValid() || !bIsRecording)
    {
        return;
    }

    const FString SanitizedName = BaseFileName.IsEmpty() ? TEXT("OmniCapture") : BaseFileName;
    FString Directory = OutputDirectory.IsEmpty() ? (FPaths::ProjectSavedDir() / TEXT("OmniCaptures")) : OutputDirectory;
    Directory = FPaths::ConvertRelativePathToFull(Directory);
    IFileManager::Get().MakeDirectory(*Directory, true);

    USoundSubmix* Submix = TargetSubmix.IsValid() ? TargetSubmix.Get() : nullptr;
    UAudioMixerBlueprintLibrary::StopRecordingOutput(WorldPtr.Get(), EAudioRecordingExportType::WavFile, SanitizedName, Directory, Submix);
    OutputFilePath = Directory / (SanitizedName + TEXT(".wav"));

    UnregisterListener();

    bIsRecording = false;

    {
        FScopeLock Lock(&PacketCS);
        FOmniAudioPacket Packet;
        while (PendingPackets.Dequeue(Packet))
        {
        }
        PendingPacketCount = 0;
    }

    AudioClockOrigin = -1.0;
    AudioStartTime = 0.0;
    DroppedPacketCount = 0;
    bLoggedOverflowWarning = false;
    bPaused.Store(false);
}

void FOmniCaptureAudioRecorder::GatherAudio(double FrameTimestamp, TArray<FOmniAudioPacket>& OutPackets)
{
    FScopeLock Lock(&PacketCS);

    const double Threshold = FrameTimestamp + (1.0 / 120.0);
    for (;;)
    {
        FOmniAudioPacket Packet;
        if (!PendingPackets.Peek(Packet))
        {
            break;
        }

        if (Packet.Timestamp > Threshold)
        {
            break;
        }

        PendingPackets.Dequeue(Packet);
        PendingPacketCount.DecrementExchange();
        OutPackets.Add(MoveTemp(Packet));
    }
}

FString FOmniCaptureAudioRecorder::GetDebugStatus() const
{
    const int32 Pending = PendingPacketCount.Load();
    const int32 Dropped = DroppedPacketCount.Load();
    const FString SubmixName = TargetSubmix.IsValid() ? TargetSubmix->GetName() : TEXT("Master");
    return FString::Printf(TEXT("AudioPackets:%d Dropped:%d SR:%d Submix:%s"), Pending, Dropped, CachedSampleRate, *SubmixName);
}

int32 FOmniCaptureAudioRecorder::GetPendingPacketCount() const
{
    return PendingPacketCount.Load();
}

void FOmniCaptureAudioRecorder::SetPaused(bool bInPaused)
{
    bPaused.Store(bInPaused);
}

void FOmniCaptureAudioRecorder::RegisterListener()
{
#if WITH_AUDIOMIXER
    if (!MixerDevice)
    {
        return;
    }

    if (!SubmixListener)
    {
        SubmixListener = new FOmniCaptureSubmixListener(*this);
    }

    USoundSubmix* Submix = TargetSubmix.IsValid() ? TargetSubmix.Get() : nullptr;
    MixerDevice->RegisterSubmixBufferListener(SubmixListener, Submix);
#endif
}

void FOmniCaptureAudioRecorder::UnregisterListener()
{
#if WITH_AUDIOMIXER
    if (!MixerDevice || !SubmixListener)
    {
        return;
    }

    USoundSubmix* Submix = TargetSubmix.IsValid() ? TargetSubmix.Get() : nullptr;
    MixerDevice->UnregisterSubmixBufferListener(SubmixListener, Submix);

    delete SubmixListener;
    SubmixListener = nullptr;
#endif
}

void FOmniCaptureAudioRecorder::HandleSubmixBuffer(const float* AudioData, int32 NumSamples, int32 NumChannels, int32 SampleRate, double AudioClock)
{
    if (!bIsRecording)
    {
        return;
    }

    if (bPaused.Load())
    {
        return;
    }

#if WITH_AUDIOMIXER
    CachedSampleRate = SampleRate;

    if (AudioClockOrigin < 0.0)
    {
        AudioClockOrigin = AudioClock;
    }

    const double RelativeTimestamp = FMath::Max(0.0, AudioClock - AudioClockOrigin);
    FOmniAudioPacket Packet;
    Packet.Timestamp = RelativeTimestamp;
    Packet.SampleRate = SampleRate;
    Packet.NumChannels = NumChannels;
    Packet.PCM16.SetNumUninitialized(NumSamples);

    for (int32 Index = 0; Index < NumSamples; ++Index)
    {
        const float SampleValue = AudioData[Index] * Gain;
        const int32 IntValue = FMath::RoundToInt(SampleValue * 32767.0f);
        Packet.PCM16[Index] = static_cast<int16>(FMath::Clamp(IntValue, -32768, 32767));
    }

    {
        FScopeLock Lock(&PacketCS);
        bool bDropped = false;
        while (PendingPacketCount.Load() >= GMaxPendingAudioPackets)
        {
            FOmniAudioPacket DiscardedPacket;
            if (!PendingPackets.Dequeue(DiscardedPacket))
            {
                PendingPacketCount = 0;
                break;
            }

            PendingPacketCount.DecrementExchange();
            DroppedPacketCount.IncrementExchange();
            bDropped = true;
        }

        PendingPackets.Enqueue(MoveTemp(Packet));
        PendingPacketCount.IncrementExchange();

        if (bDropped && !bLoggedOverflowWarning.Exchange(true))
        {
            UE_LOG(LogOmniCaptureAudio, Warning, TEXT("OmniCapture audio queue overflowed. Dropping oldest packets to keep audio in sync."));
        }
    }
#else
    (void)AudioData;
    (void)NumSamples;
    (void)NumChannels;
    (void)SampleRate;
    (void)AudioClock;
#endif
}

