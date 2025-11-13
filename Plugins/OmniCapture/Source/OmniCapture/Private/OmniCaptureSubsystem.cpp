#include "OmniCaptureSubsystem.h"

#include "OmniCaptureAudioRecorder.h"
#include "OmniCaptureDirectorActor.h"
#include "OmniCaptureEquirectConverter.h"
#include "OmniCaptureNVENCEncoder.h"
#include "OmniCaptureImageWriter.h"
#include "OmniCaptureRigActor.h"
#include "OmniCaptureRingBuffer.h"
#include "OmniCapturePreviewActor.h"
#include "OmniCaptureMuxer.h"
#include "OmniCaptureSettingsValidator.h"

#include "Curves/CurveFloat.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFile.h"
#include "HAL/IConsoleManager.h"
#include "RenderingThread.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "RHI.h"
#include "PixelFormat.h"
#include "Math/UnrealMathUtility.h"
#include "Engine/RendererSettings.h"
#include "Misc/EngineVersionComparison.h"
#include "DataDrivenShaderPlatformInfo.h"

#if RHI_RAYTRACING
#include "RayTracingDefinitions.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogOmniCaptureSubsystem, Log, All);

namespace OmniCapture
{
    static constexpr TCHAR RigActorName[] = TEXT("OmniCaptureRig");
    static constexpr TCHAR DirectorActorName[] = TEXT("OmniCaptureDirector");
    static const FString WarningLowDisk = TEXT("Storage space is low for OmniCapture output");
    static const FString WarningFrameDrop = TEXT("Frame drops detected - rendering slower than encode path");
    static const FString WarningLowFps = TEXT("Capture frame rate is below the configured target");
}

namespace
{
    constexpr int32 GMaxOmniDiagnostics = 256;

    EOmniCaptureDiagnosticLevel ConvertVerbosityToDiagnostic(ELogVerbosity::Type Verbosity)
    {
        switch (Verbosity)
        {
        case ELogVerbosity::Error:
        case ELogVerbosity::Fatal:
            return EOmniCaptureDiagnosticLevel::Error;
        case ELogVerbosity::Warning:
            return EOmniCaptureDiagnosticLevel::Warning;
        default:
            return EOmniCaptureDiagnosticLevel::Info;
        }
    }
}

void UOmniCaptureSubsystem::SetDiagnosticContext(const FString& StepName)
{
    CurrentDiagnosticStep = StepName;
}

void UOmniCaptureSubsystem::AppendDiagnostic(EOmniCaptureDiagnosticLevel Level, const FString& Message, const FString& StepOverride)
{
    FOmniCaptureDiagnosticEntry& Entry = DiagnosticLog.AddDefaulted_GetRef();
    Entry.Timestamp = FDateTime::UtcNow();
    Entry.SecondsSinceCaptureStart = CaptureStartTime > 0.0 ? static_cast<float>(FPlatformTime::Seconds() - CaptureStartTime) : 0.0f;
    const int32 AttemptId = CurrentDiagnosticAttemptId > 0 ? CurrentDiagnosticAttemptId : (ActiveCaptureAttemptId > 0 ? ActiveCaptureAttemptId : 0);
    Entry.AttemptIndex = AttemptId;
    Entry.Step = StepOverride.IsEmpty() ? (CurrentDiagnosticStep.IsEmpty() ? TEXT("General") : CurrentDiagnosticStep) : StepOverride;
    Entry.Message = Message;
    Entry.Level = Level;

    if (DiagnosticLog.Num() > GMaxOmniDiagnostics)
    {
        const int32 Excess = DiagnosticLog.Num() - GMaxOmniDiagnostics;
        DiagnosticLog.RemoveAt(0, Excess, EAllowShrinking::No);
    }

    if (Level == EOmniCaptureDiagnosticLevel::Error)
    {
        LastErrorMessage = Message;
    }
}

void UOmniCaptureSubsystem::AppendDiagnosticFromVerbosity(ELogVerbosity::Type Verbosity, const FString& Message, const FString& StepOverride)
{
    AppendDiagnostic(ConvertVerbosityToDiagnostic(Verbosity), Message, StepOverride);
}

void UOmniCaptureSubsystem::LogDiagnosticMessage(ELogVerbosity::Type Verbosity, const FString& StepName, const FString& Message)
{
    switch (Verbosity)
    {
    case ELogVerbosity::Fatal:
    case ELogVerbosity::Error:
        UE_LOG(LogOmniCaptureSubsystem, Error, TEXT("%s"), *Message);
        break;
    case ELogVerbosity::Warning:
        UE_LOG(LogOmniCaptureSubsystem, Warning, TEXT("%s"), *Message);
        break;
    case ELogVerbosity::Display:
    case ELogVerbosity::Log:
    case ELogVerbosity::Verbose:
    case ELogVerbosity::VeryVerbose:
    case ELogVerbosity::SetColor:
    case ELogVerbosity::BreakOnLog:
    default:
        UE_LOG(LogOmniCaptureSubsystem, Log, TEXT("%s"), *Message);
        break;
    }
    AppendDiagnosticFromVerbosity(Verbosity, Message, StepName);
}

void UOmniCaptureSubsystem::RecordCaptureFailure(const FString& StepName, const FString& FailureMessage, ELogVerbosity::Type Verbosity)
{
    const int32 AttemptId = ActiveCaptureAttemptId > 0
        ? ActiveCaptureAttemptId
        : (CurrentDiagnosticAttemptId > 0 ? CurrentDiagnosticAttemptId : (CaptureAttemptCounter > 0 ? CaptureAttemptCounter : 0));

    if (AttemptId <= 0)
    {
        LogDiagnosticMessage(Verbosity, StepName, FailureMessage);
        return;
    }

    CurrentDiagnosticAttemptId = AttemptId;
    LogDiagnosticMessage(Verbosity, StepName, FailureMessage);

    const double Now = FPlatformTime::Seconds();
    const double StartTime = ActiveAttemptStartTime > 0.0 ? ActiveAttemptStartTime : CaptureStartTime;
    const float DurationSeconds = StartTime > 0.0 ? static_cast<float>(Now - StartTime) : 0.0f;
    const FString SummaryStep = FString::Printf(TEXT("Attempt %d Summary"), AttemptId);
    const FString SummaryMessage = FString::Printf(TEXT("Capture attempt #%d failed after %.2fs at step '%s'. Reason: %s"), AttemptId, DurationSeconds, *StepName, *FailureMessage);
    LogDiagnosticMessage(ELogVerbosity::Error, SummaryStep, SummaryMessage);

    LastErrorMessage = FailureMessage;

    ActiveCaptureAttemptId = 0;
    CurrentDiagnosticAttemptId = 0;
    ActiveAttemptStartTime = 0.0;
    CaptureStartTime = 0.0;
    bIsCapturing = false;
    bIsPaused = false;
    State = EOmniCaptureState::Idle;
}

void UOmniCaptureSubsystem::RecordCaptureCompletion(bool bFinalizeOutputs)
{
    const int32 AttemptId = ActiveCaptureAttemptId > 0
        ? ActiveCaptureAttemptId
        : (CurrentDiagnosticAttemptId > 0 ? CurrentDiagnosticAttemptId : 0);

    if (AttemptId <= 0)
    {
        return;
    }

    CurrentDiagnosticAttemptId = AttemptId;

    const double Now = FPlatformTime::Seconds();
    const double StartTime = ActiveAttemptStartTime > 0.0 ? ActiveAttemptStartTime : CaptureStartTime;
    const float DurationSeconds = StartTime > 0.0 ? static_cast<float>(Now - StartTime) : 0.0f;

    const FString Outcome = bFinalizeOutputs ? TEXT("completed") : TEXT("stopped without finalization");

    FString OutputDetail;
    if (bFinalizeOutputs)
    {
        if (!LastFinalizedOutput.IsEmpty())
        {
            OutputDetail = FString::Printf(TEXT("Final output: %s"), *LastFinalizedOutput);
        }
        else if (bLastCaptureUsedImageSequenceFallback && !LastImageSequenceFallbackDirectory.IsEmpty())
        {
            OutputDetail = FString::Printf(TEXT("Image sequence stored in %s"), *LastImageSequenceFallbackDirectory);
        }
        else if (ActiveSettings.OutputFormat == EOmniOutputFormat::ImageSequence)
        {
            OutputDetail = FString::Printf(TEXT("Image sequence stored in %s"), *ActiveSettings.OutputDirectory);
        }
        else if (!RecordedVideoPath.IsEmpty())
        {
            OutputDetail = FString::Printf(TEXT("Encoded video: %s"), *RecordedVideoPath);
        }
        else
        {
            OutputDetail = TEXT("No finalized output was generated.");
        }
    }
    else
    {
        OutputDetail = TEXT("Finalization skipped by request.");
    }

    const FString SummaryStep = FString::Printf(TEXT("Attempt %d Summary"), AttemptId);
    const FString SummaryMessage = FString::Printf(TEXT("Capture attempt #%d %s after %.2fs. Frames captured: %d. Dropped frames: %d. %s"),
        AttemptId,
        *Outcome,
        DurationSeconds,
        FrameCounter,
        DroppedFrameCount,
        *OutputDetail);
    LogDiagnosticMessage(ELogVerbosity::Log, SummaryStep, SummaryMessage);

    ActiveCaptureAttemptId = 0;
    ActiveAttemptStartTime = 0.0;
}

void UOmniCaptureSubsystem::GetCaptureDiagnosticLog(TArray<FOmniCaptureDiagnosticEntry>& OutEntries) const
{
    OutEntries = DiagnosticLog;
}

void UOmniCaptureSubsystem::ClearCaptureDiagnosticLog()
{
    DiagnosticLog.Reset();
    LastErrorMessage.Reset();
    CurrentDiagnosticStep.Reset();
    if (bIsCapturing && ActiveCaptureAttemptId > 0)
    {
        CurrentDiagnosticAttemptId = ActiveCaptureAttemptId;
    }
    else
    {
        CurrentDiagnosticAttemptId = 0;
    }
}

void UOmniCaptureSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    SetDiagnosticContext(TEXT("Subsystem"));
    LogDiagnosticMessage(ELogVerbosity::Log, TEXT("Subsystem"), TEXT("OmniCapture subsystem initialized"));
}

void UOmniCaptureSubsystem::Deinitialize()
{
    EndCapture(false);
    Super::Deinitialize();
}

