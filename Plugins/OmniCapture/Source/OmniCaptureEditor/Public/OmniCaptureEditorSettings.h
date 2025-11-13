#pragma once

#include "CoreMinimal.h"
#include "OmniCaptureTypes.h"
#include "OmniCaptureEditorSettings.generated.h"

UCLASS(Config=EditorPerProjectUserSettings)
class OMNICAPTUREEDITOR_API UOmniCaptureEditorSettings : public UObject
{
    GENERATED_BODY()

public:
    UOmniCaptureEditorSettings();

    UPROPERTY(EditAnywhere, Config, Category = "Capture")
    FOmniCaptureSettings CaptureSettings;

    UPROPERTY(EditAnywhere, Config, Category = "Capture")
    bool bPreferNVENCWhenAvailable = true;

    UPROPERTY(EditAnywhere, Config, Category = "Capture")
    bool bAutoOpenPanel = true;
};
