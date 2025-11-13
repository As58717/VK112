#pragma once

#include "CoreMinimal.h"
#include "OmniCaptureTypes.h"

/**
 * Provides compatibility validation helpers for runtime OmniCapture settings.
 */
struct OMNICAPTURE_API FOmniCaptureSettingsValidator
{
    /**
     * Applies validation rules and safe fallbacks for projection / coverage / stereo combinations.
     *
     * @param InOutSettings            Settings to validate and potentially mutate.
     * @param OutWarnings              Optional warning messages emitted for each applied fallback.
     * @param OutFailureReason         Optional fatal failure description if no safe fallback exists.
     * @return                         true if the settings remain valid (possibly after fixups).
     */
    static bool ApplyCompatibilityFixups(FOmniCaptureSettings& InOutSettings, TArray<FString>& OutWarnings, FString* OutFailureReason = nullptr);
};

