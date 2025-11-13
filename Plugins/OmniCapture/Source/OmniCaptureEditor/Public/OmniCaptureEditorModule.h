
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Delegates/Delegate.h"

class FSpawnTabArgs;
class SDockTab;

class FOmniCaptureEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    TSharedRef<SDockTab> SpawnCaptureTab(const FSpawnTabArgs& Args);
    void RegisterMenus();
    void HandleOpenPanel();

private:
    FDelegateHandle MenuRegistrationHandle;
};
