#include "OmniCaptureEquirectConverter.h"

#include "OmniCaptureIncludeFixes.h" // 统一兼容：TRT2D + TRTResource
#include "OmniCaptureTypes.h"

#include "GlobalShader.h"
#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"
#include "RHIStaticStates.h"
#include "RenderTargetPool.h"
#include "PixelFormat.h"
#if __has_include("RenderGraphUtils/Public/ComputeShaderUtils.h")
#include "RenderGraphUtils/Public/ComputeShaderUtils.h"
#elif __has_include("RenderGraphUtils/ComputeShaderUtils.h")
#include "RenderGraphUtils/ComputeShaderUtils.h"
#elif __has_include("ComputeShaderUtils.h")
#include "ComputeShaderUtils.h"
#endif
#include "RHICommandList.h"
#include "HAL/PlatformProcess.h"

namespace
{
    struct FCPUFaceData
    {
        int32 Resolution = 0;
        EOmniCapturePixelPrecision Precision = EOmniCapturePixelPrecision::Unknown;
        TArray<FLinearColor> Pixels;

        bool IsValid() const
        {
            return Resolution > 0 && Pixels.Num() == Resolution * Resolution;
        }
    };

    struct FCPUCubemap
    {
        FCPUFaceData Faces[6];
        EOmniCapturePixelPrecision Precision = EOmniCapturePixelPrecision::Unknown;

        bool IsValid() const
        {
            for (int32 Index = 0; Index < 6; ++Index)
            {
                if (!Faces[Index].IsValid())
                {
                    return false;
                }
            }

            return Precision != EOmniCapturePixelPrecision::Unknown;
        }
    };

    EOmniCapturePixelPrecision PixelPrecisionFromFormat(EPixelFormat Format)
    {
        switch (Format)
        {
        case PF_A32B32G32R32F:
            return EOmniCapturePixelPrecision::FullFloat;
        case PF_FloatRGBA:
        case PF_FloatRGB:
#if defined(PF_A16B16G16R16F)
        case PF_A16B16G16R16F:
#endif
            return EOmniCapturePixelPrecision::HalfFloat;
        default:
            return EOmniCapturePixelPrecision::Unknown;
        }
    }

    EOmniCapturePixelPrecision ResolvePrecisionFromTextures(const TArray<FTextureRHIRef, TInlineAllocator<6>>& Textures)
    {
        for (const FTextureRHIRef& Texture : Textures)
        {
            if (Texture.IsValid())
            {
                return PixelPrecisionFromFormat(Texture->GetFormat());
            }
        }

        return EOmniCapturePixelPrecision::Unknown;
    }

    EOmniCapturePixelPrecision ResolvePrecisionFromEye(const FOmniEyeCapture& Eye)
    {
        if (UTextureRenderTarget2D* RenderTarget = Eye.GetPrimaryRenderTarget())
        {
            return PixelPrecisionFromFormat(RenderTarget->GetFormat());
        }

        return EOmniCapturePixelPrecision::Unknown;
    }

    EPixelFormat GetPixelFormatForPrecision(EOmniCapturePixelPrecision Precision)
    {
        return Precision == EOmniCapturePixelPrecision::FullFloat ? PF_A32B32G32R32F : OmniCapture::GetHalfFloatPixelFormat();
    }


