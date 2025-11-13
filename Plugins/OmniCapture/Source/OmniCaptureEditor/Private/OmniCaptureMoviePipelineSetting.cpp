#include "OmniCaptureMoviePipelineSetting.h"

#include "MoviePipeline.h"
#include "MoviePipelineSetting.h"
#include "Misc/EngineVersionComparison.h"
#include "OmniCaptureSubsystem.h"
#include "Internationalization/Text.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "OmniCaptureMoviePipeline"

UOmniCaptureMoviePipelineSetting::UOmniCaptureMoviePipelineSetting()
{
#if UE_VERSION_OLDER_THAN(5, 5, 0)
    bEnabled = true;
#else
    SetIsEnabled(true);
#endif
}

void UOmniCaptureMoviePipelineSetting::CacheSubsystem(UMoviePipeline* InPipeline)
{
    if (!CachedSubsystem.IsValid() && InPipeline)
    {
        if (UWorld* World = InPipeline->GetWorld())
        {
            CachedSubsystem = World->GetSubsystem<UOmniCaptureSubsystem>();
        }
    }
}

void UOmniCaptureMoviePipelineSetting::SetupForPipelineImpl(UMoviePipeline* InPipeline)
{
    CacheSubsystem(InPipeline);

    if (UOmniCaptureSubsystem* Subsystem = CachedSubsystem.Get())
    {
        if (!Subsystem->IsCapturing())
        {
            Subsystem->BeginCapture(CaptureSettings);
        }
    }
}

void UOmniCaptureMoviePipelineSetting::TeardownForPipelineImpl(UMoviePipeline* InPipeline)
{
    CacheSubsystem(InPipeline);

    if (UOmniCaptureSubsystem* Subsystem = CachedSubsystem.Get())
    {
        if (Subsystem->IsCapturing())
        {
            Subsystem->EndCapture(true);
        }
    }

    CachedSubsystem.Reset();
}

FText UOmniCaptureMoviePipelineSetting::GetCategoryText() const
{
    return LOCTEXT("Category", "OmniCapture");
}

FText UOmniCaptureMoviePipelineSetting::GetDisplayText() const
{
    return LOCTEXT("Display", "OmniCapture Runtime Capture");
}

#undef LOCTEXT_NAMESPACE
