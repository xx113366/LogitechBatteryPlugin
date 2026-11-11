// dlgtest.cpp - 自动弹出设置窗口，验证「应用」可反复生效且不关闭窗口
#include <windows.h>
#ifndef _WIN32_IE
#define _WIN32_IE 0x0600
#endif
#include <commctrl.h>
#include <gdiplus.h>
#include <thread>
#include <cstdio>
#include <string>
#include "../src/PluginInterface.h"

static const CLSID kPng =
    { 0x557CF406, 0x1A04, 0x11D3, { 0x9A, 0x73, 0x00, 0x00, 0xF8, 0x1E, 0xF3, 0x2E } };
// 测试用配置目录：相对当前工作目录动态生成，不含任何机器专属路径
static std::wstring g_cfg_dir;
static std::wstring IniPath() { return g_cfg_dir + L"LogitechBattery.ini"; }

static void Shot(HWND w, const wchar_t* out) {
    RECT rc{};
    GetWindowRect(w, &rc);
    const int W = rc.right - rc.left, H = rc.bottom - rc.top;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, W, H);
    HGDIOBJ old = SelectObject(mem, bmp);
    if (!PrintWindow(w, mem, 0)) BitBlt(mem, 0, 0, W, H, screen, rc.left, rc.top, SRCCOPY);

    Gdiplus::GdiplusStartupInput si;
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartup(&token, &si, nullptr);
    Gdiplus::Bitmap gb(bmp, nullptr);
    printf("saved %ls -> %d\n", out, (int)gb.Save(out, &kPng, nullptr));

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

static void PrintIni(const char* tag) {
    std::wstring ini = IniPath();
    printf("%s: scale=%d gap=%d mode=%d style=%d count=%d\n", tag,
           (int)GetPrivateProfileIntW(L"general", L"mouse_scale", -1, ini.c_str()),
           (int)GetPrivateProfileIntW(L"general", L"mouse_gap", -1, ini.c_str()),
           (int)GetPrivateProfileIntW(L"general", L"display_mode", -1, ini.c_str()),
           (int)GetPrivateProfileIntW(L"general", L"icon_style", -1, ini.c_str()),
           (int)GetPrivateProfileIntW(L"general", L"device_count", -1, ini.c_str()));
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);

    wchar_t cwd[MAX_PATH]{};
    GetCurrentDirectoryW(MAX_PATH, cwd);
    const std::wstring build_dir = std::wstring(cwd) + L"\\build";
    CreateDirectoryW(build_dir.c_str(), nullptr);
    g_cfg_dir = build_dir + L"\\dlgcfg\\";
    CreateDirectoryW(g_cfg_dir.c_str(), nullptr);
    DeleteFileW(IniPath().c_str());     // 每次从默认配置开始

    HMODULE h = LoadLibraryW(L"build\\LogitechBattery.dll");
    if (!h) { printf("LoadLibrary failed: %lu\n", GetLastError()); return 1; }
    auto get = (ITMPlugin* (*)())GetProcAddress(h, "TMPluginGetInstance");
    ITMPlugin* plugin = get();
    plugin->OnExtenedInfo(ITMPlugin::EI_CONFIG_DIR, g_cfg_dir.c_str());

    int ret = -1;
    std::thread worker([&] { ret = (int)plugin->ShowOptionsDialog(nullptr); });
    Sleep(1200);

    HWND w = FindWindowW(L"LogiBatteryOptionsWindow", nullptr);
    printf("dialog hwnd=%p\n", (void*)w);
    if (!w) { worker.join(); return 1; }
    SetForegroundWindow(w);
    Sleep(150);
    Shot(w, L"build/options_dialog.png");
    PrintIni("initial");

    HWND tb_size = GetDlgItem(w, 101);
    HWND tb_gap = GetDlgItem(w, 102);
    HWND cb_mode = GetDlgItem(w, 103);

    // 额外截一张「电池内显示百分比」模式，用来展示新选项
    SendMessageW(cb_mode, CB_SETCURSEL, 1, 0);   // index 1 -> display_mode 3
    PostMessageW(w, WM_COMMAND, MAKEWPARAM(103, CBN_SELCHANGE), (LPARAM)cb_mode);
    Sleep(300);
    Shot(w, L"build/options_dialog_inside.png");
    SendMessageW(cb_mode, CB_SETCURSEL, 0, 0);
    PostMessageW(w, WM_COMMAND, MAKEWPARAM(103, CBN_SELCHANGE), (LPARAM)cb_mode);
    Sleep(150);

    auto apply = [&](int scale, int gap, int mode) {
        SendMessageW(tb_size, TBM_SETPOS, (WPARAM)TRUE, scale);
        PostMessageW(w, WM_HSCROLL, 0, (LPARAM)tb_size);
        SendMessageW(tb_gap, TBM_SETPOS, (WPARAM)TRUE, gap);
        PostMessageW(w, WM_HSCROLL, 0, (LPARAM)tb_gap);
        SendMessageW(cb_mode, CB_SETCURSEL, mode, 0);
        PostMessageW(w, WM_COMMAND, MAKEWPARAM(103, CBN_SELCHANGE), (LPARAM)cb_mode);
        Sleep(150);
        PostMessageW(w, WM_COMMAND, MAKEWPARAM(111, BN_CLICKED), 0);   // ID_APPLY
        Sleep(350);
    };

    apply(130, 14, 1);
    printf("after apply #1: window still open = %d\n", (int)IsWindow(w));
    PrintIni("apply #1");
    Shot(w, L"build/options_dialog_applied.png");

    apply(115, 10, 0);
    printf("after apply #2: window still open = %d\n", (int)IsWindow(w));
    PrintIni("apply #2");

    // 点过「应用」后再点「取消」，改动不应回滚
    PostMessageW(w, WM_COMMAND, MAKEWPARAM(110, BN_CLICKED), 0);       // ID_CANCEL
    worker.join();
    printf("ShowOptionsDialog returned %d (0=OR_OPTION_CHANGED)\n", ret);
    PrintIni("final after cancel");
    printf("item width = %d\n", plugin->GetItem(0)->GetItemWidthEx(nullptr));

    FreeLibrary(h);
    return 0;
}
