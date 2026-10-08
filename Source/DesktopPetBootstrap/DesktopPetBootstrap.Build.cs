using UnrealBuildTool;
// 提前加载的窗口防护模块只依赖 Core，不在引擎初始化前使用 UObject 或 Slate。
public class DesktopPetBootstrap : ModuleRules
{
    public DesktopPetBootstrap(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.Add("Core");
        PublicSystemLibraries.Add("user32.lib");
    }
}
