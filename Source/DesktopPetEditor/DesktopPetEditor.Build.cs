using UnrealBuildTool;

// 工具栏和工程配置仅在编辑器加载，打包游戏不依赖这些模块。
public class DesktopPetEditor : ModuleRules
{
    public DesktopPetEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "EditorSubsystem" });
        PrivateDependencyModuleNames.AddRange(new[] {
            "Engine", "DesktopPet", "UnrealEd", "EngineSettings", "DeveloperSettings", "DeveloperToolSettings",
            "Slate", "SlateCore", "ToolMenus", "LevelEditor", "Projects", "Settings"
        });
    }
}
