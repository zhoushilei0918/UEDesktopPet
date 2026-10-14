#pragma once
#include "DesktopPetTypes.h"

/** 只保存通用参数，不引用角色、Widget、关卡或任何编辑器模块。 */
namespace DesktopPetConfigStorage
{
    /** 返回当前工程的用户存档路径；非法档名返回空字符串。编辑器与独立游戏隔离。 */
    FString GetPath(const FString& Profile);
    /** 先写临时文件，再原子替换旧文件；失败时保留原存档。 */
    bool Save(const FString& Profile, const FDesktopPetConfig& Config, FString& Error);
    /** 全部解析成功才更新输出；缺少的新字段沿用调用方提供的初始值。 */
    bool Load(const FString& Profile, FDesktopPetConfig& InOutConfig, FString& Error);
    /** 只判断文件是否存在，不代表内容已经通过校验。 */
    bool Exists(const FString& Profile);
    /** 只删除指定档案；文件原本不存在也视为成功。 */
    bool Delete(const FString& Profile, FString& Error);
} // namespace DesktopPetConfigStorage
