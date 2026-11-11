// preview.cpp - 用插件的图标绘制代码生成效果预览 PNG（开发用，不参与插件编译）
// 构建：g++ -std=c++17 -O2 -o build/preview.exe probe/preview.cpp src/battery_icon.cpp -lgdiplus -lgdi32 -luser32 -static ...
#include <windows.h>
#include <gdiplus.h>
#include <cstdio>
#include <vector>

#include "../src/battery_icon.h"

static const CLSID kPngEncoder =
    { 0x557CF406, 0x1A04, 0x11D3, { 0x9A, 0x73, 0x00, 0x00, 0xF8, 0x1E, 0xF3, 0x2E } };

struct Case {
    const wchar_t* caption;
    int percent;
    bool charging;
    bool valid;
    int style;
    bool show_text;
    const wchar_t* text;
    bool inside = false;      // 百分比显示在电池内部
};

int main() {
    using namespace Gdiplus;
    GdiplusStartupInput si;
    ULONG_PTR token = 0;
    if (GdiplusStartup(&token, &si, nullptr) != Ok) {
        printf("GdiplusStartup failed\n");
        return 1;
    }

    const int W = 900, H = 470;
    Bitmap bmp(W, H, PixelFormat32bppARGB);
    Graphics g(&bmp);
    g.SetSmoothingMode(SmoothingModeHighQuality);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    SolidBrush light(Color(255, 250, 250, 250));
    SolidBrush dark(Color(255, 32, 33, 38));
    SolidBrush ink_light(Color(255, 34, 34, 34));
    SolidBrush ink_dark(Color(255, 238, 238, 238));
    Font title(L"Microsoft YaHei", 17.f, FontStyleBold, UnitPixel);
    Font sub(L"Microsoft YaHei", 10.5f, FontStyleRegular, UnitPixel);
    Font cap(L"Microsoft YaHei", 10.f, FontStyleRegular, UnitPixel);
    StringFormat sf;
    sf.SetAlignment(StringAlignmentNear);
    sf.SetLineAlignment(StringAlignmentCenter);

    // 深色区从 258 起：第一行图标 y=268，必须整块落在深色背景上，否则白色描边会被切掉
    const float dark_top = 258.f;
    g.FillRectangle(&light, 0.f, 0.f, (float)W, dark_top);
    g.FillRectangle(&dark, 0.f, dark_top, (float)W, (float)H - dark_top);

    g.DrawString(L"TrafficMonitor · 罗技鼠标电量图标", -1, &title,
                 RectF(24.f, 12.f, 600.f, 30.f), &sf, &ink_light);
    g.DrawString(L"第一行：默认图标的电量状态  ·  第二行：多种数字显示方式（电池内 / 右侧 / 造型电池）  ·  充电叠加闪电，低电量自动变色", -1, &sub,
                 RectF(24.f, 42.f, 860.f, 20.f), &sf, &ink_light);

    const Case cases[] = {
        { L"100%  满电",        100, false, true,  0, false, L"" },
        { L"72%  正常",          72, false, true,  0, false, L"" },
        { L"60%  充电中",        60, true,  true,  0, false, L"" },
        { L"30%  偏低",          30, false, true,  0, false, L"" },
        { L"9%  低电量",          9, false, true,  0, false, L"" },
        { L"未读取到数据",       -1, false, false, 0, false, L"" },
    };
    const Case cases2[] = {
        { L"电池内 76%",         76, false, true,  0, false, L"", true },
        { L"电池内 充电 60%+",   60, true,  true,  0, false, L"", true },
        { L"电池内 低电量 9%",    9, false, true,  0, false, L"", true },
        { L"图标右侧 72%",       72, false, true,  0, true,  L"72%" },
        { L"鼠标造型电池 100%", 100, false, true,  1, false, L"" },
        { L"鼠标造型 充电中",    60, true,  true,  1, false, L"" },
    };

    auto row = [&](const Case* list, float y0, bool on_dark, float scale, bool captions) {
        for (int i = 0; i < 6; ++i) {
            const Case& c = list[i];
            float cell_x = 24.f + i * 146.f;
            float cell_w = 140.f;

            icon::Options opt;
            opt.style = c.style;
            opt.show_text = c.show_text;
            opt.number_inside = c.inside;
            icon::Status st;
            st.percent = c.percent;
            st.charging = c.charging;
            st.valid = c.valid;
            st.text = c.text;

            COLORREF outline = on_dark ? RGB(238, 238, 238) : RGB(38, 38, 38);
            icon::Draw(g, cell_x, y0, cell_w, 40.f, scale, outline, on_dark, st, opt);

            if (captions) {
                SolidBrush& br = on_dark ? ink_dark : ink_light;
                g.DrawString(c.caption, -1, &cap,
                             RectF(cell_x, y0 + 42.f, cell_w, 20.f), &sf, &br);
            }
        }
    };

    // 浅色主题
    row(cases,  82.f,  false, 2.4f, true);
    row(cases2, 158.f, false, 2.4f, true);
    g.DrawString(L"深色主题", -1, &sub, RectF(24.f, 230.f, 200.f, 20.f), &sf, &ink_light);

    // 深色主题
    row(cases,  268.f, true, 2.4f, true);
    row(cases2, 344.f, true, 2.4f, true);

    // 1:1 真实大小示意（放在一条模拟任务栏上）
    SolidBrush bar(Color(255, 24, 24, 28));
    g.FillRectangle(&bar, 0.f, 430.f, (float)W, 40.f);
    SolidBrush txt(Color(255, 205, 205, 205));
    Font tiny(L"Microsoft YaHei", 9.5f, FontStyleRegular, UnitPixel);
    g.DrawString(L"1:1 任务栏实际大小    ↑ 12.3 KB/s     CPU 18%      ", -1, &tiny,
                 RectF(24.f, 432.f, 420.f, 36.f), &sf, &txt);
    for (int i = 0; i < 4; ++i) {
        icon::Options opt; opt.style = 0;
        icon::Status st;
        st.percent = (i == 0) ? 72 : (i == 1) ? 60 : (i == 2) ? 12 : -1;
        st.charging = (i == 1);
        st.valid = (i != 3);
        st.text = L"";
        icon::Draw(g, 450.f + i * 62.f, 432.f, 56.f, 36.f, 1.f,
                   RGB(235, 235, 235), true, st, opt);
    }
    // 电池内数字的 1:1 效果
    icon::Options in; in.style = 0; in.number_inside = true;
    icon::Status st2; st2.percent = 76; st2.valid = true; st2.text = L"";
    icon::Draw(g, 706.f, 432.f, 72.f, 36.f, 1.f,
               RGB(235, 235, 235), true, st2, in);

    Status s = bmp.Save(L"build/icon_preview.png", &kPngEncoder, nullptr);
    printf("preview saved: %s (status=%d)\n", s == Ok ? "ok" : "FAILED", (int)s);
    fflush(stdout);
    // 不调用 GdiplusShutdown：局部 GDI+ 对象在本函数返回时才析构，
    // 提前关闭 GDI+ 会让它们的析构访问已释放的运行时（进程退出时系统统一回收）。
    (void)token;
    return s == Ok ? 0 : 1;
}
