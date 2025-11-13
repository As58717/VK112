#include "OmniCaptureEditorModule.h"

#include "LevelEditor.h"
#include "OmniCaptureEditorSettings.h"
#include "SOmniCaptureControlPanel.h"
#include "Framework/Docking/TabManager.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Misc/EngineVersionComparison.h"

static const FName OmniCapturePanelTabName(TEXT("OmniCapturePanel"));

void FOmniCaptureEditorModule::StartupModule()
{
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(OmniCapturePanelTabName, FOnSpawnTab::CreateRaw(this, &FOmniCaptureEditorModule::SpawnCaptureTab))
        .SetDisplayName(NSLOCTEXT("OmniCaptureEditor", "CapturePanelTitle", "Omni Capture"))
        .SetTooltipText(NSLOCTEXT("OmniCaptureEditor", "CapturePanelTooltip", "Open the Omni Capture control panel"))
        .SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));

#if UE_VERSION_OLDER_THAN(5, 6, 0)
    MenuRegistrationHandle = UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FOmniCaptureEditorModule::RegisterMenus));
#else
    MenuRegistrationHandle = UToolMenus::Get()->RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FOmniCaptureEditorModule::RegisterMenus));
#endif

    if (const UOmniCaptureEditorSettings* Settings = GetDefault<UOmniCaptureEditorSettings>())
    {
        if (Settings->bAutoOpenPanel)
        {
            FGlobalTabmanager::Get()->TryInvokeTab(OmniCapturePanelTabName);
        }
    }
}

void FOmniCaptureEditorModule::ShutdownModule()
{
    if (UToolMenus* ToolMenus = UToolMenus::TryGet())
    {
        if (MenuRegistrationHandle.IsValid())
        {
#if UE_VERSION_OLDER_THAN(5, 6, 0)
            UToolMenus::UnregisterStartupCallback(MenuRegistrationHandle);
#endif
        }
        ToolMenus->UnregisterOwner(this);
    }

    MenuRegistrationHandle = FDelegateHandle();

    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(OmniCapturePanelTabName);
}

TSharedRef<SDockTab> FOmniCaptureEditorModule::SpawnCaptureTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SOmniCaptureControlPanel)
        ];
}

void FOmniCaptureEditorModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);
    if (UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar"))
    {
        FToolMenuSection& Section = Menu->FindOrAddSection("OmniCapture");
        Section.AddEntry(FToolMenuEntry::InitToolBarButton(
            TEXT("OmniCaptureToggle"),
            FUIAction(FExecuteAction::CreateRaw(this, &FOmniCaptureEditorModule::HandleOpenPanel)),
            NSLOCTEXT("OmniCaptureEditor", "ToolbarLabel", "Omni Capture"),
            NSLOCTEXT("OmniCaptureEditor", "ToolbarTooltip", "Open the Omni Capture control panel"),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details")));
    }
}

void FOmniCaptureEditorModule::HandleOpenPanel()
{
    FGlobalTabmanager::Get()->TryInvokeTab(OmniCapturePanelTabName);
}

IMPLEMENT_MODULE(FOmniCaptureEditorModule, OmniCaptureEditor)
