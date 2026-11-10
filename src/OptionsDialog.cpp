// OptionsDialog.cpp - 图形化设置窗口
//
// 纯 Win32 控件 + 模态消息循环，不用 MFC、不用 .rc 资源脚本，三种构建方式都能直接编译。
// 上半部是实时预览（浅色 / 深色主题各一行），下半部是滑块和下拉框。
// 预览调用与插件完全相同的 icon::Draw()，所见即所得。

#include "OptionsDialog.h"
#include "battery_icon.h"

#ifndef _WIN32_IE
#define _WIN32_IE 0x0600
#endif
#include <commctrl.h>
#include <shellapi.h>
#include <gdiplus.h>

#include <cstdio>
#include <cwchar>
#include <vector>

extern HMODULE GetPluginModuleHandle();     // dllmain.cpp

namespace optsdlg {
namespace {

    const wchar_t* const kClassName = L"LogiBatteryOptionsWindow";
    const UINT kWakeMsg = WM_APP + 17;

    enum {
        ID_SIZE = 101, ID_GAP, ID_MODE, ID_STYLE, ID_COUNT,
        ID_SIZE_VAL, ID_GAP_VAL, ID_OPEN, ID_OK, ID_CANCEL, ID_APPLY
    };

    const int kClientW = 470;
    const int kClientH = 380;

    const wchar_t* const kHintNormal =
        L"拖动滑块实时预览；「应用」立即生效且不关闭窗口，「确定」保存并关闭。";
    const wchar_t* const kHintApplied =
        L"已应用到任务栏。可继续调整，或点「确定」保存并关闭。";

    RECT RectOf(int l, int t, int r, int b) { return RECT{ l, t, r, b }; }
    RECT PreviewLight() { return RectOf(16, 32, 454, 74); }
    RECT PreviewDark()  { return RectOf(16, 74, 454, 116); }
    RECT PreviewAll()   { return RectOf(16, 32, 454, 116); }

    struct Context {
        HWND hwnd = nullptr;
        HWND parent = nullptr;
        HFONT font = nullptr;
        HWND h_size = nullptr, h_gap = nullptr, h_mode = nullptr;
        HWND h_style = nullptr, h_count = nullptr;
        HWND h_size_val = nullptr, h_gap_val = nullptr, h_hint = nullptr;
        std::wstring ini_path;
        ApplyCallback on_apply = nullptr;
        void* user = nullptr;
        Values v{};
        bool ok = false;
        bool applied = false;
        bool done = false;
    };

    static int s_marker = 0;   // 位于 DLL 数据段，用于反查模块句柄