    class FOmniEquirectCS final : public FGlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER(FOmniEquirectCS);
        SHADER_USE_PARAMETER_STRUCT(FOmniEquirectCS, FGlobalShader);

        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
            SHADER_PARAMETER(FVector2f, OutputResolution)
            SHADER_PARAMETER(int32, FaceResolution)
            SHADER_PARAMETER(int32, bStereo)
            SHADER_PARAMETER(float, SeamStrength)
            SHADER_PARAMETER(float, PolarStrength)
            SHADER_PARAMETER(int32, StereoLayout)
            SHADER_PARAMETER(int32, bHalfSphere)
            SHADER_PARAMETER(float, Padding)
            SHADER_PARAMETER(float, LongitudeSpan)
            SHADER_PARAMETER(float, LatitudeSpan)
            SHADER_PARAMETER_SAMPLER(SamplerState, FaceSampler)
            SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float4>, LeftFaces)
            SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float4>, RightFaces)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
        END_SHADER_PARAMETER_STRUCT()

        static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
        {
            return true;
        }
    };

    IMPLEMENT_GLOBAL_SHADER(FOmniEquirectCS, "/Plugin/OmniCapture/Private/OmniEquirectCS.usf", "MainCS", SF_Compute);

    class FOmniFisheyeCS final : public FGlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER(FOmniFisheyeCS);
        SHADER_USE_PARAMETER_STRUCT(FOmniFisheyeCS, FGlobalShader);

        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
            SHADER_PARAMETER(FVector2f, OutputResolution)
            SHADER_PARAMETER(FVector2f, EyeResolution)
            SHADER_PARAMETER(float, FovRadians)
            SHADER_PARAMETER(int32, FaceResolution)
            SHADER_PARAMETER(int32, bStereo)
            SHADER_PARAMETER(int32, StereoLayout)
            SHADER_PARAMETER(int32, bHalfSphere)
            SHADER_PARAMETER(float, SeamStrength)
            SHADER_PARAMETER(float, Padding)
            SHADER_PARAMETER_SAMPLER(SamplerState, FaceSampler)
            SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float4>, LeftFaces)
            SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float4>, RightFaces)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
        END_SHADER_PARAMETER_STRUCT()

        static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
        {
            return true;
        }
    };

    IMPLEMENT_GLOBAL_SHADER(FOmniFisheyeCS, "/Plugin/OmniCapture/Private/OmniFisheyeCS.usf", "MainCS", SF_Compute);

    class FOmniConvertToYUVLumaCS final : public FGlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER(FOmniConvertToYUVLumaCS);
        SHADER_USE_PARAMETER_STRUCT(FOmniConvertToYUVLumaCS, FGlobalShader);

        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
            SHADER_PARAMETER(FVector2f, OutputSize)
            SHADER_PARAMETER(FVector2f, ChromaSize)
            SHADER_PARAMETER(int32, Format)
            SHADER_PARAMETER(int32, ColorSpace)
            SHADER_PARAMETER(int32, bLinearInput)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceTexture)
            SHADER_PARAMETER_SAMPLER(SamplerState, SourceSampler)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, LumaOutput)
        END_SHADER_PARAMETER_STRUCT()

        static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
        {
            return true;
        }
    };

    IMPLEMENT_GLOBAL_SHADER(FOmniConvertToYUVLumaCS, "/Plugin/OmniCapture/Private/OmniColorConvertCS.usf", "ConvertLuma", SF_Compute);

    class FOmniConvertToYUVChromaCS final : public FGlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER(FOmniConvertToYUVChromaCS);
        SHADER_USE_PARAMETER_STRUCT(FOmniConvertToYUVChromaCS, FGlobalShader);

        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
            SHADER_PARAMETER(FVector2f, OutputSize)
            SHADER_PARAMETER(FVector2f, ChromaSize)
            SHADER_PARAMETER(int32, Format)
            SHADER_PARAMETER(int32, ColorSpace)
            SHADER_PARAMETER(int32, bLinearInput)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceTexture)
            SHADER_PARAMETER_SAMPLER(SamplerState, SourceSampler)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint2>, ChromaOutput)
        END_SHADER_PARAMETER_STRUCT()

        static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
        {
            return true;
        }
    };

    IMPLEMENT_GLOBAL_SHADER(FOmniConvertToYUVChromaCS, "/Plugin/OmniCapture/Private/OmniColorConvertCS.usf", "ConvertChroma", SF_Compute);

    class FOmniConvertToBGRACS final : public FGlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER(FOmniConvertToBGRACS);
        SHADER_USE_PARAMETER_STRUCT(FOmniConvertToBGRACS, FGlobalShader);

        BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
            SHADER_PARAMETER(FVector2f, OutputSize)
            SHADER_PARAMETER(FVector2f, ChromaSize)
            SHADER_PARAMETER(int32, Format)
            SHADER_PARAMETER(int32, ColorSpace)
            SHADER_PARAMETER(int32, bLinearInput)
            SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceTexture)
            SHADER_PARAMETER_SAMPLER(SamplerState, SourceSampler)
            SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutputTexture)
        END_SHADER_PARAMETER_STRUCT()

        static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
        {
            return true;
        }
    };

    IMPLEMENT_GLOBAL_SHADER(FOmniConvertToBGRACS, "/Plugin/OmniCapture/Private/OmniColorConvertCS.usf", "ConvertBGRA", SF_Compute);

    bool ReadFaceData(UTextureRenderTarget2D* RenderTarget, FCPUFaceData& OutFace)
    {
        if (!RenderTarget)
        {
            return false;
        }

        FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource();
        if (!Resource)
        {
            return false;
        }

        const int32 SizeX = RenderTarget->SizeX;
        const int32 SizeY = RenderTarget->SizeY;
        if (SizeX <= 0 || SizeY <= 0 || SizeX != SizeY)
        {
            return false;
        }

        OutFace.Pixels.Reset();
        OutFace.Precision = PixelPrecisionFromFormat(RenderTarget->GetFormat());

        // Use the standard UNorm readback mode instead of the Min/Max resolve
        // path.  RCM_MinMax performs additional math on the HDR buffer which
        // skews the colour channels when we subsequently treat the data as
        // regular colour pixels.  That manifested as a visible green tint in
        // 2D captures.  RCM_UNorm leaves the pixel values untouched so that
        // our later linear → sRGB conversions behave correctly.
        FReadSurfaceDataFlags Flags(RCM_UNorm);
        Flags.SetLinearToGamma(false);

        if (OutFace.Precision == EOmniCapturePixelPrecision::FullFloat)
        {
            if (!Resource->ReadLinearColorPixels(OutFace.Pixels, Flags, FIntRect()))
            {
                return false;
            }
        }
        else
        {
            TArray<FFloat16Color> HalfPixels;
            if (!Resource->ReadFloat16Pixels(HalfPixels, Flags, FIntRect()))
            {
                return false;
            }

            OutFace.Precision = EOmniCapturePixelPrecision::HalfFloat;
            OutFace.Pixels.SetNum(HalfPixels.Num());
            for (int32 Index = 0; Index < HalfPixels.Num(); ++Index)
            {
                OutFace.Pixels[Index] = FLinearColor(HalfPixels[Index]);
            }
        }

        OutFace.Resolution = SizeX;
        return OutFace.IsValid();
    }

    bool BuildCPUCubemap(const FOmniEyeCapture& Eye, FCPUCubemap& OutCubemap)
    {
        OutCubemap.Precision = EOmniCapturePixelPrecision::Unknown;

        for (int32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
        {
            if (!ReadFaceData(Eye.Faces[FaceIndex].RenderTarget, OutCubemap.Faces[FaceIndex]))
            {
                return false;
            }

            if (OutCubemap.Precision == EOmniCapturePixelPrecision::Unknown)
            {
                OutCubemap.Precision = OutCubemap.Faces[FaceIndex].Precision;
            }
            else if (OutCubemap.Precision != OutCubemap.Faces[FaceIndex].Precision)
            {
                OutCubemap.Precision = EOmniCapturePixelPrecision::Unknown;
                return false;
            }
        }

        return OutCubemap.IsValid();
    }

    FVector DirectionFromEquirectPixelCPU(const FIntPoint& Pixel, const FIntPoint& EyeResolution, double LongitudeSpan, double LatitudeSpan, float& OutLatitude)
    {
        const FVector2D UV((static_cast<double>(Pixel.X) + 0.5) / EyeResolution.X, (static_cast<double>(Pixel.Y) + 0.5) / EyeResolution.Y);
        const double Longitude = (UV.X - 0.5) * LongitudeSpan;
        const double Latitude = (0.5 - UV.Y) * LatitudeSpan;
        OutLatitude = static_cast<float>(Latitude);

        const double CosLat = FMath::Cos(Latitude);
        const double SinLat = FMath::Sin(Latitude);
        const double CosLon = FMath::Cos(Longitude);
        const double SinLon = FMath::Sin(Longitude);

        FVector Direction;
        Direction.X = CosLat * CosLon;
        Direction.Y = SinLat;
        Direction.Z = CosLat * SinLon;
        return Direction.GetSafeNormal();
    }

    FVector DirectionFromFisheyePixelCPU(const FIntPoint& Pixel, const FIntPoint& EyeResolution, double FovRadians, bool& bOutValid)
    {
        if (EyeResolution.X <= 0 || EyeResolution.Y <= 0)
        {
            bOutValid = false;
            return FVector::ZeroVector;
        }

        const FVector2D UV((static_cast<double>(Pixel.X) + 0.5) / EyeResolution.X, (static_cast<double>(Pixel.Y) + 0.5) / EyeResolution.Y);
        FVector2D Normalized = FVector2D(UV.X * 2.0 - 1.0, 1.0 - UV.Y * 2.0);

        const double Radius = Normalized.Size();
        if (Radius > 1.0)
        {
            bOutValid = false;
            return FVector::ZeroVector;
        }

        const double HalfFov = FMath::Clamp(FovRadians * 0.5, 0.0, PI);
        const double Theta = Radius * HalfFov;
        const double Phi = FMath::Atan2(Normalized.Y, Normalized.X);
        const double SinTheta = FMath::Sin(Theta);

        FVector Direction;
        Direction.X = FMath::Cos(Theta);
        Direction.Y = SinTheta * FMath::Sin(Phi);
        Direction.Z = SinTheta * FMath::Cos(Phi);

        bOutValid = true;
        return Direction.GetSafeNormal();
    }

    void DirectionToFaceUVCPU(const FVector& Direction, uint32& OutFaceIndex, FVector2D& OutUV, int32 FaceResolution, float SeamStrength)
    {
        const FVector AbsDir = Direction.GetAbs();

        if (AbsDir.X >= AbsDir.Y && AbsDir.X >= AbsDir.Z)
        {
            if (Direction.X > 0.0f)
            {
                OutFaceIndex = 0;
                OutUV = FVector2D(-Direction.Z, Direction.Y) / AbsDir.X;
            }
            else
            {
                OutFaceIndex = 1;
                OutUV = FVector2D(Direction.Z, Direction.Y) / AbsDir.X;
            }
        }
        else if (AbsDir.Y >= AbsDir.X && AbsDir.Y >= AbsDir.Z)
        {
            if (Direction.Y > 0.0f)
            {
                OutFaceIndex = 2;
                OutUV = FVector2D(Direction.X, -Direction.Z) / AbsDir.Y;
            }
            else
            {
                OutFaceIndex = 3;
                OutUV = FVector2D(Direction.X, Direction.Z) / AbsDir.Y;
            }
        }
        else
        {
            if (Direction.Z > 0.0f)
            {
                OutFaceIndex = 4;
                OutUV = FVector2D(Direction.X, Direction.Y) / AbsDir.Z;
            }
            else
            {
                OutFaceIndex = 5;
                OutUV = FVector2D(-Direction.X, Direction.Y) / AbsDir.Z;
            }
        }

        OutUV = (OutUV + FVector2D(1.0, 1.0)) * 0.5f;

        const double Resolution = static_cast<double>(FMath::Max(1, FaceResolution));
        const double Scale = FMath::Lerp(1.0, (Resolution - 1.0) / Resolution, SeamStrength);
        const double Bias = (0.5 / Resolution) * SeamStrength;
        OutUV = FVector2D(OutUV.X * Scale + Bias, OutUV.Y * Scale + Bias);
        OutUV.X = FMath::Clamp(OutUV.X, 0.0f, 1.0f);
        OutUV.Y = FMath::Clamp(OutUV.Y, 0.0f, 1.0f);
    }

    FLinearColor SampleCubemapCPU(const FCPUCubemap& Cubemap, const FVector& Direction, int32 FaceResolution, float SeamStrength)
    {
        uint32 FaceIndex = 0;
        FVector2D FaceUV = FVector2D::ZeroVector;
        DirectionToFaceUVCPU(Direction, FaceIndex, FaceUV, FaceResolution, SeamStrength);

        const FCPUFaceData& Face = Cubemap.Faces[FaceIndex];
        const int32 SampleX = FMath::Clamp(static_cast<int32>(FaceUV.X * (Face.Resolution - 1)), 0, Face.Resolution - 1);
        const int32 SampleY = FMath::Clamp(static_cast<int32>(FaceUV.Y * (Face.Resolution - 1)), 0, Face.Resolution - 1);
        const int32 SampleIndex = SampleY * Face.Resolution + SampleX;

        return Face.Pixels.IsValidIndex(SampleIndex)
            ? FLinearColor(Face.Pixels[SampleIndex])
            : FLinearColor::Black;
    }

    void ApplyPolarMitigation(float PolarStrength, float Latitude, FVector& Direction)
    {
        if (PolarStrength <= 0.0f)
        {
            return;
        }

        double PoleFactor = FMath::Clamp(FMath::Abs(Latitude) / (PI * 0.5), 0.0, 1.0);
        PoleFactor = FMath::Pow(PoleFactor, 4.0);
        const double Blend = PoleFactor * PolarStrength;
        if (Blend <= 0.0)
        {
            return;
        }

        const FVector PoleVector(0.0f, Latitude >= 0.0f ? 1.0f : -1.0f, 0.0f);
        Direction = FVector(FMath::Lerp(Direction.X, PoleVector.X, Blend),
            FMath::Lerp(Direction.Y, PoleVector.Y, Blend),
            FMath::Lerp(Direction.Z, PoleVector.Z, Blend));
        Direction.Normalize();
    }

    void AddYUVConversionPasses(
        FRDGBuilder& GraphBuilder,
        const FOmniCaptureSettings& Settings,
        bool bSourceLinear,
        int32 OutputWidth,
        int32 OutputHeight,
        FRDGTextureRef SourceTexture,
        FRDGTextureRef& OutLuma,
        FRDGTextureRef& OutChroma)
    {
        if (!SourceTexture)
        {
            return;
        }

        const bool bNV12 = Settings.NVENCColorFormat == EOmniCaptureColorFormat::NV12;
        const bool bP010 = Settings.NVENCColorFormat == EOmniCaptureColorFormat::P010;
        if (!bNV12 && !bP010)
        {
            return;
        }

        const EPixelFormat LumaFormat = bNV12 ? PF_R8 : PF_R16_UINT;
        const EPixelFormat ChromaFormat = bNV12 ? PF_R8G8 : PF_R16G16_UINT;

        FRDGTextureDesc LumaDesc = FRDGTextureDesc::Create2D(FIntPoint(OutputWidth, OutputHeight), LumaFormat, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV);
        FRDGTextureDesc ChromaDesc = FRDGTextureDesc::Create2D(FIntPoint(FMath::Max(OutputWidth / 2, 1), FMath::Max(OutputHeight / 2, 1)), ChromaFormat, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV);

        OutLuma = GraphBuilder.CreateTexture(LumaDesc, TEXT("OmniNVENC_Luma"));
        OutChroma = GraphBuilder.CreateTexture(ChromaDesc, TEXT("OmniNVENC_Chroma"));

        FOmniConvertToYUVLumaCS::FParameters* LumaParameters = GraphBuilder.AllocParameters<FOmniConvertToYUVLumaCS::FParameters>();
        LumaParameters->OutputSize = FVector2f(OutputWidth, OutputHeight);
        LumaParameters->ChromaSize = FVector2f(ChromaDesc.Extent.X, ChromaDesc.Extent.Y);
        LumaParameters->Format = bNV12 ? 0 : 1;
        LumaParameters->ColorSpace = static_cast<int32>(Settings.ColorSpace);
        LumaParameters->bLinearInput = bSourceLinear ? 1 : 0;
        LumaParameters->SourceTexture = SourceTexture;
        LumaParameters->SourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
        LumaParameters->LumaOutput = GraphBuilder.CreateUAV(OutLuma);

        TShaderMapRef<FOmniConvertToYUVLumaCS> LumaShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        const FIntVector LumaGroupCount(
            FMath::DivideAndRoundUp(OutputWidth, 8),
            FMath::DivideAndRoundUp(OutputHeight, 8),
            1);

        FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("OmniCapture::YUVLuma"), LumaShader, LumaParameters, LumaGroupCount);

        FOmniConvertToYUVChromaCS::FParameters* ChromaParameters = GraphBuilder.AllocParameters<FOmniConvertToYUVChromaCS::FParameters>();
        ChromaParameters->OutputSize = FVector2f(OutputWidth, OutputHeight);
        ChromaParameters->ChromaSize = FVector2f(ChromaDesc.Extent.X, ChromaDesc.Extent.Y);
        ChromaParameters->Format = bNV12 ? 0 : 1;
        ChromaParameters->ColorSpace = static_cast<int32>(Settings.ColorSpace);
        ChromaParameters->bLinearInput = bSourceLinear ? 1 : 0;
        ChromaParameters->SourceTexture = SourceTexture;
        ChromaParameters->SourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
        ChromaParameters->ChromaOutput = GraphBuilder.CreateUAV(OutChroma);

        TShaderMapRef<FOmniConvertToYUVChromaCS> ChromaShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        const FIntVector ChromaGroupCount(
            FMath::DivideAndRoundUp(ChromaDesc.Extent.X, 8),
            FMath::DivideAndRoundUp(ChromaDesc.Extent.Y, 8),
            1);

        FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("OmniCapture::YUVChroma"), ChromaShader, ChromaParameters, ChromaGroupCount);
    }

    FRDGTextureRef AddBGRAPackingPass(
        FRDGBuilder& GraphBuilder,
        const FOmniCaptureSettings& Settings,
        bool bSourceLinear,
        int32 OutputWidth,
        int32 OutputHeight,
        FRDGTextureRef SourceTexture)
    {
        if (!SourceTexture)
        {
            return nullptr;
        }

        FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(FIntPoint(OutputWidth, OutputHeight), PF_B8G8R8A8, FClearValueBinding::Transparent, TexCreate_ShaderResource | TexCreate_UAV);
        FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(Desc, TEXT("OmniNVENC_BGRA"));

        FOmniConvertToBGRACS::FParameters* Parameters = GraphBuilder.AllocParameters<FOmniConvertToBGRACS::FParameters>();
        Parameters->OutputSize = FVector2f(OutputWidth, OutputHeight);
        Parameters->ChromaSize = FVector2f(OutputWidth * 0.5f, OutputHeight * 0.5f);
        Parameters->Format = 0;
        Parameters->ColorSpace = static_cast<int32>(Settings.ColorSpace);
        Parameters->bLinearInput = bSourceLinear ? 1 : 0;
        Parameters->SourceTexture = SourceTexture;
        Parameters->SourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
        Parameters->OutputTexture = GraphBuilder.CreateUAV(OutputTexture);

        TShaderMapRef<FOmniConvertToBGRACS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        const FIntVector GroupCount(
            FMath::DivideAndRoundUp(OutputWidth, 8),
            FMath::DivideAndRoundUp(OutputHeight, 8),
            1);

        FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("OmniCapture::BGRAPack"), Shader, Parameters, GroupCount);

        return OutputTexture;
    }

    FRDGTextureRef BuildFaceArray(FRDGBuilder& GraphBuilder, const TArray<FTextureRHIRef, TInlineAllocator<6>>& Faces, int32 FaceResolution, EPixelFormat PixelFormat, const TCHAR* DebugName)
    {
        if (Faces.Num() == 0)
        {
            return nullptr;
        }

        FRDGTextureDesc ArrayDesc = FRDGTextureDesc::Create2DArray(FIntPoint(FaceResolution, FaceResolution), PixelFormat, FClearValueBinding::Transparent, TexCreate_ShaderResource | TexCreate_UAV, Faces.Num());
        FRDGTextureRef ArrayTexture = GraphBuilder.CreateTexture(ArrayDesc, DebugName);

        for (int32 Index = 0; Index < Faces.Num(); ++Index)
        {
            if (!Faces[Index].IsValid())
            {
                continue;
            }

            FRDGTextureRef SourceTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Faces[Index], *FString::Printf(TEXT("%sFace%d"), DebugName, Index)));
            FRHICopyTextureInfo CopyInfo;
            CopyInfo.SourceSliceIndex = 0;
            CopyInfo.DestSliceIndex = Index;
            CopyInfo.NumSlices = 1;

            AddCopyTexturePass(GraphBuilder, SourceTexture, ArrayTexture, CopyInfo);
        }

        return ArrayTexture;
    }

    void ConvertOnRenderThread(const FOmniCaptureSettings Settings, const TArray<FTextureRHIRef, TInlineAllocator<6>> LeftFaces, const TArray<FTextureRHIRef, TInlineAllocator<6>> RightFaces, FOmniCaptureEquirectResult& OutResult)
    {
        const int32 FaceResolution = Settings.Resolution;
        const bool bStereo = Settings.Mode == EOmniCaptureMode::Stereo;
        const bool bSideBySide = bStereo && Settings.StereoLayout == EOmniCaptureStereoLayout::SideBySide;
        const FIntPoint OutputSize = Settings.GetEquirectResolution();
        const int32 OutputWidth = OutputSize.X;
        const int32 OutputHeight = OutputSize.Y;
        const bool bUseLinear = Settings.Gamma == EOmniCaptureGamma::Linear;
        const float LongitudeSpan = Settings.GetLongitudeSpanRadians();
        const float LatitudeSpan = Settings.GetLatitudeSpanRadians();
        const bool bHalfSphere = Settings.IsVR180();

        FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
        FRDGBuilder GraphBuilder(RHICmdList);

        EOmniCapturePixelPrecision Precision = ResolvePrecisionFromTextures(LeftFaces);
        if (Precision == EOmniCapturePixelPrecision::Unknown)
        {
            Precision = Settings.HDRPrecision == EOmniCaptureHDRPrecision::FullFloat
                ? EOmniCapturePixelPrecision::FullFloat
                : EOmniCapturePixelPrecision::HalfFloat;
        }

        const EPixelFormat FacePixelFormat = GetPixelFormatForPrecision(Precision);

        FRDGTextureRef LeftArray = BuildFaceArray(GraphBuilder, LeftFaces, FaceResolution, FacePixelFormat, TEXT("OmniLeftFaces"));
        FRDGTextureRef RightArray = bStereo ? BuildFaceArray(GraphBuilder, RightFaces, FaceResolution, FacePixelFormat, TEXT("OmniRightFaces")) : LeftArray;

        if (!LeftArray)
        {
            GraphBuilder.Execute();
            return;
        }

        FRDGTextureDesc OutputDesc = FRDGTextureDesc::Create2D(FIntPoint(OutputWidth, OutputHeight), FacePixelFormat, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV | TexCreate_RenderTargetable);
        FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("OmniEquirectOutput"));

        FOmniEquirectCS::FParameters* Parameters = GraphBuilder.AllocParameters<FOmniEquirectCS::FParameters>();
        Parameters->OutputResolution = FVector2f(OutputWidth, OutputHeight);
        Parameters->FaceResolution = FaceResolution;
        Parameters->bStereo = bStereo ? 1 : 0;
        Parameters->SeamStrength = Settings.SeamBlend;
        Parameters->PolarStrength = Settings.PolarDampening;
        Parameters->StereoLayout = Settings.StereoLayout == EOmniCaptureStereoLayout::TopBottom ? 0 : 1;
        Parameters->Padding = 0.0f;
        Parameters->LongitudeSpan = LongitudeSpan;
        Parameters->LatitudeSpan = LatitudeSpan;
        Parameters->bHalfSphere = bHalfSphere ? 1 : 0;
        Parameters->LeftFaces = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(LeftArray));
        Parameters->RightFaces = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(RightArray));
        Parameters->FaceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
        Parameters->OutputTexture = GraphBuilder.CreateUAV(OutputTexture);

        TShaderMapRef<FOmniEquirectCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        const FIntVector GroupCount(
            FMath::DivideAndRoundUp(OutputWidth, 8),
            FMath::DivideAndRoundUp(OutputHeight, 8),
            1);

        FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("OmniCapture::Equirect"), ComputeShader, Parameters, GroupCount);

        FRDGTextureRef LumaTexture = nullptr;
        FRDGTextureRef ChromaTexture = nullptr;
        FRDGTextureRef BGRATexture = nullptr;
        if (Settings.OutputFormat == EOmniOutputFormat::NVENCHardware)
        {
            if (Settings.NVENCColorFormat == EOmniCaptureColorFormat::BGRA)
            {
                BGRATexture = AddBGRAPackingPass(GraphBuilder, Settings, bUseLinear, OutputWidth, OutputHeight, OutputTexture);
            }
            else
            {
                AddYUVConversionPasses(GraphBuilder, Settings, bUseLinear, OutputWidth, OutputHeight, OutputTexture, LumaTexture, ChromaTexture);
            }
        }

        TRefCountPtr<IPooledRenderTarget> ExtractedOutput;
        TRefCountPtr<IPooledRenderTarget> ExtractedLuma;
        TRefCountPtr<IPooledRenderTarget> ExtractedChroma;
        TRefCountPtr<IPooledRenderTarget> ExtractedBGRA;
        GraphBuilder.QueueTextureExtraction(OutputTexture, &ExtractedOutput);
        if (LumaTexture)
        {
            GraphBuilder.QueueTextureExtraction(LumaTexture, &ExtractedLuma);
        }
        if (ChromaTexture)
        {
            GraphBuilder.QueueTextureExtraction(ChromaTexture, &ExtractedChroma);
        }
        if (BGRATexture)
        {
            GraphBuilder.QueueTextureExtraction(BGRATexture, &ExtractedBGRA);
        }
        GraphBuilder.Execute();

        if (!ExtractedOutput.IsValid())
        {
            return;
        }

        OutResult.bUsedCPUFallback = false;
        OutResult.OutputTarget = ExtractedOutput;
        if (FRHITexture* OutputRHI = ExtractedOutput->GetRHI())
        {
            OutResult.Texture = OutputRHI;
        }
        OutResult.Size = FIntPoint(OutputWidth, OutputHeight);
        OutResult.bIsLinear = bUseLinear;

        if (ExtractedLuma.IsValid())
        {
            OutResult.EncoderPlanes.Add(ExtractedLuma);
        }
        if (ExtractedChroma.IsValid())
        {
            OutResult.EncoderPlanes.Add(ExtractedChroma);
        }
        if (ExtractedBGRA.IsValid())
        {
            OutResult.EncoderPlanes.Add(ExtractedBGRA);

            if (FRHITexture* BGRATextureRHI = ExtractedBGRA->GetRHI())
            {
                OutResult.Texture = BGRATextureRHI;
            }
        }

        if (OutResult.Texture.IsValid())
        {
            FGPUFenceRHIRef Fence = RHICreateGPUFence(TEXT("OmniEquirectFence"));
            if (Fence.IsValid())
            {
                RHICmdList.WriteGPUFence(Fence);
                OutResult.ReadyFence = Fence;
            }
        }

        FRHITexture* OutputTextureRHI = ExtractedOutput->GetRHI();
        if (!OutputTextureRHI)
        {
            return;
        }

        FRHIGPUTextureReadback Readback(TEXT("OmniEquirectReadback"));
        Readback.EnqueueCopy(RHICmdList, OutputTextureRHI, FResolveRect(0, 0, OutputWidth, OutputHeight));
        RHICmdList.SubmitCommandsAndFlushGPU();

        while (!Readback.IsReady())
        {
            FPlatformProcess::SleepNoStats(0.001f);
        }

        const uint32 PixelCount = OutputWidth * OutputHeight;
        const uint32 BytesPerPixel = Precision == EOmniCapturePixelPrecision::FullFloat ? sizeof(FLinearColor) : sizeof(FFloat16Color);
        int32 RowPitchInBytes = 0;
        const uint8* RawData = static_cast<const uint8*>(Readback.Lock(RowPitchInBytes));

        if (RawData)
        {
            const uint32 RowStrideInBytes = RowPitchInBytes > 0
                ? static_cast<uint32>(RowPitchInBytes)
                : static_cast<uint32>(OutputWidth * BytesPerPixel);
            if (bUseLinear)
            {
                if (Precision == EOmniCapturePixelPrecision::FullFloat)
                {
                    TUniquePtr<TImagePixelData<FLinearColor>> PixelData = MakeUnique<TImagePixelData<FLinearColor>>(FIntPoint(OutputWidth, OutputHeight));
                    PixelData->Pixels.SetNum(PixelCount);

                    FLinearColor* DestData = PixelData->Pixels.GetData();
                    for (int32 Row = 0; Row < OutputHeight; ++Row)
                    {
                        const uint8* SourceRowBytes = RawData + RowStrideInBytes * Row;
                        const FLinearColor* SourceRow = reinterpret_cast<const FLinearColor*>(SourceRowBytes);
                        FMemory::Memcpy(DestData + Row * OutputWidth, SourceRow, OutputWidth * BytesPerPixel);
                    }

                    OutResult.PixelData = MoveTemp(PixelData);
                    OutResult.PixelDataType = EOmniCapturePixelDataType::LinearColorFloat32;

                    OutResult.PreviewPixels.SetNum(PixelCount);
                    const TImagePixelData<FLinearColor>* FloatData = static_cast<const TImagePixelData<FLinearColor>*>(OutResult.PixelData.Get());
                    if (FloatData)
                    {
                        for (uint32 Index = 0; Index < PixelCount; ++Index)
                        {
                            OutResult.PreviewPixels[Index] = FloatData->Pixels[Index].ToFColor(true);
                        }
                    }
                }
                else
                {
                    TUniquePtr<TImagePixelData<FFloat16Color>> PixelData = MakeUnique<TImagePixelData<FFloat16Color>>(FIntPoint(OutputWidth, OutputHeight));
                    PixelData->Pixels.SetNum(PixelCount);

                    FFloat16Color* DestData = PixelData->Pixels.GetData();
                    for (int32 Row = 0; Row < OutputHeight; ++Row)
                    {
                        const uint8* SourceRowBytes = RawData + RowStrideInBytes * Row;
                        const FFloat16Color* SourceRow = reinterpret_cast<const FFloat16Color*>(SourceRowBytes);
                        FMemory::Memcpy(DestData + Row * OutputWidth, SourceRow, OutputWidth * BytesPerPixel);
                    }

                    OutResult.PixelData = MoveTemp(PixelData);
                    OutResult.PixelDataType = EOmniCapturePixelDataType::LinearColorFloat16;

                    OutResult.PreviewPixels.SetNum(PixelCount);
                    const TImagePixelData<FFloat16Color>* FloatData = static_cast<const TImagePixelData<FFloat16Color>*>(OutResult.PixelData.Get());
                    if (FloatData)
                    {
                        for (uint32 Index = 0; Index < PixelCount; ++Index)
                        {
                            const FFloat16Color& Source = FloatData->Pixels[Index];
                            const FLinearColor Linear(Source.R.GetFloat(), Source.G.GetFloat(), Source.B.GetFloat(), Source.A.GetFloat());
                            OutResult.PreviewPixels[Index] = Linear.ToFColor(true);
                        }
                    }
                }
            }
            else
            {
                TUniquePtr<TImagePixelData<FColor>> PixelData = MakeUnique<TImagePixelData<FColor>>(FIntPoint(OutputWidth, OutputHeight));
                PixelData->Pixels.SetNum(PixelCount);
                OutResult.PreviewPixels.SetNum(PixelCount);

                for (int32 Row = 0; Row < OutputHeight; ++Row)
                {
                    const uint8* SourceRow = RawData + RowStrideInBytes * Row;
                    FColor* DestRow = PixelData->Pixels.GetData() + Row * OutputWidth;
                    for (int32 Column = 0; Column < OutputWidth; ++Column)
                    {
                        FLinearColor Linear;
                        if (Precision == EOmniCapturePixelPrecision::FullFloat)
                        {
                            const FLinearColor* Pixel = reinterpret_cast<const FLinearColor*>(SourceRow) + Column;
                            Linear = *Pixel;
                        }
                        else
                        {
                            const FFloat16Color* Pixel = reinterpret_cast<const FFloat16Color*>(SourceRow) + Column;
                            Linear = FLinearColor(Pixel->R.GetFloat(), Pixel->G.GetFloat(), Pixel->B.GetFloat(), Pixel->A.GetFloat());
                        }
                        const FColor SRGB = Linear.ToFColor(true);
                        DestRow[Column] = SRGB;
                        OutResult.PreviewPixels[Row * OutputWidth + Column] = SRGB;
                    }
                }

                OutResult.PixelData = MoveTemp(PixelData);
                OutResult.PixelDataType = EOmniCapturePixelDataType::Color8;
            }
        }

        Readback.Unlock();
        OutResult.PixelPrecision = Precision;
    }

    void ConvertFisheyeOnRenderThread(const FOmniCaptureSettings Settings, const TArray<FTextureRHIRef, TInlineAllocator<6>> LeftFaces, const TArray<FTextureRHIRef, TInlineAllocator<6>> RightFaces, FOmniCaptureEquirectResult& OutResult)
    {
        const int32 FaceResolution = Settings.Resolution;
        const bool bStereo = Settings.Mode == EOmniCaptureMode::Stereo;
        const FIntPoint OutputSize = Settings.GetOutputResolution();
        const FIntPoint EyeSize = Settings.GetFisheyeResolution();
        const bool bUseLinear = Settings.Gamma == EOmniCaptureGamma::Linear;
        const bool bHalfSphere = Settings.IsVR180();
        const float FovRadians = FMath::DegreesToRadians(FMath::Clamp(Settings.FisheyeFOV, 0.0f, 360.0f));

        FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
        FRDGBuilder GraphBuilder(RHICmdList);

        EOmniCapturePixelPrecision Precision = ResolvePrecisionFromTextures(LeftFaces);
        if (Precision == EOmniCapturePixelPrecision::Unknown)
        {
            Precision = Settings.HDRPrecision == EOmniCaptureHDRPrecision::FullFloat
                ? EOmniCapturePixelPrecision::FullFloat
                : EOmniCapturePixelPrecision::HalfFloat;
        }

        const EPixelFormat FacePixelFormat = GetPixelFormatForPrecision(Precision);

        FRDGTextureRef LeftArray = BuildFaceArray(GraphBuilder, LeftFaces, FaceResolution, FacePixelFormat, TEXT("OmniFisheyeLeftFaces"));
        FRDGTextureRef RightArray = bStereo ? BuildFaceArray(GraphBuilder, RightFaces, FaceResolution, FacePixelFormat, TEXT("OmniFisheyeRightFaces")) : LeftArray;

        if (!LeftArray)
        {
            GraphBuilder.Execute();
            return;
        }

        FRDGTextureDesc OutputDesc = FRDGTextureDesc::Create2D(OutputSize, FacePixelFormat, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV | TexCreate_RenderTargetable);
        FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("OmniFisheyeOutput"));

        FOmniFisheyeCS::FParameters* Parameters = GraphBuilder.AllocParameters<FOmniFisheyeCS::FParameters>();
        Parameters->OutputResolution = FVector2f(OutputSize.X, OutputSize.Y);
        Parameters->EyeResolution = FVector2f(EyeSize.X, EyeSize.Y);
        Parameters->FovRadians = FovRadians;
        Parameters->FaceResolution = FaceResolution;
        Parameters->bStereo = bStereo ? 1 : 0;
        Parameters->StereoLayout = Settings.StereoLayout == EOmniCaptureStereoLayout::TopBottom ? 0 : 1;
        Parameters->bHalfSphere = bHalfSphere ? 1 : 0;
        Parameters->SeamStrength = Settings.SeamBlend;
        Parameters->Padding = 0.0f;
        Parameters->LeftFaces = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(LeftArray));
        Parameters->RightFaces = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(RightArray));
        Parameters->FaceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
        Parameters->OutputTexture = GraphBuilder.CreateUAV(OutputTexture);

        TShaderMapRef<FOmniFisheyeCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        const FIntVector GroupCount(
            FMath::DivideAndRoundUp(OutputSize.X, 8),
            FMath::DivideAndRoundUp(OutputSize.Y, 8),
            1);

        FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("OmniCapture::Fisheye"), ComputeShader, Parameters, GroupCount);

        FRDGTextureRef LumaTexture = nullptr;
        FRDGTextureRef ChromaTexture = nullptr;
        FRDGTextureRef BGRATexture = nullptr;

        if (Settings.OutputFormat == EOmniOutputFormat::NVENCHardware)
        {
            if (Settings.NVENCColorFormat == EOmniCaptureColorFormat::BGRA)
            {
                BGRATexture = AddBGRAPackingPass(GraphBuilder, Settings, bUseLinear, OutputSize.X, OutputSize.Y, OutputTexture);
            }
            else
            {
                AddYUVConversionPasses(GraphBuilder, Settings, bUseLinear, OutputSize.X, OutputSize.Y, OutputTexture, LumaTexture, ChromaTexture);
            }
        }

        TRefCountPtr<IPooledRenderTarget> ExtractedOutput;
        TRefCountPtr<IPooledRenderTarget> ExtractedLuma;
        TRefCountPtr<IPooledRenderTarget> ExtractedChroma;
        TRefCountPtr<IPooledRenderTarget> ExtractedBGRA;

        GraphBuilder.QueueTextureExtraction(OutputTexture, &ExtractedOutput);
        if (LumaTexture)
        {
            GraphBuilder.QueueTextureExtraction(LumaTexture, &ExtractedLuma);
        }
        if (ChromaTexture)
        {
            GraphBuilder.QueueTextureExtraction(ChromaTexture, &ExtractedChroma);
        }
        if (BGRATexture)
        {
            GraphBuilder.QueueTextureExtraction(BGRATexture, &ExtractedBGRA);
        }

        GraphBuilder.Execute();

        OutResult.Size = OutputSize;
        OutResult.bIsLinear = bUseLinear;
        OutResult.bUsedCPUFallback = false;
        OutResult.OutputTarget = ExtractedOutput;
        OutResult.GPUSource = ExtractedOutput;
        OutResult.Texture.SafeRelease();
        OutResult.ReadyFence.SafeRelease();
        OutResult.EncoderPlanes.Reset();

        if (ExtractedOutput.IsValid())
        {
            OutResult.Texture = ExtractedOutput->GetRHI();
        }

        if (Settings.OutputFormat == EOmniOutputFormat::NVENCHardware)
        {
            if (Settings.NVENCColorFormat == EOmniCaptureColorFormat::BGRA)
            {
                if (ExtractedBGRA.IsValid())
                {
                    OutResult.EncoderPlanes.Add(ExtractedBGRA);

                    if (FRHITexture* BGRATextureRHI = ExtractedBGRA->GetRHI())
                    {
                        OutResult.Texture = BGRATextureRHI;
                    }
                }
            }
            else
            {
                if (ExtractedLuma.IsValid())
                {
                    OutResult.EncoderPlanes.Add(ExtractedLuma);
                }
                if (ExtractedChroma.IsValid())
                {
                    OutResult.EncoderPlanes.Add(ExtractedChroma);
                }
            }

            if (OutResult.Texture.IsValid())
            {
                FGPUFenceRHIRef Fence = RHICreateGPUFence(TEXT("OmniFisheyeFence"));
                if (Fence.IsValid())
                {
                    RHICmdList.WriteGPUFence(Fence);
                    OutResult.ReadyFence = Fence;
                }
            }
        }

        if (!ExtractedOutput.IsValid())
        {
            return;
        }

        FRHITexture* OutputTextureRHI = ExtractedOutput->GetRHI();
        if (!OutputTextureRHI)
        {
            return;
        }

        FRHIGPUTextureReadback Readback(TEXT("OmniFisheyeReadback"));
        Readback.EnqueueCopy(RHICmdList, OutputTextureRHI, FResolveRect(0, 0, OutputSize.X, OutputSize.Y));
        RHICmdList.SubmitCommandsAndFlushGPU();

        while (!Readback.IsReady())
        {
            FPlatformProcess::SleepNoStats(0.001f);
        }

        const uint32 PixelCount = OutputSize.X * OutputSize.Y;
        const uint32 BytesPerPixel = Precision == EOmniCapturePixelPrecision::FullFloat ? sizeof(FLinearColor) : sizeof(FFloat16Color);
        int32 RowPitchInBytes = 0;
        const uint8* RawData = static_cast<const uint8*>(Readback.Lock(RowPitchInBytes));

        if (RawData)
        {
            const uint32 RowStrideInBytes = RowPitchInBytes > 0
                ? static_cast<uint32>(RowPitchInBytes)
                : static_cast<uint32>(OutputSize.X * BytesPerPixel);
            if (bUseLinear)
            {
                if (Precision == EOmniCapturePixelPrecision::FullFloat)
                {
                    TUniquePtr<TImagePixelData<FLinearColor>> PixelData = MakeUnique<TImagePixelData<FLinearColor>>(OutputSize);
                    PixelData->Pixels.SetNum(PixelCount);

                    FLinearColor* DestData = PixelData->Pixels.GetData();
                    for (int32 Row = 0; Row < OutputSize.Y; ++Row)
                    {
                        const uint8* SourceRowBytes = RawData + RowStrideInBytes * Row;
                        const FLinearColor* SourceRow = reinterpret_cast<const FLinearColor*>(SourceRowBytes);
                        FMemory::Memcpy(DestData + Row * OutputSize.X, SourceRow, OutputSize.X * BytesPerPixel);
                    }

                    OutResult.PixelData = MoveTemp(PixelData);
                    OutResult.PixelDataType = EOmniCapturePixelDataType::LinearColorFloat32;

                    OutResult.PreviewPixels.SetNum(PixelCount);
                    const TImagePixelData<FLinearColor>* FloatData = static_cast<const TImagePixelData<FLinearColor>*>(OutResult.PixelData.Get());
                    if (FloatData)
                    {
                        for (uint32 Index = 0; Index < PixelCount; ++Index)
                        {
                            OutResult.PreviewPixels[Index] = FloatData->Pixels[Index].ToFColor(true);
                        }
                    }
                }
                else
                {
                    TUniquePtr<TImagePixelData<FFloat16Color>> PixelData = MakeUnique<TImagePixelData<FFloat16Color>>(OutputSize);
                    PixelData->Pixels.SetNum(PixelCount);

                    FFloat16Color* DestData = PixelData->Pixels.GetData();
                    for (int32 Row = 0; Row < OutputSize.Y; ++Row)
                    {
                        const uint8* SourceRowBytes = RawData + RowStrideInBytes * Row;
                        const FFloat16Color* SourceRow = reinterpret_cast<const FFloat16Color*>(SourceRowBytes);
                        FMemory::Memcpy(DestData + Row * OutputSize.X, SourceRow, OutputSize.X * BytesPerPixel);
                    }

                    OutResult.PixelData = MoveTemp(PixelData);
                    OutResult.PixelDataType = EOmniCapturePixelDataType::LinearColorFloat16;

                    OutResult.PreviewPixels.SetNum(PixelCount);
                    const TImagePixelData<FFloat16Color>* FloatData = static_cast<const TImagePixelData<FFloat16Color>*>(OutResult.PixelData.Get());
                    if (FloatData)
                    {
                        for (uint32 Index = 0; Index < PixelCount; ++Index)
                        {
                            const FFloat16Color& Source = FloatData->Pixels[Index];
                            const FLinearColor Linear(Source.R.GetFloat(), Source.G.GetFloat(), Source.B.GetFloat(), Source.A.GetFloat());
                            OutResult.PreviewPixels[Index] = Linear.ToFColor(true);
                        }
                    }
                }
            }
            else
            {
                TUniquePtr<TImagePixelData<FColor>> PixelData = MakeUnique<TImagePixelData<FColor>>(OutputSize);
                PixelData->Pixels.SetNum(PixelCount);
                OutResult.PreviewPixels.SetNum(PixelCount);

                for (int32 Row = 0; Row < OutputSize.Y; ++Row)
                {
                    const uint8* SourceRow = RawData + RowStrideInBytes * Row;
                    FColor* DestRow = PixelData->Pixels.GetData() + Row * OutputSize.X;
                    for (int32 Column = 0; Column < OutputSize.X; ++Column)
                    {
                        FLinearColor Linear;
                        if (Precision == EOmniCapturePixelPrecision::FullFloat)
                        {
                            const FLinearColor* Pixel = reinterpret_cast<const FLinearColor*>(SourceRow) + Column;
                            Linear = *Pixel;
                        }
                        else
                        {
                            const FFloat16Color* Pixel = reinterpret_cast<const FFloat16Color*>(SourceRow) + Column;
                            Linear = FLinearColor(Pixel->R.GetFloat(), Pixel->G.GetFloat(), Pixel->B.GetFloat(), Pixel->A.GetFloat());
                        }
                        const FColor SRGB = Linear.ToFColor(true);
                        DestRow[Column] = SRGB;
                        OutResult.PreviewPixels[Row * OutputSize.X + Column] = SRGB;
                    }
                }

                OutResult.PixelData = MoveTemp(PixelData);
                OutResult.PixelDataType = EOmniCapturePixelDataType::Color8;
            }
        }

        Readback.Unlock();
        OutResult.PixelPrecision = Precision;
    }
}

    void ConvertPlanarOnRenderThread(const FOmniCaptureSettings Settings, FTextureRHIRef SourceTexture, const FIntPoint OutputSize, bool bSourceLinear, FOmniCaptureEquirectResult& OutResult)
    {
        if (!SourceTexture.IsValid())
        {
            return;
        }

        const int32 OutputWidth = OutputSize.X;
        const int32 OutputHeight = OutputSize.Y;

        FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
        FRDGBuilder GraphBuilder(RHICmdList);

        EOmniCapturePixelPrecision Precision = PixelPrecisionFromFormat(SourceTexture->GetFormat());
        if (Precision == EOmniCapturePixelPrecision::Unknown)
        {
            Precision = Settings.HDRPrecision == EOmniCaptureHDRPrecision::FullFloat
                ? EOmniCapturePixelPrecision::FullFloat
                : EOmniCapturePixelPrecision::HalfFloat;
        }

        FRDGTextureRef Source = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(SourceTexture, TEXT("OmniPlanarSource")));

        FRDGTextureRef LumaTexture = nullptr;
        FRDGTextureRef ChromaTexture = nullptr;
        FRDGTextureRef BGRATexture = nullptr;

        if (Settings.OutputFormat == EOmniOutputFormat::NVENCHardware)
        {
            if (Settings.NVENCColorFormat == EOmniCaptureColorFormat::BGRA)
            {
                BGRATexture = AddBGRAPackingPass(GraphBuilder, Settings, bSourceLinear, OutputWidth, OutputHeight, Source);
            }
            else
            {
                AddYUVConversionPasses(GraphBuilder, Settings, bSourceLinear, OutputWidth, OutputHeight, Source, LumaTexture, ChromaTexture);
            }
        }

        TRefCountPtr<IPooledRenderTarget> ExtractedSource;
        TRefCountPtr<IPooledRenderTarget> ExtractedLuma;
        TRefCountPtr<IPooledRenderTarget> ExtractedChroma;
        TRefCountPtr<IPooledRenderTarget> ExtractedBGRA;

        GraphBuilder.QueueTextureExtraction(Source, &ExtractedSource);
        if (LumaTexture)
        {
            GraphBuilder.QueueTextureExtraction(LumaTexture, &ExtractedLuma);
        }
        if (ChromaTexture)
        {
            GraphBuilder.QueueTextureExtraction(ChromaTexture, &ExtractedChroma);
        }
        if (BGRATexture)
        {
            GraphBuilder.QueueTextureExtraction(BGRATexture, &ExtractedBGRA);
        }

        GraphBuilder.Execute();

        OutResult.OutputTarget = ExtractedSource;
        OutResult.GPUSource = ExtractedSource;
        OutResult.Texture = SourceTexture;
        OutResult.Size = OutputSize;
        OutResult.bIsLinear = bSourceLinear;
        OutResult.bUsedCPUFallback = false;
        OutResult.EncoderPlanes.Reset();
        OutResult.ReadyFence.SafeRelease();
        OutResult.PixelPrecision = Precision;

        if (Settings.OutputFormat == EOmniOutputFormat::NVENCHardware)
        {
            if (Settings.NVENCColorFormat == EOmniCaptureColorFormat::BGRA)
            {
                if (ExtractedBGRA.IsValid())
                {
                    OutResult.EncoderPlanes.Add(ExtractedBGRA);
                    if (FRHITexture* BGRAResource = ExtractedBGRA->GetRHI())
                    {
                        OutResult.Texture = BGRAResource;
                    }
                }
            }
            else
            {
                if (ExtractedLuma.IsValid())
                {
                    OutResult.EncoderPlanes.Add(ExtractedLuma);
                }
                if (ExtractedChroma.IsValid())
                {
                    OutResult.EncoderPlanes.Add(ExtractedChroma);
                }
            }

            if (OutResult.Texture.IsValid())
            {
                FGPUFenceRHIRef Fence = RHICreateGPUFence(TEXT("OmniPlanarFence"));
                if (Fence.IsValid())
                {
                    RHICmdList.WriteGPUFence(Fence);
                    OutResult.ReadyFence = Fence;
                }
            }
        }
    }

