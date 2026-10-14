#include "DesktopPetConfigStorage.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "Windows/WindowsHWrapper.h"

namespace DesktopPetConfigStorage
{
    namespace
    {
        constexpr int32 FormatVersion = 1;
        constexpr int64 MaximumFileBytes = 128 * 1024;
        const TCHAR* FormatName = TEXT("DesktopPet.RuntimeConfig");

        /** 档名不是任意路径：限定字符和长度，避免蓝图输入越过本工程存档目录。 */
        bool IsValidProfile(const FString& Profile)
        {
            if (Profile.IsEmpty() || Profile.Len() > 64)
            {
                return false;
            }
            for (TCHAR C : Profile)
            {
                if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_' ||
                      C == '-'))
                {
                    return false;
                }
            }
            return true;
        }
        bool ResolvePath(const FString& Profile, FString& Path, FString& Error)
        {
            Error.Reset();
            Path = GetPath(Profile);
            if (!Path.IsEmpty())
            {
                return true;
            }
            Error = TEXT("配置档名必须为 1～64 个英文字母、数字、下划线或连字符。");
            return false;
        }
    } // namespace

    FString GetPath(const FString& Profile)
    {
        if (!IsValidProfile(Profile))
        {
            return FString();
        }
        /** 固定前缀同时避开 Windows 的 CON/AUX 等保留文件名；不使用机器绝对路径或工程名。 */
        return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("DesktopPet") /
                                                 (GIsEditor ? TEXT("EditorProfiles") : TEXT("Profiles")) /
                                                 (TEXT("PetConfig_") + Profile + TEXT(".json")));
    }

    bool Save(const FString& Profile, const FDesktopPetConfig& Config, FString& Error)
    {
        FString Path;
        if (!ResolvePath(Profile, Path, Error))
        {
            return false;
        }
        FDesktopPetConfig Snapshot = Config;
        Snapshot.Normalize();
        const TSharedPtr<FJsonObject> Values = FJsonObjectConverter::UStructToJsonObject(Snapshot);
        if (!Values.IsValid())
        {
            Error = TEXT("桌宠配置序列化失败。");
            return false;
        }
        const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetStringField(TEXT("format"), FormatName);
        Root->SetNumberField(TEXT("version"), FormatVersion);
        Root->SetObjectField(TEXT("config"), Values);
        FString Text;
        if (!FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text)))
        {
            Error = TEXT("桌宠配置 JSON 序列化失败。");
            return false;
        }
        if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true))
        {
            Error = TEXT("无法创建桌宠配置目录，请检查目录写入权限。");
            return false;
        }
        // 临时文件与目标位于同一目录。写入失败不触碰旧文件，替换完成才向蓝图报告成功。
        const FString Temp = Path + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp");
        if (!FFileHelper::SaveStringToFile(Text, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        {
            IFileManager::Get().Delete(*Temp, false, false, true);
            Error = TEXT("无法写入桌宠配置临时文件。");
            return false;
        }
        if (!::MoveFileExW(*Temp, *Path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            const uint32 Code = ::GetLastError();
            IFileManager::Get().Delete(*Temp, false, false, true);
            Error = FString::Printf(TEXT("无法替换桌宠配置文件，Windows 错误码：%u。旧存档已保留。"), Code);
            return false;
        }
        return true;
    }

    bool Load(const FString& Profile, FDesktopPetConfig& InOutConfig, FString& Error)
    {
        FString Path;
        if (!ResolvePath(Profile, Path, Error))
        {
            return false;
        }
        const int64 Bytes = IFileManager::Get().FileSize(*Path);
        if (Bytes < 0)
        {
            Error = TEXT("尚未找到该档案的桌宠配置。");
            return false;
        }
        if (Bytes == 0 || Bytes > MaximumFileBytes)
        {
            Error = TEXT("桌宠配置文件大小异常，未应用存档。");
            return false;
        }
        FString Text;
        TSharedPtr<FJsonObject> Root;
        if (!FFileHelper::LoadFileToString(Text, *Path) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
        {
            Error = TEXT("无法读取桌宠配置或 JSON 已损坏，未应用存档。");
            return false;
        }
        FString Format;
        double Version = 0;
        const TSharedPtr<FJsonObject>* Values = nullptr;
        if (!Root->TryGetStringField(TEXT("format"), Format) || Format != FormatName ||
            !Root->TryGetNumberField(TEXT("version"), Version) || Version != FormatVersion ||
            !Root->TryGetObjectField(TEXT("config"), Values) || !Values || !Values->IsValid() ||
            (*Values)->Values.IsEmpty())
        {
            Error = TEXT("桌宠配置格式或版本不受支持，未应用存档。");
            return false;
        }
        // 在副本中导入，避免部分字段成功、后续字段失败时污染正在使用的配置。
        FDesktopPetConfig Candidate = InOutConfig;
        FText Reason;
        if (!FJsonObjectConverter::JsonObjectToUStruct(Values->ToSharedRef(), &Candidate, 0, 0, false, &Reason))
        {
            Error = TEXT("桌宠配置字段无效：") + Reason.ToString();
            return false;
        }
        Candidate.Normalize();
        InOutConfig = Candidate;
        return true;
    }

    bool Exists(const FString& Profile)
    {
        const FString Path = GetPath(Profile);
        return !Path.IsEmpty() && IFileManager::Get().FileExists(*Path);
    }

    bool Delete(const FString& Profile, FString& Error)
    {
        FString Path;
        if (!ResolvePath(Profile, Path, Error))
        {
            return false;
        }
        if (IFileManager::Get().Delete(*Path, false, false, true))
        {
            return true;
        }
        Error = TEXT("无法删除桌宠配置，请检查文件占用或权限。");
        return false;
    }
} // namespace DesktopPetConfigStorage
