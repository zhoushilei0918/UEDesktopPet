#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"
#include "DesktopPetMemory.h"
#include "DesktopPetFramePacing.h"

/** 着色器随插件分发，使用公开虚拟路径映射，无任何项目内容资产依赖。 */
class FDesktopPetModule : public IModuleInterface
{
public:
    // 撤销插件自己的全局 CVar 层，避免模块卸载后仍残留预算。
    virtual void ShutdownModule() override {FDesktopPetFramePacing::Shutdown();FDesktopPetMemory::Shutdown();}
    virtual void StartupModule() override
    {
        // 重定向由 Config/DefaultDesktopPet.ini 随引擎对象系统初始化加载。
        // 此模块为 Shader 提前加载，不能在 PostConfigInit 阶段调用 CoreRedirects API。
        const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("DesktopPet"));
        if(Plugin.IsValid())AddShaderSourceDirectoryMapping(TEXT("/Plugin/DesktopPet"),FPaths::Combine(Plugin->GetBaseDir(),TEXT("Shaders")));
    }
};
IMPLEMENT_MODULE(FDesktopPetModule,DesktopPet)
