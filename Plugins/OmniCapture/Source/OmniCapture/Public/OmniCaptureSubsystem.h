#pragma once

#include "OmniCaptureTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "OmniCaptureRingBuffer.h"
#include "OmniCaptureImageWriter.h"
#include "OmniCaptureAudioRecorder.h"
#include "OmniCaptureNVENCEncoder.h"
#include "OmniCaptureMuxer.h"
#include "Templates/Atomic.h"
#include "Logging/LogVerbosity.h"
#include "OmniCaptureOptional.h"
#include "OmniCaptureSubsystem.generated.h"

class AOmniCaptureRigActor;
class AOmniCaptureDirectorActor;
class AOmniCapturePreviewActor;
class UTexture2D;
class IConsoleVariable;

struct FOmniCaptureSegmentRecord
{
    int32 SegmentIndex = 0;
    FString Directory;
    FString BaseFileName;
    FString AudioPath;
    FString VideoPath;
    TArray<FOmniCaptureFrameMetadata> Frames;
    int32 DroppedFrames = 0;
    bool bHasImageSequence = false;
};

UCLASS()
class OMNICAPTURE_API UOmniCaptureSubsystem final : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    void BeginCapture(const FOmniCaptureSettings& InSettings);

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    void EndCapture(bool bFinalize = true);

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    bool IsCapturing() const { return bIsCapturing; }

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    void PauseCapture();

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    void ResumeCapture();

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    bool IsPaused() const { return bIsPaused; }

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    bool CapturePanoramaStill(const FOmniCaptureSettings& InSettings, FString& OutFilePath);

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    bool CanPause() const;

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    bool CanResume() const;

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    FString GetStatusString() const;

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    const TArray<FString>& GetActiveWarnings() const { return ActiveWarnings; }

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    FOmniCaptureRingBufferStats GetRingBufferStats() const { return LatestRingBufferStats; }

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    FOmniAudioSyncStats GetAudioSyncStats() const;

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    const FOmniCaptureSettings& GetActiveSettings() const { return ActiveSettings; }

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    double GetCurrentFrameRate() const { return CurrentCaptureFPS; }

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    UTexture2D* GetPreviewTexture() const;

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    bool HasFinalizedOutput() const { return !LastFinalizedOutput.IsEmpty(); }

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    FString GetLastFinalizedOutputPath() const { return LastFinalizedOutput; }

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    FString GetLastStillImagePath() const { return LastStillImagePath; }

    UFUNCTION(BlueprintCallable, Category = "OmniCapture")
    void SetPreviewVisualizationMode(EOmniCapturePreviewView InView);

    UFUNCTION(BlueprintCallable, Category = "OmniCapture|Diagnostics")
    void GetCaptureDiagnosticLog(TArray<FOmniCaptureDiagnosticEntry>& OutEntries) const;

    UFUNCTION(BlueprintCallable, Category = "OmniCapture|Diagnostics")
    void ClearCaptureDiagnosticLog();

    UFUNCTION(BlueprintCallable, Category = "OmniCapture|Diagnostics")
    FString GetLastErrorMessage() const { return LastErrorMessage; }

    void SetActiveDiagnosticVerbosity(EOmniCaptureLogVerbosity InVerbosity);

    void SetPendingRigTransform(const FTransform& InTransform);

private:
    void CreateRig();
    void DestroyRig();

    void CreateTickActor();
    void DestroyTickActor();

    void SpawnPreviewActor();
    void DestroyPreviewActor();
    void InitializeOutputWriters();
    void ShutdownOutputWriters(bool bFinalizeOutputs);
    void FinalizeOutputs(bool bFinalizeOutputs);

    bool ValidateEnvironment();
    bool ApplyFallbacks(FString* OutFailureReason = nullptr);

    void InitializeAudioRecording();
    void ShutdownAudioRecording();

    void TickCapture(float DeltaTime);
    void CaptureFrame();
    void FlushRingBuffer();
    void UpdateDynamicStereoParameters();
    void ApplyRenderFeatureOverrides();
    void RestoreRenderFeatureOverrides();

    void HandleDroppedFrame();

    void ConfigureActiveSegment();
    void RotateSegmentIfNeeded();
    void CompleteActiveSegment(bool bStoreResults);
    int64 CalculateActiveSegmentSizeBytes() const;
    void UpdateRuntimeWarnings();
    void AddWarningUnique(const FString& Warning);
    void RemoveWarning(const FString& Warning);
    void ResetDynamicWarnings();

    FString BuildOutputDirectory() const;
    FString BuildFrameFileName(int32 FrameIndex, const FString& Extension) const;

    void SetDiagnosticContext(const FString& StepName);
    void AppendDiagnostic(EOmniCaptureDiagnosticLevel Level, const FString& Message, const FString& StepOverride = FString());
    void AppendDiagnosticFromVerbosity(ELogVerbosity::Type Verbosity, const FString& Message, const FString& StepOverride = FString());
    void LogDiagnosticMessage(ELogVerbosity::Type Verbosity, const FString& StepName, const FString& Message);
    FString GetActiveDiagnosticStep() const { return CurrentDiagnosticStep; }
    void ApplyRigTransform(AOmniCaptureRigActor* Rig);
    void RecordCaptureFailure(const FString& StepName, const FString& FailureMessage, ELogVerbosity::Type Verbosity = ELogVerbosity::Error);
    void RecordCaptureCompletion(bool bFinalizeOutputs);
    bool ShouldRecordDiagnostic(EOmniCaptureDiagnosticLevel Level) const;
    bool ShouldLogVerbosity(ELogVerbosity::Type Verbosity) const;

