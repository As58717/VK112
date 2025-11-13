#include "OmniCaptureRigActor.h"

#include "Components/SceneComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "OmniCaptureIncludeFixes.h"
#include "UObject/Package.h"
#include "Kismet/KismetMathLibrary.h"
#include "OmniCaptureTypes.h"  // 增加头文件
#include "OmniCaptureVersion.h"
#include "UObject/UnrealType.h"

namespace
{
    constexpr int32 CubemapFaceCount = 6;

    struct FAuxiliaryPassConfig
    {
        ESceneCaptureSource CaptureSource = ESceneCaptureSource::SCS_FinalColorHDR;
        EPixelFormat PixelFormat = PF_FloatRGBA;
        FLinearColor ClearColor = FLinearColor::Black;
        bool bLinearTarget = true;
    };

    bool GetAuxiliaryPassConfig(EOmniCaptureAuxiliaryPassType PassType, FAuxiliaryPassConfig& OutConfig)
    {
        switch (PassType)
        {
        case EOmniCaptureAuxiliaryPassType::SceneDepth:
            OutConfig.CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
            OutConfig.PixelFormat = PF_R32_FLOAT;
            OutConfig.ClearColor = FLinearColor::White;
            OutConfig.bLinearTarget = true;
            return true;
        case EOmniCaptureAuxiliaryPassType::WorldNormal:
            OutConfig.CaptureSource = ESceneCaptureSource::SCS_Normal;
            OutConfig.PixelFormat = OmniCapture::GetHalfFloatPixelFormat();
            OutConfig.ClearColor = FLinearColor::Black;
            OutConfig.bLinearTarget = true;
            return true;
        case EOmniCaptureAuxiliaryPassType::BaseColor:
            OutConfig.CaptureSource = ESceneCaptureSource::SCS_BaseColor;
            OutConfig.PixelFormat = PF_FloatRGBA;
            OutConfig.ClearColor = FLinearColor::Black;
            OutConfig.bLinearTarget = true;
            return true;
#if !OMNICAPTURE_UE_VERSION_AT_LEAST(5, 5, 0)
        case EOmniCaptureAuxiliaryPassType::Roughness:
            OutConfig.CaptureSource = ESceneCaptureSource::SCS_Roughness;
            OutConfig.PixelFormat = PF_R16F;
            OutConfig.ClearColor = FLinearColor::Black;
            OutConfig.bLinearTarget = true;
            return true;
        case EOmniCaptureAuxiliaryPassType::AmbientOcclusion:
            OutConfig.CaptureSource = ESceneCaptureSource::SCS_AmbientOcclusion;
            OutConfig.PixelFormat = PF_R16F;
            OutConfig.ClearColor = FLinearColor::White;
            OutConfig.bLinearTarget = true;
            return true;
#else
        case EOmniCaptureAuxiliaryPassType::Roughness:
        case EOmniCaptureAuxiliaryPassType::AmbientOcclusion:
            return false;
#endif
        case EOmniCaptureAuxiliaryPassType::MotionVector:
            OutConfig.CaptureSource = ESceneCaptureSource::SCS_FinalColorHDR;
            OutConfig.PixelFormat = PF_FloatRGBA;
            OutConfig.ClearColor = FLinearColor::Black;
            OutConfig.bLinearTarget = true;
            return true;
        default:
            return false;
        }
    }

}

AOmniCaptureRigActor::AOmniCaptureRigActor()
{
    PrimaryActorTick.bCanEverTick = false;
    PrimaryActorTick.bStartWithTickEnabled = false;

    RigRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RigRoot"));
    SetRootComponent(RigRoot);

    LeftEyeRoot = CreateDefaultSubobject<USceneComponent>(TEXT("LeftEyeRoot"));
    LeftEyeRoot->SetupAttachment(RigRoot);

    RightEyeRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RightEyeRoot"));
    RightEyeRoot->SetupAttachment(RigRoot);
}

