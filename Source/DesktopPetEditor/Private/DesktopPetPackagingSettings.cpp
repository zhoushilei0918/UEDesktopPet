#include "DesktopPetPackagingSettings.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

UDesktopPetPackagingSettings::UDesktopPetPackagingSettings()
{
    CategoryName=TEXT("Pet");
}

void UDesktopPetPackagingSettings::EnsurePackagingAutomation()
{
    const TSharedPtr<IPlugin> Plugin=IPluginManager::Get().FindPlugin(TEXT("DesktopPet"));
    if(!Plugin||!Plugin->IsEnabled()||FPaths::GetProjectFilePath().IsEmpty())return;
    // UE 源码版从项目 Build 发现自动化脚本；生成链接到插件实现的薄入口。
    // 不改项目渲染、Target.cs、业务代码或其他插件开关，用户无需指定输出 EXE。
    const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("Build/DesktopPet"));
    const FString File=Directory/TEXT("DesktopPet.Automation.csproj");
    FString Content;
    if(!FFileHelper::LoadFileToString(Content,*(Plugin->GetBaseDir()/TEXT("Build/Automation/DesktopPet.Automation.csproj.template"))))
    {UE_LOG(LogTemp,Error,TEXT("DesktopPet: 自动打包名称模板缺失，请完整安装插件的 Build 目录。"));return;}
    FString Source=FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir()/TEXT("Build/Automation/DesktopPet.Automation.cs"));
    const FString Engine=FPaths::ConvertRelativePathToFull(FPaths::EngineDir());
    if(FPaths::IsUnderDirectory(Source,Engine))
    {FPaths::MakePathRelativeTo(Source,*Engine);Source=TEXT("$(EngineDir)/")+Source;}
    else FPaths::MakePathRelativeTo(Source,*(Directory+TEXT("/")));
    // XML 路径按数据转义，支持包含空格和 & 的项目目录。
    Source.ReplaceInline(TEXT("&"),TEXT("&amp;"));Source.ReplaceInline(TEXT("\""),TEXT("&quot;"));
    Source.ReplaceInline(TEXT("'"),TEXT("&apos;"));Source.ReplaceInline(TEXT("<"),TEXT("&lt;"));Source.ReplaceInline(TEXT(">"),TEXT("&gt;"));
    Content.ReplaceInline(TEXT("__PET_SOURCE__"),*Source);
    FString Previous;
    if(FFileHelper::LoadFileToString(Previous,*File))
    {
        if(Previous==Content)return;
        if(!Previous.Contains(TEXT("DesktopPet 自动生成的 UAT 接入")))
        {UE_LOG(LogTemp,Error,TEXT("DesktopPet: 自动化入口被其他文件占用，未覆盖：%s"),*File);return;}
    }
    if(!IFileManager::Get().MakeDirectory(*Directory,true)||!FFileHelper::SaveStringToFile(Content,*File,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        UE_LOG(LogTemp,Error,TEXT("DesktopPet: 无法生成自动打包名称入口：%s"),*File);
}
