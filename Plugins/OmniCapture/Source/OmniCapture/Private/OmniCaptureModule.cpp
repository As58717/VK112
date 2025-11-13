#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Logging/LogMacros.h"
#include "ShaderCore.h"
#include "Misc/Paths.h"
#include "OmniCaptureNVENCEncoder.h"

DEFINE_LOG_CATEGORY_STATIC(LogOmniCapture, Log, All);

class FOmniCaptureModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        UE_LOG(LogOmniCapture, Display, TEXT("OmniCapture module startup"));

        if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("OmniCapture")))
        {
            const FString ShaderDirectory = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
            AddShaderSourceDirectoryMapping(TEXT("/Plugin/OmniCapture"), ShaderDirectory);
        }

        FOmniCaptureNVENCEncoder::LogRuntimeStatus();
    }

    virtual void ShutdownModule() override
    {
        UE_LOG(LogOmniCapture, Display, TEXT("OmniCapture module shutdown"));
    }
};

IMPLEMENT_MODULE(FOmniCaptureModule, OmniCapture)
