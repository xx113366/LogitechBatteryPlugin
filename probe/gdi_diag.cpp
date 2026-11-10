// gdi_diag.cpp - 验证 DrawToHDC 在 DIB / DDB 内存 DC 上的填充是否真的落像素
#include <windows.h>
#include <cstdio>
#include "../src/battery_icon.h"

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    HDC screen = GetDC(nullptr);

    icon::Options opt;
    icon::Status st;
    st.percent = 76;
    st.valid = true;
    st.charging = false;

    // ---- 1) 32bpp DIB section（最接近 TrafficMonitor 的双缓冲位图）----
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 100;
    bi.bmiHeader.biHeight = -40;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, dib);
    RECT all{ 0, 0, 100, 40 };
    FillRect(mem, &all, (HBRUSH)GetStockObject(WHITE_BRUSH));

    printf("LOGPIXELSY=%d  width=%d\n", GetDeviceCaps(mem, LOGPIXELSY), icon::BaseWidth(opt));
    icon::DrawToHDC(mem, 2, 2, 40, 24, RGB(30, 30, 30), false, st, opt);
    GdiFlush();

    auto* px = (unsigned char*)bits;
    printf("--- DIB section, y=12, x=18..38 (BGRA) ---\n");
    for (int x = 18; x <= 38; x += 2) {
        unsigned char* p = px + (12 * 100 + x) * 4;
        printf("x=%2d  B=%3d G=%3d R=%3d A=%3d\n", x, p[0], p[1], p[2], p[3]);
    }

    SelectObject(mem, old);
    DeleteObject(dib);
    DeleteDC(mem);

    // ---- 2) DDB（host_test 当前用的方式）----
    HDC mem2 = CreateCompatibleDC(screen);
    HBITMAP ddb = CreateCompatibleBitmap(screen, 100, 40);
    HGDIOBJ old2 = SelectObject(mem2, ddb);
    FillRect(mem2, &all, (HBRUSH)GetStockObject(WHITE_BRUSH));
    icon::DrawToHDC(mem2, 2, 2, 40, 24, RGB(30, 30, 30), false, st, opt);
    printf("--- DDB, y=12, x=18..38 (COLORREF) ---\n");
    for (int x = 18; x <= 38; x += 2) {
        COLORREF c = GetPixel(mem2, x, 12);
        printf("x=%2d  R=%3d G=%3d B=%3d\n", x, GetRValue(c), GetGValue(c), GetBValue(c));
    }
    SelectObject(mem2, old2);
    DeleteObject(ddb);
    DeleteDC(mem2);

    ReleaseDC(nullptr, screen);
    return 0;
}
