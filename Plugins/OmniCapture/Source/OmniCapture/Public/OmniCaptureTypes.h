#pragma once
#include "CoreMinimal.h"
#include "RHI.h"
#include "RHIResources.h"
#include "ImageWriteTypes.h"
#include "Misc/EngineVersionComparison.h"
#include "ImagePixelData.h"
#include "RenderGraphResources.h"
#include "Misc/DateTime.h"
#include "OmniCaptureTypes.generated.h"

namespace OmniCapture
{
    static FORCEINLINE EPixelFormat GetHalfFloatPixelFormat()
    {
#if defined(PF_A16B16G16R16F)
        return PF_A16B16G16R16F;
#else
        return PF_FloatRGBA;
#endif
    }
}

class UCurveFloat;

UENUM(BlueprintType)
enum class EOmniCaptureMode : uint8 { Mono, Stereo };

UENUM(BlueprintType)
enum class EOmniCaptureProjection : uint8
{
        Equirectangular,
        Fisheye,
        Planar2D,
        Cylindrical,
        FullDome,
        SphericalMirror
};

UENUM(BlueprintType)
enum class EOmniCaptureFisheyeType : uint8
{
        Hemispherical,
        OmniDirectional
};

UENUM(BlueprintType)
enum class EOmniCaptureCoverage : uint8 { FullSphere, HalfSphere };

UENUM(BlueprintType)
enum class EOmniCaptureStereoLayout : uint8 { TopBottom, SideBySide };

UENUM(BlueprintType)
enum class EOmniOutputFormat : uint8
{
	ImageSequence = 0 UMETA(DisplayName = "Image Sequence"),
	NVENCHardware = 1 UMETA(DisplayName = "NVENC Hardware"),
	PNGSequence = ImageSequence UMETA(Hidden),
};

UENUM(BlueprintType)
enum class EOmniCaptureImageFormat : uint8 { PNG, JPG, EXR, BMP };

UENUM(BlueprintType)
enum class EOmniCaptureEXRCompression : uint8
{
    None,
    Zip,
    Zips,
    Piz,
    Pxr24,
    Dwaa,
    Dwab,
    Rle
};
UENUM(BlueprintType)
enum class EOmniCaptureHDRPrecision : uint8
{
    HalfFloat UMETA(DisplayName = "16-bit Half Float"),
    FullFloat UMETA(DisplayName = "32-bit Float")
};

enum class EOmniCapturePixelPrecision : uint8
{
    Unknown,
    HalfFloat,
    FullFloat
};

enum class EOmniCapturePixelDataType : uint8
{
    Unknown,
    LinearColorFloat32,
    LinearColorFloat16,
    Color8,
    ScalarFloat32,
    Vector2Float32
};

UENUM(BlueprintType)
enum class EOmniCaptureGamma : uint8 { SRGB, Linear };

UENUM(BlueprintType)
enum class EOmniCapturePNGBitDepth : uint8
{
        BitDepth16 = 0 UMETA(DisplayName = "16-bit Color"),
        BitDepth32 = 1 UMETA(DisplayName = "32-bit Color"),
        BitDepth8 = 2 UMETA(DisplayName = "8-bit Color")
};

UENUM(BlueprintType)
enum class EOmniCaptureColorSpace : uint8 { BT709, BT2020, HDR10 };

UENUM(BlueprintType)
enum class EOmniCaptureCodec : uint8 { H264, HEVC };

UENUM(BlueprintType)
enum class EOmniCaptureColorFormat : uint8 { NV12, P010, BGRA };

UENUM(BlueprintType)
enum class EOmniCaptureNVENCD3D12Interop : uint8
{
        Bridge UMETA(DisplayName = "D3D11-on-12 Bridge"),
        Native UMETA(DisplayName = "Native D3D12")
};

UENUM(BlueprintType)
enum class EOmniCaptureRateControlMode : uint8 { ConstantBitrate, VariableBitrate, Lossless };

UENUM(BlueprintType)
enum class EOmniCaptureState : uint8 { Idle, Recording, Paused, DroppedFrames, Finalizing };

UENUM(BlueprintType)
enum class EOmniCaptureRingBufferPolicy : uint8 { DropOldest, BlockProducer };

UENUM(BlueprintType)
enum class EOmniCapturePreviewView : uint8 { StereoComposite, LeftEye, RightEye };

