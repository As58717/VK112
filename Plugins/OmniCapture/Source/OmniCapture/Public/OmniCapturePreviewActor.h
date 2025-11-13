#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OmniCaptureTypes.h"
#include "OmniCapturePreviewActor.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UTexture2D;
struct FOmniCaptureEquirectResult;

UCLASS()
class OMNICAPTURE_API AOmniCapturePreviewActor : public AActor
{
    GENERATED_BODY()

public:
    AOmniCapturePreviewActor();

    void Initialize(float InScale, const FIntPoint& InitialResolution);
    void UpdatePreviewTexture(const FOmniCaptureEquirectResult& Result, const FOmniCaptureSettings& Settings);
    void SetPreviewEnabled(bool bEnabled);
    void SetPreviewView(EOmniCapturePreviewView InView);
    UTexture2D* GetPreviewTexture() const { return PreviewTexture; }
    FIntPoint GetPreviewResolution() const { return PreviewResolution; }

protected:
    virtual void BeginPlay() override;

private:
    void EnsureMaterial();
    void ApplyTexture(UTexture2D* Texture);
    void ResizePreviewTexture(const FIntPoint& Size);
    void UpdatePreviewAspectRatio(const FIntPoint& Size);

private:
    UPROPERTY(Transient)
    UStaticMeshComponent* ScreenComponent;

    UPROPERTY(Transient)
    UMaterialInstanceDynamic* DynamicMaterial = nullptr;

    UPROPERTY(Transient)
    UTexture2D* PreviewTexture = nullptr;

    FName TextureParameterName = TEXT("SpriteTexture");
    float PreviewScale = 1.0f;
    FIntPoint PreviewResolution = FIntPoint::ZeroValue;
    EOmniCapturePreviewView PreviewViewMode = EOmniCapturePreviewView::StereoComposite;
    TArray<FColor> PreviewScratchBuffer;
};
