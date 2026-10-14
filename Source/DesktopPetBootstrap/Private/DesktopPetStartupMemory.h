#pragma once
/** 引擎初始化前配置分配粒度。与运行时缓存预算分离，不能在蓝图中伪装成即时生效的参数。 */
class FDesktopPetStartupMemory
{
public:
    static void Startup();
    static void Shutdown();
};