UENUM(BlueprintType)
enum class EOmniCaptureDiagnosticLevel : uint8
{
        Info,
        Warning,
        Error
};

UENUM(BlueprintType)
enum class EOmniCaptureAuxiliaryPassType : uint8
{
        None,
        SceneDepth,
        WorldNormal,
        BaseColor,
        Roughness,
        AmbientOcclusion,
        MotionVector
};

USTRUCT(BlueprintType)
struct OMNICAPTURE_API FOmniCaptureDiagnosticEntry
{
        GENERATED_BODY()

        UPROPERTY(BlueprintReadOnly, Category = "Diagnostics")
        FDateTime Timestamp;

        UPROPERTY(BlueprintReadOnly, Category = "Diagnostics")
        float SecondsSinceCaptureStart = 0.0f;

        UPROPERTY(BlueprintReadOnly, Category = "Diagnostics")
        int32 AttemptIndex = 0;

        UPROPERTY(BlueprintReadOnly, Category = "Diagnostics")
        FString Step;

        UPROPERTY(BlueprintReadOnly, Category = "Diagnostics")
        FString Message;

        UPROPERTY(BlueprintReadOnly, Category = "Diagnostics")
        EOmniCaptureDiagnosticLevel Level = EOmniCaptureDiagnosticLevel::Info;
};

USTRUCT(BlueprintType)
struct FOmniCaptureRenderFeatureOverrides
{
        GENERATED_BODY()

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering") bool bForceRayTracing = false;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering") bool bForcePathTracing = false;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering") bool bForceLumen = false;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering") bool bEnableDLSS = false;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering") bool bEnableBloom = false;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering") bool bEnableAntiAliasing = true;
};

USTRUCT(BlueprintType)
struct FOmniCaptureQuality
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video") int32 TargetBitrateKbps = 60000;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video") int32 MaxBitrateKbps = 80000;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video") int32 GOPLength = 60;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video") int32 BFrames = 2;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video") bool bLowLatency = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video") EOmniCaptureRateControlMode RateControlMode = EOmniCaptureRateControlMode::ConstantBitrate;
};

