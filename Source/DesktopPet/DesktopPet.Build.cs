using UnrealBuildTool;
// 通用运行时不依赖任何项目角色、菜单或 Niagara 测试资产。
public class DesktopPet : ModuleRules
{
    public DesktopPet(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]{"Core","CoreUObject","Engine","DeveloperSettings","UMG","InputCore","EnhancedInput"});
        PrivateDependencyModuleNames.AddRange(new[]{"DesktopPetBootstrap","Slate","SlateCore","ApplicationCore","RenderCore","RHI","ImageWrapper","Json"});
        PublicSystemLibraries.AddRange(new[]{"user32.lib","gdi32.lib"});
    }
}
