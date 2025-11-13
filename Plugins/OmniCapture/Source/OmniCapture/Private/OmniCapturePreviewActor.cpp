#include "OmniCapturePreviewActor.h"

#include "Components/StaticMeshComponent.h"
#include "OmniCaptureEquirectConverter.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "OmniCaptureIncludeFixes.h"

namespace
{
    static UStaticMesh* LoadPreviewPlane()
    {
        static TWeakObjectPtr<UStaticMesh> CachedMesh;
        if (!CachedMesh.IsValid())
        {
            CachedMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
        }
        return CachedMesh.Get();
    }

    static UMaterialInterface* LoadPreviewMaterial()
    {
        static TWeakObjectPtr<UMaterialInterface> CachedMaterial;
        if (!CachedMaterial.IsValid())
        {
            CachedMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultSpriteMaterial.DefaultSpriteMaterial"));
        }
        return CachedMaterial.Get();
    }
}

AOmniCapturePreviewActor::AOmniCapturePreviewActor()
{
    PrimaryActorTick.bCanEverTick = false;
    ScreenComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewScreen"));
    SetRootComponent(ScreenComponent);
    ScreenComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ScreenComponent->bHiddenInGame = true;
}

void AOmniCapturePreviewActor::Initialize(float InScale, const FIntPoint& InitialResolution)
{
    PreviewScale = FMath::Max(0.1f, InScale);
    PreviewResolution = InitialResolution;

    if (UStaticMesh* PlaneMesh = LoadPreviewPlane())
    {
        ScreenComponent->SetStaticMesh(PlaneMesh);
    }

    UpdatePreviewAspectRatio(InitialResolution);
    ScreenComponent->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));
    EnsureMaterial();
}

void AOmniCapturePreviewActor::BeginPlay()
{
    Super::BeginPlay();
    EnsureMaterial();
}

void AOmniCapturePreviewActor::EnsureMaterial()
{
    if (!ScreenComponent)
    {
        return;
    }

    if (!DynamicMaterial)
    {
        if (UMaterialInterface* BaseMaterial = LoadPreviewMaterial())
        {
            DynamicMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
            ScreenComponent->SetMaterial(0, DynamicMaterial);
        }
    }
}

void AOmniCapturePreviewActor::SetPreviewEnabled(bool bEnabled)
{
    if (ScreenComponent)
    {
        ScreenComponent->SetVisibility(bEnabled);
        ScreenComponent->bHiddenInGame = !bEnabled;
    }
}

void AOmniCapturePreviewActor::ResizePreviewTexture(const FIntPoint& Size)
{
    if (Size.X <= 0 || Size.Y <= 0)
    {
        return;
    }

    UpdatePreviewAspectRatio(Size);

    if (PreviewTexture && PreviewTexture->GetSizeX() == Size.X && PreviewTexture->GetSizeY() == Size.Y)
    {
        return;
    }

    PreviewTexture = UTexture2D::CreateTransient(Size.X, Size.Y, PF_B8G8R8A8);
    PreviewTexture->MipGenSettings = TMGS_NoMipmaps;
    PreviewTexture->CompressionSettings = TC_HDR;
    PreviewTexture->SRGB = true;
    PreviewTexture->UpdateResource();

    ApplyTexture(PreviewTexture);
}

void AOmniCapturePreviewActor::UpdatePreviewAspectRatio(const FIntPoint& Size)
{
    if (!ScreenComponent || Size.X <= 0 || Size.Y <= 0)
    {
        return;
    }

    PreviewResolution = Size;

    const float AspectRatio = static_cast<float>(Size.X) / static_cast<float>(Size.Y);
    const float ClampedAspect = FMath::Max(0.25f, AspectRatio);

    FVector Scale(PreviewScale, PreviewScale, PreviewScale);
    Scale.Y *= ClampedAspect;
    ScreenComponent->SetRelativeScale3D(Scale);
}

void AOmniCapturePreviewActor::ApplyTexture(UTexture2D* Texture)
{
    EnsureMaterial();
    if (DynamicMaterial && Texture)
    {
        DynamicMaterial->SetTextureParameterValue(TextureParameterName, Texture);
    }
}

void AOmniCapturePreviewActor::SetPreviewView(EOmniCapturePreviewView InView)
{
    PreviewViewMode = InView;
}

void AOmniCapturePreviewActor::UpdatePreviewTexture(const FOmniCaptureEquirectResult& Result, const FOmniCaptureSettings& Settings)
{
    const FIntPoint Size = Result.Size;
    if (Size.X <= 0 || Size.Y <= 0)
    {
        return;
    }

    const bool bStereo = Settings.IsStereo();
    const bool bShowSingleEye = bStereo && PreviewViewMode != EOmniCapturePreviewView::StereoComposite;

    FIntPoint TargetSize = Size;
    if (bShowSingleEye)
    {
        if (Settings.StereoLayout == EOmniCaptureStereoLayout::SideBySide)
        {
            TargetSize.X = FMath::Max(1, Size.X / 2);
        }
        else
        {
            TargetSize.Y = FMath::Max(1, Size.Y / 2);
        }
    }

    ResizePreviewTexture(TargetSize);
    if (!PreviewTexture)
    {
        return;
    }

    FTexture2DMipMap& Mip = PreviewTexture->GetPlatformData()->Mips[0];
    void* TextureMemory = Mip.BulkData.Lock(LOCK_READ_WRITE);

    if (Result.PreviewPixels.Num() != Size.X * Size.Y)
    {
        Mip.BulkData.Unlock();
        return;
    }

    if (bShowSingleEye)
    {
        const int32 OutputPixels = TargetSize.X * TargetSize.Y;
        PreviewScratchBuffer.SetNum(OutputPixels, EAllowShrinking::No);

        if (Settings.StereoLayout == EOmniCaptureStereoLayout::SideBySide)
        {
            const int32 EyeWidth = TargetSize.X;
            const int32 SourceWidth = Size.X;
            const int32 StartX = PreviewViewMode == EOmniCapturePreviewView::LeftEye ? 0 : EyeWidth;
            for (int32 Row = 0; Row < Size.Y; ++Row)
            {
                const int32 DestIndex = Row * EyeWidth;
                const int32 SourceIndex = Row * SourceWidth + StartX;
                FMemory::Memcpy(PreviewScratchBuffer.GetData() + DestIndex, Result.PreviewPixels.GetData() + SourceIndex, EyeWidth * sizeof(FColor));
            }
        }
        else
        {
            const int32 EyeHeight = TargetSize.Y;
            const int32 SourceWidth = Size.X;
            const int32 StartRow = PreviewViewMode == EOmniCapturePreviewView::LeftEye ? 0 : EyeHeight;
            for (int32 Row = 0; Row < EyeHeight; ++Row)
            {
                const int32 DestIndex = Row * TargetSize.X;
                const int32 SourceIndex = (StartRow + Row) * SourceWidth;
                FMemory::Memcpy(PreviewScratchBuffer.GetData() + DestIndex, Result.PreviewPixels.GetData() + SourceIndex, TargetSize.X * sizeof(FColor));
            }
        }

        FMemory::Memcpy(TextureMemory, PreviewScratchBuffer.GetData(), PreviewScratchBuffer.Num() * sizeof(FColor));
    }
    else
    {
        const int32 CopyCount = FMath::Min(Result.PreviewPixels.Num(), TargetSize.X * TargetSize.Y);
        FMemory::Memcpy(TextureMemory, Result.PreviewPixels.GetData(), CopyCount * sizeof(FColor));
    }

    Mip.BulkData.Unlock();
    PreviewTexture->UpdateResource();
}