    HMODULE ModuleHandle() {
        HMODULE m = GetPluginModuleHandle();
        if (m) return m;
        HMODULE h = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)&s_marker, &h);
        return h ? h : GetModuleHandleW(nullptr);
    }

    HFONT MakeFont() {
        return CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                           L"Microsoft YaHei UI");
    }

    HWND AddCtl(Context* c, const wchar_t* cls, const wchar_t* text, DWORD style,
                int x, int y, int w, int h, int id) {
        HWND ctrl = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                                    x, y, w, h, c->hwnd,
                                    (HMENU)(INT_PTR)id, ModuleHandle(), nullptr);
        if (ctrl) SendMessageW(ctrl, WM_SETFONT, (WPARAM)c->font, TRUE);
        return ctrl;
    }

    void AddComboItem(HWND cb, const wchar_t* text) {
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)text);
    }

    void RefreshLabels(Context* c) {
        wchar_t b[32] = {};
        swprintf_s(b, L"%d%%", c->v.mouse_scale);
        SetWindowTextW(c->h_size_val, b);
        swprintf_s(b, L"%d px", c->v.mouse_gap);
        SetWindowTextW(c->h_gap_val, b);
    }

    void InvalidatePreview(HWND h) {
        RECT r = PreviewAll();
        InvalidateRect(h, &r, FALSE);
    }

    void ReadControls(Context* c) {
        c->v.mouse_scale = (int)SendMessageW(c->h_size, TBM_GETPOS, 0, 0);
        c->v.mouse_gap = (int)SendMessageW(c->h_gap, TBM_GETPOS, 0, 0);
        c->v.display_mode = (int)SendMessageW(c->h_mode, CB_GETCURSEL, 0, 0);
        c->v.icon_style = (int)SendMessageW(c->h_style, CB_GETCURSEL, 0, 0);
        c->v.device_count = (int)SendMessageW(c->h_count, CB_GETCURSEL, 0, 0) + 1;
    }

    void RestoreHint(Context* c) {
        if (c->h_hint) SetWindowTextW(c->h_hint, kHintNormal);
    }

    void DrawPreview(HDC hdc, const Values& v) {
        RECT light = PreviewLight();
        RECT dark = PreviewDark();

        HBRUSH bl = CreateSolidBrush(RGB(246, 246, 244));
        HBRUSH bd = CreateSolidBrush(RGB(40, 42, 48));
        FillRect(hdc, &light, bl);
        FillRect(hdc, &dark, bd);
        DeleteObject(bl);
        DeleteObject(bd);
        RECT all = PreviewAll();
        FrameRect(hdc, &all, (HBRUSH)GetStockObject(GRAY_BRUSH));

        wchar_t value[32] = {}, full[64] = {};
        swprintf_s(value, L"%d%%%s", v.preview_percent, v.preview_charging ? L"+" : L"");
        swprintf_s(full, L"鼠标电量  %s", value);

        if (v.display_mode == 2) {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(30, 30, 30));
            DrawTextW(hdc, full, -1, &light, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SetTextColor(hdc, RGB(240, 240, 240));
            DrawTextW(hdc, full, -1, &dark, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return;
        }

        icon::Options o;
        o.gap = v.mouse_gap;
        o.mouse_scale = v.mouse_scale / 100.f;
        o.style = v.icon_style;
        o.show_text = (v.display_mode == 1);
        icon::Status st;
        st.percent = v.preview_percent;
        st.valid = true;
        st.charging = v.preview_charging;
        st.text = value;

        Gdiplus::Graphics g(hdc);
        // 上下各留 3px，图标顶端（滚轮）才不会贴着预览区边框
        const float inset = 3.f;
        icon::Draw(g, (float)light.left + 20, (float)light.top + inset,
                   (float)(light.right - light.left - 40),
                   (float)(light.bottom - light.top) - 2.f * inset,
                   2.2f, RGB(32, 32, 32), false, st, o);
        icon::Draw(g, (float)dark.left + 20, (float)dark.top + inset,
                   (float)(dark.right - dark.left - 40),
                   (float)(dark.bottom - dark.top) - 2.f * inset,
                   2.2f, RGB(238, 238, 238), true, st, o);
    }

    void Finish(Context* c, bool ok) {
        c->ok = ok;
        c->done = true;
        PostMessageW(c->hwnd, kWakeMsg, 0, 0);   // 唤醒模态循环
    }

    LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        Context* c = (Context*)GetWindowLongPtrW(h, GWLP_USERDATA);

        if (msg == WM_CREATE) {
            c = (Context*)((CREATESTRUCTW*)l)->lpCreateParams;
            c->hwnd = h;
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)c);
            c->font = MakeFont();

            INITCOMMONCONTROLSEX icex{};
            icex.dwSize = sizeof(icex);
            icex.dwICC = ICC_BAR_CLASSES;
            InitCommonControlsEx(&icex);

            AddCtl(c, L"STATIC", L"预览（上：浅色主题　下：深色主题）", SS_LEFT,
                   16, 10, 340, 20, 0);

            AddCtl(c, L"STATIC", L"鼠标大小", SS_LEFT, 16, 126, 72, 20, 0);
            c->h_size = AddCtl(c, L"msctls_trackbar32", L"", TBS_HORZ | TBS_AUTOTICKS,
                               92, 124, 268, 24, ID_SIZE);
            c->h_size_val = AddCtl(c, L"STATIC", L"", SS_LEFT, 368, 126, 86, 20, ID_SIZE_VAL);
            SendMessageW(c->h_size, TBM_SETRANGE, (WPARAM)TRUE, (LPARAM)MAKELONG(50, 150));
            SendMessageW(c->h_size, TBM_SETTICFREQ, 25, 0);
            SendMessageW(c->h_size, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)c->v.mouse_scale);

            AddCtl(c, L"STATIC", L"图标间距", SS_LEFT, 16, 158, 72, 20, 0);
            c->h_gap = AddCtl(c, L"msctls_trackbar32", L"", TBS_HORZ | TBS_AUTOTICKS,
                              92, 156, 268, 24, ID_GAP);
            c->h_gap_val = AddCtl(c, L"STATIC", L"", SS_LEFT, 368, 158, 86, 20, ID_GAP_VAL);
            SendMessageW(c->h_gap, TBM_SETRANGE, (WPARAM)TRUE, (LPARAM)MAKELONG(0, 24));
            SendMessageW(c->h_gap, TBM_SETTICFREQ, 4, 0);
            SendMessageW(c->h_gap, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)c->v.mouse_gap);

            AddCtl(c, L"STATIC", L"显示模式", SS_LEFT, 16, 194, 72, 20, 0);
            c->h_mode = AddCtl(c, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                               92, 190, 362, 100, ID_MODE);
            AddComboItem(c->h_mode, L"仅图标（任务栏上无文字）");
            AddComboItem(c->h_mode, L"图标 + 百分比数字");
            AddComboItem(c->h_mode, L"纯文字（标签 + 数值）");
            SendMessageW(c->h_mode, CB_SETCURSEL, (WPARAM)c->v.display_mode, 0);

            AddCtl(c, L"STATIC", L"图标样式", SS_LEFT, 16, 224, 72, 20, 0);
            c->h_style = AddCtl(c, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                                92, 220, 362, 100, ID_STYLE);
            AddComboItem(c->h_style, L"鼠标图标 + 横向电池（默认）");
            AddComboItem(c->h_style, L"鼠标造型电池（自下而上填充）");
            SendMessageW(c->h_style, CB_SETCURSEL, (WPARAM)c->v.icon_style, 0);

            AddCtl(c, L"STATIC", L"设备数量", SS_LEFT, 16, 254, 72, 20, 0);
            c->h_count = AddCtl(c, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                                92, 250, 362, 100, ID_COUNT);
            AddComboItem(c->h_count, L"1 个");
            AddComboItem(c->h_count, L"2 个");
            AddComboItem(c->h_count, L"3 个");
            SendMessageW(c->h_count, CB_SETCURSEL, (WPARAM)(c->v.device_count - 1), 0);

            c->h_hint = AddCtl(c, L"STATIC", kHintNormal,
                               SS_LEFT, 16, 290, 440, 20, 0);

            AddCtl(c, L"BUTTON", L"打开完整配置文件", BS_PUSHBUTTON,
                   16, 330, 150, 28, ID_OPEN);
            AddCtl(c, L"BUTTON", L"应用", BS_PUSHBUTTON, 246, 330, 64, 28, ID_APPLY);
            AddCtl(c, L"BUTTON", L"取消", BS_PUSHBUTTON, 318, 330, 64, 28, ID_CANCEL);
            AddCtl(c, L"BUTTON", L"确定", BS_DEFPUSHBUTTON, 390, 330, 64, 28, ID_OK);

            RefreshLabels(c);
            return 0;
        }

        if (!c) return DefWindowProcW(h, msg, w, l);

        switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(h, &ps);
            if (hdc) {
                DrawPreview(hdc, c->v);
                EndPaint(h, &ps);
            }
            return 0;
        }
        case WM_HSCROLL: {
            HWND ctl = (HWND)l;
            if (ctl == c->h_size || ctl == c->h_gap) {
                int pos = (int)SendMessageW(ctl, TBM_GETPOS, 0, 0);
                if (ctl == c->h_size) c->v.mouse_scale = pos;
                else                 c->v.mouse_gap = pos;
                RefreshLabels(c);
                RestoreHint(c);
                InvalidatePreview(h);
            }
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(w);
            const int code = HIWORD(w);
            if (id == ID_MODE && code == CBN_SELCHANGE) {
                c->v.display_mode = (int)SendMessageW(c->h_mode, CB_GETCURSEL, 0, 0);
                RestoreHint(c);
                InvalidatePreview(h);
            } else if (id == ID_STYLE && code == CBN_SELCHANGE) {
                c->v.icon_style = (int)SendMessageW(c->h_style, CB_GETCURSEL, 0, 0);
                RestoreHint(c);
                InvalidatePreview(h);
            } else if (id == ID_COUNT && code == CBN_SELCHANGE) {
                c->v.device_count = (int)SendMessageW(c->h_count, CB_GETCURSEL, 0, 0) + 1;
                RestoreHint(c);
            } else if (id == ID_APPLY) {
                ReadControls(c);
                if (c->on_apply) c->on_apply(c->user, c->v);
                c->applied = true;
                if (c->h_hint) SetWindowTextW(c->h_hint, kHintApplied);
                RefreshLabels(c);
                InvalidatePreview(h);
            } else if (id == ID_OK) {
                ReadControls(c);
                Finish(c, true);
            } else if (id == ID_CANCEL) {
                Finish(c, false);
            } else if (id == ID_OPEN) {
                std::wstring param = c->ini_path;
                ShellExecuteW(h, L"open", L"notepad.exe", param.c_str(), nullptr, SW_SHOWNORMAL);
                Finish(c, false);      // 不保存，避免覆盖记事本里的改动
            }
            return 0;
        }
        case WM_CLOSE:
            Finish(c, false);
            return 0;
        case WM_DESTROY:
            if (c->font) { DeleteObject(c->font); c->font = nullptr; }
            return 0;
        default:
            break;
        }
        return DefWindowProcW(h, msg, w, l);
    }

    void RegisterClassOnce() {
        static bool done = false;
        if (done) return;
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = ModuleHandle();
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = kClassName;
        done = (RegisterClassExW(&wc) != 0);
    }

} // namespace

    bool Show(HWND parent, const std::wstring& ini_path, Values& values,
              ApplyCallback on_apply, void* user) {
        // icon::Draw() 不走 DrawToHDC()，这里确保 GDI+ 已初始化（只做一次）
        static bool gdiplus_ready = [] {
            Gdiplus::GdiplusStartupInput si;
            ULONG_PTR token = 0;
            return Gdiplus::GdiplusStartup(&token, &si, nullptr) == Gdiplus::Ok;
        }();
        (void)gdiplus_ready;

        RegisterClassOnce();
        Context ctx;
        ctx.parent = parent;
        ctx.ini_path = ini_path;
        ctx.on_apply = on_apply;
        ctx.user = user;
        ctx.v = values;

        RECT rc{ 0, 0, kClientW, kClientH };
        const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
        const DWORD exstyle = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
        AdjustWindowRectEx(&rc, style, FALSE, exstyle);
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;

        int x = 0, y = 0;
        RECT pr{};
        bool center_on_parent = false;
        if (parent && GetWindowRect(parent, &pr)) {
            const int pw = pr.right - pr.left, ph = pr.bottom - pr.top;
            if (pw >= 300 && ph >= 200) {
                x = pr.left + (pw - w) / 2;
                y = pr.top + (ph - h) / 2;
                center_on_parent = true;
            }
        }
        if (!center_on_parent) {
            x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
            y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
        }

        HWND hwnd = CreateWindowExW(exstyle, kClassName, L"罗技设备电量 · 显示设置",
                                    style, x, y, w, h, parent, nullptr,
                                    ModuleHandle(), &ctx);
        if (!hwnd) return false;

        if (parent) EnableWindow(parent, FALSE);
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);

        MSG msg{};
        while (!ctx.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (IsDialogMessageW(hwnd, &msg)) continue;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (parent) {
            EnableWindow(parent, TRUE);
            SetForegroundWindow(parent);
        }
        DestroyWindow(hwnd);

        // 点过「应用」后即使再点「取消」，改动也已生效——不回滚，和 Windows 常规对话框一致
        const bool changed = (ctx.ok || ctx.applied);
        if (changed) values = ctx.v;
        return changed;
    }

} // namespace optsdlg
