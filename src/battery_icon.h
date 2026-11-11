// battery_icon.h - 任务栏「鼠标 + 电池」图标绘制（GDI+ 抗锯齿）
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <wtypes.h>      // PROPID：MinGW 的 gdiplus.h 依赖它，MSVC 下无害
#include <gdiplus.h>

namespace icon {

    // 图标样式
    enum Style {
        kMouseAndBattery = 0,      // 鼠标剪影 + 横向电池（默认，辨识度最高）
        kMouseShapedBattery = 1,   // 鼠标造型本身就是电池，电量自下而上填充
    };

    struct Options {
        int  style = kMouseAndBattery;
        bool show_text = false;     // 图标右侧附带百分比文字（默认关闭）
        bool number_inside = false; // 直接在电池内部画百分比（电池会自动加宽）
        int  gap = 8;               // 鼠标与电池之间的留白（96 DPI 下的像素，约一个空格）
        float mouse_scale = 1.0f;   // 鼠标图标缩放系数（1.0 = 14.5 px 高）
    };

    struct Status {
        int   percent = -1;         // 0..100，-1 表示未知
        bool  charging = false;
        bool  valid = false;
        const wchar_t* text = L"";  // show_text 时显示的文字，例如 L"60%+"
    };

    // 96 DPI 下所需宽度（供 IPluginItem::GetItemWidth 使用）
    int BaseWidth(const Options& opt);

    // 指定 DPI 下的实际像素宽度（供 IPluginItem::GetItemWidthEx 使用）
    int PixelWidth(const Options& opt, int dpi);

    // 绘制到 HDC（插件 DrawItem 调用）
    void DrawToHDC(HDC hdc, int x, int y, int w, int h,
                   COLORREF outline_color, bool dark_mode,
                   const Status& status, const Options& opt);

    // 绘制到 GDI+ 画布（预览程序调用，保证「所见即所得」）
    void Draw(Gdiplus::Graphics& g, float x, float y, float w, float h,
              float scale, COLORREF outline_color, bool dark_mode,
              const Status& status, const Options& opt);

} // namespace icon
