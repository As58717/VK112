#include "OmniCaptureTypes.h"

#include "Math/UnrealMathUtility.h"
#include "UObject/UnrealType.h"

namespace
{
    FORCEINLINE int32 AlignDimension(int32 Value, int32 Alignment)
    {
        if (Value <= 0)
        {
            return Alignment > 0 ? Alignment : 1;
        }

        if (Alignment <= 1)
        {
            return Value;
        }

        const int32 Rounded = ((Value + Alignment - 1) / Alignment) * Alignment;
        return FMath::Max(Alignment, Rounded);
    }

    FORCEINLINE FIntPoint AlignPoint(const FIntPoint& Value, int32 Alignment)
    {
        return FIntPoint(AlignDimension(Value.X, Alignment), AlignDimension(Value.Y, Alignment));
    }
}

FIntPoint FOmniCaptureSettings::GetEquirectResolution() const
{
    if (IsPlanar())
    {
        return GetPlanarResolution();
    }

    const bool bHalfSphere = IsVR180();
    const int32 Alignment = GetEncoderAlignmentRequirement();

    FIntPoint EyeResolution(Resolution * (bHalfSphere ? 1 : 2), Resolution);
    EyeResolution.X = AlignDimension(EyeResolution.X, Alignment);
    EyeResolution.Y = AlignDimension(EyeResolution.Y, Alignment);

    FIntPoint OutputResolution = EyeResolution;

    if (IsStereo())
    {
        if (StereoLayout == EOmniCaptureStereoLayout::SideBySide)
        {
            OutputResolution.X = AlignDimension(EyeResolution.X * 2, Alignment);
            OutputResolution.Y = AlignDimension(EyeResolution.Y, Alignment);
            OutputResolution.X = AlignDimension(OutputResolution.X, 2);
        }
        else
        {
            OutputResolution.X = AlignDimension(EyeResolution.X, Alignment);
            OutputResolution.Y = AlignDimension(EyeResolution.Y * 2, Alignment);
            OutputResolution.Y = AlignDimension(OutputResolution.Y, 2);
        }
    }
    else
    {
        OutputResolution.X = AlignDimension(OutputResolution.X, Alignment);
        OutputResolution.Y = AlignDimension(OutputResolution.Y, Alignment);
    }

    OutputResolution.X = FMath::Max(2, OutputResolution.X);
    OutputResolution.Y = FMath::Max(2, OutputResolution.Y);

    return OutputResolution;
}

FIntPoint FOmniCaptureSettings::GetPlanarResolution() const
{
    FIntPoint Base = PlanarResolution;
    Base.X = FMath::Max(1, Base.X);
    Base.Y = FMath::Max(1, Base.Y);

    const int32 Scale = FMath::Max(1, PlanarIntegerScale);
    Base.X *= Scale;
    Base.Y *= Scale;

    const int32 Alignment = GetEncoderAlignmentRequirement();
    Base = AlignPoint(Base, Alignment);

    Base.X = FMath::Max(2, Base.X);
    Base.Y = FMath::Max(2, Base.Y);

    return Base;
}

FIntPoint FOmniCaptureSettings::GetFisheyeResolution() const
{
    FIntPoint Base = FisheyeResolution;
    Base.X = FMath::Max(2, Base.X);
    Base.Y = FMath::Max(2, Base.Y);

    const int32 Alignment = GetEncoderAlignmentRequirement();
    Base = AlignPoint(Base, Alignment);

    Base.X = FMath::Max(2, Base.X);
    Base.Y = FMath::Max(2, Base.Y);

    return Base;
}

FIntPoint FOmniCaptureSettings::GetOutputResolution() const
{
    if (IsPlanar())
    {
        return GetPlanarResolution();
    }

    if (IsFisheye())
    {
        if (ShouldConvertFisheyeToEquirect())
        {
            return GetEquirectResolution();
        }

        const FIntPoint EyeResolution = GetFisheyeResolution();
        FIntPoint Output = EyeResolution;

        if (IsStereo())
        {
            if (StereoLayout == EOmniCaptureStereoLayout::SideBySide)
            {
                Output.X = AlignDimension(EyeResolution.X * 2, GetEncoderAlignmentRequirement());
            }
            else
            {
                Output.Y = AlignDimension(EyeResolution.Y * 2, GetEncoderAlignmentRequirement());
            }
        }

        Output.X = FMath::Max(2, Output.X);
        Output.Y = FMath::Max(2, Output.Y);
        return Output;
    }

    return GetEquirectResolution();
}

FIntPoint FOmniCaptureSettings::GetPerEyeOutputResolution() const
{
    if (IsPlanar())
    {
        return GetPlanarResolution();
    }

    if (IsFisheye())
    {
        if (ShouldConvertFisheyeToEquirect())
        {
            const FIntPoint Output = GetEquirectResolution();
            if (!IsStereo())
            {
                return Output;
            }

            if (StereoLayout == EOmniCaptureStereoLayout::SideBySide)
            {
                return FIntPoint(FMath::Max(1, Output.X / 2), Output.Y);
            }

            return FIntPoint(Output.X, FMath::Max(1, Output.Y / 2));
        }

        return GetFisheyeResolution();
    }

    const FIntPoint Output = GetEquirectResolution();
    if (!IsStereo())
    {
        return Output;
    }

    if (StereoLayout == EOmniCaptureStereoLayout::SideBySide)
    {
        return FIntPoint(FMath::Max(1, Output.X / 2), Output.Y);
    }

    return FIntPoint(Output.X, FMath::Max(1, Output.Y / 2));
}