void UOmniCaptureSubsystem::BeginCapture(const FOmniCaptureSettings& InSettings)
{
    if (bIsCapturing)
    {
        LogDiagnosticMessage(ELogVerbosity::Warning, TEXT("BeginCapture"), TEXT("Capture already running"));
        return;
    }

    ClearCaptureDiagnosticLog();

    ActiveCaptureAttemptId = ++CaptureAttemptCounter;
    CurrentDiagnosticAttemptId = ActiveCaptureAttemptId;
    ActiveAttemptStartTime = FPlatformTime::Seconds();

    SetDiagnosticContext(TEXT("BeginCapture"));
    AppendDiagnostic(EOmniCaptureDiagnosticLevel::Info, FString::Printf(TEXT("Capture request received (Attempt #%d)."), ActiveCaptureAttemptId), TEXT("BeginCapture"));

    if (InSettings.Resolution <= 0)
    {
        RecordCaptureFailure(TEXT("BeginCapture"), FString::Printf(TEXT("Invalid capture resolution (%d)."), InSettings.Resolution));
        return;
    }

    OriginalSettings = InSettings;
    OriginalSettings.MigrateDeprecatedOverrides();
    ActiveSettings = InSettings;
    ActiveSettings.MigrateDeprecatedOverrides();
    FOmniCaptureNVENCEncoder::SetRuntimeDirectoryOverride(ActiveSettings.GetEffectiveNVENCRuntimeDirectory());
    FOmniCaptureNVENCEncoder::SetDllOverridePath(ActiveSettings.NVENCDllPathOverride);
    FOmniCaptureNVENCEncoder::InvalidateCachedCapabilities();
    ActiveSettings.OutputDirectory = BuildOutputDirectory();

    BaseOutputDirectory = ActiveSettings.OutputDirectory;
    BaseOutputFileName = ActiveSettings.OutputFileName.IsEmpty() ? TEXT("OmniCapture") : ActiveSettings.OutputFileName;
    CurrentSegmentIndex = 0;
    CapturedFrameMetadata.Empty();
    CompletedSegments.Empty();
    RecordedAudioPath.Reset();
    RecordedVideoPath.Reset();
    LastFinalizedOutput.Empty();
    LastStillImagePath.Empty();
    OutputMuxer.Reset();
    bUsingNVENCImageFallback.Store(false);
    bCapturedImageSequenceThisSegment = false;
    bLastCaptureUsedImageSequenceFallback = false;
    LastImageSequenceFallbackDirectory.Reset();

    ActiveWarnings.Empty();
    LatestRingBufferStats = FOmniCaptureRingBufferStats();
    AudioStats = FOmniAudioSyncStats();
    ResetDynamicWarnings();

    bIsPaused = false;
    bDroppedFrames = false;
    DroppedFrameCount = 0;
    RecordedSegmentDroppedFrames = 0;
    CurrentCaptureFPS = 0.0;
    LastFpsSampleTime = 0.0;
    FramesSinceLastFpsSample = 0;
    LastRuntimeWarningCheckTime = FPlatformTime::Seconds();
    LastSegmentSizeCheckTime = LastRuntimeWarningCheckTime;

    SetDiagnosticContext(TEXT("ValidateEnvironment"));
    AppendDiagnostic(EOmniCaptureDiagnosticLevel::Info, TEXT("Validating capture environment."), TEXT("ValidateEnvironment"));
    const bool bEnvironmentOk = ValidateEnvironment();

    {
        TArray<FString> CompatibilityWarnings;
        FString CompatibilityFailure;
        if (!FOmniCaptureSettingsValidator::ApplyCompatibilityFixups(ActiveSettings, CompatibilityWarnings, &CompatibilityFailure))
        {
            const FString FailureMessage = CompatibilityFailure.IsEmpty()
                ? TEXT("Capture aborted due to incompatible projection settings.")
                : FString::Printf(TEXT("Capture aborted due to incompatible projection settings: %s"), *CompatibilityFailure);
            RecordCaptureFailure(TEXT("ValidateEnvironment"), FailureMessage);
            return;
        }

        for (const FString& Warning : CompatibilityWarnings)
        {
            AddWarningUnique(Warning);
        }
    }

    FString FallbackFailureReason;
    if (!ApplyFallbacks(&FallbackFailureReason))
    {
        const FString FailureMessage = FallbackFailureReason.IsEmpty()
            ? TEXT("Capture aborted due to environment validation failure.")
            : FString::Printf(TEXT("Capture aborted due to environment validation failure: %s"), *FallbackFailureReason);
        RecordCaptureFailure(TEXT("ValidateEnvironment"), FailureMessage);
        return;
    }
    AppendDiagnostic(EOmniCaptureDiagnosticLevel::Info, TEXT("Environment validation completed."), TEXT("ValidateEnvironment"));
    if (!bEnvironmentOk && ActiveWarnings.Num() > 0)
    {
        const FString CombinedWarnings = FString::Join(ActiveWarnings, TEXT("; "));
        LogDiagnosticMessage(ELogVerbosity::Warning, TEXT("ValidateEnvironment"), FString::Printf(TEXT("Capture environment warnings: %s"), *CombinedWarnings));
    }

    ConfigureActiveSegment();

    UWorld* World = GetWorld();
    if (!World)
    {
        RecordCaptureFailure(TEXT("BeginCapture"), TEXT("Invalid world context for capture (GetWorld returned null)."));
        return;
    }

    DynamicParameterStartTime = FPlatformTime::Seconds();
    LastDynamicInterPupillaryDistance = ActiveSettings.InterPupillaryDistanceCm;
    LastDynamicConvergence = ActiveSettings.EyeConvergenceDistanceCm;

    ApplyRenderFeatureOverrides();

    SetDiagnosticContext(TEXT("CreateRig"));
    AppendDiagnostic(EOmniCaptureDiagnosticLevel::Info, TEXT("Spawning capture rig."), TEXT("CreateRig"));
    CreateRig();
    if (!RigActor.IsValid())
    {
        RecordCaptureFailure(TEXT("CreateRig"), TEXT("Failed to create capture rig (AOmniCaptureRigActor was not spawned)."));
        RestoreRenderFeatureOverrides();
        DynamicParameterStartTime = 0.0;
        LastDynamicInterPupillaryDistance = -1.0f;
        LastDynamicConvergence = -1.0f;
        return;
    }

    UpdateDynamicStereoParameters();

    SetDiagnosticContext(TEXT("CreateTickActor"));
    AppendDiagnostic(EOmniCaptureDiagnosticLevel::Info, TEXT("Spawning capture tick actor."), TEXT("CreateTickActor"));
    CreateTickActor();
    if (!TickActor.IsValid())
    {
        RecordCaptureFailure(TEXT("CreateTickActor"), TEXT("Failed to create capture tick actor (AOmniCaptureDirectorActor was not spawned)."));
        DestroyRig();
        RestoreRenderFeatureOverrides();
        DynamicParameterStartTime = 0.0;
        LastDynamicInterPupillaryDistance = -1.0f;
        LastDynamicConvergence = -1.0f;
        return;
    }

    SpawnPreviewActor();

    SetDiagnosticContext(TEXT("InitializeOutputs"));
    AppendDiagnostic(EOmniCaptureDiagnosticLevel::Info, TEXT("Initializing output writers."), TEXT("InitializeOutputs"));
    InitializeOutputWriters();

    OutputMuxer = MakeUnique<FOmniCaptureMuxer>();
    if (OutputMuxer)
    {
        OutputMuxer->Initialize(ActiveSettings, ActiveSettings.OutputDirectory);
        OutputMuxer->BeginRealtimeSession(ActiveSettings);
    }

    RingBuffer = MakeUnique<FOmniCaptureRingBuffer>();
    RingBuffer->Initialize(ActiveSettings, [this](TUniquePtr<FOmniCaptureFrame>&& Frame)
    {
        if (!Frame.IsValid())
        {
            return;
        }

        if (OutputMuxer)
        {
            OutputMuxer->PushFrame(*Frame);
            AudioStats = OutputMuxer->GetAudioStats();
            if (AudioRecorder)
            {
                AudioStats.PendingPackets += AudioRecorder->GetPendingPacketCount();
            }
        }

        switch (ActiveSettings.OutputFormat)
        {
        case EOmniOutputFormat::ImageSequence:
            if (ImageWriter)
            {
                const FString FileName = BuildFrameFileName(Frame->Metadata.FrameIndex, ActiveSettings.GetImageFileExtension());
                ImageWriter->EnqueueFrame(MoveTemp(Frame), FileName);
            }
            break;
        case EOmniOutputFormat::NVENCHardware:
            if (NVENCEncoder)
            {
                NVENCEncoder->EnqueueFrame(*Frame);
            }
            if (bUsingNVENCImageFallback.Load() && ImageWriter && Frame.IsValid())
            {
                const FString FileName = BuildFrameFileName(Frame->Metadata.FrameIndex, ActiveSettings.GetImageFileExtension());
                ImageWriter->EnqueueFrame(MoveTemp(Frame), FileName);
            }
            break;
        default:
            break;
        }

        if (RingBuffer.IsValid())
        {
            LatestRingBufferStats = RingBuffer->GetStats();
            if (LatestRingBufferStats.DroppedFrames > DroppedFrameCount)
            {
                DroppedFrameCount = LatestRingBufferStats.DroppedFrames;
                HandleDroppedFrame();
            }
        }
    });

    InitializeAudioRecording();

    bIsCapturing = true;
    bDroppedFrames = false;
    DroppedFrameCount = 0;
    FrameCounter = 0;
    CaptureStartTime = FPlatformTime::Seconds();
    CurrentSegmentStartTime = CaptureStartTime;
    LastSegmentSizeCheckTime = CurrentSegmentStartTime;
    LastRuntimeWarningCheckTime = CurrentSegmentStartTime;
    PreviewFrameInterval = (ActiveSettings.bEnablePreviewWindow && ActiveSettings.PreviewFrameRate > 0.f) ? (1.0 / FMath::Max(1.0f, ActiveSettings.PreviewFrameRate)) : 0.0;
    LastPreviewUpdateTime = CaptureStartTime;
    State = EOmniCaptureState::Recording;

    const FIntPoint OutputDimensions = ActiveSettings.GetOutputResolution();
    const TCHAR* CoverageLabel = ActiveSettings.IsPlanar()
        ? TEXT("Planar2D")
        : (ActiveSettings.Coverage == EOmniCaptureCoverage::HalfSphere ? TEXT("180") : TEXT("360"));
    const TCHAR* LayoutLabel = ActiveSettings.Mode == EOmniCaptureMode::Stereo
        ? (ActiveSettings.StereoLayout == EOmniCaptureStereoLayout::TopBottom ? TEXT("Top-Bottom") : TEXT("Side-by-Side"))
        : TEXT("Mono");
    const TCHAR* ProjectionLabel = ActiveSettings.IsPlanar()
        ? TEXT("Planar")
        : (ActiveSettings.IsFisheye() ? TEXT("Fisheye") : TEXT("Equirect"));
    const FString BeginSummary = FString::Printf(TEXT("Attempt #%d -> Begin capture %s %s (%dx%d -> %dx%d, %s %s) (%s, %s, %s) -> %s"),
        ActiveCaptureAttemptId,
        ActiveSettings.Mode == EOmniCaptureMode::Stereo ? TEXT("Stereo") : TEXT("Mono"),
        CoverageLabel,
        ActiveSettings.IsPlanar() ? ActiveSettings.PlanarResolution.X : (ActiveSettings.IsFisheye() ? ActiveSettings.FisheyeResolution.X : ActiveSettings.Resolution),
        ActiveSettings.IsPlanar() ? ActiveSettings.PlanarResolution.Y : (ActiveSettings.IsFisheye() ? ActiveSettings.FisheyeResolution.Y : ActiveSettings.Resolution),
        OutputDimensions.X,
        OutputDimensions.Y,
        ProjectionLabel,
        LayoutLabel,
        ActiveSettings.OutputFormat == EOmniOutputFormat::ImageSequence ? TEXT("Image") : TEXT("NVENC"),
        ActiveSettings.Gamma == EOmniCaptureGamma::Linear ? TEXT("Linear") : TEXT("sRGB"),
        ActiveSettings.Codec == EOmniCaptureCodec::HEVC ? TEXT("HEVC") : TEXT("H.264"),
        *ActiveSettings.OutputDirectory);
    LogDiagnosticMessage(ELogVerbosity::Log, TEXT("BeginCapture"), BeginSummary);
    SetDiagnosticContext(TEXT("CaptureLoop"));
    AppendDiagnostic(EOmniCaptureDiagnosticLevel::Info, TEXT("Capture pipeline initialized."), TEXT("CaptureLoop"));
}