USTRUCT(BlueprintType)
struct OMNICAPTURE_API FOmniCaptureSettings
{
        GENERATED_BODY()
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture") EOmniCaptureMode Mode = EOmniCaptureMode::Mono;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture") EOmniCaptureProjection Projection = EOmniCaptureProjection::Equirectangular;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture") EOmniCaptureCoverage Coverage = EOmniCaptureCoverage::FullSphere;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture") EOmniCaptureStereoLayout StereoLayout = EOmniCaptureStereoLayout::TopBottom;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = 1024, UIMin = 1024)) int32 Resolution = 4096;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = 16, UIMin = 64)) FIntPoint PlanarResolution = FIntPoint(3840, 2160);
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = 1, UIMin = 1)) int32 PlanarIntegerScale = 1;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture|Fisheye", meta = (EditCondition = "Projection == EOmniCaptureProjection::Fisheye")) EOmniCaptureFisheyeType FisheyeType = EOmniCaptureFisheyeType::Hemispherical;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture|Fisheye", meta = (ClampMin = 90.0, ClampMax = 360.0, UIMin = 90.0, UIMax = 360.0, EditCondition = "Projection == EOmniCaptureProjection::Fisheye")) float FisheyeFOV = 180.0f;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture|Fisheye", meta = (ClampMin = 256, UIMin = 256, EditCondition = "Projection == EOmniCaptureProjection::Fisheye")) FIntPoint FisheyeResolution = FIntPoint(4096, 4096);
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture|Fisheye", meta = (EditCondition = "Projection == EOmniCaptureProjection::Fisheye")) bool bFisheyeConvertToEquirect = false;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = 0.0, UIMin = 0.0)) float TargetFrameRate = 60.0f;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture") EOmniCaptureGamma Gamma = EOmniCaptureGamma::SRGB;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture") bool bEnablePreviewWindow = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = 0.1, UIMin = 0.1)) float PreviewScreenScale = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = 1.0, UIMin = 5.0, ClampMax = 240.0)) float PreviewFrameRate = 30.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture") bool bRecordAudio = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture") float AudioGain = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture") TSoftObjectPtr<class USoundSubmix> SubmixToRecord;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture") float InterPupillaryDistanceCm = 6.4f;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture|Stereo", meta = (ClampMin = 0.0, UIMin = 0.0)) float EyeConvergenceDistanceCm = 0.0f;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture|Stereo") UCurveFloat* InterpupillaryDistanceCurve = nullptr;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture|Stereo") UCurveFloat* EyeConvergenceCurve = nullptr;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = 0.0, UIMin = 0.0)) float SegmentDurationSeconds = 0.0f;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = 0)) int32 SegmentSizeLimitMB = 0;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = 0, UIMin = 0)) int32 SegmentFrameCount = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture") bool bCreateSegmentSubfolders = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output") EOmniOutputFormat OutputFormat = EOmniOutputFormat::ImageSequence;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output") EOmniCaptureImageFormat ImageFormat = EOmniCaptureImageFormat::PNG;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output") EOmniCaptureHDRPrecision HDRPrecision = EOmniCaptureHDRPrecision::HalfFloat;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output") EOmniCapturePNGBitDepth PNGBitDepth = EOmniCapturePNGBitDepth::BitDepth32;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output") FString OutputDirectory;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output") FString OutputFileName = TEXT("OmniCapture");
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output") EOmniCaptureColorSpace ColorSpace = EOmniCaptureColorSpace::BT709;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output") bool bEnableFastStart = true;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output|EXR") bool bPackEXRAuxiliaryLayers = true;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output|EXR") bool bUseEXRMultiPart = false;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output|EXR") EOmniCaptureEXRCompression EXRCompression = EOmniCaptureEXRCompression::Zip;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output") bool bForceConstantFrameRate = true;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output") bool bAllowNVENCFallback = true;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (ClampMin = 1, UIMin = 1)) int32 MaxPendingImageTasks = 8;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Diagnostics", meta = (ClampMin = 0)) int32 MinimumFreeDiskSpaceGB = 2;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Diagnostics", meta = (ClampMin = 0.1, ClampMax = 1.0)) float LowFrameRateWarningRatio = 0.85f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output") FString PreferredFFmpegPath;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = 0.0, ClampMax = 1.0)) float SeamBlend = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = 0.0, ClampMax = 1.0)) float PolarDampening = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output") FOmniCaptureQuality Quality;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NVENC") EOmniCaptureCodec Codec = EOmniCaptureCodec::HEVC;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NVENC") EOmniCaptureColorFormat NVENCColorFormat = EOmniCaptureColorFormat::NV12;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NVENC") bool bZeroCopy = true;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NVENC") EOmniCaptureNVENCD3D12Interop D3D12InteropMode = EOmniCaptureNVENCD3D12Interop::Bridge;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NVENC", meta = (ClampMin = 0, UIMin = 0)) int32 RingBufferCapacity = 6;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NVENC") EOmniCaptureRingBufferPolicy RingBufferPolicy = EOmniCaptureRingBufferPolicy::DropOldest;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NVENC") FString NVENCRuntimeDirectory;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NVENC") FString NVENCDllPathOverride;
        UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use NVENCRuntimeDirectory instead.")) FString AVEncoderModulePathOverride_DEPRECATED;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output") bool bOpenPreviewOnFinalize = false;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview") EOmniCapturePreviewView PreviewVisualization = EOmniCapturePreviewView::StereoComposite;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metadata") bool bGenerateManifest = true;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metadata") bool bWriteSpatialMetadata = true;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metadata") bool bWriteXMPMetadata = true;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Metadata") bool bInjectFFmpegMetadata = true;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering") FOmniCaptureRenderFeatureOverrides RenderingOverrides;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering") TArray<EOmniCaptureAuxiliaryPassType> AuxiliaryPasses;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Offline Rendering") bool bEnableOfflineSampling = false;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Offline Rendering", meta = (EditCondition = "bEnableOfflineSampling", ClampMin = 1, UIMin = 1)) int32 TemporalSampleCount = 1;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Offline Rendering", meta = (EditCondition = "bEnableOfflineSampling", ClampMin = 1, UIMin = 1)) int32 SpatialSampleCount = 1;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Offline Rendering", meta = (EditCondition = "bEnableOfflineSampling", ClampMin = 0, UIMin = 0)) int32 WarmUpFrameCount = 0;

        FIntPoint GetEquirectResolution() const;
        FIntPoint GetPlanarResolution() const;
        FIntPoint GetFisheyeResolution() const;
        FIntPoint GetOutputResolution() const;
        FIntPoint GetPerEyeOutputResolution() const;
        bool IsStereo() const;
        bool IsVR180() const;
        bool IsFisheye() const;
        bool IsPlanar() const;
        bool IsCylindrical() const;
        bool IsFullDome() const;
        bool IsSphericalMirror() const;
        bool SupportsSphericalMetadata() const;
        bool UseDualFisheyeLayout() const;
        bool ShouldConvertFisheyeToEquirect() const;
        FString GetStereoModeMetadataTag() const;
        int32 GetEncoderAlignmentRequirement() const;
        float GetHorizontalFOVDegrees() const;
        float GetVerticalFOVDegrees() const;
        float GetLongitudeSpanRadians() const;
        float GetLatitudeSpanRadians() const;
        FString GetImageFileExtension() const;

        FString GetEffectiveNVENCRuntimeDirectory() const
        {
            return !NVENCRuntimeDirectory.IsEmpty() ? NVENCRuntimeDirectory : AVEncoderModulePathOverride_DEPRECATED;
        }

        void SetNVENCRuntimeDirectory(const FString& InDirectory)
        {
            NVENCRuntimeDirectory = InDirectory;
            AVEncoderModulePathOverride_DEPRECATED.Empty();
        }

        void MigrateDeprecatedOverrides()
        {
            if (NVENCRuntimeDirectory.IsEmpty() && !AVEncoderModulePathOverride_DEPRECATED.IsEmpty())
            {
                NVENCRuntimeDirectory = AVEncoderModulePathOverride_DEPRECATED;
            }

            AVEncoderModulePathOverride_DEPRECATED.Empty();
        }
};