private:
    friend class AOmniCaptureDirectorActor;

    struct FConsoleVariableOverrideRecord
    {
        IConsoleVariable* Variable = nullptr;
        FString PreviousValue;
    };

    FOmniCaptureSettings ActiveSettings;
    FOmniCaptureSettings OriginalSettings;

    bool bIsCapturing = false;
    bool bIsPaused = false;
    bool bDroppedFrames = false;

    int32 DroppedFrameCount = 0;
    int32 RecordedSegmentDroppedFrames = 0;

    int32 FrameCounter = 0;
    int32 CaptureAttemptCounter = 0;
    int32 ActiveCaptureAttemptId = 0;
    int32 CurrentDiagnosticAttemptId = 0;
    double CaptureStartTime = 0.0;
    double ActiveAttemptStartTime = 0.0;
    double LastPreviewUpdateTime = 0.0;
    double PreviewFrameInterval = 0.0;
    double CurrentCaptureFPS = 0.0;
    double LastFpsSampleTime = 0.0;
    int32 FramesSinceLastFpsSample = 0;
    double LastRuntimeWarningCheckTime = 0.0;
    double LastSegmentSizeCheckTime = 0.0;
    double CurrentSegmentStartTime = 0.0;
    int32 CurrentSegmentIndex = 0;
    double DynamicParameterStartTime = 0.0;
    float LastDynamicInterPupillaryDistance = -1.0f;
    float LastDynamicConvergence = -1.0f;

    TWeakObjectPtr<AOmniCaptureRigActor> RigActor;
    TWeakObjectPtr<AOmniCaptureDirectorActor> TickActor;
    TWeakObjectPtr<AOmniCapturePreviewActor> PreviewActor;

    TUniquePtr<FOmniCaptureRingBuffer> RingBuffer;
    TUniquePtr<FOmniCaptureImageWriter> ImageWriter;
    TUniquePtr<FOmniCaptureAudioRecorder> AudioRecorder;
    TUniquePtr<FOmniCaptureNVENCEncoder> NVENCEncoder;
    TUniquePtr<FOmniCaptureMuxer> OutputMuxer;

    TAtomic<bool> bUsingNVENCImageFallback{ false };
    bool bCapturedImageSequenceThisSegment = false;
    bool bLastCaptureUsedImageSequenceFallback = false;
    FString LastImageSequenceFallbackDirectory;

    TArray<FOmniCaptureFrameMetadata> CapturedFrameMetadata;
    TArray<FOmniCaptureSegmentRecord> CompletedSegments;
    FString RecordedAudioPath;
    FString RecordedVideoPath;
    FString LastFinalizedOutput;
    FString LastStillImagePath;
    FString BaseOutputDirectory;
    FString BaseOutputFileName;

    EOmniCaptureLogVerbosity ActiveDiagnosticVerbosity = EOmniCaptureLogVerbosity::Info;

    TOptional<FTransform> PendingRigTransform;
    FTransform LastRigTransform = FTransform::Identity;

    TArray<FString> ActiveWarnings;
    FOmniCaptureRingBufferStats LatestRingBufferStats;
    FOmniAudioSyncStats AudioStats;

    EOmniCaptureState State = EOmniCaptureState::Idle;

    TArray<FConsoleVariableOverrideRecord> ConsoleOverrideRecords;
    bool bRenderOverridesApplied = false;

    TArray<FOmniCaptureDiagnosticEntry> DiagnosticLog;
    FString CurrentDiagnosticStep;
    FString LastErrorMessage;
};