namespace
{
    void ConvertOnCPU(const FOmniCaptureSettings& Settings, const FOmniEyeCapture& LeftEye, const FOmniEyeCapture& RightEye, FOmniCaptureEquirectResult& OutResult)
    {
        FCPUCubemap LeftCubemap;
        if (!BuildCPUCubemap(LeftEye, LeftCubemap))
        {
            return;
        }

        FCPUCubemap RightCubemap;
        if (Settings.Mode == EOmniCaptureMode::Stereo)
        {
            if (!BuildCPUCubemap(RightEye, RightCubemap))
            {
                return;
            }
        }

        const bool bStereo = Settings.Mode == EOmniCaptureMode::Stereo;
        const bool bSideBySide = bStereo && Settings.StereoLayout == EOmniCaptureStereoLayout::SideBySide;
        const int32 FaceResolution = LeftCubemap.Faces[0].Resolution;
        const FIntPoint OutputSize = Settings.GetEquirectResolution();
        const int32 OutputWidth = OutputSize.X;
        const int32 OutputHeight = OutputSize.Y;
        const double LongitudeSpan = Settings.GetLongitudeSpanRadians();
        const double LatitudeSpan = Settings.GetLatitudeSpanRadians();
        const bool bHalfSphere = Settings.IsVR180();

        OutResult.Size = FIntPoint(OutputWidth, OutputHeight);
        OutResult.bIsLinear = Settings.Gamma == EOmniCaptureGamma::Linear;
        OutResult.bUsedCPUFallback = true;
        OutResult.OutputTarget.SafeRelease();
        OutResult.Texture.SafeRelease();
        OutResult.ReadyFence.SafeRelease();
        OutResult.EncoderPlanes.Reset();

        const int32 PixelCount = OutputWidth * OutputHeight;
        OutResult.PreviewPixels.SetNum(PixelCount);
        OutResult.PixelPrecision = LeftCubemap.Precision;

        auto ProcessPixel = [&](auto& PixelArray, auto ConvertColor)
        {
            for (int32 Y = 0; Y < OutputHeight; ++Y)
            {
                for (int32 X = 0; X < OutputWidth; ++X)
                {
                    const int32 Index = Y * OutputWidth + X;

                    FIntPoint EyePixel(X, Y);
                    FIntPoint EyeResolution(OutputWidth, OutputHeight);
                    bool bRightEye = false;

                    if (bStereo)
                    {
                        if (bSideBySide)
                        {
                            const int32 EyeWidth = OutputWidth / 2;
                            bRightEye = X >= EyeWidth;
                            EyePixel.X = X % EyeWidth;
                            EyeResolution = FIntPoint(EyeWidth, OutputHeight);
                        }
                        else
                        {
                            const int32 EyeHeight = OutputHeight / 2;
                            bRightEye = Y >= EyeHeight;
                            EyePixel.Y = Y % EyeHeight;
                            EyeResolution = FIntPoint(OutputWidth, EyeHeight);
                        }
                    }

                    float Latitude = 0.0f;
                    FVector Direction = DirectionFromEquirectPixelCPU(EyePixel, EyeResolution, LongitudeSpan, LatitudeSpan, Latitude);
                    ApplyPolarMitigation(Settings.PolarDampening, Latitude, Direction);

                    if (bHalfSphere && Direction.X < 0.0f)
                    {
                        PixelArray[Index] = ConvertColor(FLinearColor::Transparent);
                        OutResult.PreviewPixels[Index] = FColor::Transparent;
                        continue;
                    }

                    const FLinearColor LinearColor = SampleCubemapCPU(
                        (bStereo && bRightEye) ? RightCubemap : LeftCubemap,
                        Direction,
                        FaceResolution,
                        Settings.SeamBlend);

                    PixelArray[Index] = ConvertColor(LinearColor);
                    OutResult.PreviewPixels[Index] = LinearColor.ToFColor(true);
                }
            }
        };

        if (OutResult.bIsLinear)
        {
            if (OutResult.PixelPrecision == EOmniCapturePixelPrecision::FullFloat)
            {
                TUniquePtr<TImagePixelData<FLinearColor>> PixelData = MakeUnique<TImagePixelData<FLinearColor>>(OutResult.Size);
                PixelData->Pixels.SetNum(PixelCount);
                ProcessPixel(PixelData->Pixels, [](const FLinearColor& Linear) { return Linear; });
                OutResult.PixelData = MoveTemp(PixelData);
                OutResult.PixelDataType = EOmniCapturePixelDataType::LinearColorFloat32;
            }
            else
            {
                OutResult.PixelPrecision = EOmniCapturePixelPrecision::HalfFloat;
                TUniquePtr<TImagePixelData<FFloat16Color>> PixelData = MakeUnique<TImagePixelData<FFloat16Color>>(OutResult.Size);
                PixelData->Pixels.SetNum(PixelCount);
                ProcessPixel(PixelData->Pixels, [](const FLinearColor& Linear) { return FFloat16Color(Linear); });
                OutResult.PixelData = MoveTemp(PixelData);
                OutResult.PixelDataType = EOmniCapturePixelDataType::LinearColorFloat16;
            }
        }
        else
        {
            TUniquePtr<TImagePixelData<FColor>> PixelData = MakeUnique<TImagePixelData<FColor>>(OutResult.Size);
            PixelData->Pixels.SetNum(PixelCount);
            ProcessPixel(PixelData->Pixels, [](const FLinearColor& Linear) { return Linear.ToFColor(true); });
            OutResult.PixelData = MoveTemp(PixelData);
            OutResult.PixelDataType = EOmniCapturePixelDataType::Color8;
        }
    }

