// zoom.cpp - 放大截图片段便于检查任务栏图标（开发用）
#include <windows.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <cstdio>

static const CLSID kPng =
    { 0x557CF406, 0x1A04, 0x11D3, { 0x9A, 0x73, 0x00, 0x00, 0xF8, 0x1E, 0xF3, 0x2E } };

int main() {
    using namespace Gdiplus;
    GdiplusStartupInput si; ULONG_PTR token = 0;
    GdiplusStartup(&token, &si, nullptr);
    int n = 0;
    LPWSTR* av = CommandLineToArgvW(GetCommandLineW(), &n);
    if (n < 2) { printf("usage: zoom.exe <png> [scale] [crop_x] [crop_y] [crop_w] [crop_h]\n"); return 1; }
    int scale = (n > 2) ? _wtoi(av[2]) : 5;
    int cx = (n > 3) ? _wtoi(av[3]) : 0;
    int cy = (n > 4) ? _wtoi(av[4]) : 0;
    int cw = (n > 5) ? _wtoi(av[5]) : 0;
    int ch = (n > 6) ? _wtoi(av[6]) : 0;

    Bitmap src(av[1]);
    if (src.GetLastStatus() != Ok) { printf("load failed\n"); return 1; }
    const int W = (int)src.GetWidth(), H = (int)src.GetHeight();
    if (cw <= 0) cw = W;
    if (ch <= 0) ch = H;
    printf("source %dx%d, crop (%d,%d) %dx%d, scale %d\n", W, H, cx, cy, cw, ch, scale);

    Bitmap dst(cw * scale, ch * scale, PixelFormat32bppARGB);
    {
        Graphics g(&dst);
        g.SetInterpolationMode(InterpolationModeNearestNeighbor);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        g.DrawImage(&src, Rect(0, 0, cw * scale, ch * scale),
                    cx, cy, cw, ch, UnitPixel);
    }
    Status st = dst.Save(L"build/taskbar_zoom.png", &kPng, nullptr);
    printf("saved build/taskbar_zoom.png (%s)\n", st == Ok ? "ok" : "FAILED");
    return st == Ok ? 0 : 1;
}