void AOmniCaptureRigActor::Configure(const FOmniCaptureSettings& InSettings)
{
    CachedSettings = InSettings;

    // 清除之前的捕获组件
    for (USceneCaptureComponent2D* Capture : LeftEyeCaptures)
    {
        if (Capture)
        {
            Capture->DestroyComponent();
        }
    }

    for (USceneCaptureComponent2D* Capture : RightEyeCaptures)
    {
        if (Capture)
        {
            Capture->DestroyComponent();
        }
    }

    for (auto& Pair : LeftAuxiliaryCaptures)
    {
        for (USceneCaptureComponent2D* Capture : Pair.Value.CaptureComponents)
        {
            if (Capture)
            {
                Capture->DestroyComponent();
            }
        }
    }

    for (auto& Pair : RightAuxiliaryCaptures)
    {
        for (USceneCaptureComponent2D* Capture : Pair.Value.CaptureComponents)
        {
            if (Capture)
            {
                Capture->DestroyComponent();
            }
        }
    }

    for (UTextureRenderTarget2D* RenderTarget : RenderTargets)
    {
        if (RenderTarget)
        {
            RenderTarget->ConditionalBeginDestroy();
        }
    }

    LeftEyeCaptures.Empty();
    RightEyeCaptures.Empty();
    LeftAuxiliaryCaptures.Empty();
    RightAuxiliaryCaptures.Empty();
    RenderTargets.Empty();

    const bool bPlanar = CachedSettings.IsPlanar();
    const int32 FaceCount = bPlanar ? 1 : CubemapFaceCount;
    const FIntPoint TargetSize = bPlanar
        ? CachedSettings.GetPlanarResolution()
        : FIntPoint(CachedSettings.Resolution, CachedSettings.Resolution);

    const float IPDHalf = CachedSettings.Mode == EOmniCaptureMode::Stereo
        ? CachedSettings.InterPupillaryDistanceCm * 0.5f
        : 0.0f;

    BuildEyeRig(EOmniCaptureEye::Left, -IPDHalf, FaceCount);
    ConfigureAuxiliaryTargets(EOmniCaptureEye::Left, FaceCount, TargetSize);

    if (CachedSettings.Mode == EOmniCaptureMode::Stereo)
    {
        BuildEyeRig(EOmniCaptureEye::Right, IPDHalf, FaceCount);
        ConfigureAuxiliaryTargets(EOmniCaptureEye::Right, FaceCount, TargetSize);
    }

    ApplyStereoParameters();
}

void AOmniCaptureRigActor::Capture(FOmniEyeCapture& OutLeftEye, FOmniEyeCapture& OutRightEye) const
{
    CaptureEye(EOmniCaptureEye::Left, OutLeftEye);

    if (CachedSettings.Mode == EOmniCaptureMode::Stereo && RightEyeCaptures.Num() > 0)
    {
        CaptureEye(EOmniCaptureEye::Right, OutRightEye);
    }
    else
    {
        OutRightEye = OutLeftEye;
    }
}

