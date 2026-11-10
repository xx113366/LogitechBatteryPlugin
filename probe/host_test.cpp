// host_test.cpp - 模拟 TrafficMonitor 加载插件并驱动它
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include "../src/PluginInterface.h"

static std::string U8(const wchar_t* w) {
    if (!w) return "(null)";
    int need = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (need <= 1) return std::string();
    std::string s(need - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], need, nullptr, nullptr);
    return s;
}

// 测试用配置目录：相对当前工作目录动态生成，不含任何机器专属路径
static std::wstring g_cfg_dir;
static std::wstring g_ini_path;

class FakeApp : public ITrafficMonitor {
public:
    int GetAPIVersion() override { return 1; }
    const wchar_t* GetVersion() override { return L"1.85"; }
    double GetMonitorValue(MonitorItem) override { return 0.0; }
    const wchar_t* GetMonitorValueString(MonitorItem, int) override { return L""; }
    void ShowNotifyMessage(const wchar_t* msg) override { printf("  [通知] %s\n", U8(msg).c_str()); }
    unsigned short GetLanguageId() const override { return 0; }
    const wchar_t* GetPluginConfigDir() const override { return g_cfg_dir.c_str(); }
    int GetDPI(DPIType) const override { return 96; }
    unsigned int GetThemeColor() const override { return 0; }
    const wchar_t* GetStringRes(const wchar_t*, const wchar_t*) override { return L""; }
    void* GetMainWindowHwnd() override { return nullptr; }
    void* GetTaskbarWindowHwnd() override { return nullptr; }
};

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetConsoleOutputCP(CP_UTF8);

    wchar_t cwd[MAX_PATH]{};
    GetCurrentDirectoryW(MAX_PATH, cwd);
    const std::wstring build_dir = std::wstring(cwd) + L"\\build";
    CreateDirectoryW(build_dir.c_str(), nullptr);
    g_cfg_dir = build_dir + L"\\config\\";
    CreateDirectoryW(g_cfg_dir.c_str(), nullptr);
    g_ini_path = g_cfg_dir + L"LogitechBattery.ini";

    const wchar_t* dll = (argc > 1) ? L"build\\LogitechBattery.dll" : L"build\\LogitechBattery.dll";

    HMODULE h = LoadLibraryW(dll);
    if (!h) { printf("LoadLibrary 失败: %lu\n", GetLastError()); return 1; }
    printf("DLL 加载成功\n");

    using GetInstanceFn = ITMPlugin * (*)();
    auto get_instance = (GetInstanceFn)GetProcAddress(h, "TMPluginGetInstance");
    if (!get_instance) { printf("找不到 TMPluginGetInstance\n"); return 1; }

    ITMPlugin* plugin = get_instance();
    if (!plugin) { printf("TMPluginGetInstance 返回空\n"); return 1; }

    printf("API 版本: %d\n", plugin->GetAPIVersion());
    printf("名称: %s\n", U8(plugin->GetInfo(ITMPlugin::TMI_NAME)).c_str());
    printf("描述: %s\n", U8(plugin->GetInfo(ITMPlugin::TMI_DESCRIPTION)).c_str());
    printf("作者: %s\n", U8(plugin->GetInfo(ITMPlugin::TMI_AUTHOR)).c_str());
    printf("版本: %s\n", U8(plugin->GetInfo(ITMPlugin::TMI_VERSION)).c_str());

    printf("命令数: %d", plugin->GetCommandCount());
    for (int i = 0; i < plugin->GetCommandCount(); ++i)
        printf("  [%d] %s", i, U8(plugin->GetCommandName(i)).c_str());
    printf("\n");

    FakeApp app;
    plugin->OnInitialize(&app);
    printf("OnInitialize 完成\n");
    plugin->OnExtenedInfo(ITMPlugin::EI_CONFIG_DIR, app.GetPluginConfigDir());
    plugin->OnExtenedInfo(ITMPlugin::EI_VALUE_TEXT_COLOR, L"#000000");
    plugin->OnExtenedInfo(ITMPlugin::EI_LABEL_TEXT_COLOR, L"#333333");

    for (int i = 0; i < 4; ++i) {
        IPluginItem* item = plugin->GetItem(i);
        if (!item) { printf("GetItem(%d) = nullptr\n", i); continue; }
        printf("项目%d: 名称=%s ID=%s 标签=%s 值=%s 示例=%s 自绘=%d\n", i,
               U8(item->GetItemName()).c_str(), U8(item->GetItemId()).c_str(),
               U8(item->GetItemLableText()).c_str(), U8(item->GetItemValueText()).c_str(),
               U8(item->GetItemValueSampleText()).c_str(), item->IsCustomDraw());
    }

    printf("\n--- 模拟主程序每秒调用 DataRequired ---\n");
    for (int t = 0; t < 30; ++t) {
        plugin->DataRequired();
        IPluginItem* item = plugin->GetItem(0);
        printf("t=%2ds  %-22s %-8s | %s\n", t,
               U8(item->GetItemLableText()).c_str(),
               U8(item->GetItemValueText()).c_str(),
               U8(plugin->GetTooltipInfo()).c_str());
        Sleep(300);
    }

    printf("\n--- 再次强制刷新 ---\n");
    plugin->OnPluginCommand(0, nullptr, nullptr);
    // 重新枚举 + 扫描是分步状态机，需要等它跑完，否则画出来的是「未知电量」图标
    IPluginItem* item = plugin->GetItem(0);
    for (int t = 0; t < 50; ++t) {
        plugin->DataRequired();
        Sleep(200);
        if (wcscmp(item->GetItemValueText(), L"--") != 0) break;
    }
    printf("最终: %s = %s\n", U8(item->GetItemLableText()).c_str(), U8(item->GetItemValueText()).c_str());
    printf("悬停提示: %s\n", U8(plugin->GetTooltipInfo()).c_str());

    printf("\n--- 热重载测试：直接改 ini，看是否无需重启即生效 ---\n");
    WritePrivateProfileStringW(L"general", L"mouse_gap", L"16", g_ini_path.c_str());
    for (int t = 0; t < 3; ++t) { plugin->DataRequired(); Sleep(200); }
    printf("mouse_gap=16 -> 宽度 %d\n", item->GetItemWidthEx(nullptr));
    WritePrivateProfileStringW(L"general", L"mouse_gap", L"8", g_ini_path.c_str());
    for (int t = 0; t < 3; ++t) { plugin->DataRequired(); Sleep(200); }
    printf("mouse_gap=8  -> 宽度 %d\n", item->GetItemWidthEx(nullptr));
    WritePrivateProfileStringW(L"general", L"mouse_scale", L"150", g_ini_path.c_str());
    for (int t = 0; t < 3; ++t) { plugin->DataRequired(); Sleep(200); }
    printf("mouse_scale=150 -> 宽度 %d\n", item->GetItemWidthEx(nullptr));
    WritePrivateProfileStringW(L"general", L"mouse_scale", L"100", g_ini_path.c_str());
    for (int t = 0; t < 3; ++t) { plugin->DataRequired(); Sleep(200); }
    printf("mouse_scale=100 -> 宽度 %d\n", item->GetItemWidthEx(nullptr));

    // 自绘测试：IsCustomDraw 为真时，在内存 DC 上绘制并统计像素
    if (item->IsCustomDraw()) {
        HDC screen = GetDC(nullptr);
        HDC mem = CreateCompatibleDC(screen);
        HBITMAP bmp = CreateCompatibleBitmap(screen, 300, 40);
        HGDIOBJ old = SelectObject(mem, bmp);
        HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HGDIOBJ old_font = SelectObject(mem, font);
        RECT all{ 0, 0, 300, 40 };
        FillRect(mem, &all, (HBRUSH)GetStockObject(WHITE_BRUSH));

        int w = item->GetItemWidthEx(mem);
        printf("自绘宽度 GetItemWidthEx = %d\n", w);
        item->DrawItem(mem, 2, 2, w, 24, false);

        int colored = 0, dark = 0;
        for (int y = 2; y < 26; ++y) {
            for (int x = 0; x < 120; ++x) {
                COLORREF c = GetPixel(mem, x, y);
                if (c != CLR_INVALID && c != RGB(255, 255, 255)) {
                    ++colored;
                    if (GetRValue(c) < 100 && GetGValue(c) < 100) ++dark;
                }
            }
        }
        printf("DrawItem 绘制了 %d 个非白像素（其中深色 %d 个）\n", colored, dark);

        // 用 ASCII 把绘制结果画出来，便于肉眼检查（# 深色描边/文字，+ 电量填充）
        printf("---- 绘制区域预览（2x2 像素取样）----\n");
        for (int yy = 0; yy < 30; ++yy) {
            for (int xx = 0; xx < 120; xx += 2) {
                COLORREF c = GetPixel(mem, xx, yy);
                if (c == RGB(255, 255, 255) || c == CLR_INVALID) {
                    printf(".");
                } else if (GetRValue(c) < 120 && GetGValue(c) < 120 && GetBValue(c) < 120) {
                    printf("#");
                } else {
                    printf("+");
                }
            }
            printf("\n");
        }
        printf("------------------------------------\n");

        SelectObject(mem, old_font);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
    } else {
        printf("已关闭自绘（display_mode=2，由主程序绘制文字）\n");
    }

    FreeLibrary(h);
    printf("卸载完成\n");
    return 0;
}