    void ConvertFisheyeOnCPU(const FOmniCaptureSettings& Settings, const FOmniEyeCapture& LeftEye, const FOmniEyeCapture& RightEye, FOmniCaptureEquirectResult& OutResult)
    {
        FCPUCubemap LeftCubemap;
        if (!BuildCPUCubemap(LeftEye, LeftCubemap))
        {
            return;
        }

        FCPUCubemap RightCubemap;
        if (Settings.Mode == EOmniCaptureMode::Stereo)
        {
            if (!BuildCPUCubemap(RightEye, RightCubemap))
            {
                return;
            }
        }

        const bool bStereo = Settings.Mode == EOmniCaptureMode::Stereo;
        const bool bSideBySide = bStereo && Settings.StereoLayout == EOmniCaptureStereoLayout::SideBySide;
        const int32 FaceResolution = LeftCubemap.Faces[0].Resolution;
        const FIntPoint OutputSize = Settings.GetOutputResolution();
        const FIntPoint EyeSize = Settings.GetFisheyeResolution();
        const bool bHalfSphere = Settings.IsVR180();
        const double FovRadians = FMath::DegreesToRadians(FMath::Clamp(Settings.FisheyeFOV, 0.0f, 360.0f));

        OutResult.Size = OutputSize;
        OutResult.bIsLinear = Settings.Gamma == EOmniCaptureGamma::Linear;
        OutResult.bUsedCPUFallback = true;
        OutResult.OutputTarget.SafeRelease();
        OutResult.Texture.SafeRelease();
        OutResult.ReadyFence.SafeRelease();
        OutResult.EncoderPlanes.Reset();

        const int32 PixelCount = OutputSize.X * OutputSize.Y;
        OutResult.PreviewPixels.SetNum(PixelCount);
        OutResult.PixelPrecision = LeftCubemap.Precision;

        auto ProcessPixel = [&](auto& PixelArray, auto ConvertColor)
        {
            for (int32 Y = 0; Y < OutputSize.Y; ++Y)
            {
                for (int32 X = 0; X < OutputSize.X; ++X)
                {
                    const int32 Index = Y * OutputSize.X + X;

                    FIntPoint EyePixel(X, Y);
                    FIntPoint EyeResolution = EyeSize;
                    bool bRightEye = false;

                    if (bStereo)
                    {
                        if (bSideBySide)
                        {
                            const int32 EyeWidth = FMath::Max(1, EyeSize.X);
                            bRightEye = X >= EyeWidth;
                            EyePixel.X = X % EyeWidth;
                            EyeResolution = FIntPoint(EyeWidth, EyeSize.Y);
                        }
                        else
                        {
                            const int32 EyeHeight = FMath::Max(1, EyeSize.Y);
                            bRightEye = Y >= EyeHeight;
                            EyePixel.Y = Y % EyeHeight;
                            EyeResolution = FIntPoint(EyeSize.X, EyeHeight);
                        }
                    }

                    bool bValid = false;
                    FVector Direction = DirectionFromFisheyePixelCPU(EyePixel, EyeResolution, FovRadians, bValid);
                    if (!bValid)
                    {
                        PixelArray[Index] = ConvertColor(FLinearColor::Transparent);
                        OutResult.PreviewPixels[Index] = FColor::Transparent;
                        continue;
                    }

                    if (bHalfSphere && Direction.X < 0.0f)
                    {
                        PixelArray[Index] = ConvertColor(FLinearColor::Transparent);
                        OutResult.PreviewPixels[Index] = FColor::Transparent;
                        continue;
                    }

                    const FLinearColor LinearColor = SampleCubemapCPU(
                        (bStereo && bRightEye) ? RightCubemap : LeftCubemap,
                        Direction,
                        FaceResolution,
                        Settings.SeamBlend);

                    PixelArray[Index] = ConvertColor(LinearColor);
                    OutResult.PreviewPixels[Index] = LinearColor.ToFColor(true);
                }
            }
        };

        if (OutResult.bIsLinear)
        {
            if (OutResult.PixelPrecision == EOmniCapturePixelPrecision::FullFloat)
            {
                TUniquePtr<TImagePixelData<FLinearColor>> PixelData = MakeUnique<TImagePixelData<FLinearColor>>(OutputSize);
                PixelData->Pixels.SetNum(PixelCount);
                ProcessPixel(PixelData->Pixels, [](const FLinearColor& Linear) { return Linear; });
                OutResult.PixelData = MoveTemp(PixelData);
                OutResult.PixelDataType = EOmniCapturePixelDataType::LinearColorFloat32;
            }
            else
            {
                OutResult.PixelPrecision = EOmniCapturePixelPrecision::HalfFloat;
                TUniquePtr<TImagePixelData<FFloat16Color>> PixelData = MakeUnique<TImagePixelData<FFloat16Color>>(OutputSize);
                PixelData->Pixels.SetNum(PixelCount);
                ProcessPixel(PixelData->Pixels, [](const FLinearColor& Linear) { return FFloat16Color(Linear); });
                OutResult.PixelData = MoveTemp(PixelData);
                OutResult.PixelDataType = EOmniCapturePixelDataType::LinearColorFloat16;
            }
        }
        else
        {
            TUniquePtr<TImagePixelData<FColor>> PixelData = MakeUnique<TImagePixelData<FColor>>(OutputSize);
            PixelData->Pixels.SetNum(PixelCount);
            ProcessPixel(PixelData->Pixels, [](const FLinearColor& Linear) { return Linear.ToFColor(true); });
            OutResult.PixelData = MoveTemp(PixelData);
            OutResult.PixelDataType = EOmniCapturePixelDataType::Color8;
        }
    }
}