void UOmniCaptureSubsystem::EndCapture(bool bFinalize)
{
    if (!bIsCapturing)
    {
        return;
    }

    const int32 AttemptId = ActiveCaptureAttemptId > 0 ? ActiveCaptureAttemptId : CurrentDiagnosticAttemptId;
    LogDiagnosticMessage(ELogVerbosity::Log, TEXT("EndCapture"), FString::Printf(TEXT("Attempt #%d -> End capture (Finalize=%d)"), AttemptId, bFinalize ? 1 : 0));

    bIsCapturing = false;
    bIsPaused = false;
    State = EOmniCaptureState::Finalizing;

    RestoreRenderFeatureOverrides();
    DynamicParameterStartTime = 0.0;
    LastDynamicInterPupillaryDistance = -1.0f;
    LastDynamicConvergence = -1.0f;

    DestroyTickActor();
    DestroyPreviewActor();
    DestroyRig();

    ShutdownAudioRecording();

    if (RingBuffer)
    {
        RingBuffer->Flush();
        RingBuffer.Reset();
    }

    ShutdownOutputWriters(bFinalize);
    if (OutputMuxer)
    {
        OutputMuxer->EndRealtimeSession();
    }
    FinalizeOutputs(bFinalize);

    RecordCaptureCompletion(bFinalize);

    SetDiagnosticContext(TEXT("Idle"));
    AppendDiagnostic(EOmniCaptureDiagnosticLevel::Info, TEXT("Capture session ended."), TEXT("Idle"));

    CurrentDiagnosticAttemptId = 0;
    CaptureStartTime = 0.0;

    State = EOmniCaptureState::Idle;
    LatestRingBufferStats = FOmniCaptureRingBufferStats();
    AudioStats = FOmniAudioSyncStats();
}

void UOmniCaptureSubsystem::PauseCapture()
{
    if (!bIsCapturing || bIsPaused)
    {
        return;
    }

    bIsPaused = true;
    State = EOmniCaptureState::Paused;
    SetDiagnosticContext(TEXT("Paused"));
    AppendDiagnostic(EOmniCaptureDiagnosticLevel::Info, TEXT("Capture paused."), TEXT("Paused"));

    if (RingBuffer)
    {
        RingBuffer->Flush();
    }

    if (AudioRecorder)
    {
        AudioRecorder->SetPaused(true);
    }

    if (OutputMuxer)
    {
        OutputMuxer->EndRealtimeSession();
    }
}

void UOmniCaptureSubsystem::ResumeCapture()
{
    if (!bIsCapturing || !bIsPaused)
    {
        return;
    }

    bIsPaused = false;
    State = bDroppedFrames ? EOmniCaptureState::DroppedFrames : EOmniCaptureState::Recording;
    LastFpsSampleTime = 0.0;
    FramesSinceLastFpsSample = 0;
    SetDiagnosticContext(TEXT("CaptureLoop"));
    AppendDiagnostic(EOmniCaptureDiagnosticLevel::Info, TEXT("Capture resumed."), TEXT("CaptureLoop"));

    if (AudioRecorder)
    {
        AudioRecorder->SetPaused(false);
    }

    if (OutputMuxer)
    {
        OutputMuxer->BeginRealtimeSession(ActiveSettings);
    }
}

