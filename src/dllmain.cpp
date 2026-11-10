// dllmain.cpp - DLL 入口与插件导出函数
#include <windows.h>
#include "LogiBatteryPlugin.h"

// 注册设置窗口的窗口类时需要 DLL 自身的 HINSTANCE（不能是主程序 exe 的）
static HMODULE g_plugin_module = nullptr;

HMODULE GetPluginModuleHandle() { return g_plugin_module; }

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    (void)lpReserved;
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        g_plugin_module = hModule;
        DisableThreadLibraryCalls(hModule);
        break;
    case DLL_PROCESS_DETACH:
        break;
    default:
        break;
    }
    return TRUE;
}

#ifdef __cplusplus
extern "C" {
#endif

// TrafficMonitor 通过此函数获取插件对象
__declspec(dllexport) ITMPlugin* TMPluginGetInstance();

#ifdef __cplusplus
}
#endif

ITMPlugin* TMPluginGetInstance()
{
    return &CLogiBatteryPlugin::Instance();
}
