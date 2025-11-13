using UnrealBuildTool;

public class OmniCaptureEditor : ModuleRules
{
    public OmniCaptureEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.NoPCHs;

        bUseUnity = false;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "RenderCore",
            "RHI",
            "Slate",
            "SlateCore",
            "UMG",
            "OmniCapture",
            "MovieRenderPipelineCore",
            "MovieRenderPipelineSettings",
            "ApplicationCore"        // Clipboard functionality relies on ApplicationCore exports
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "InputCore",
            "EditorStyle",
            "LevelEditor",         // 编辑器核心功能
            "Projects",
            "PropertyEditor",      // 用于自定义属性编辑
            "ToolMenus",           // 用于创建自定义工具栏
            "UnrealEd",            // 编辑器核心功能
            "DesktopPlatform",     // 用于输出目录选择对话框
            "RHI",
            "RHICore",
            "MovieRenderPipelineEditor"
        });
    }
}