FOmniCaptureEquirectResult FOmniCaptureEquirectConverter::ConvertToEquirectangular(const FOmniCaptureSettings& Settings, const FOmniEyeCapture& LeftEye, const FOmniEyeCapture& RightEye)
{
    FOmniCaptureEquirectResult Result;

    if (Settings.Resolution <= 0)
    {
        return Result;
    }

    TArray<FTextureRHIRef, TInlineAllocator<6>> LeftFaces;
    TArray<FTextureRHIRef, TInlineAllocator<6>> RightFaces;

    for (int32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
    {
        if (UTextureRenderTarget2D* LeftTarget = LeftEye.Faces[FaceIndex].RenderTarget)
        {
            if (FTextureRenderTargetResource* Resource = LeftTarget->GameThread_GetRenderTargetResource())
            {
                if (FTextureRHIRef Texture = Resource->GetTextureRHI())
                {
                    LeftFaces.Add(Texture);
                }
            }
        }

        if (Settings.Mode == EOmniCaptureMode::Stereo)
        {
            if (UTextureRenderTarget2D* RightTarget = RightEye.Faces[FaceIndex].RenderTarget)
            {
                if (FTextureRenderTargetResource* Resource = RightTarget->GameThread_GetRenderTargetResource())
                {
                    if (FTextureRHIRef Texture = Resource->GetTextureRHI())
                    {
                        RightFaces.Add(Texture);
                    }
                }
            }
        }
    }

    if (LeftFaces.Num() != 6)
    {
        return Result;
    }

    if (Settings.Mode == EOmniCaptureMode::Stereo && RightFaces.Num() != 6)
    {
        return Result;
    }

    bool bSupportsCompute = GDynamicRHI != nullptr;
#if defined(GRHISupportsComputeShaders)
    bSupportsCompute = bSupportsCompute && GRHISupportsComputeShaders;
#elif defined(GSupportsComputeShaders)
    bSupportsCompute = bSupportsCompute && GSupportsComputeShaders;
#else
    bSupportsCompute = false;
#endif
    if (!bSupportsCompute)
    {
        ConvertOnCPU(Settings, LeftEye, RightEye, Result);
        return Result;
    }

    FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool();

    ENQUEUE_RENDER_COMMAND(OmniCaptureEquirect)([Settings, LeftFaces, RightFaces, &Result, CompletionEvent](FRHICommandListImmediate&)
    {
        ConvertOnRenderThread(Settings, LeftFaces, RightFaces, Result);
        CompletionEvent->Trigger();
    });

    CompletionEvent->Wait();
    FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);

    if (!Result.PixelData.IsValid() && (!Result.Texture.IsValid() || !Result.OutputTarget.IsValid()))
    {
        ConvertOnCPU(Settings, LeftEye, RightEye, Result);
    }

    return Result;
}

