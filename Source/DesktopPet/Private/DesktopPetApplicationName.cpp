#include "DesktopPetApplicationName.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Windows/WindowsHWrapper.h"

FString DesktopPetApplicationName::Get()
{
    // 打包时将名称写进游戏 EXE，运行时不需要编辑器模块、额外 ini 或外部工具。
    const HMODULE Module=GetModuleHandleW(nullptr);
    const HRSRC Resource=FindResourceW(Module,TEXT("DESKTOPPET_APPLICATION_NAME"),MAKEINTRESOURCEW(10));
    if(Resource)
    {
        const DWORD Bytes=SizeofResource(Module,Resource);
        const HGLOBAL Loaded=LoadResource(Module,Resource);
        const WCHAR* Text=Loaded?static_cast<const WCHAR*>(LockResource(Loaded)):nullptr;
        if(Text&&Bytes>=2&&Bytes<=258&&Bytes%2==0&&Text[Bytes/2-1]==0)
            return FString(Text);
    }
    FString Name;
#if WITH_EDITOR
    // PIE / Standalone 共用当前设置，不修改 UnrealEditor.exe 或编辑器工作台标题。
    if(GConfig)GConfig->GetString(TEXT("/Script/DesktopPetEditor.DesktopPetPackagingSettings"),TEXT("PetApplicationName"),Name,GEditorIni);
    Name.TrimStartAndEndInline();
    if(!Name.IsEmpty())return Name;
#endif
    if(GConfig)GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"),TEXT("ProjectName"),Name,GGameIni);
    Name.TrimStartAndEndInline();
    return Name.IsEmpty()?FString(FApp::GetProjectName()):Name;
}
