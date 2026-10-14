#include "DesktopPetPackagingSettings.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

UDesktopPetPackagingSettings::UDesktopPetPackagingSettings()
{
    CategoryName = TEXT("Pet");
}

void UDesktopPetPackagingSettings::EnsurePackagingAutomation()
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DesktopPet"));
    if (!Plugin || !Plugin->IsEnabled() || FPaths::GetProjectFilePath().IsEmpty())
    {
        return;
    }
    // 源码和模板随 Source 分发；项目 Build 下只生成可删除、可重建的 UAT 入口。
    // 不改项目渲染、Target.cs、业务代码或其他插件开关，用户无需指定输出 EXE。
    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Build/DesktopPet"));
    const FString File = Directory / TEXT("DesktopPet.Automation.csproj");
    const FString AutomationDirectory = Plugin->GetBaseDir() / TEXT("Source/DesktopPetAutomation");
    FString Source = FPaths::ConvertRelativePathToFull(AutomationDirectory / TEXT("DesktopPet.Automation.cs"));
    if (!IFileManager::Get().FileExists(*Source))
    {
        UE_LOG(LogTemp, Error, TEXT("DesktopPet: 自动打包命名源码缺失，请完整安装插件的 Source/DesktopPetAutomation 目录。"));
        return;
    }
    FString Content;
    if (!FFileHelper::LoadFileToString(
            Content, *(AutomationDirectory / TEXT("DesktopPet.Automation.csproj.template"))))
    {
        UE_LOG(LogTemp, Error, TEXT("DesktopPet: 自动打包名称模板缺失，请完整安装插件的 Source/DesktopPetAutomation 目录。"));
        return;
    }
    const FString Engine = FPaths::ConvertRelativePathToFull(FPaths::EngineDir());
    if (FPaths::IsUnderDirectory(Source, Engine))
    {
        FPaths::MakePathRelativeTo(Source, *Engine);
        Source = TEXT("$(EngineDir)/") + Source;
    }
    else
    {
        FPaths::MakePathRelativeTo(Source, *(Directory + TEXT("/")));
    }
    // XML 路径按数据转义，支持包含空格和 & 的项目目录。
    Source.ReplaceInline(TEXT("&"), TEXT("&amp;"));
    Source.ReplaceInline(TEXT("\""), TEXT("&quot;"));
    Source.ReplaceInline(TEXT("'"), TEXT("&apos;"));
    Source.ReplaceInline(TEXT("<"), TEXT("&lt;"));
    Source.ReplaceInline(TEXT(">"), TEXT("&gt;"));
    Content.ReplaceInline(TEXT("__PET_SOURCE__"), *Source);
    FString Previous;
    if (FFileHelper::LoadFileToString(Previous, *File))
    {
        if (Previous == Content)
        {
            return;
        }
        if (!Previous.Contains(TEXT("DesktopPet 自动生成的 UAT 接入")))
        {
            UE_LOG(LogTemp, Error, TEXT("DesktopPet: 自动化入口被其他文件占用，未覆盖：%s"), *File);
            return;
        }
    }
    if (!IFileManager::Get().MakeDirectory(*Directory, true) ||
        !FFileHelper::SaveStringToFile(Content, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        UE_LOG(LogTemp, Error, TEXT("DesktopPet: 无法生成自动打包名称入口：%s"), *File);
    }
}