FOmniCaptureEquirectResult FOmniCaptureEquirectConverter::ConvertToFisheye(const FOmniCaptureSettings& Settings, const FOmniEyeCapture& LeftEye, const FOmniEyeCapture& RightEye)
{
    FOmniCaptureEquirectResult Result;

    if (!Settings.IsFisheye() || Settings.Resolution <= 0)
    {
        return Result;
    }

    TArray<FTextureRHIRef, TInlineAllocator<6>> LeftFaces;
    TArray<FTextureRHIRef, TInlineAllocator<6>> RightFaces;

    for (int32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
    {
        if (UTextureRenderTarget2D* LeftTarget = LeftEye.Faces[FaceIndex].RenderTarget)
        {
            if (FTextureRenderTargetResource* Resource = LeftTarget->GameThread_GetRenderTargetResource())
            {
                if (FTextureRHIRef Texture = Resource->GetTextureRHI())
                {
                    LeftFaces.Add(Texture);
                }
            }
        }

        if (Settings.Mode == EOmniCaptureMode::Stereo)
        {
            if (UTextureRenderTarget2D* RightTarget = RightEye.Faces[FaceIndex].RenderTarget)
            {
                if (FTextureRenderTargetResource* Resource = RightTarget->GameThread_GetRenderTargetResource())
                {
                    if (FTextureRHIRef Texture = Resource->GetTextureRHI())
                    {
                        RightFaces.Add(Texture);
                    }
                }
            }
        }
    }

    if (LeftFaces.Num() != 6)
    {
        return Result;
    }

    if (Settings.Mode == EOmniCaptureMode::Stereo && RightFaces.Num() != 6)
    {
        return Result;
    }

    bool bSupportsCompute = GDynamicRHI != nullptr;
#if defined(GRHISupportsComputeShaders)
    bSupportsCompute = bSupportsCompute && GRHISupportsComputeShaders;
#elif defined(GSupportsComputeShaders)
    bSupportsCompute = bSupportsCompute && GSupportsComputeShaders;
#else
    bSupportsCompute = false;
#endif

    if (bSupportsCompute)
    {
        FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool();
        ENQUEUE_RENDER_COMMAND(OmniCaptureFisheyeConvert)([Settings, LeftFaces, RightFaces, &Result, CompletionEvent](FRHICommandListImmediate&)
        {
            ConvertFisheyeOnRenderThread(Settings, LeftFaces, RightFaces.Num() > 0 ? RightFaces : LeftFaces, Result);
            CompletionEvent->Trigger();
        });

        CompletionEvent->Wait();
        FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);
    }
    else
    {
        ConvertFisheyeOnCPU(Settings, LeftEye, RightEye, Result);
    }

    return Result;
}

