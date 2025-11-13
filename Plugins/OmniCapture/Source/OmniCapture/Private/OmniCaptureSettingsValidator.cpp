#include "OmniCaptureSettingsValidator.h"

#include "Internationalization/Text.h"

namespace
{
    struct FProjectionCompatibility
    {
        TArray<EOmniCaptureCoverage, TInlineAllocator<2>> SupportedCoverage;
        bool bSupportsStereo = false;
        bool bKnown = false;
    };

    FProjectionCompatibility DescribeProjectionCompatibility(EOmniCaptureProjection Projection)
    {
        FProjectionCompatibility Info;
        Info.bKnown = true;

        switch (Projection)
        {
        case EOmniCaptureProjection::Equirectangular:
            Info.SupportedCoverage = { EOmniCaptureCoverage::FullSphere, EOmniCaptureCoverage::HalfSphere };
            Info.bSupportsStereo = true;
            break;
        case EOmniCaptureProjection::Fisheye:
            Info.SupportedCoverage = { EOmniCaptureCoverage::FullSphere, EOmniCaptureCoverage::HalfSphere };
            Info.bSupportsStereo = true;
            break;
        case EOmniCaptureProjection::Planar2D:
            Info.SupportedCoverage = { EOmniCaptureCoverage::FullSphere };
            Info.bSupportsStereo = false;
            break;
        case EOmniCaptureProjection::Cylindrical:
            Info.SupportedCoverage = { EOmniCaptureCoverage::FullSphere };
            Info.bSupportsStereo = true;
            break;
        case EOmniCaptureProjection::FullDome:
            Info.SupportedCoverage = { EOmniCaptureCoverage::HalfSphere };
            Info.bSupportsStereo = false;
            break;
        case EOmniCaptureProjection::SphericalMirror:
            Info.SupportedCoverage = { EOmniCaptureCoverage::FullSphere };
            Info.bSupportsStereo = false;
            break;
        default:
            Info.bKnown = false;
            break;
        }

        return Info;
    }

    FString ProjectionToString(EOmniCaptureProjection Projection)
    {
        if (const UEnum* Enum = StaticEnum<EOmniCaptureProjection>())
        {
            return Enum->GetDisplayNameTextByValue(static_cast<int64>(Projection)).ToString();
        }

        return TEXT("Unknown Projection");
    }

    FString CoverageToString(EOmniCaptureCoverage Coverage)
    {
        if (const UEnum* Enum = StaticEnum<EOmniCaptureCoverage>())
        {
            return Enum->GetDisplayNameTextByValue(static_cast<int64>(Coverage)).ToString();
        }

        return TEXT("Unknown Coverage");
    }
}

bool FOmniCaptureSettingsValidator::ApplyCompatibilityFixups(FOmniCaptureSettings& InOutSettings, TArray<FString>& OutWarnings, FString* OutFailureReason)
{
    if (OutFailureReason)
    {
        OutFailureReason->Reset();
    }

    const auto EmitWarning = [&OutWarnings](const FString& Warning)
    {
        OutWarnings.Add(Warning);
    };

    FProjectionCompatibility Compatibility = DescribeProjectionCompatibility(InOutSettings.Projection);
    if (!Compatibility.bKnown)
    {
        EmitWarning(FString::Printf(TEXT("Projection %s is unsupported at runtime - switching to Equirectangular."), *ProjectionToString(InOutSettings.Projection)));
        InOutSettings.Projection = EOmniCaptureProjection::Equirectangular;
        InOutSettings.Coverage = EOmniCaptureCoverage::FullSphere;
        Compatibility = DescribeProjectionCompatibility(InOutSettings.Projection);
    }

    if (!Compatibility.bKnown)
    {
        if (OutFailureReason)
        {
            *OutFailureReason = TEXT("No supported projection is available for the supplied settings.");
        }
        return false;
    }

    if (!Compatibility.SupportedCoverage.Contains(InOutSettings.Coverage))
    {
        if (Compatibility.SupportedCoverage.Num() == 0)
        {
            if (OutFailureReason)
            {
                *OutFailureReason = FString::Printf(TEXT("Projection %s does not support any coverage modes."), *ProjectionToString(InOutSettings.Projection));
            }
            return false;
        }

        const EOmniCaptureCoverage FallbackCoverage = Compatibility.SupportedCoverage[0];
        EmitWarning(FString::Printf(TEXT("%s projection does not support %s coverage - switching to %s."), *ProjectionToString(InOutSettings.Projection), *CoverageToString(InOutSettings.Coverage), *CoverageToString(FallbackCoverage)));
        InOutSettings.Coverage = FallbackCoverage;
    }

    if (!Compatibility.bSupportsStereo && InOutSettings.Mode == EOmniCaptureMode::Stereo)
    {
        EmitWarning(FString::Printf(TEXT("%s projection does not support stereo output - switching to mono."), *ProjectionToString(InOutSettings.Projection)));
        InOutSettings.Mode = EOmniCaptureMode::Mono;
    }

    if (InOutSettings.IsFisheye() && InOutSettings.Coverage == EOmniCaptureCoverage::HalfSphere && InOutSettings.FisheyeType != EOmniCaptureFisheyeType::Hemispherical)
    {
        EmitWarning(TEXT("Half-sphere fisheye capture requires hemispherical projection - forcing Hemispherical fisheye type."));
        InOutSettings.FisheyeType = EOmniCaptureFisheyeType::Hemispherical;
    }

    return true;
}