FName GetAuxiliaryLayerName(EOmniCaptureAuxiliaryPassType PassType)
{
    if (PassType == EOmniCaptureAuxiliaryPassType::None)
    {
        return TEXT("Aux_None");
    }

    if (const UEnum* Enum = StaticEnum<EOmniCaptureAuxiliaryPassType>())
    {
        FString RawName = Enum->GetNameStringByValue(static_cast<int64>(PassType));
        RawName.ReplaceInline(TEXT("EOmniCaptureAuxiliaryPassType::"), TEXT(""));
        RawName.ReplaceInline(TEXT("::"), TEXT("_"));
        RawName.ReplaceInline(TEXT("."), TEXT("_"));
        return FName(*FString::Printf(TEXT("Aux_%s"), *RawName));
    }

    return TEXT("Aux_Unknown");
}

bool FOmniCaptureSettings::IsStereo() const
{
    return Mode == EOmniCaptureMode::Stereo;
}

bool FOmniCaptureSettings::IsFisheye() const
{
    return Projection == EOmniCaptureProjection::Fisheye;
}

bool FOmniCaptureSettings::IsPlanar() const
{
    return Projection == EOmniCaptureProjection::Planar2D;
}

bool FOmniCaptureSettings::IsCylindrical() const
{
    return Projection == EOmniCaptureProjection::Cylindrical;
}

bool FOmniCaptureSettings::IsFullDome() const
{
    return Projection == EOmniCaptureProjection::FullDome;
}

bool FOmniCaptureSettings::IsSphericalMirror() const
{
    return Projection == EOmniCaptureProjection::SphericalMirror;
}

bool FOmniCaptureSettings::SupportsSphericalMetadata() const
{
    if (IsPlanar())
    {
        return false;
    }

    if (IsCylindrical() || IsFullDome() || IsSphericalMirror())
    {
        return false;
    }

    return true;
}

bool FOmniCaptureSettings::IsVR180() const
{
    return Coverage == EOmniCaptureCoverage::HalfSphere;
}

bool FOmniCaptureSettings::UseDualFisheyeLayout() const
{
    return IsFisheye() && IsStereo();
}

bool FOmniCaptureSettings::ShouldConvertFisheyeToEquirect() const
{
    return bFisheyeConvertToEquirect && IsFisheye();
}

FString FOmniCaptureSettings::GetStereoModeMetadataTag() const
{
    if (!IsStereo())
    {
        return TEXT("mono");
    }

    return StereoLayout == EOmniCaptureStereoLayout::TopBottom
        ? TEXT("top-bottom")
        : TEXT("left-right");
}

namespace
{
    static int32 CalculateLcm(int32 A, int32 B)
    {
        if (A == 0 || B == 0)
        {
            return 0;
        }

        const int32 Gcd = FMath::GreatestCommonDivisor(FMath::Abs(A), FMath::Abs(B));
        return (A / Gcd) * B;
    }
}

int32 FOmniCaptureSettings::GetEncoderAlignmentRequirement() const
{
    int32 Alignment = 2;

    if (OutputFormat == EOmniOutputFormat::NVENCHardware)
    {
        Alignment = CalculateLcm(Alignment, 64);

        switch (NVENCColorFormat)
        {
        case EOmniCaptureColorFormat::P010:
            Alignment = CalculateLcm(Alignment, 4);
            break;
        case EOmniCaptureColorFormat::BGRA:
            Alignment = FMath::Max(1, Alignment);
            break;
        default:
            break;
        }
    }

    return FMath::Max(1, Alignment);
}

float FOmniCaptureSettings::GetHorizontalFOVDegrees() const
{
    if (IsFisheye())
    {
        return FMath::Clamp(FisheyeFOV, 0.0f, 360.0f);
    }

    if (IsPlanar())
    {
        return 90.0f;
    }

    if (IsCylindrical())
    {
        return IsVR180() ? 180.0f : 360.0f;
    }

    if (IsFullDome())
    {
        return 180.0f;
    }

    if (IsSphericalMirror())
    {
        return IsVR180() ? 200.0f : 220.0f;
    }

    return IsVR180() ? 180.0f : 360.0f;
}

float FOmniCaptureSettings::GetVerticalFOVDegrees() const
{
    if (IsFisheye())
    {
        return FMath::Clamp(FisheyeFOV, 0.0f, 360.0f);
    }

    if (IsPlanar())
    {
        return 90.0f;
    }

    if (IsCylindrical())
    {
        return 180.0f;
    }

    if (IsFullDome())
    {
        return 180.0f;
    }

    if (IsSphericalMirror())
    {
        return 180.0f;
    }

    return 180.0f;
}

float FOmniCaptureSettings::GetLongitudeSpanRadians() const
{
    return FMath::DegreesToRadians(GetHorizontalFOVDegrees());
}

float FOmniCaptureSettings::GetLatitudeSpanRadians() const
{
    return FMath::DegreesToRadians(GetVerticalFOVDegrees());
}

FString FOmniCaptureSettings::GetImageFileExtension() const
{
    switch (ImageFormat)
    {
    case EOmniCaptureImageFormat::JPG:
        return TEXT(".jpg");
    case EOmniCaptureImageFormat::EXR:
        return TEXT(".exr");
    case EOmniCaptureImageFormat::BMP:
        return TEXT(".bmp");
    case EOmniCaptureImageFormat::PNG:
    default:
        return TEXT(".png");
    }
}
