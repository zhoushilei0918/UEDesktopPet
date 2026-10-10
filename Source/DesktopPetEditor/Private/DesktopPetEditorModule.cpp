#include "DesktopPetEditorSubsystem.h"
#include "DesktopPetPackagingSettings.h"
#include "Editor.h"
#include "Interfaces/IPluginManager.h"
#include "ISettingsModule.h"
#include "Modules/ModuleManager.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Brushes/SlateImageBrush.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "DesktopPetEditor"

/** 模块只负责按钮和图标注册，配置业务由编辑器子系统统一实现。 */
class FDesktopPetEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        UDesktopPetPackagingSettings::EnsurePackagingAutomation();
        if (IsRunningCommandlet()) return;
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DesktopPet"));
        if (!Plugin) return;
        Style = MakeShared<FSlateStyleSet>(TEXT("DesktopPetEditorStyle"));
        const FString Icon = Plugin->GetBaseDir() / TEXT("Resources/Icon128.png");
        Style->Set(TEXT("DesktopPet.Configure"), new FSlateImageBrush(Icon, FVector2D(40,40)));
        Style->Set(TEXT("DesktopPet.Configure.Small"), new FSlateImageBrush(Icon, FVector2D(20,20)));
        FSlateStyleRegistry::RegisterSlateStyle(*Style);
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FDesktopPetEditorModule::RegisterMenus));
    }

    virtual void ShutdownModule() override
    {
        // 热重载、关闭编辑器时一并移除回调，避免重复工具栏或悬空图标。
        UToolMenus::UnRegisterStartupCallback(this);
        UToolMenus::UnregisterOwner(this);
        if (Style) { FSlateStyleRegistry::UnRegisterSlateStyle(*Style); Style.Reset(); }
    }

private:
    TSharedPtr<FSlateStyleSet> Style;

    static UDesktopPetEditorSubsystem* GetPetTools()
    {
        return GEditor ? GEditor->GetEditorSubsystem<UDesktopPetEditorSubsystem>() : nullptr;
    }

    static void OpenPetSettings()
    {
        if(auto* Settings=FModuleManager::LoadModulePtr<ISettingsModule>(TEXT("Settings")))
            Settings->ShowViewer(TEXT("Project"),TEXT("Pet"),TEXT("DesktopPetSettings"));
    }

    void RegisterMenus()
    {
        FToolMenuOwnerScoped Owner(this);
        const FUIAction Configure(
            FExecuteAction::CreateLambda([] { if(auto* Tools=GetPetTools())Tools->PetConfigureProject(); }),
            FCanExecuteAction::CreateLambda([] { const auto* Tools=GetPetTools();return Tools&&Tools->PetCanConfigureProject(); }));
        const FUIAction SettingsAction(FExecuteAction::CreateStatic(&FDesktopPetEditorModule::OpenPetSettings));
        const FSlateIcon Icon(Style->GetStyleSetName(),TEXT("DesktopPet.Configure"));
        const FText ConfigureLabel=LOCTEXT("AutoConfigure","自动设置项目");
        const FText ConfigureTip=LOCTEXT("AutoConfigureTip","补齐桌宠默认配置、DX11/DX12 与当前地图打包配置；选中的模型可自动接入。保留已有画面设置，生成备份报告。");
        const FText SettingsLabel=LOCTEXT("OpenSettings","打开桌宠项目设置");
        const FText SettingsTip=LOCTEXT("OpenSettingsTip","快速定位到 Project Settings → Pet → Desktop Pet。");

        // 固定的两项菜单可由 UE ToolMenus 正常扩展、撤销注册；点击头像只打开菜单，不立即修改工程。
        UToolMenu* DropDown=UToolMenus::Get()->RegisterMenu(TEXT("DesktopPet.ToolbarMenu"));
        auto& Actions=DropDown->AddSection(TEXT("DesktopPet"));
        Actions.AddMenuEntry(TEXT("DesktopPet.ConfigureProject"),ConfigureLabel,ConfigureTip,Icon,Configure);
        Actions.AddMenuEntry(TEXT("DesktopPet.Settings"),SettingsLabel,SettingsTip,Icon,SettingsAction);
        UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.LevelEditorToolBar.User"))
            ->FindOrAddSection(TEXT("DesktopPet"))
            .AddEntry(FToolMenuEntry::InitComboButton(TEXT("DesktopPet.Menu"),FUIAction(),
                FOnGetContent::CreateLambda([]{return UToolMenus::Get()->GenerateWidget(TEXT("DesktopPet.ToolbarMenu"),FToolMenuContext());}),
                LOCTEXT("ToolbarPet","桌宠"),LOCTEXT("ToolbarPetTip","打开桌宠工具菜单。"),Icon));

        // 主菜单仍保留备用入口，操作与头像下拉菜单共用同一实现。
        auto& Section=UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"))->FindOrAddSection(TEXT("DesktopPet"),LOCTEXT("Group","DesktopPet"));
        Section.AddMenuEntry(TEXT("DesktopPet.ConfigureProject"),ConfigureLabel,ConfigureTip,Icon,Configure);
        Section.AddMenuEntry(TEXT("DesktopPet.Settings"),SettingsLabel,SettingsTip,Icon,SettingsAction);
        Section.AddMenuEntry(TEXT("DesktopPet.PackagingName"),LOCTEXT("PackagingName","打包程序名称"),LOCTEXT("PackagingNameTip","设置一个名称，正常打包时自动应用到入口 EXE、进程与窗口。"),Icon,
            FUIAction(FExecuteAction::CreateLambda([]{
                if(auto* Settings=FModuleManager::LoadModulePtr<ISettingsModule>(TEXT("Settings")))
                    Settings->ShowViewer(TEXT("Project"),TEXT("Pet"),TEXT("DesktopPetPackagingSettings"));
            })));
    }

};

IMPLEMENT_MODULE(FDesktopPetEditorModule, DesktopPetEditor)
#undef LOCTEXT_NAMESPACE
