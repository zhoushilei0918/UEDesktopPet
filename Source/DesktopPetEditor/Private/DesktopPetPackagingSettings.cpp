#include "DesktopPetPackagingSettings.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/MessageDialog.h"
#include "Windows/WindowsHWrapper.h"

UDesktopPetPackagingSettings::UDesktopPetPackagingSettings()
{
    // 留空代表插件不覆盖 UE 的原生命名，不把工程名自动填成自定义值。
    CategoryName=TEXT("Pet");
}

namespace DesktopPetPackaging
{
    /** VERSIONINFO 的块长度包含其子块；字符串长度以 UTF-16 字符计，其余值长度以字节计。 */
    struct FBlock
    {
        uint16 Type=0;
        FString Key;
        TArray<uint8> Value;
        TArray<FBlock> Children;
    };
    static int32 Align4(int32 Position){return (Position+3)&~3;}
    static uint16 Read16(const TArray<uint8>& Data,int32 Offset)
    {uint16 Value=0;FMemory::Memcpy(&Value,Data.GetData()+Offset,2);return Value;}
    static void Write16(TArray<uint8>& Data,uint16 Value)
    {Data.Append(reinterpret_cast<const uint8*>(&Value),2);}
    static void Pad(TArray<uint8>& Data){while(Data.Num()%4)Data.Add(0);}
    static bool Parse(const TArray<uint8>& Data,int32 Start,int32 Limit,FBlock& Block)
    {
        if(Start<0||Start+6>Limit||Limit>Data.Num())return false;
        const int32 End=Start+Read16(Data,Start);
        const uint16 ValueLength=Read16(Data,Start+2);Block.Type=Read16(Data,Start+4);
        if(End>Limit||End<=Start+6||Block.Type>1)return false;
        int32 P=Start+6;
        while(P+2<=End&&Read16(Data,P)!=0){Block.Key.AppendChar(TCHAR(Read16(Data,P)));P+=2;}
        if(P+2>End)return false;
        P=Align4(P+2);
        const int32 ValueBytes=ValueLength*(Block.Type==1?2:1);
        if(P+ValueBytes>End)return ValueBytes==0&&P-End<4;
        Block.Value.Append(Data.GetData()+P,ValueBytes);P=Align4(P+ValueBytes);
        while(P+6<=End)
        {
            const int32 Length=Read16(Data,P);
            if(Length<6)return false;
            FBlock Child;if(!Parse(Data,P,End,Child))return false;
            Block.Children.Add(MoveTemp(Child));P=Align4(P+Length);
        }
        return true;
    }
    static bool Encode(const FBlock& Block,TArray<uint8>& Data)
    {
        const int32 Start=Data.Num();Write16(Data,0);
        const int32 ValueLength=Block.Type==1?Block.Value.Num()/2:Block.Value.Num();
        if(ValueLength>MAX_uint16)return false;
        Write16(Data,uint16(ValueLength));Write16(Data,Block.Type);
        for(TCHAR C:Block.Key)Write16(Data,uint16(C));Write16(Data,0);Pad(Data);
        Data.Append(Block.Value);
        for(const auto& Child:Block.Children){Pad(Data);if(!Encode(Child,Data))return false;}
        const int32 Length=Data.Num()-Start;if(Length>MAX_uint16)return false;
        const uint16 Size=uint16(Length);FMemory::Memcpy(Data.GetData()+Start,&Size,2);return true;
    }
    static void SetString(FBlock& Table,const TCHAR* Key,const FString& Text)
    {
        FBlock* Item=Table.Children.FindByPredicate([&](const FBlock& B){return B.Key==Key;});
        if(!Item){Item=&Table.Children.AddDefaulted_GetRef();Item->Key=Key;}
        Item->Type=1;Item->Value.Reset();
        for(TCHAR C:Text)Write16(Item->Value,uint16(C));Write16(Item->Value,0);
    }
    struct FVersion { WORD Language=0;TArray<uint8> Bytes; };
    static BOOL CALLBACK CollectLanguage(HMODULE Module,LPCWSTR Type,LPCWSTR Name,WORD Language,LONG_PTR Param)
    {
        auto& Versions=*reinterpret_cast<TArray<FVersion>*>(Param);
        HRSRC Resource=FindResourceExW(Module,Type,Name,Language);
        HGLOBAL Loaded=Resource?LoadResource(Module,Resource):nullptr;
        const void* Data=Loaded?LockResource(Loaded):nullptr;
        if(!Data)return false;
        auto& Version=Versions.AddDefaulted_GetRef();Version.Language=Language;
        Version.Bytes.Append(static_cast<const uint8*>(Data),SizeofResource(Module,Resource));return true;
    }
    static bool Prepare(const FString& Path,const FString& Name,TArray<FVersion>& Versions,FString& Result)
    {
        // 重写签名文件会破坏 Authenticode；只接受本地未签名的构建产物。
        TArray<uint8> File;
        if(!FFileHelper::LoadFileToArray(File,*Path)||File.Num()<sizeof(IMAGE_DOS_HEADER))return false;
        IMAGE_DOS_HEADER DOS{};FMemory::Memcpy(&DOS,File.GetData(),sizeof(DOS));
        if(DOS.e_magic!=IMAGE_DOS_SIGNATURE||DOS.e_lfanew<0||int64(DOS.e_lfanew)+sizeof(IMAGE_NT_HEADERS64)>File.Num())return false;
        IMAGE_NT_HEADERS64 PE{};FMemory::Memcpy(&PE,File.GetData()+DOS.e_lfanew,sizeof(PE));
        if(PE.Signature!=IMAGE_NT_SIGNATURE||PE.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)return false;
        if(PE.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY].Size)
        {Result=TEXT("EXE 已签名，请先对未签名的打包产物设置名称，再执行签名。");return false;}
        HMODULE Module=LoadLibraryExW(*Path,nullptr,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        if(!Module)return false;
        const bool Collected=EnumResourceLanguagesW(Module,MAKEINTRESOURCEW(16),MAKEINTRESOURCEW(1),CollectLanguage,reinterpret_cast<LONG_PTR>(&Versions))!=false;
        FreeLibrary(Module);
        if(!Collected||Versions.IsEmpty())return false;
        for(auto& Version:Versions)
        {
            FBlock Root;if(!Parse(Version.Bytes,0,Version.Bytes.Num(),Root)||Root.Key!=TEXT("VS_VERSION_INFO"))return false;
            bool Changed=false;
            for(auto& Info:Root.Children)if(Info.Key==TEXT("StringFileInfo"))for(auto& Table:Info.Children)
            {SetString(Table,TEXT("FileDescription"),Name);SetString(Table,TEXT("ProductName"),Name);Changed=true;}
            if(!Changed)return false;
            Version.Bytes.Reset();if(!Encode(Root,Version.Bytes))return false;
        }
        return true;
    }
    static bool Apply(const FString& Path,const TArray<FVersion>& Versions)
    {
        HANDLE Update=BeginUpdateResourceW(*Path,false);if(!Update)return false;
        for(const auto& Version:Versions)
            if(!UpdateResourceW(Update,MAKEINTRESOURCEW(16),MAKEINTRESOURCEW(1),Version.Language,const_cast<uint8*>(Version.Bytes.GetData()),Version.Bytes.Num()))
            {EndUpdateResourceW(Update,true);return false;}
        return EndUpdateResourceW(Update,false)!=false;
    }
}

