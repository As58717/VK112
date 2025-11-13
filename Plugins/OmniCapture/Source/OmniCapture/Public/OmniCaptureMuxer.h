#pragma once

#include "CoreMinimal.h"
#include "OmniCaptureTypes.h"

class OMNICAPTURE_API FOmniCaptureMuxer
{
public:
    void Initialize(const FOmniCaptureSettings& Settings, const FString& InOutputDirectory);
    bool FinalizeCapture(const FOmniCaptureSettings& Settings, const TArray<FOmniCaptureFrameMetadata>& Frames, const FString& AudioPath, const FString& VideoPath, int32 DroppedFrames);
    void BeginRealtimeSession(const FOmniCaptureSettings& Settings);
    void EndRealtimeSession();
    void PushFrame(const FOmniCaptureFrame& Frame);
    FOmniAudioSyncStats GetAudioStats() const { return AudioStats; }
    static FString ResolveFFmpegBinary(const FOmniCaptureSettings& Settings);
    static bool IsFFmpegAvailable(const FOmniCaptureSettings& Settings, FString* OutResolvedPath = nullptr);

private:
    bool WriteManifest(const FOmniCaptureSettings& Settings, const TArray<FOmniCaptureFrameMetadata>& Frames, const FString& AudioPath, const FString& VideoPath, int32 DroppedFrames, FString& OutManifestPath) const;
    bool TryInvokeFFmpeg(const FOmniCaptureSettings& Settings, const TArray<FOmniCaptureFrameMetadata>& Frames, const FString& AudioPath, const FString& VideoPath) const;
    bool WriteSpatialMetadata(const FOmniCaptureSettings& Settings) const;
    FString BuildFFmpegBinaryPath() const;
    double CalculateFrameRate(const TArray<FOmniCaptureFrameMetadata>& Frames) const;

private:
    FString OutputDirectory;
    FString BaseFileName;
    mutable FString CachedFFmpegPath;
    FOmniAudioSyncStats AudioStats;
    double LastVideoTimestamp = 0.0;
    double LastAudioTimestamp = 0.0;
    double DriftWarningThresholdMs = 25.0;
    bool bRealtimeSessionActive = false;
};