FOmniCaptureEquirectResult FOmniCaptureEquirectConverter::ConvertToPlanar(const FOmniCaptureSettings& Settings, const FOmniEyeCapture& SourceEye)
{
    FOmniCaptureEquirectResult Result;

    if (!Settings.IsPlanar())
    {
        return Result;
    }

    UTextureRenderTarget2D* RenderTarget = SourceEye.GetPrimaryRenderTarget();
    if (!RenderTarget)
    {
        return Result;
    }

    FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource();
    if (!Resource)
    {
        return Result;
    }

    const FIntPoint OutputSize(RenderTarget->SizeX, RenderTarget->SizeY);
    if (OutputSize.X <= 0 || OutputSize.Y <= 0)
    {
        return Result;
    }

    Result.Size = OutputSize;
    Result.bIsLinear = Settings.Gamma == EOmniCaptureGamma::Linear;
    Result.bUsedCPUFallback = false;
    Result.ReadyFence.SafeRelease();
    Result.EncoderPlanes.Reset();
    Result.OutputTarget.SafeRelease();
    Result.GPUSource.SafeRelease();

    const int32 PixelCount = OutputSize.X * OutputSize.Y;

    if (Result.bIsLinear)
    {
        TUniquePtr<TImagePixelData<FFloat16Color>> PixelData = MakeUnique<TImagePixelData<FFloat16Color>>(OutputSize);
        TArray<FFloat16Color> TempPixels;
        FReadSurfaceDataFlags ReadFlags(RCM_UNorm, CubeFace_MAX);
        if (!Resource->ReadFloat16Pixels(TempPixels, ReadFlags, FIntRect()))
        {
            PixelData.Reset();
        }
        else
        {
            PixelData->Pixels = MoveTemp(TempPixels);
            Result.PixelData = MoveTemp(PixelData);
            Result.PixelDataType = EOmniCapturePixelDataType::LinearColorFloat16;
        }
    }
    else
    {
        TUniquePtr<TImagePixelData<FColor>> PixelData = MakeUnique<TImagePixelData<FColor>>(OutputSize);
        TArray<FColor> TempPixels;
        FReadSurfaceDataFlags ReadFlags(RCM_UNorm, CubeFace_MAX);
        if (!Resource->ReadPixels(TempPixels, ReadFlags, FIntRect()))
        {
            PixelData.Reset();
        }
        else
        {
            PixelData->Pixels = MoveTemp(TempPixels);
            Result.PixelData = MoveTemp(PixelData);
            Result.PixelDataType = EOmniCapturePixelDataType::Color8;
        }
    }

    if (!Result.PixelData.IsValid())
    {
        Result.PreviewPixels.Reset();
        return Result;
    }

    Result.PreviewPixels.SetNum(PixelCount);
    if (Result.bIsLinear)
    {
        const TImagePixelData<FFloat16Color>* FloatData = static_cast<const TImagePixelData<FFloat16Color>*>(Result.PixelData.Get());
        if (FloatData)
        {
            for (int32 Index = 0; Index < PixelCount; ++Index)
            {
                Result.PreviewPixels[Index] = FLinearColor(FloatData->Pixels[Index]).ToFColor(true);
            }
        }
    }
    else
    {
        const TImagePixelData<FColor>* ColorData = static_cast<const TImagePixelData<FColor>*>(Result.PixelData.Get());
        if (ColorData)
        {
            Result.PreviewPixels = ColorData->Pixels;
        }
    }

    if (Result.PreviewPixels.Num() != PixelCount)
    {
        Result.PreviewPixels.Init(FColor::Black, PixelCount);
    }

    Result.Texture = Resource->GetRenderTargetTexture();

    if (Result.Texture.IsValid() && Settings.OutputFormat == EOmniOutputFormat::NVENCHardware)
    {
        FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool();
        ENQUEUE_RENDER_COMMAND(OmniCapturePlanarConvert)([Settings, SourceTexture = Result.Texture, OutputSize, bLinear = Result.bIsLinear, &Result, CompletionEvent](FRHICommandListImmediate& RHICmdList)
        {
            ConvertPlanarOnRenderThread(Settings, SourceTexture, OutputSize, bLinear, Result);
            CompletionEvent->Trigger();
        });

        CompletionEvent->Wait();
        FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);
    }

    return Result;
}
