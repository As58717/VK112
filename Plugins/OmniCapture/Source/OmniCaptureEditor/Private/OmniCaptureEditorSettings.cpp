#include "OmniCaptureEditorSettings.h"

UOmniCaptureEditorSettings::UOmniCaptureEditorSettings()
{
    CaptureSettings.OutputFormat = EOmniOutputFormat::NVENCHardware;
    CaptureSettings.OutputFileName = TEXT("OmniCapture");
}