bool UOmniCaptureSubsystem::CapturePanoramaStill(const FOmniCaptureSettings& InSettings, FString& OutFilePath)
{
    OutFilePath.Empty();

    SetDiagnosticContext(TEXT("StillCapture"));
    AppendDiagnostic(EOmniCaptureDiagnosticLevel::Info, TEXT("Still capture request received."), TEXT("StillCapture"));

    if (bIsCapturing)
    {
        LogDiagnosticMessage(ELogVerbosity::Warning, TEXT("StillCapture"), TEXT("Cannot capture still image while recording is active."));
        return false;
    }

    if (InSettings.Resolution <= 0)
    {
        LogDiagnosticMessage(ELogVerbosity::Error, TEXT("StillCapture"), TEXT("Invalid resolution supplied for still capture."));
        return false;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        LogDiagnosticMessage(ELogVerbosity::Error, TEXT("StillCapture"), TEXT("No valid world available for still capture."));
        return false;
    }

    LastStillImagePath.Empty();

    FOmniCaptureSettings StillSettings = InSettings;
    StillSettings.OutputFormat = EOmniOutputFormat::ImageSequence;

    {
        TArray<FString> CompatibilityWarnings;
        FString CompatibilityFailure;
        if (!FOmniCaptureSettingsValidator::ApplyCompatibilityFixups(StillSettings, CompatibilityWarnings, &CompatibilityFailure))
        {
            LogDiagnosticMessage(ELogVerbosity::Error, TEXT("StillCapture"), CompatibilityFailure.IsEmpty() ? TEXT("Still capture aborted due to incompatible projection settings.") : CompatibilityFailure);
            return false;
        }

        for (const FString& Warning : CompatibilityWarnings)
        {
            AddWarningUnique(Warning);
        }
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParams.ObjectFlags |= RF_Transient;

    AOmniCaptureRigActor* TempRig = World->SpawnActor<AOmniCaptureRigActor>(AOmniCaptureRigActor::StaticClass(), FTransform::Identity, SpawnParams);
    if (!TempRig)
    {
        LogDiagnosticMessage(ELogVerbosity::Error, TEXT("StillCapture"), TEXT("Failed to spawn capture rig for still capture."));
        return false;
    }

    TempRig->Configure(StillSettings);
    ApplyRigTransform(TempRig);

    FOmniEyeCapture LeftEye;
    FOmniEyeCapture RightEye;
    TempRig->Capture(LeftEye, RightEye);

    FlushRenderingCommands();

    auto ConvertFrame = [](const FOmniCaptureSettings& CaptureSettings, const FOmniEyeCapture& Left, const FOmniEyeCapture& Right)
    {
        if (CaptureSettings.IsPlanar())
        {
            return FOmniCaptureEquirectConverter::ConvertToPlanar(CaptureSettings, Left);
        }

        if (CaptureSettings.IsFisheye() && !CaptureSettings.ShouldConvertFisheyeToEquirect())
        {
            return FOmniCaptureEquirectConverter::ConvertToFisheye(CaptureSettings, Left, Right);
        }

        return FOmniCaptureEquirectConverter::ConvertToEquirectangular(CaptureSettings, Left, Right);
    };

    FOmniCaptureEquirectResult Result = ConvertFrame(StillSettings, LeftEye, RightEye);

    TMap<FName, FOmniCaptureLayerPayload> AuxiliaryLayers;
    if (StillSettings.AuxiliaryPasses.Num() > 0)
    {
        auto BuildAuxEye = [](const FOmniEyeCapture& SourceEye, EOmniCaptureAuxiliaryPassType PassType)
        {
            FOmniEyeCapture AuxEye;
            AuxEye.ActiveFaceCount = SourceEye.ActiveFaceCount;
            for (int32 FaceIndex = 0; FaceIndex < AuxEye.ActiveFaceCount && FaceIndex < UE_ARRAY_COUNT(AuxEye.Faces); ++FaceIndex)
            {
                AuxEye.Faces[FaceIndex].RenderTarget = SourceEye.Faces[FaceIndex].GetAuxiliaryRenderTarget(PassType);
            }
            return AuxEye;
        };

        for (EOmniCaptureAuxiliaryPassType PassType : StillSettings.AuxiliaryPasses)
        {
            if (PassType == EOmniCaptureAuxiliaryPassType::None)
            {
                continue;
            }

            const FOmniEyeCapture AuxLeft = BuildAuxEye(LeftEye, PassType);
            const FOmniEyeCapture AuxRight = BuildAuxEye(RightEye, PassType);
            FOmniCaptureEquirectResult AuxResult = ConvertFrame(StillSettings, AuxLeft, AuxRight);
            if (AuxResult.PixelData.IsValid())
            {
                FOmniCaptureLayerPayload Payload;
                Payload.PixelData = MoveTemp(AuxResult.PixelData);
                Payload.bLinear = AuxResult.bIsLinear;
                Payload.Precision = AuxResult.PixelPrecision;
                Payload.PixelDataType = AuxResult.PixelDataType;
                AuxiliaryLayers.Add(GetAuxiliaryLayerName(PassType), MoveTemp(Payload));
            }
        }
    }

    World->DestroyActor(TempRig);

    if (!Result.PixelData.IsValid())
    {
        LogDiagnosticMessage(ELogVerbosity::Warning, TEXT("StillCapture"), TEXT("Still capture did not generate pixel data. Check cubemap rig configuration."));
        return false;
    }

    FString OutputDirectory = StillSettings.OutputDirectory;
    if (OutputDirectory.IsEmpty())
    {
        OutputDirectory = FPaths::ProjectSavedDir() / TEXT("OmniCaptures");
    }
    OutputDirectory = FPaths::ConvertRelativePathToFull(OutputDirectory);
    IFileManager::Get().MakeDirectory(*OutputDirectory, true);

    const FString BaseName = StillSettings.OutputFileName.IsEmpty() ? TEXT("OmniCaptureStill") : StillSettings.OutputFileName;
    const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    const FString Extension = StillSettings.GetImageFileExtension();
    const FString FileName = FString::Printf(TEXT("%s_%s%s"), *BaseName, *Timestamp, *Extension);
    OutFilePath = OutputDirectory / FileName;

    FOmniCaptureImageWriter Writer;
    FOmniCaptureSettings WriterSettings = StillSettings;
    WriterSettings.OutputDirectory = OutputDirectory;
    WriterSettings.OutputFileName = BaseName;
    Writer.Initialize(WriterSettings, OutputDirectory);

    TUniquePtr<FOmniCaptureFrame> Frame = MakeUnique<FOmniCaptureFrame>();
    Frame->Metadata.FrameIndex = 0;
    Frame->Metadata.Timecode = 0.0;
    Frame->Metadata.bKeyFrame = true;
    Frame->PixelData = MoveTemp(Result.PixelData);
    Frame->bLinearColor = Result.bIsLinear;
    Frame->bUsedCPUFallback = Result.bUsedCPUFallback;
    Frame->PixelDataType = Result.PixelDataType;
    Frame->AuxiliaryLayers = MoveTemp(AuxiliaryLayers);

    Writer.EnqueueFrame(MoveTemp(Frame), FileName);
    Writer.Flush();

    LastStillImagePath = OutFilePath;
    LastFinalizedOutput = OutFilePath;

    LogDiagnosticMessage(ELogVerbosity::Log, TEXT("StillCapture"), FString::Printf(TEXT("Panoramic still saved to %s"), *OutFilePath));

    return true;
}

bool UOmniCaptureSubsystem::CanPause() const
{
    return bIsCapturing && !bIsPaused;
}

bool UOmniCaptureSubsystem::CanResume() const
{
    return bIsCapturing && bIsPaused;
}

FString UOmniCaptureSubsystem::GetStatusString() const
{
    if (!bIsCapturing)
    {
        FString Status;
        if (State == EOmniCaptureState::Finalizing)
        {
            Status = TEXT("Finalizing");
        }
        else
        {
            Status = TEXT("Idle");
        }

        if (!LastStillImagePath.IsEmpty())
        {
            Status += TEXT(" | Last Still: ") + LastStillImagePath;
        }

        if (ActiveWarnings.Num() > 0)
        {
            Status += TEXT(" | Warnings: ");
            Status += FString::Join(ActiveWarnings, TEXT("; "));
        }

        return Status;
    }

    FString Status;
    switch (State)
    {
    case EOmniCaptureState::Recording:
        Status = bDroppedFrames ? TEXT("Recording (Dropped Frames)") : TEXT("Recording");
        break;
    case EOmniCaptureState::Paused:
        Status = TEXT("Paused");
        break;
    case EOmniCaptureState::DroppedFrames:
        Status = TEXT("Recording (Dropped Frames)");
        break;
    case EOmniCaptureState::Finalizing:
        Status = TEXT("Finalizing");
        break;
    default:
        Status = TEXT("Idle");
        break;
    }

    Status += FString::Printf(TEXT(" | Frames:%d Pending:%d Dropped:%d Blocked:%d"), FrameCounter, LatestRingBufferStats.PendingFrames, LatestRingBufferStats.DroppedFrames, LatestRingBufferStats.BlockedPushes);
    Status += FString::Printf(TEXT(" | FPS:%.2f"), CurrentCaptureFPS);
    Status += FString::Printf(TEXT(" | Segment:%d"), CurrentSegmentIndex);

    Status += FString::Printf(TEXT(" | Audio Drift:%.2fms (Max %.2fms) Pending:%d"), AudioStats.DriftMilliseconds, AudioStats.MaxObservedDriftMilliseconds, AudioStats.PendingPackets);
    if (AudioStats.bInError)
    {
        Status += TEXT(" | AudioSyncError");
    }
    if (AudioRecorder)
    {
        Status += TEXT(" | ") + AudioRecorder->GetDebugStatus();
    }

    if (ActiveWarnings.Num() > 0)
    {
        Status += TEXT(" | Warnings: ");
        Status += FString::Join(ActiveWarnings, TEXT("; "));
    }

    return Status;
}

FOmniAudioSyncStats UOmniCaptureSubsystem::GetAudioSyncStats() const
{
    return AudioStats;
}

UTexture2D* UOmniCaptureSubsystem::GetPreviewTexture() const
{
    if (const AOmniCapturePreviewActor* Preview = PreviewActor.Get())
    {
        return Preview->GetPreviewTexture();
    }
    return nullptr;
}

void UOmniCaptureSubsystem::SetPreviewVisualizationMode(EOmniCapturePreviewView InView)
{
    ActiveSettings.PreviewVisualization = InView;
    OriginalSettings.PreviewVisualization = InView;

    if (AOmniCapturePreviewActor* Preview = PreviewActor.Get())
    {
        Preview->SetPreviewView(InView);
    }

    LastPreviewUpdateTime = 0.0;
}

void UOmniCaptureSubsystem::SetPendingRigTransform(const FTransform& InTransform)
{
    PendingRigTransform = InTransform;
    LastRigTransform = InTransform;
}

void UOmniCaptureSubsystem::CreateRig()
{
    DestroyRig();

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    if (AOmniCaptureRigActor* NewRig = World->SpawnActor<AOmniCaptureRigActor>(SpawnParams))
    {
        NewRig->Configure(ActiveSettings);
        ApplyRigTransform(NewRig);
        RigActor = NewRig;
    }
}

void UOmniCaptureSubsystem::ApplyRigTransform(AOmniCaptureRigActor* Rig)
{
    if (!Rig)
    {
        return;
    }

    if (PendingRigTransform.IsSet())
    {
        Rig->SetActorTransform(PendingRigTransform.GetValue());
        LastRigTransform = PendingRigTransform.GetValue();
        PendingRigTransform.Reset();
    }
    else
    {
        Rig->SetActorTransform(LastRigTransform);
    }
}

void UOmniCaptureSubsystem::DestroyRig()
{
    if (AActor* Rig = RigActor.Get())
    {
        Rig->Destroy();
    }
    RigActor.Reset();
}

void UOmniCaptureSubsystem::CreateTickActor()
{
    DestroyTickActor();

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    if (AOmniCaptureDirectorActor* Director = World->SpawnActor<AOmniCaptureDirectorActor>(SpawnParams))
    {
        Director->Initialize(this);
        TickActor = Director;
    }
}

void UOmniCaptureSubsystem::DestroyTickActor()
{
    if (AActor* Director = TickActor.Get())
    {
        Director->Destroy();
    }
    TickActor.Reset();
}

void UOmniCaptureSubsystem::SpawnPreviewActor()
{
    DestroyPreviewActor();

    if (!ActiveSettings.bEnablePreviewWindow)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    FActorSpawnParameters SpawnParameters;
    SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    if (AOmniCapturePreviewActor* Preview = World->SpawnActor<AOmniCapturePreviewActor>(SpawnParameters))
    {
        const FIntPoint OutputSize = ActiveSettings.GetOutputResolution();
        Preview->Initialize(ActiveSettings.PreviewScreenScale, OutputSize);
        Preview->SetPreviewEnabled(true);
        Preview->SetPreviewView(ActiveSettings.PreviewVisualization);
        if (RigActor.IsValid())
        {
            Preview->AttachToActor(RigActor.Get(), FAttachmentTransformRules::KeepWorldTransform);
            const float Offset = FMath::Max(OutputSize.X, OutputSize.Y) * 0.05f;
            Preview->SetActorLocation(RigActor->GetActorLocation() + FVector(Offset, 0.0f, 0.0f));
        }
        PreviewActor = Preview;
    }
}

void UOmniCaptureSubsystem::DestroyPreviewActor()
{
    if (AActor* Preview = PreviewActor.Get())
    {
        Preview->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
        Preview->Destroy();
    }
    PreviewActor.Reset();
}

void UOmniCaptureSubsystem::InitializeOutputWriters()
{
    RecordedVideoPath.Reset();
    bUsingNVENCImageFallback.Store(false);

    switch (ActiveSettings.OutputFormat)
    {
    case EOmniOutputFormat::ImageSequence:
        ImageWriter = MakeUnique<FOmniCaptureImageWriter>();
        ImageWriter->Initialize(ActiveSettings, ActiveSettings.OutputDirectory);
        AppendDiagnostic(EOmniCaptureDiagnosticLevel::Info, TEXT("Image sequence writer initialized."), TEXT("InitializeOutputs"));
        break;
    case EOmniOutputFormat::NVENCHardware:
        NVENCEncoder = MakeUnique<FOmniCaptureNVENCEncoder>();
        NVENCEncoder->Initialize(ActiveSettings, ActiveSettings.OutputDirectory);
        if (NVENCEncoder->IsInitialized())
        {
            RecordedVideoPath = NVENCEncoder->GetOutputFilePath();
            AppendDiagnostic(EOmniCaptureDiagnosticLevel::Info, FString::Printf(TEXT("NVENC output will be written to %s"), *RecordedVideoPath), TEXT("InitializeOutputs"));
        }
        else
        {
            const FString NvencError = NVENCEncoder->GetLastError().IsEmpty() ? TEXT("NVENC encoder failed to initialize.") : NVENCEncoder->GetLastError();
            LogDiagnosticMessage(ELogVerbosity::Error, TEXT("InitializeOutputs"), NvencError);
        }

        if (ActiveSettings.bAllowNVENCFallback)
        {
            ImageWriter = MakeUnique<FOmniCaptureImageWriter>();
            ImageWriter->Initialize(ActiveSettings, ActiveSettings.OutputDirectory);
            bUsingNVENCImageFallback.Store(true);
            AppendDiagnostic(EOmniCaptureDiagnosticLevel::Info, TEXT("Image sequence writer initialized for NVENC fallback."), TEXT("InitializeOutputs"));
        }
        break;
    default:
        break;
    }
}

void UOmniCaptureSubsystem::ShutdownOutputWriters(bool bFinalizeOutputs)
{
    if (ImageWriter)
    {
        ImageWriter->Flush();
        ImageWriter.Reset();
    }

    bUsingNVENCImageFallback.Store(false);

    if (NVENCEncoder)
    {
        if (bFinalizeOutputs)
        {
            NVENCEncoder->Finalize();
        }
        NVENCEncoder.Reset();
    }
}

void UOmniCaptureSubsystem::FinalizeOutputs(bool bFinalizeOutputs)
{
    SetDiagnosticContext(TEXT("FinalizeOutputs"));
    AppendDiagnostic(EOmniCaptureDiagnosticLevel::Info, FString::Printf(TEXT("Finalize outputs requested (Finalize=%s)."), bFinalizeOutputs ? TEXT("true") : TEXT("false")), TEXT("FinalizeOutputs"));
    bLastCaptureUsedImageSequenceFallback = false;
    LastImageSequenceFallbackDirectory.Reset();

    if (!bFinalizeOutputs)
    {
        CapturedFrameMetadata.Empty();
        CompletedSegments.Empty();
        RecordedAudioPath.Reset();
        RecordedVideoPath.Reset();
        LastFinalizedOutput.Empty();
        LastStillImagePath.Empty();
        OutputMuxer.Reset();
        RecordedSegmentDroppedFrames = 0;
        return;
    }

    if (CapturedFrameMetadata.Num() > 0)
    {
        CompleteActiveSegment(true);
    }

    if (CompletedSegments.Num() == 0)
    {
        LogDiagnosticMessage(ELogVerbosity::Warning, TEXT("FinalizeOutputs"), TEXT("FinalizeOutputs called with no captured frames"));
        OutputMuxer.Reset();
        RecordedAudioPath.Reset();
        RecordedVideoPath.Reset();
        LastFinalizedOutput.Empty();
        LastStillImagePath.Empty();
        return;
    }

    if (!OutputMuxer)
    {
        OutputMuxer = MakeUnique<FOmniCaptureMuxer>();
    }

    LastFinalizedOutput.Empty();

    for (const FOmniCaptureSegmentRecord& Segment : CompletedSegments)
    {
        if (!OutputMuxer)
        {
            break;
        }

        FOmniCaptureSettings SegmentSettings = ActiveSettings;
        SegmentSettings.OutputDirectory = Segment.Directory;
        SegmentSettings.OutputFileName = Segment.BaseFileName;

        OutputMuxer->Initialize(SegmentSettings, Segment.Directory);
        OutputMuxer->BeginRealtimeSession(SegmentSettings);

        const bool bMuxingExpected = SegmentSettings.OutputFormat != EOmniOutputFormat::ImageSequence;
        const bool bFallbackFromNVENC = (OriginalSettings.OutputFormat == EOmniOutputFormat::NVENCHardware && SegmentSettings.OutputFormat == EOmniOutputFormat::ImageSequence);
        const bool bSuccess = OutputMuxer->FinalizeCapture(SegmentSettings, Segment.Frames, Segment.AudioPath, Segment.VideoPath, Segment.DroppedFrames);
        OutputMuxer->EndRealtimeSession();

        const FString FinalVideoPath = Segment.Directory / (Segment.BaseFileName + TEXT(".mp4"));
        const bool bFinalFileExists = bMuxingExpected ? FPaths::FileExists(FinalVideoPath) : true;

        if (!bSuccess || (bMuxingExpected && !bFinalFileExists))
        {
            LogDiagnosticMessage(ELogVerbosity::Warning, TEXT("FinalizeOutputs"), FString::Printf(TEXT("Output muxing failed for segment %d. Check OmniCapture manifest for details."), Segment.SegmentIndex));
            if (Segment.bHasImageSequence)
            {
                LogDiagnosticMessage(ELogVerbosity::Warning, TEXT("FinalizeOutputs"), FString::Printf(TEXT("Image sequence frames saved to %s with base name %s."), *Segment.Directory, *Segment.BaseFileName));
                if (LastImageSequenceFallbackDirectory.IsEmpty())
                {
                    LastImageSequenceFallbackDirectory = Segment.Directory;
                }
                if (OriginalSettings.OutputFormat == EOmniOutputFormat::NVENCHardware)
                {
                    bLastCaptureUsedImageSequenceFallback = true;
                }
            }
            else
            {
                LogDiagnosticMessage(ELogVerbosity::Warning, TEXT("FinalizeOutputs"), TEXT("No image sequence fallback was recorded for this segment."));
            }
        }
        else if (Segment.bHasImageSequence)
        {
            if (!bMuxingExpected)
            {
                const ELogVerbosity::Type Verbosity = bFallbackFromNVENC ? ELogVerbosity::Warning : ELogVerbosity::Log;
                LogDiagnosticMessage(Verbosity, TEXT("FinalizeOutputs"), FString::Printf(TEXT("Image sequence frames saved to %s with base name %s."), *Segment.Directory, *Segment.BaseFileName));
                if (LastImageSequenceFallbackDirectory.IsEmpty())
                {
                    LastImageSequenceFallbackDirectory = Segment.Directory;
                }
                if (bFallbackFromNVENC)
                {
                    bLastCaptureUsedImageSequenceFallback = true;
                }
            }
            else if (SegmentSettings.OutputFormat == EOmniOutputFormat::NVENCHardware)
            {
                AppendDiagnostic(EOmniCaptureDiagnosticLevel::Info, FString::Printf(TEXT("Image sequence fallback saved alongside NVENC output in %s."), *Segment.Directory), TEXT("FinalizeOutputs"));
            }
        }

        LastFinalizedOutput = (bSuccess && bMuxingExpected && bFinalFileExists) ? FinalVideoPath : FString();
        if (!LastFinalizedOutput.IsEmpty())
        {
            AppendDiagnostic(EOmniCaptureDiagnosticLevel::Info, FString::Printf(TEXT("Muxed output ready: %s"), *LastFinalizedOutput), TEXT("FinalizeOutputs"));
        }

        if (SegmentSettings.bOpenPreviewOnFinalize && !LastFinalizedOutput.IsEmpty())
        {
            FPlatformProcess::LaunchFileInDefaultExternalApplication(*LastFinalizedOutput);
        }
    }

    CompletedSegments.Empty();
    CapturedFrameMetadata.Reset();
    RecordedAudioPath.Reset();
    RecordedVideoPath.Reset();
    OutputMuxer.Reset();
    RecordedSegmentDroppedFrames = 0;
}

bool UOmniCaptureSubsystem::ValidateEnvironment()
{
    bool bResult = true;

    const FString GpuBrand = FPlatformMisc::GetPrimaryGPUBrand();
    if (!GpuBrand.IsEmpty())
    {
        AddWarningUnique(FString::Printf(TEXT("GPU: %s"), *GpuBrand));
    }

#if PLATFORM_WINDOWS
    if (GDynamicRHI)
    {
        const ERHIInterfaceType InterfaceType = GDynamicRHI->GetInterfaceType();
        if (InterfaceType != ERHIInterfaceType::D3D11 && InterfaceType != ERHIInterfaceType::D3D12)
        {
            AddWarningUnique(TEXT("OmniCapture requires D3D11 or D3D12 for GPU capture. Current RHI is unsupported."));
            bResult = false;
        }
    }
    else
    {
        AddWarningUnique(TEXT("Unable to resolve active RHI interface. Zero-copy NVENC will be disabled."));
        bResult = false;
    }
#else
    AddWarningUnique(TEXT("OmniCapture NVENC pipeline is Windows-only; PNG sequence mode is recommended."));
    if (ActiveSettings.OutputFormat == EOmniOutputFormat::NVENCHardware)
    {
        bResult = false;
    }
#endif

    if (ActiveSettings.OutputFormat == EOmniOutputFormat::NVENCHardware)
    {
        const FOmniNVENCCapabilities Caps = FOmniCaptureNVENCEncoder::QueryCapabilities();
        if (!Caps.AdapterName.IsEmpty())
        {
            AddWarningUnique(FString::Printf(TEXT("Adapter: %s"), *Caps.AdapterName));
        }
        if (!Caps.DriverVersion.IsEmpty())
        {
            AddWarningUnique(FString::Printf(TEXT("Driver: %s"), *Caps.DriverVersion));
        }

        if (!Caps.bHardwareAvailable)
        {
            if (!Caps.HardwareFailureReason.IsEmpty())
            {
                AddWarningUnique(FString::Printf(TEXT("NVENC hardware encoder unavailable: %s"), *Caps.HardwareFailureReason));
            }
            else
            {
                AddWarningUnique(TEXT("NVENC hardware encoder unavailable"));
            }
            bResult = false;
        }
        if (ActiveSettings.Codec == EOmniCaptureCodec::HEVC && !Caps.bSupportsHEVC)
        {
            if (!Caps.CodecFailureReason.IsEmpty())
            {
                AddWarningUnique(FString::Printf(TEXT("HEVC codec unsupported: %s"), *Caps.CodecFailureReason));
            }
            else
            {
                AddWarningUnique(TEXT("HEVC codec unsupported by detected NVENC hardware"));
            }
            bResult = false;
        }
        if (ActiveSettings.NVENCColorFormat == EOmniCaptureColorFormat::P010 && !Caps.bSupports10Bit)
        {
            if (!Caps.P010FailureReason.IsEmpty())
            {
                AddWarningUnique(FString::Printf(TEXT("P010 / Main10 NVENC path unavailable: %s"), *Caps.P010FailureReason));
            }
            else
            {
                AddWarningUnique(TEXT("P010 / Main10 NVENC path unavailable on this GPU"));
            }
            bResult = false;
        }
        if (ActiveSettings.NVENCColorFormat == EOmniCaptureColorFormat::NV12 && !Caps.bSupportsNV12)
        {
            if (!Caps.NV12FailureReason.IsEmpty())
            {
                AddWarningUnique(FString::Printf(TEXT("NV12 NVENC path unavailable: %s"), *Caps.NV12FailureReason));
            }
            else
            {
                AddWarningUnique(TEXT("NV12 NVENC path unavailable on this GPU"));
            }
            bResult = false;
        }

        bool bCanQueryPixelFormat = true;
        EPixelFormat PixelFormat = PF_B8G8R8A8;

        if (ActiveSettings.NVENCColorFormat == EOmniCaptureColorFormat::NV12)
        {
#if defined(PF_NV12)
            PixelFormat = PF_NV12;
#else
            bCanQueryPixelFormat = false;
            AddWarningUnique(TEXT("NV12 NVENC path unsupported by this engine build"));
            bResult = false;
#endif
        }
        else if (ActiveSettings.NVENCColorFormat == EOmniCaptureColorFormat::P010)
        {
#if defined(PF_P010)
            PixelFormat = PF_P010;
#else
            bCanQueryPixelFormat = false;
            AddWarningUnique(TEXT("P010 / Main10 NVENC path unavailable on this engine build"));
            bResult = false;
#endif
        }

        if (bCanQueryPixelFormat && !GPixelFormats[PixelFormat].Supported)
        {
            AddWarningUnique(TEXT("Requested NVENC pixel format is not supported by the active RHI"));
            bResult = false;
        }

        if (ActiveSettings.bZeroCopy)
        {
#if PLATFORM_WINDOWS
            if (!GDynamicRHI || (GDynamicRHI->GetInterfaceType() != ERHIInterfaceType::D3D11 && GDynamicRHI->GetInterfaceType() != ERHIInterfaceType::D3D12))
            {
                AddWarningUnique(TEXT("Zero-copy NVENC requires D3D11 or D3D12; zero-copy will be disabled."));
                bResult = false;
            }
#else
            AddWarningUnique(TEXT("Zero-copy NVENC is only available on Windows/D3D; zero-copy will be disabled."));
            bResult = false;
#endif
        }
    }

    FString ResolvedFFmpeg;
    if (!FOmniCaptureMuxer::IsFFmpegAvailable(ActiveSettings, &ResolvedFFmpeg))
    {
        AddWarningUnique(TEXT("FFmpeg not detected - automatic muxing disabled"));
    }
    else if (!ResolvedFFmpeg.IsEmpty() && !ResolvedFFmpeg.Equals(TEXT("ffmpeg"), ESearchCase::IgnoreCase))
    {
        AddWarningUnique(FString::Printf(TEXT("FFmpeg: %s"), *ResolvedFFmpeg));
    }

    uint64 FreeBytes = 0;
    uint64 TotalBytes = 0;
    if (FPlatformMisc::GetDiskTotalAndFreeSpace(*ActiveSettings.OutputDirectory, TotalBytes, FreeBytes))
    {
        const uint64 MinFreeBytes = static_cast<uint64>(FMath::Max(0, ActiveSettings.MinimumFreeDiskSpaceGB)) * 1024ull * 1024ull * 1024ull;
        if (MinFreeBytes > 0 && FreeBytes < MinFreeBytes)
        {
            AddWarningUnique(OmniCapture::WarningLowDisk);
        }
    }
    else
    {
        AddWarningUnique(TEXT("Unable to query disk space for capture output"));
    }

    return bResult;
}

bool UOmniCaptureSubsystem::ApplyFallbacks(FString* OutFailureReason)
{
    if (OutFailureReason)
    {
        OutFailureReason->Reset();
    }

    if (ActiveSettings.OutputFormat == EOmniOutputFormat::NVENCHardware)
    {
#if !PLATFORM_WINDOWS
        AddWarningUnique(TEXT("NVENC output is not supported on this platform; switching to PNG sequence."));
        ActiveSettings.OutputFormat = EOmniOutputFormat::ImageSequence;
        return true;
#endif

        const FOmniNVENCCapabilities Caps = FOmniCaptureNVENCEncoder::QueryCapabilities();

        if (!Caps.bHardwareAvailable)
        {
            const FString Reason = Caps.HardwareFailureReason.IsEmpty() ? TEXT("NVENC is unavailable") : Caps.HardwareFailureReason;
            if (ActiveSettings.bAllowNVENCFallback)
            {
                AddWarningUnique(FString::Printf(TEXT("Falling back to PNG sequence because NVENC is unavailable: %s"), *Reason));
                ActiveSettings.OutputFormat = EOmniOutputFormat::ImageSequence;
                return true;
            }

            AddWarningUnique(FString::Printf(TEXT("NVENC required but unavailable: %s"), *Reason));
            if (OutFailureReason)
            {
                *OutFailureReason = Reason;
            }
            return false;
        }

        if (ActiveSettings.Codec == EOmniCaptureCodec::HEVC && !Caps.bSupportsHEVC)
        {
            const FString Reason = Caps.CodecFailureReason.IsEmpty() ? TEXT("HEVC unsupported - falling back to H.264") : FString::Printf(TEXT("HEVC unsupported (%s) - falling back to H.264"), *Caps.CodecFailureReason);
            AddWarningUnique(Reason);
            ActiveSettings.Codec = EOmniCaptureCodec::H264;
        }

        if (ActiveSettings.NVENCColorFormat == EOmniCaptureColorFormat::P010 && !Caps.bSupports10Bit)
        {
            const FString Reason = Caps.P010FailureReason.IsEmpty() ? TEXT("P010 unsupported - switching to NV12") : FString::Printf(TEXT("P010 unsupported (%s) - switching to NV12"), *Caps.P010FailureReason);
            AddWarningUnique(Reason);
            ActiveSettings.NVENCColorFormat = EOmniCaptureColorFormat::NV12;
        }

        if (ActiveSettings.NVENCColorFormat == EOmniCaptureColorFormat::NV12 && !Caps.bSupportsNV12)
        {
            const FString Reason = Caps.NV12FailureReason.IsEmpty() ? TEXT("NV12 unsupported - switching to BGRA") : FString::Printf(TEXT("NV12 unsupported (%s) - switching to BGRA"), *Caps.NV12FailureReason);
            AddWarningUnique(Reason);
            ActiveSettings.NVENCColorFormat = EOmniCaptureColorFormat::BGRA;
        }

        if (!FOmniCaptureNVENCEncoder::SupportsColorFormat(ActiveSettings.NVENCColorFormat))
        {
            AddWarningUnique(TEXT("Requested NVENC color format unavailable - switching to BGRA"));
            ActiveSettings.NVENCColorFormat = EOmniCaptureColorFormat::BGRA;
        }

        if (ActiveSettings.bZeroCopy)
        {
#if PLATFORM_WINDOWS
            if (!GDynamicRHI || (GDynamicRHI->GetInterfaceType() != ERHIInterfaceType::D3D11 && GDynamicRHI->GetInterfaceType() != ERHIInterfaceType::D3D12))
            {
                AddWarningUnique(TEXT("Zero-copy not supported on this RHI - disabling zero-copy"));
                ActiveSettings.bZeroCopy = false;
            }
#else
            AddWarningUnique(TEXT("Zero-copy NVENC disabled on this platform"));
            ActiveSettings.bZeroCopy = false;
#endif
        }
    }

    return true;
}

void UOmniCaptureSubsystem::InitializeAudioRecording()
{
    const bool bIsPNGSequence = ActiveSettings.OutputFormat == EOmniOutputFormat::ImageSequence &&
        ActiveSettings.ImageFormat == EOmniCaptureImageFormat::PNG;

    if (bIsPNGSequence)
    {
        if (ActiveSettings.bRecordAudio)
        {
            AddWarningUnique(TEXT("Audio recording is disabled for PNG image sequences to prevent extended A/V drift."));
        }
        return;
    }

    if (!ActiveSettings.bRecordAudio)
    {
        return;
    }

    SetDiagnosticContext(TEXT("Audio"));
    AppendDiagnostic(EOmniCaptureDiagnosticLevel::Info, TEXT("Initializing audio recording."), TEXT("Audio"));

    UWorld* World = GetWorld();
    if (!World)
    {
        LogDiagnosticMessage(ELogVerbosity::Warning, TEXT("Audio"), TEXT("Audio recording skipped - invalid world context."));
        return;
    }

    AudioRecorder = MakeUnique<FOmniCaptureAudioRecorder>();
    if (AudioRecorder->Initialize(World, ActiveSettings))
    {
        AudioRecorder->Start();
        AppendDiagnostic(EOmniCaptureDiagnosticLevel::Info, TEXT("Audio recorder started."), TEXT("Audio"));
    }
    else
    {
        LogDiagnosticMessage(ELogVerbosity::Warning, TEXT("Audio"), TEXT("Failed to initialize audio recorder."));
        AudioRecorder.Reset();
    }
}

void UOmniCaptureSubsystem::ShutdownAudioRecording()
{
    if (!AudioRecorder)
    {
        return;
    }

    AudioRecorder->Stop(ActiveSettings.OutputDirectory, ActiveSettings.OutputFileName);
    RecordedAudioPath = AudioRecorder->GetOutputFilePath();
    if (!RecordedAudioPath.IsEmpty())
    {
        LogDiagnosticMessage(ELogVerbosity::Log, TEXT("Audio"), FString::Printf(TEXT("Audio recording saved to %s"), *RecordedAudioPath));
    }
    AudioRecorder.Reset();
}

void UOmniCaptureSubsystem::TickCapture(float DeltaTime)
{
    if (!bIsCapturing)
    {
        return;
    }

    if (!bIsPaused)
    {
        UpdateDynamicStereoParameters();
        RotateSegmentIfNeeded();
        CaptureFrame();
    }

    UpdateRuntimeWarnings();
}

void UOmniCaptureSubsystem::CaptureFrame()
{
    if (!RigActor.IsValid() || !RingBuffer)
    {
        HandleDroppedFrame();
        return;
    }

    FOmniEyeCapture LeftEye;
    FOmniEyeCapture RightEye;
    RigActor->Capture(LeftEye, RightEye);

    FlushRenderingCommands();

    auto ConvertActiveFrame = [](const FOmniCaptureSettings& CaptureSettings, const FOmniEyeCapture& Left, const FOmniEyeCapture& Right)
    {
        if (CaptureSettings.IsPlanar())
        {
            return FOmniCaptureEquirectConverter::ConvertToPlanar(CaptureSettings, Left);
        }

        if (CaptureSettings.IsFisheye() && !CaptureSettings.ShouldConvertFisheyeToEquirect())
        {
            return FOmniCaptureEquirectConverter::ConvertToFisheye(CaptureSettings, Left, Right);
        }

        return FOmniCaptureEquirectConverter::ConvertToEquirectangular(CaptureSettings, Left, Right);
    };

    FOmniCaptureEquirectResult ConversionResult = ConvertActiveFrame(ActiveSettings, LeftEye, RightEye);

    TMap<FName, FOmniCaptureLayerPayload> AuxiliaryLayers;
    if (ActiveSettings.AuxiliaryPasses.Num() > 0)
    {
        auto BuildAuxiliaryEye = [](const FOmniEyeCapture& SourceEye, EOmniCaptureAuxiliaryPassType PassType)
        {
            FOmniEyeCapture AuxEye;
            AuxEye.ActiveFaceCount = SourceEye.ActiveFaceCount;
            for (int32 FaceIndex = 0; FaceIndex < AuxEye.ActiveFaceCount && FaceIndex < UE_ARRAY_COUNT(AuxEye.Faces); ++FaceIndex)
            {
                AuxEye.Faces[FaceIndex].RenderTarget = SourceEye.Faces[FaceIndex].GetAuxiliaryRenderTarget(PassType);
            }
            return AuxEye;
        };

        for (EOmniCaptureAuxiliaryPassType PassType : ActiveSettings.AuxiliaryPasses)
        {
            if (PassType == EOmniCaptureAuxiliaryPassType::None)
            {
                continue;
            }

            const FOmniEyeCapture AuxLeft = BuildAuxiliaryEye(LeftEye, PassType);
            const FOmniEyeCapture AuxRight = BuildAuxiliaryEye(RightEye, PassType);
            FOmniCaptureEquirectResult AuxResult = ConvertActiveFrame(ActiveSettings, AuxLeft, AuxRight);
            if (AuxResult.PixelData.IsValid())
            {
                FOmniCaptureLayerPayload Payload;
                Payload.PixelData = MoveTemp(AuxResult.PixelData);
                Payload.bLinear = AuxResult.bIsLinear;
                Payload.Precision = AuxResult.PixelPrecision;
                Payload.PixelDataType = AuxResult.PixelDataType;
                AuxiliaryLayers.Add(GetAuxiliaryLayerName(PassType), MoveTemp(Payload));
            }
        }
    }
    const bool bRequiresGPU = ActiveSettings.OutputFormat == EOmniOutputFormat::NVENCHardware;
    if (!ConversionResult.PixelData.IsValid())
    {
        HandleDroppedFrame();
        return;
    }

    if (bRequiresGPU && !ConversionResult.Texture.IsValid())
    {
        HandleDroppedFrame();
        return;
    }

    TUniquePtr<FOmniCaptureFrame> Frame = MakeUnique<FOmniCaptureFrame>();
    Frame->Metadata.FrameIndex = FrameCounter++;
    Frame->Metadata.Timecode = FPlatformTime::Seconds() - CaptureStartTime;
    Frame->Metadata.bKeyFrame = (Frame->Metadata.FrameIndex % ActiveSettings.Quality.GOPLength) == 0;

    ++FramesSinceLastFpsSample;
    const double NowSeconds = FPlatformTime::Seconds();
    if (LastFpsSampleTime <= 0.0)
    {
        LastFpsSampleTime = NowSeconds;
    }
    const double SampleElapsed = NowSeconds - LastFpsSampleTime;
    if (SampleElapsed >= 1.0)
    {
        const double SafeElapsed = FMath::Max(SampleElapsed, KINDA_SMALL_NUMBER);
        CurrentCaptureFPS = static_cast<double>(FramesSinceLastFpsSample) / SafeElapsed;
        FramesSinceLastFpsSample = 0;
        LastFpsSampleTime = NowSeconds;
    }

    Frame->PixelData = MoveTemp(ConversionResult.PixelData);
    Frame->GPUSource = ConversionResult.OutputTarget;
    Frame->Texture = ConversionResult.Texture;
    Frame->ReadyFence = ConversionResult.ReadyFence;
    Frame->bLinearColor = ConversionResult.bIsLinear;
    Frame->bUsedCPUFallback = ConversionResult.bUsedCPUFallback;
    Frame->PixelDataType = ConversionResult.PixelDataType;
    Frame->PixelPrecision = ConversionResult.PixelPrecision;
    Frame->EncoderTextures.Reset();
    Frame->AuxiliaryLayers = MoveTemp(AuxiliaryLayers);
    for (const TRefCountPtr<IPooledRenderTarget>& Plane : ConversionResult.EncoderPlanes)
    {
        if (!Plane.IsValid())
        {
            continue;
        }

        if (FRHITexture* PlaneTexture = Plane->GetRHI())
        {
            Frame->EncoderTextures.Add(PlaneTexture);
        }
    }
    if (Frame->EncoderTextures.Num() == 0 && Frame->Texture.IsValid())
    {
        Frame->EncoderTextures.Add(Frame->Texture);
    }

    if (AudioRecorder)
    {
        AudioRecorder->GatherAudio(Frame->Metadata.Timecode, Frame->AudioPackets);
    }

    CapturedFrameMetadata.Add(Frame->Metadata);

    if (ImageWriter && (ActiveSettings.OutputFormat == EOmniOutputFormat::ImageSequence || bUsingNVENCImageFallback.Load()))
    {
        bCapturedImageSequenceThisSegment = true;
    }

    RingBuffer->Enqueue(MoveTemp(Frame));

    if (RingBuffer)
    {
        LatestRingBufferStats = RingBuffer->GetStats();
    }

    if (PreviewActor.IsValid())
    {
        const double Now = FPlatformTime::Seconds();
        if (PreviewFrameInterval <= 0.0 || (Now - LastPreviewUpdateTime) >= PreviewFrameInterval)
        {
            PreviewActor->UpdatePreviewTexture(ConversionResult, ActiveSettings);
            LastPreviewUpdateTime = Now;
        }
    }
}

void UOmniCaptureSubsystem::FlushRingBuffer()
{
    if (RingBuffer)
    {
        RingBuffer->Flush();
    }
}

void UOmniCaptureSubsystem::UpdateDynamicStereoParameters()
{
    if (!RigActor.IsValid())
    {
        return;
    }

    float TargetIPD = ActiveSettings.InterPupillaryDistanceCm;
    float TargetConvergence = ActiveSettings.EyeConvergenceDistanceCm;

    const double Now = FPlatformTime::Seconds();
    const float ElapsedSeconds = static_cast<float>(Now - DynamicParameterStartTime);

    if (ActiveSettings.InterpupillaryDistanceCurve)
    {
        TargetIPD = FMath::Max(0.0f, ActiveSettings.InterpupillaryDistanceCurve->GetFloatValue(ElapsedSeconds));
    }

    if (ActiveSettings.EyeConvergenceCurve)
    {
        TargetConvergence = FMath::Max(0.0f, ActiveSettings.EyeConvergenceCurve->GetFloatValue(ElapsedSeconds));
    }

    if (!FMath::IsNearlyEqual(TargetIPD, LastDynamicInterPupillaryDistance) || !FMath::IsNearlyEqual(TargetConvergence, LastDynamicConvergence))
    {
        if (AOmniCaptureRigActor* Rig = RigActor.Get())
        {
            Rig->UpdateStereoParameters(TargetIPD, TargetConvergence);
        }

        LastDynamicInterPupillaryDistance = TargetIPD;
        LastDynamicConvergence = TargetConvergence;
        ActiveSettings.InterPupillaryDistanceCm = TargetIPD;
        ActiveSettings.EyeConvergenceDistanceCm = TargetConvergence;
    }
}

void UOmniCaptureSubsystem::ApplyRenderFeatureOverrides()
{
    const FOmniCaptureRenderFeatureOverrides& Overrides = ActiveSettings.RenderingOverrides;

    ConsoleOverrideRecords.Reset();
    bRenderOverridesApplied = false;

    const FString StepName = TEXT("RenderOverrides");

    auto WarnUnsupported = [this, &StepName](const FString& Message)
    {
        if (Message.IsEmpty())
        {
            return;
        }

        AddWarningUnique(Message);
        LogDiagnosticMessage(ELogVerbosity::Warning, StepName, Message);
    };

    const URendererSettings* RendererSettings = GetDefault<URendererSettings>();

    bool bRayTracingSupported = false;
    FString RayTracingFailureReason;

#if RHI_RAYTRACING
    bRayTracingSupported = GRHISupportsRayTracing;
    if (!bRayTracingSupported)
    {
        RayTracingFailureReason = TEXT("Hardware or RHI does not support ray tracing.");
    }
#if UE_VERSION_OLDER_THAN(5, 5, 0)
    else if (RendererSettings && !RendererSettings->bSupportRayTracing)
    {
        bRayTracingSupported = false;
        RayTracingFailureReason = TEXT("Ray tracing disabled in project renderer settings.");
    }
#endif
    else if (!IsRayTracingAllowed())
    {
        bRayTracingSupported = false;
        RayTracingFailureReason = TEXT("Ray tracing disabled by runtime configuration.");
    }
#else
    RayTracingFailureReason = TEXT("Engine build lacks ray tracing support.");
#endif

    bool bPathTracingSupported = false;
    FString PathTracingFailureReason;

#if UE_VERSION_OLDER_THAN(5, 5, 0)
    const bool bProjectPathTracingEnabled = !RendererSettings || RendererSettings->bSupportPathTracing;
#else
    const bool bProjectPathTracingEnabled = true;
#endif

    if (!bProjectPathTracingEnabled)
    {
        PathTracingFailureReason = TEXT("Path tracing disabled in project renderer settings.");
    }
    else
    {
#if RHI_RAYTRACING
        if (!bRayTracingSupported)
        {
            PathTracingFailureReason = TEXT("Path tracing requires ray tracing support.");
        }
        else if (!FDataDrivenShaderPlatformInfo::GetSupportsPathTracing(GMaxRHIShaderPlatform))
        {
            PathTracingFailureReason = TEXT("Path tracing unsupported on the active shader platform.");
        }
        else
        {
            bPathTracingSupported = true;
        }
#else
        PathTracingFailureReason = TEXT("Engine build lacks ray tracing support required for path tracing.");
#endif
    }

    bool bLumenSupported = false;
    FString LumenFailureReason;

#if UE_VERSION_OLDER_THAN(5, 5, 0)
    const bool bProjectLumenEnabled = !RendererSettings || RendererSettings->bSupportLumen;
#else
    const bool bProjectLumenEnabled = true;
#endif

    if (!bProjectLumenEnabled)
    {
        LumenFailureReason = TEXT("Lumen disabled in project renderer settings.");
    }
    else if (!FDataDrivenShaderPlatformInfo::GetSupportsLumenGI(GMaxRHIShaderPlatform))
    {
        LumenFailureReason = TEXT("Lumen unsupported on the active shader platform.");
    }
    else
    {
        bLumenSupported = true;
    }

    bool bDLSSSupported = false;
    FString DLSSFailureReason;

    if (IConsoleManager::Get().FindConsoleVariable(TEXT("r.NGX.DLSS.Enable")))
    {
        bDLSSSupported = true;
    }
    else
    {
        DLSSFailureReason = TEXT("DLSS console variables not found; NGX runtime unavailable.");
    }

    const bool bApplyRayTracing = Overrides.bForceRayTracing && bRayTracingSupported;
    if (Overrides.bForceRayTracing && !bApplyRayTracing)
    {
        WarnUnsupported(TEXT("Ray tracing override ignored: ") + RayTracingFailureReason);
    }

    const bool bApplyPathTracing = Overrides.bForcePathTracing && bPathTracingSupported;
    if (Overrides.bForcePathTracing && !bApplyPathTracing)
    {
        WarnUnsupported(TEXT("Path tracing override ignored: ") + PathTracingFailureReason);
    }

    const bool bApplyLumen = Overrides.bForceLumen && bLumenSupported;
    if (Overrides.bForceLumen && !bApplyLumen)
    {
        WarnUnsupported(TEXT("Lumen override ignored: ") + LumenFailureReason);
    }

    const bool bApplyDLSS = Overrides.bEnableDLSS && bDLSSSupported;
    if (Overrides.bEnableDLSS && !bApplyDLSS)
    {
        WarnUnsupported(TEXT("DLSS override ignored: ") + DLSSFailureReason);
    }

    auto ApplyStringOverride = [this](bool bShouldApply, const TCHAR* Name, const TCHAR* Value)
    {
        if (!bShouldApply)
        {
            return;
        }

        if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
        {
            FConsoleVariableOverrideRecord Record;
            Record.Variable = Var;
            Record.PreviousValue = Var->GetString();
            ConsoleOverrideRecords.Add(Record);
            Var->Set(Value, ECVF_SetByCode);
        }
    };

    auto ApplyNumericOverride = [this](bool bShouldApply, const TCHAR* Name, int32 Value)
    {
        if (!bShouldApply)
        {
            return;
        }

        if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
        {
            FConsoleVariableOverrideRecord Record;
            Record.Variable = Var;
            Record.PreviousValue = Var->GetString();
            ConsoleOverrideRecords.Add(Record);
            Var->Set(Value, ECVF_SetByCode);
        }
    };

    auto ApplyFloatOverride = [this](bool bShouldApply, const TCHAR* Name, float Value)
    {
        if (!bShouldApply)
        {
            return;
        }

        if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
        {
            FConsoleVariableOverrideRecord Record;
            Record.Variable = Var;
            Record.PreviousValue = Var->GetString();
            ConsoleOverrideRecords.Add(Record);
            Var->Set(Value, ECVF_SetByCode);
        }
    };

    ApplyStringOverride(bApplyRayTracing, TEXT("r.RayTracing.Force"), TEXT("1"));
    ApplyStringOverride(bApplyPathTracing, TEXT("r.PathTracing"), TEXT("1"));
    ApplyStringOverride(bApplyPathTracing, TEXT("r.PathTracing.Enable"), TEXT("1"));
    ApplyStringOverride(bApplyLumen, TEXT("r.Lumen.HardwareRayTracing"), TEXT("1"));
    ApplyStringOverride(bApplyLumen, TEXT("r.Lumen.ScreenProbeGather"), TEXT("1"));
    ApplyStringOverride(bApplyDLSS, TEXT("r.NGX.DLSS.Enable"), TEXT("1"));
    ApplyNumericOverride(Overrides.bEnableBloom, TEXT("r.DefaultFeature.Bloom"), 1);
    ApplyNumericOverride(Overrides.bEnableBloom, TEXT("r.BloomQuality"), 5);
    ApplyNumericOverride(Overrides.bEnableAntiAliasing, TEXT("r.DefaultFeature.AntiAliasing"), 2);
    ApplyNumericOverride(Overrides.bEnableAntiAliasing, TEXT("r.AntiAliasingMethod"), 4);

    const bool bOffline = ActiveSettings.bEnableOfflineSampling;
    ApplyNumericOverride(bOffline, TEXT("r.MoviePipeline.WarmUpCount"), ActiveSettings.WarmUpFrameCount);
    ApplyNumericOverride(bOffline, TEXT("r.SpatialSampleCount"), ActiveSettings.SpatialSampleCount);
    ApplyNumericOverride(bOffline, TEXT("r.TemporalAASamples"), ActiveSettings.TemporalSampleCount);
    ApplyNumericOverride(bOffline, TEXT("r.PathTracing.SamplesPerPixel"), ActiveSettings.TemporalSampleCount);
    ApplyFloatOverride(bOffline, TEXT("r.SecondaryScreenPercentage.MoviePipeline"), 100.0f);

    bRenderOverridesApplied = ConsoleOverrideRecords.Num() > 0;
}

void UOmniCaptureSubsystem::RestoreRenderFeatureOverrides()
{
    if (bRenderOverridesApplied)
    {
        for (FConsoleVariableOverrideRecord& Record : ConsoleOverrideRecords)
        {
            if (Record.Variable)
            {
                Record.Variable->Set(*Record.PreviousValue, ECVF_SetByCode);
            }
        }
    }

    ConsoleOverrideRecords.Reset();
    bRenderOverridesApplied = false;
    DynamicParameterStartTime = 0.0;
    LastDynamicInterPupillaryDistance = -1.0f;
    LastDynamicConvergence = -1.0f;
}

void UOmniCaptureSubsystem::HandleDroppedFrame()
{
    bDroppedFrames = true;
    State = EOmniCaptureState::DroppedFrames;
    ++DroppedFrameCount;
    AddWarningUnique(OmniCapture::WarningFrameDrop);
    LogDiagnosticMessage(ELogVerbosity::Warning, TEXT("CaptureLoop"), TEXT("OmniCapture frame dropped"));
}

void UOmniCaptureSubsystem::ConfigureActiveSegment()
{
    const FString SegmentSuffix = (CurrentSegmentIndex == 0)
        ? FString()
        : FString::Printf(TEXT("_seg%02d"), CurrentSegmentIndex);

    FString SegmentDirectory = BaseOutputDirectory;
    if (ActiveSettings.bCreateSegmentSubfolders)
    {
        SegmentDirectory = BaseOutputDirectory / FString::Printf(TEXT("Segment_%02d"), CurrentSegmentIndex);
    }

    ActiveSettings.OutputDirectory = SegmentDirectory;
    ActiveSettings.OutputFileName = BaseOutputFileName + SegmentSuffix;

    IFileManager::Get().MakeDirectory(*ActiveSettings.OutputDirectory, true);

    CapturedFrameMetadata.Empty();
    RecordedAudioPath.Reset();
    RecordedVideoPath.Reset();
    bCapturedImageSequenceThisSegment = false;

    CurrentSegmentStartTime = FPlatformTime::Seconds();
    LastSegmentSizeCheckTime = CurrentSegmentStartTime;
}

void UOmniCaptureSubsystem::RotateSegmentIfNeeded()
{
    if (!bIsCapturing)
    {
        return;
    }

    const double Now = FPlatformTime::Seconds();
    bool bShouldRotate = false;

    if (ActiveSettings.SegmentDurationSeconds > 0.0f)
    {
        const double SegmentElapsed = Now - CurrentSegmentStartTime;
        if (SegmentElapsed >= ActiveSettings.SegmentDurationSeconds)
        {
            bShouldRotate = true;
        }
    }

    if (!bShouldRotate && ActiveSettings.SegmentFrameCount > 0)
    {
        if (CapturedFrameMetadata.Num() >= ActiveSettings.SegmentFrameCount)
        {
            bShouldRotate = true;
        }
    }

    if (!bShouldRotate && ActiveSettings.SegmentSizeLimitMB > 0)
    {
        if ((Now - LastSegmentSizeCheckTime) >= 1.0)
        {
            LastSegmentSizeCheckTime = Now;
            const int64 SegmentBytes = CalculateActiveSegmentSizeBytes();
            const int64 LimitBytes = static_cast<int64>(ActiveSettings.SegmentSizeLimitMB) * 1024 * 1024;
            if (LimitBytes > 0 && SegmentBytes >= LimitBytes)
            {
                bShouldRotate = true;
            }
        }
    }

    if (!bShouldRotate || CapturedFrameMetadata.Num() == 0)
    {
        return;
    }

    LogDiagnosticMessage(ELogVerbosity::Log, TEXT("SegmentRotation"), FString::Printf(TEXT("Rotating capture segment -> %d"), CurrentSegmentIndex + 1));

    if (RingBuffer)
    {
        RingBuffer->Flush();
    }

    if (OutputMuxer)
    {
        OutputMuxer->EndRealtimeSession();
    }

    ShutdownAudioRecording();
    ShutdownOutputWriters(true);
    CompleteActiveSegment(true);

    ++CurrentSegmentIndex;
    ConfigureActiveSegment();

    InitializeOutputWriters();

    if (!OutputMuxer)
    {
        OutputMuxer = MakeUnique<FOmniCaptureMuxer>();
    }

    if (OutputMuxer)
    {
        OutputMuxer->Initialize(ActiveSettings, ActiveSettings.OutputDirectory);
        OutputMuxer->BeginRealtimeSession(ActiveSettings);
        AudioStats = FOmniAudioSyncStats();
    }

    InitializeAudioRecording();

    CurrentSegmentStartTime = FPlatformTime::Seconds();
    LastSegmentSizeCheckTime = CurrentSegmentStartTime;
    LastFpsSampleTime = 0.0;
    FramesSinceLastFpsSample = 0;
}

void UOmniCaptureSubsystem::CompleteActiveSegment(bool bStoreResults)
{
    if (!bStoreResults)
    {
        CapturedFrameMetadata.Empty();
        RecordedAudioPath.Reset();
        RecordedVideoPath.Reset();
        bCapturedImageSequenceThisSegment = false;
        return;
    }

    if (CapturedFrameMetadata.Num() == 0)
    {
        CapturedFrameMetadata.Empty();
        RecordedAudioPath.Reset();
        RecordedVideoPath.Reset();
        bCapturedImageSequenceThisSegment = false;
        return;
    }

    FOmniCaptureSegmentRecord SegmentRecord;
    SegmentRecord.SegmentIndex = CurrentSegmentIndex;
    SegmentRecord.Directory = ActiveSettings.OutputDirectory;
    SegmentRecord.BaseFileName = ActiveSettings.OutputFileName;
    SegmentRecord.AudioPath = RecordedAudioPath;
    SegmentRecord.VideoPath = RecordedVideoPath;
    const int32 TotalDroppedFrames = DroppedFrameCount;
    const int32 SegmentDroppedFrames = FMath::Max(0, TotalDroppedFrames - RecordedSegmentDroppedFrames);
    SegmentRecord.DroppedFrames = SegmentDroppedFrames;
    RecordedSegmentDroppedFrames = TotalDroppedFrames;
    SegmentRecord.Frames = MoveTemp(CapturedFrameMetadata);
    SegmentRecord.bHasImageSequence = bCapturedImageSequenceThisSegment || ActiveSettings.OutputFormat == EOmniOutputFormat::ImageSequence;

    CompletedSegments.Add(MoveTemp(SegmentRecord));

    CapturedFrameMetadata.Reset();
    RecordedAudioPath.Reset();
    RecordedVideoPath.Reset();
    bCapturedImageSequenceThisSegment = false;
}

int64 UOmniCaptureSubsystem::CalculateActiveSegmentSizeBytes() const
{
    int64 TotalBytes = 0;
    IFileManager& FileManager = IFileManager::Get();

    if (ActiveSettings.OutputFormat == EOmniOutputFormat::NVENCHardware)
    {
        if (!RecordedVideoPath.IsEmpty())
        {
            const int64 BitstreamSize = FileManager.FileSize(*RecordedVideoPath);
            if (BitstreamSize > 0)
            {
                TotalBytes += BitstreamSize;
            }
        }
    }
    else
    {
        class FSegmentStatVisitor final : public IPlatformFile::FDirectoryStatVisitor
        {
        public:
            FSegmentStatVisitor(const FString& InPrefix, int64& InTotal)
                : Prefix(InPrefix)
                , Total(InTotal)
            {
            }

            virtual bool Visit(const TCHAR* FilenameOrDirectory, const FFileStatData& StatData) override
            {
                if (StatData.bIsDirectory)
                {
                    return true;
                }

                const FString FileName = FPaths::GetCleanFilename(FilenameOrDirectory);
                if (Prefix.IsEmpty() || FileName.StartsWith(Prefix))
                {
                    Total += StatData.FileSize;
                }
                return true;
            }

        private:
            FString Prefix;
            int64& Total;
        };

        FSegmentStatVisitor Visitor(ActiveSettings.OutputFileName, TotalBytes);
        FileManager.IterateDirectoryStat(*ActiveSettings.OutputDirectory, Visitor);
    }

    if (!RecordedAudioPath.IsEmpty())
    {
        const int64 AudioSize = FileManager.FileSize(*RecordedAudioPath);
        if (AudioSize > 0)
        {
            TotalBytes += AudioSize;
        }
    }

    return TotalBytes;
}

void UOmniCaptureSubsystem::UpdateRuntimeWarnings()
{
    const double Now = FPlatformTime::Seconds();
    if ((Now - LastRuntimeWarningCheckTime) < 1.0)
    {
        return;
    }

    LastRuntimeWarningCheckTime = Now;

    if (ActiveSettings.MinimumFreeDiskSpaceGB > 0)
    {
        uint64 FreeBytes = 0;
        uint64 TotalBytes = 0;
        const uint64 ThresholdBytes = static_cast<uint64>(ActiveSettings.MinimumFreeDiskSpaceGB) * 1024ull * 1024ull * 1024ull;
        if (ThresholdBytes > 0 && FPlatformMisc::GetDiskTotalAndFreeSpace(*ActiveSettings.OutputDirectory, TotalBytes, FreeBytes))
        {
            if (FreeBytes < ThresholdBytes)
            {
                AddWarningUnique(OmniCapture::WarningLowDisk);
            }
            else
            {
                RemoveWarning(OmniCapture::WarningLowDisk);
            }
        }
    }

    if (ActiveSettings.TargetFrameRate > 0.0f)
    {
        const double ThresholdFps = ActiveSettings.TargetFrameRate * FMath::Clamp(static_cast<double>(ActiveSettings.LowFrameRateWarningRatio), 0.1, 1.0);
        if (!bIsPaused && CurrentCaptureFPS > 0.0 && CurrentCaptureFPS < ThresholdFps)
        {
            AddWarningUnique(OmniCapture::WarningLowFps);
        }
        else
        {
            RemoveWarning(OmniCapture::WarningLowFps);
            if (!bDroppedFrames)
            {
                RemoveWarning(OmniCapture::WarningFrameDrop);
            }
        }
    }
}

void UOmniCaptureSubsystem::AddWarningUnique(const FString& Warning)
{
    if (!Warning.IsEmpty())
    {
        if (!ActiveWarnings.Contains(Warning))
        {
            ActiveWarnings.Add(Warning);
            AppendDiagnostic(EOmniCaptureDiagnosticLevel::Warning, FString::Printf(TEXT("Warning active: %s"), *Warning), GetActiveDiagnosticStep());
        }
    }
}

void UOmniCaptureSubsystem::RemoveWarning(const FString& Warning)
{
    if (!Warning.IsEmpty())
    {
        const int32 Removed = ActiveWarnings.Remove(Warning);
        if (Removed > 0)
        {
            AppendDiagnostic(EOmniCaptureDiagnosticLevel::Info, FString::Printf(TEXT("Warning cleared: %s"), *Warning), GetActiveDiagnosticStep());
        }
    }
}

void UOmniCaptureSubsystem::ResetDynamicWarnings()
{
    RemoveWarning(OmniCapture::WarningLowDisk);
    RemoveWarning(OmniCapture::WarningFrameDrop);
    RemoveWarning(OmniCapture::WarningLowFps);
}

FString UOmniCaptureSubsystem::BuildOutputDirectory() const
{
    if (!ActiveSettings.OutputDirectory.IsEmpty())
    {
        return FPaths::ConvertRelativePathToFull(ActiveSettings.OutputDirectory);
    }

    return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("OmniCaptures"));
}

FString UOmniCaptureSubsystem::BuildFrameFileName(int32 FrameIndex, const FString& Extension) const
{
    return FString::Printf(TEXT("%s_%06d%s"), *ActiveSettings.OutputFileName, FrameIndex, *Extension);
}