bool UDesktopPetPackagingSettings::PetApplyNameToExecutable(const FString& ExecutablePath,const FString& DisplayName,FString& Result)
{
    using namespace DesktopPetPackaging;
    Result.Reset();const FString Name=DisplayName.TrimStartAndEnd();
    // 默认分支不读取、不备份、不修改 EXE，原生打包结果逐字节保留。
    if(Name.IsEmpty())
    {Result=TEXT("未设置自定义名称，沿用 UE 默认打包命名。未修改任何 EXE。若此前已应用自定义名称，请重新打包恢复 UE 默认结果。");return true;}
    if(Name.Len()>128||Name.Contains(TEXT("\n"))||Name.Contains(TEXT("\r")))
    {Result=TEXT("请输入 1–128 个字符的单行程序名称。");return false;}
    const FString Entry=FPaths::ConvertRelativePathToFull(ExecutablePath);
    if(!FPaths::GetExtension(Entry).Equals(TEXT("exe"),ESearchCase::IgnoreCase)||!FPaths::FileExists(Entry))
    {Result=TEXT("请选择已经打包的入口 EXE。");return false;}
    TArray<FString> Paths{Entry};
    // UE 启动器在 RCDATA 201 保存真正游戏 EXE 的相对路径；不递归修改目录中的其他程序。
    HMODULE Module=LoadLibraryExW(*Entry,nullptr,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if(Module)
    {
        HRSRC Resource=FindResourceW(Module,MAKEINTRESOURCEW(201),MAKEINTRESOURCEW(10));
        if(Resource)
        {
            const DWORD Bytes=SizeofResource(Module,Resource);HGLOBAL Loaded=LoadResource(Module,Resource);
            const WCHAR* Text=Loaded?static_cast<const WCHAR*>(LockResource(Loaded)):nullptr;
            if(Text&&Bytes>=2&&Bytes%2==0&&Text[Bytes/2-1]==0)
            {
                FString Relative(Text);FString Child=FPaths::ConvertRelativePathToFull(FPaths::GetPath(Entry),Relative);
                FPaths::NormalizeFilename(Child);FPaths::CollapseRelativeDirectories(Child);
                FString Base=FPaths::GetPath(Entry);FPaths::NormalizeFilename(Base);
                if(!FPaths::IsRelative(Relative)||!FPaths::IsUnderDirectory(Child,Base)||!FPaths::FileExists(Child))
                {FreeLibrary(Module);Result=TEXT("启动器指向的游戏 EXE 无效或不在打包目录内。");return false;}
                Paths.AddUnique(Child);
            }
        }
        FreeLibrary(Module);
    }
    TArray<TArray<FVersion>> Versions;Versions.SetNum(Paths.Num());
    // 全部预检通过才写入；尝试独占打开也能提前发现程序仍在运行或文件被占用。
    for(int32 I=0;I<Paths.Num();++I)
    {
        HANDLE File=CreateFileW(*Paths[I],GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(File==INVALID_HANDLE_VALUE){Result=TEXT("无法写入 EXE，请先关闭打包程序并检查文件是否只读：")+Paths[I];return false;}
        CloseHandle(File);
        if(!Prepare(Paths[I],Name,Versions[I],Result))
        {if(Result.IsEmpty())Result=TEXT("无法读取有效的 Win64 EXE 版本资源：")+Paths[I];return false;}
    }
    const FString Backup=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("DesktopPetPackaging")/FGuid::NewGuid().ToString());
    if(!IFileManager::Get().MakeDirectory(*Backup,true)){Result=TEXT("无法创建 EXE 备份目录。");return false;}
    for(int32 I=0;I<Paths.Num();++I)
        if(IFileManager::Get().Copy(*(Backup/FString::Printf(TEXT("%d.exe"),I)),*Paths[I])!=COPY_OK)
        {Result=TEXT("EXE 备份失败，原文件未修改。");return false;}
    for(int32 I=0;I<Paths.Num();++I)if(!Apply(Paths[I],Versions[I]))
    {
        Result=TEXT("写入失败，正在恢复原文件。备份：")+Backup;
        for(int32 J=0;J<=I;++J)
            if(IFileManager::Get().Copy(*Paths[J],*(Backup/FString::Printf(TEXT("%d.exe"),J)),true,true)!=COPY_OK)Result+=TEXT("\n恢复失败：")+Paths[J];
        return false;
    }
    Result=FString::Printf(TEXT("已设置 %d 个程序的显示名称为「%s」。下次启动生效。\n重新打包后需重新应用；EXE 文件名保持不变。\n备份：%s"),Paths.Num(),*Name,*Backup);
    return true;
}

void UDesktopPetPackagingSettings::PetApplyPackagedName()
{
    FString Result;PetApplyNameToExecutable(PetPackagedExecutable.FilePath,PetApplicationName,Result);
    FMessageDialog::Open(EAppMsgType::Ok,FText::FromString(Result));
}
