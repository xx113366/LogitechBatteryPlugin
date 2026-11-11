// gapscan.cpp - 扫描 mouse_gap 0..24 在不同绘制区宽度下的表现（诊断用）
#include <windows.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "../src/battery_icon.h"

static const CLSID kPng =
    { 0x557CF406, 0x1A04, 0x11D3, { 0x9A, 0x73, 0x00, 0x00, 0xF8, 0x1E, 0xF3, 0x2E } };

int main() {
    using namespace Gdiplus;
    GdiplusStartupInput si;
    ULONG_PTR token = 0;
    GdiplusStartup(&token, &si, nullptr);

    const int ROWS = 25, RH = 28, CW = 190, COLS = 3;
    Bitmap bmp(CW * COLS, ROWS * RH, PixelFormat32bppARGB);
    Graphics g(&bmp);
    g.Clear(Color(255, 250, 250, 250));

    Pen sep(Color(255, 210, 210, 210));
    for (int c = 1; c < COLS; ++c)
        g.DrawLine(&sep, (float)(c * CW), 0.f, (float)(c * CW), (float)(ROWS * RH));

    printf("gap | BaseW |  A:w=BaseW      |  B:w=47(旧值)  |  C:w=60\n");
    for (int gap = 0; gap <= 24; ++gap) {
        icon::Options o;
        o.gap = gap;
        o.number_inside = true;
        icon::Status st;
        st.percent = 76;
        st.valid = true;
        st.text = L"76";

        const int bw = icon::BaseWidth(o);
        const float y = (float)(gap * RH);
        const int ws[3] = { bw, 47, 60 };
        float eff_s[3] = {};

        for (int c = 0; c < 3; ++c) {
            icon::Draw(g, (float)(c * CW), y, (float)ws[c], (float)RH,
                       1.f, RGB(30, 30, 30), false, st, o);
            // 复现 Draw() 的「先压缩留白、再整体缩放」逻辑，打印实际 s 与有效 gap
            float s = (float)RH / 16.f;
            if (s < 0.6f) s = 0.6f;
            if (s > 1.5f) s = 1.5f;
            icon::Options d = o;
            const float w_base = (float)ws[c] / 1.f;   // 扫描时 scale=1
            if ((float)icon::BaseWidth(d) > w_base) {
                const float excess = (float)icon::BaseWidth(d) - w_base;
                d.gap = (int)ceil((std::max)(0.f, (float)gap - excess));
            }
            float need = (float)icon::BaseWidth(d) * s;
            if (need > (float)ws[c]) s *= (float)ws[c] / need;
            if (s < 0.45f) s = 0.45f;
            eff_s[c] = s;
        }
        printf("%3d | %5d | s=%.3f mh=%.1f | s=%.3f mh=%.1f | s=%.3f mh=%.1f\n",
               gap, bw,
               eff_s[0], 14.5f * eff_s[0],
               eff_s[1], 14.5f * eff_s[1],
               eff_s[2], 14.5f * eff_s[2]);
    }

    Status s = bmp.Save(L"build/gapscan.png", &kPng, nullptr);
    printf("saved build/gapscan.png (%s)\n", s == Ok ? "ok" : "FAILED");
    return 0;
}
