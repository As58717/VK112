#pragma once

#include "MoviePipelineSetting.h"
#include "OmniCaptureTypes.h"
#include "OmniCaptureMoviePipelineSetting.generated.h"

class UMoviePipeline;
class UOmniCaptureSubsystem;

/**
 * Movie Render Queue integration setting that proxies OmniCapture runtime recording.
 */
UCLASS(BlueprintType)
class OMNICAPTUREEDITOR_API UOmniCaptureMoviePipelineSetting : public UMoviePipelineSetting
{
    GENERATED_BODY()

public:
    UOmniCaptureMoviePipelineSetting();

    /** Settings used to initialize the OmniCapture subsystem when the pipeline runs. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OmniCapture")
    FOmniCaptureSettings CaptureSettings;

protected:
    virtual void SetupForPipelineImpl(UMoviePipeline* InPipeline) override;
    virtual void TeardownForPipelineImpl(UMoviePipeline* InPipeline) override;
    virtual FText GetCategoryText() const override;
    virtual FText GetDisplayText() const override;
    virtual bool IsValidOnPrimary() const override { return true; }
    virtual bool IsValidOnShots() const override { return true; }

private:
    void CacheSubsystem(UMoviePipeline* InPipeline);

private:
    TWeakObjectPtr<UOmniCaptureSubsystem> CachedSubsystem;
};