void AOmniCaptureRigActor::BuildEyeRig(EOmniCaptureEye Eye, float IPDHalfCm, int32 FaceCount)
{
    USceneComponent* EyeRoot = Eye == EOmniCaptureEye::Left ? LeftEyeRoot : RightEyeRoot;

    if (!EyeRoot)
    {
        return;
    }

    TArray<USceneCaptureComponent2D*>& TargetArray = Eye == EOmniCaptureEye::Left ? LeftEyeCaptures : RightEyeCaptures;

    const FIntPoint TargetSize = CachedSettings.IsPlanar()
        ? CachedSettings.GetPlanarResolution()
        : FIntPoint(CachedSettings.Resolution, CachedSettings.Resolution);

    for (int32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
    {
        FString ComponentName = FString::Printf(TEXT("%s_CaptureFace_%d"), Eye == EOmniCaptureEye::Left ? TEXT("Left") : TEXT("Right"), FaceIndex);
        USceneCaptureComponent2D* CaptureComponent = NewObject<USceneCaptureComponent2D>(this, *ComponentName);
        CaptureComponent->SetupAttachment(EyeRoot);
        CaptureComponent->RegisterComponent();
        ConfigureCaptureComponent(CaptureComponent, TargetSize);

        if (!CachedSettings.IsPlanar())
        {
            FRotator FaceRotation;
            GetOrientationForFace(FaceIndex, FaceRotation);
            CaptureComponent->SetRelativeRotation(FaceRotation);
        }

        TargetArray.Add(CaptureComponent);
    }
}

void AOmniCaptureRigActor::UpdateStereoParameters(float NewIPDCm, float NewConvergenceDistanceCm)
{
    if (CachedSettings.Mode != EOmniCaptureMode::Stereo)
    {
        CachedSettings.InterPupillaryDistanceCm = 0.0f;
        CachedSettings.EyeConvergenceDistanceCm = 0.0f;
        ApplyStereoParameters();
        return;
    }

    CachedSettings.InterPupillaryDistanceCm = FMath::Max(0.0f, NewIPDCm);
    CachedSettings.EyeConvergenceDistanceCm = FMath::Max(0.0f, NewConvergenceDistanceCm);
    ApplyStereoParameters();
}

void AOmniCaptureRigActor::ApplyStereoParameters()
{
    const float HalfIPD = CachedSettings.Mode == EOmniCaptureMode::Stereo
        ? CachedSettings.InterPupillaryDistanceCm * 0.5f
        : 0.0f;

    UpdateEyeRootTransform(LeftEyeRoot, -HalfIPD, EOmniCaptureEye::Left);
    UpdateEyeRootTransform(RightEyeRoot, HalfIPD, EOmniCaptureEye::Right);
}

void AOmniCaptureRigActor::UpdateEyeRootTransform(USceneComponent* EyeRoot, float LateralOffset, EOmniCaptureEye Eye) const
{
    if (!EyeRoot)
    {
        return;
    }

    EyeRoot->SetRelativeLocation(FVector(0.0f, LateralOffset, 0.0f));

    if (!CachedSettings.IsPlanar())
    {
        EyeRoot->SetRelativeRotation(FRotator::ZeroRotator);
        return;
    }

    const float ConvergenceDistance = CachedSettings.EyeConvergenceDistanceCm;
    if (ConvergenceDistance <= KINDA_SMALL_NUMBER)
    {
        EyeRoot->SetRelativeRotation(FRotator::ZeroRotator);
        return;
    }

    const FVector EyeLocation(0.0f, LateralOffset, 0.0f);
    const FVector FocusPoint(ConvergenceDistance, 0.0f, 0.0f);
    const FRotator EyeRotation = UKismetMathLibrary::FindLookAtRotation(EyeLocation, FocusPoint);
    EyeRoot->SetRelativeRotation(EyeRotation);
}

void AOmniCaptureRigActor::ConfigureCaptureComponent(USceneCaptureComponent2D* CaptureComponent, const FIntPoint& TargetSize) const
{
    if (!CaptureComponent)
    {
        return;
    }

    // sRGB captures should include the same tonemapping as the viewport, while
    // linear captures keep the HDR buffer untouched for downstream processing.
    const bool bWantsLinearOutput = CachedSettings.Gamma == EOmniCaptureGamma::Linear;
    CaptureComponent->FOVAngle = 90.0f;
    CaptureComponent->CaptureSource = bWantsLinearOutput
        ? ESceneCaptureSource::SCS_FinalColorHDR
        : ESceneCaptureSource::SCS_FinalColorLDR;
    CaptureComponent->bCaptureEveryFrame = false;
    CaptureComponent->bCaptureOnMovement = false;
    CaptureComponent->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;

    // ✅ 修复：在 const 成员函数中允许修改 RenderTargets
    UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
    check(RenderTarget);

    // 设置渲染目标
    const EPixelFormat PixelFormat = CachedSettings.HDRPrecision == EOmniCaptureHDRPrecision::FullFloat
        ? PF_A32B32G32R32F
        : PF_FloatRGBA;
    const int32 SizeX = FMath::Max(2, TargetSize.X);
    const int32 SizeY = FMath::Max(2, TargetSize.Y);
    const bool bForceLinearGamma = bWantsLinearOutput;
    RenderTarget->InitCustomFormat(SizeX, SizeY, PixelFormat, bForceLinearGamma);
    RenderTarget->TargetGamma = CachedSettings.Gamma == EOmniCaptureGamma::Linear ? 1.0f : 2.2f;
    RenderTarget->bForceLinearGamma = bForceLinearGamma;
    RenderTarget->bAutoGenerateMips = false;
    RenderTarget->ClearColor = FLinearColor::Black;
    RenderTarget->Filter = TF_Bilinear;

    CaptureComponent->TextureTarget = RenderTarget;

    // 通过 const_cast 修改 RenderTargets 数组
    const_cast<TArray<UTextureRenderTarget2D*>&>(RenderTargets).Add(RenderTarget);
}

USceneCaptureComponent2D* AOmniCaptureRigActor::CreateAuxiliaryCaptureComponent(const FString& ComponentName, EOmniCaptureAuxiliaryPassType PassType, const FIntPoint& TargetSize) const
{
    if (PassType == EOmniCaptureAuxiliaryPassType::None)
    {
        return nullptr;
    }

    FAuxiliaryPassConfig Config;
    if (!GetAuxiliaryPassConfig(PassType, Config))
    {
        return nullptr;
    }

    AOmniCaptureRigActor* MutableThis = const_cast<AOmniCaptureRigActor*>(this);
    USceneCaptureComponent2D* CaptureComponent = NewObject<USceneCaptureComponent2D>(MutableThis, *ComponentName);
    CaptureComponent->SetRelativeLocation(FVector::ZeroVector);
    CaptureComponent->FOVAngle = 90.0f;
    CaptureComponent->CaptureSource = Config.CaptureSource;
    CaptureComponent->bCaptureEveryFrame = false;
    CaptureComponent->bCaptureOnMovement = false;
    CaptureComponent->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;

    UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
    check(RenderTarget);

    const int32 SizeX = FMath::Max(2, TargetSize.X);
    const int32 SizeY = FMath::Max(2, TargetSize.Y);
    RenderTarget->InitCustomFormat(SizeX, SizeY, Config.PixelFormat, Config.bLinearTarget);
    RenderTarget->TargetGamma = Config.bLinearTarget ? 1.0f : 2.2f;
    RenderTarget->bForceLinearGamma = Config.bLinearTarget;
    RenderTarget->bAutoGenerateMips = false;
    RenderTarget->ClearColor = Config.ClearColor;
    RenderTarget->Filter = TF_Bilinear;

    CaptureComponent->TextureTarget = RenderTarget;

    if (PassType == EOmniCaptureAuxiliaryPassType::MotionVector)
    {
        CaptureComponent->bAlwaysPersistRenderingState = true;
        CaptureComponent->ShowFlags.SetPostProcessing(false);
        CaptureComponent->ShowFlags.SetTonemapper(false);
        CaptureComponent->ShowFlags.SetBloom(false);
        CaptureComponent->ShowFlags.SetLighting(false);
        CaptureComponent->ShowFlags.SetFog(false);
        CaptureComponent->ShowFlags.SetAntiAliasing(false);
        CaptureComponent->ShowFlags.SetVisualizeMotionBlur(true);
        CaptureComponent->ShowFlags.SetMotionBlur(true);
    }

    const_cast<TArray<UTextureRenderTarget2D*>&>(RenderTargets).Add(RenderTarget);

    return CaptureComponent;
}

void AOmniCaptureRigActor::ConfigureAuxiliaryTargets(EOmniCaptureEye Eye, int32 FaceCount, const FIntPoint& TargetSize)
{
    if (CachedSettings.AuxiliaryPasses.Num() == 0)
    {
        return;
    }

    USceneComponent* EyeRoot = Eye == EOmniCaptureEye::Left ? LeftEyeRoot : RightEyeRoot;
    if (!EyeRoot)
    {
        return;
    }

    TMap<EOmniCaptureAuxiliaryPassType, FOmniCaptureAuxiliaryCaptureArray>& TargetMap = (Eye == EOmniCaptureEye::Left)
        ? const_cast<TMap<EOmniCaptureAuxiliaryPassType, FOmniCaptureAuxiliaryCaptureArray>&>(LeftAuxiliaryCaptures)
        : const_cast<TMap<EOmniCaptureAuxiliaryPassType, FOmniCaptureAuxiliaryCaptureArray>&>(RightAuxiliaryCaptures);

    for (EOmniCaptureAuxiliaryPassType Pass : CachedSettings.AuxiliaryPasses)
    {
        if (Pass == EOmniCaptureAuxiliaryPassType::None)
        {
            continue;
        }

        TArray<USceneCaptureComponent2D*>& CaptureArray = TargetMap.FindOrAdd(Pass).CaptureComponents;
        CaptureArray.SetNum(FaceCount);

        for (int32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
        {
            const FString PassName = GetAuxiliaryLayerName(Pass).ToString();
            const FString ComponentName = FString::Printf(TEXT("%s_%s_%d"), Eye == EOmniCaptureEye::Left ? TEXT("Left") : TEXT("Right"), *PassName, FaceIndex);
            if (USceneCaptureComponent2D* AuxCapture = CreateAuxiliaryCaptureComponent(ComponentName, Pass, TargetSize))
            {
                AuxCapture->SetupAttachment(EyeRoot);
                AuxCapture->RegisterComponent();

                if (!CachedSettings.IsPlanar())
                {
                    FRotator FaceRotation;
                    GetOrientationForFace(FaceIndex, FaceRotation);
                    AuxCapture->SetRelativeRotation(FaceRotation);
                }

                CaptureArray[FaceIndex] = AuxCapture;
            }
        }
    }
}

void AOmniCaptureRigActor::CaptureEye(EOmniCaptureEye Eye, FOmniEyeCapture& OutCapture) const
{
    const TArray<USceneCaptureComponent2D*>& CaptureComponents = Eye == EOmniCaptureEye::Left ? LeftEyeCaptures : RightEyeCaptures;

    OutCapture.ActiveFaceCount = CaptureComponents.Num();

    for (int32 FaceIndex = 0; FaceIndex < UE_ARRAY_COUNT(OutCapture.Faces); ++FaceIndex)
    {
        OutCapture.Faces[FaceIndex].RenderTarget = nullptr;
        OutCapture.Faces[FaceIndex].AuxiliaryTargets.Reset();
    }

    for (int32 FaceIndex = 0; FaceIndex < CaptureComponents.Num(); ++FaceIndex)
    {
        if (USceneCaptureComponent2D* CaptureComponent = CaptureComponents[FaceIndex])
        {
            CaptureComponent->CaptureScene();

            UTextureRenderTarget2D* RenderTarget = Cast<UTextureRenderTarget2D>(CaptureComponent->TextureTarget);
            OutCapture.Faces[FaceIndex].RenderTarget = RenderTarget;
        }
    }

    const TMap<EOmniCaptureAuxiliaryPassType, FOmniCaptureAuxiliaryCaptureArray>* AuxMap = Eye == EOmniCaptureEye::Left ? &LeftAuxiliaryCaptures : &RightAuxiliaryCaptures;
    if (AuxMap)
    {
        for (const TPair<EOmniCaptureAuxiliaryPassType, FOmniCaptureAuxiliaryCaptureArray>& Pair : *AuxMap)
        {
            const EOmniCaptureAuxiliaryPassType PassType = Pair.Key;
            const TArray<USceneCaptureComponent2D*>& AuxCaptures = Pair.Value.CaptureComponents;

            for (int32 FaceIndex = 0; FaceIndex < AuxCaptures.Num(); ++FaceIndex)
            {
                if (USceneCaptureComponent2D* AuxCapture = AuxCaptures[FaceIndex])
                {
                    AuxCapture->CaptureScene();
                    if (UTextureRenderTarget2D* AuxTarget = Cast<UTextureRenderTarget2D>(AuxCapture->TextureTarget))
                    {
                        OutCapture.Faces[FaceIndex].AuxiliaryTargets.Add(PassType, AuxTarget);
                    }
                }
            }
        }
    }
}

void AOmniCaptureRigActor::GetOrientationForFace(int32 FaceIndex, FRotator& OutRotation)
{
    switch (FaceIndex)
    {
    case 0: // +X
        OutRotation = FRotator(0.0f, 0.0f, 0.0f);
        break;
    case 1: // -X
        OutRotation = FRotator(0.0f, 180.0f, 0.0f);
        break;
    case 2: // +Y
        OutRotation = FRotator(0.0f, 90.0f, 0.0f);
        break;
    case 3: // -Y
        OutRotation = FRotator(0.0f, -90.0f, 0.0f);
        break;
    case 4: // +Z
        OutRotation = FRotator(-90.0f, 0.0f, 0.0f);
        break;
    case 5: // -Z
        OutRotation = FRotator(90.0f, 0.0f, 0.0f);
        break;
    default:
        OutRotation = FRotator::ZeroRotator;
        break;
    }
}

