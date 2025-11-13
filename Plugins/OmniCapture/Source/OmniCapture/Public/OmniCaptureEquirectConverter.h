#pragma once
#include "ImageWriteTypes.h"

#include "CoreMinimal.h"
#include "OmniCaptureTypes.h"
#include "OmniCaptureRigActor.h"

// 公共头只做前置声明，避免路径/版本差异在项目内扩散
class UTextureRenderTarget2D;
class FTextureRenderTargetResource;

struct FOmniCaptureEquirectResult
{
    TUniquePtr<FImagePixelData> PixelData;
    TArray<FColor> PreviewPixels;
    FIntPoint Size = FIntPoint::ZeroValue;
    bool bIsLinear = false;
    bool bUsedCPUFallback = false;
    EOmniCapturePixelPrecision PixelPrecision = EOmniCapturePixelPrecision::Unknown;
    EOmniCapturePixelDataType PixelDataType = EOmniCapturePixelDataType::Unknown;
    TRefCountPtr<IPooledRenderTarget> OutputTarget;
    TRefCountPtr<IPooledRenderTarget> GPUSource;
    FTextureRHIRef Texture;
    FGPUFenceRHIRef ReadyFence;
    TArray<TRefCountPtr<IPooledRenderTarget>> EncoderPlanes;
};

class OMNICAPTURE_API FOmniCaptureEquirectConverter
{
public:
    static FOmniCaptureEquirectResult ConvertToEquirectangular(const FOmniCaptureSettings& Settings, const FOmniEyeCapture& LeftEye, const FOmniEyeCapture& RightEye);
    static FOmniCaptureEquirectResult ConvertToFisheye(const FOmniCaptureSettings& Settings, const FOmniEyeCapture& LeftEye, const FOmniEyeCapture& RightEye);
    static FOmniCaptureEquirectResult ConvertToPlanar(const FOmniCaptureSettings& Settings, const FOmniEyeCapture& SourceEye);
};