USTRUCT()
struct FOmniAudioPacket
{
	GENERATED_BODY()
	UPROPERTY() double Timestamp = 0.0;
	UPROPERTY() int32 SampleRate = 48000;
	UPROPERTY() int32 NumChannels = 2;
	UPROPERTY() TArray<int16> PCM16;
};

USTRUCT()
struct FOmniCaptureFrameMetadata
{
        GENERATED_BODY()
        UPROPERTY() int32 FrameIndex = 0;
        UPROPERTY() double Timecode = 0.0;
        UPROPERTY() bool bKeyFrame = false;
};

struct FOmniCaptureLayerPayload
{
        TUniquePtr<FImagePixelData> PixelData;
        bool bLinear = false;
        EOmniCapturePixelPrecision Precision = EOmniCapturePixelPrecision::Unknown;
        EOmniCapturePixelDataType PixelDataType = EOmniCapturePixelDataType::Unknown;
};

struct FOmniCaptureFrame
{
        FOmniCaptureFrameMetadata Metadata;
        TUniquePtr<FImagePixelData> PixelData;
        TRefCountPtr<IPooledRenderTarget> GPUSource;
        FTextureRHIRef Texture;
        FGPUFenceRHIRef ReadyFence;
        bool bLinearColor = false;
        bool bUsedCPUFallback = false;
        EOmniCapturePixelPrecision PixelPrecision = EOmniCapturePixelPrecision::Unknown;
        EOmniCapturePixelDataType PixelDataType = EOmniCapturePixelDataType::Unknown;
        TArray<FOmniAudioPacket> AudioPackets;
        TArray<FTextureRHIRef> EncoderTextures;
        TMap<FName, FOmniCaptureLayerPayload> AuxiliaryLayers;
};

USTRUCT(BlueprintType)
struct FOmniCaptureRingBufferStats
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats") int32 PendingFrames = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats") int32 DroppedFrames = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats") int32 BlockedPushes = 0;
};

USTRUCT(BlueprintType)
struct FOmniAudioSyncStats
{
        GENERATED_BODY()
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio") double LatestVideoTimestamp = 0.0;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio") double LatestAudioTimestamp = 0.0;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio") double DriftMilliseconds = 0.0;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio") double MaxObservedDriftMilliseconds = 0.0;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio") int32 PendingPackets = 0;
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio") bool bInError = false;
};

OMNICAPTURE_API FName GetAuxiliaryLayerName(EOmniCaptureAuxiliaryPassType PassType);
