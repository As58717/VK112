#include "Misc/AutomationTest.h"

#include "OmniCaptureSettingsValidator.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOmniCaptureCylindricalCompatibilityTest, "OmniCapture.Settings.CylindricalStereoFallback", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOmniCaptureCylindricalCompatibilityTest::RunTest(const FString& Parameters)
{
    FOmniCaptureSettings Settings;
    Settings.Mode = EOmniCaptureMode::Stereo;
    Settings.Projection = EOmniCaptureProjection::Cylindrical;
    Settings.Coverage = EOmniCaptureCoverage::HalfSphere;

    TArray<FString> Warnings;
    TestTrue(TEXT("Compatibility fixups succeed for cylindrical stereo half-sphere"), FOmniCaptureSettingsValidator::ApplyCompatibilityFixups(Settings, Warnings));
    TestEqual(TEXT("Stereo preserved for cylindrical projection"), Settings.Mode, EOmniCaptureMode::Stereo);
    TestEqual(TEXT("Coverage forced to full sphere"), Settings.Coverage, EOmniCaptureCoverage::FullSphere);
    TestTrue(TEXT("Warning emitted for coverage fallback"), Warnings.Num() > 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOmniCapturePlanarStereoFallbackTest, "OmniCapture.Settings.PlanarStereoFallback", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOmniCapturePlanarStereoFallbackTest::RunTest(const FString& Parameters)
{
    FOmniCaptureSettings Settings;
    Settings.Mode = EOmniCaptureMode::Stereo;
    Settings.Projection = EOmniCaptureProjection::Planar2D;

    TArray<FString> Warnings;
    TestTrue(TEXT("Compatibility fixups succeed for planar stereo"), FOmniCaptureSettingsValidator::ApplyCompatibilityFixups(Settings, Warnings));
    TestEqual(TEXT("Planar projection forces mono"), Settings.Mode, EOmniCaptureMode::Mono);
    TestTrue(TEXT("Warning emitted for planar stereo fallback"), Warnings.Num() > 0);

    return true;
}

