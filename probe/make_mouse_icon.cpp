// make_mouse_icon.cpp - 把用户的鼠标 PNG 转成内嵌蒙版数据（一次性开发工具）
//
// 输出 src/mouse_icon_data.h / .cpp：
//   kMouseInk    主体线条的 alpha 蒙版（绘制时染成任务栏文字色）
//   kMouseAccent 红色滚轮的 alpha 蒙版（绘制时染成红色）
// 这样单文件 DLL 就能随深浅主题变色，且不需要外部 PNG。
#include <windows.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int main() {
    using namespace Gdiplus;
    GdiplusStartupInput si;
    ULONG_PTR token = 0;
    if (GdiplusStartup(&token, &si, nullptr) != Ok) { printf("GDI+ init failed\n"); return 1; }

    // 用真正的宽字符命令行，中文路径才不会在窄字符 argv 里被转坏
    int argc_w = 0;
    LPWSTR* argv_w = CommandLineToArgvW(GetCommandLineW(), &argc_w);
    // 原始素材不随仓库分发，正常用法是命令行传入自备的 PNG 路径；
    // 默认路径仅供本地开发时图方便。
    std::wstring path = (argv_w && argc_w > 1) ? argv_w[1]
        : L"assets/mouse.png";
    Bitmap src(path.c_str());
    const Gdiplus::Status load_status = src.GetLastStatus();
    if (load_status != Ok) {
        printf("cannot load PNG (status=%d)\n", (int)load_status);
        return 1;
    }
    const int W = (int)src.GetWidth(), H = (int)src.GetHeight();
    printf("PNG %dx%d  format=%d\n", W, H, (int)src.GetPixelFormat());

    // 统计 alpha 与颜色，同时求非透明像素的紧边界
    int opaque = 0, transparent = 0, reddish = 0;
    int x0 = W, y0 = H, x1 = -1, y1 = -1;
    long long rs = 0, gs = 0, bs = 0;
    long long ar = 0, ag = 0, ab = 0;
    std::vector<unsigned char> a_full(W * H), ink_full(W * H), acc_full(W * H);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            Color c;
            src.GetPixel(x, y, &c);
            const int a = c.GetA(), r = c.GetR(), g = c.GetG(), b = c.GetB();
            bool is_red = (a > 20 && r > g + 45 && r > b + 45 && r > 110);
            int ink = (255 - (std::min)(r, (std::min)(g, b))) * a / 255;
            if (ink < 0) ink = 0;
            if (ink > 255) ink = 255;
            a_full[y * W + x] = (unsigned char)a;
            ink_full[y * W + x] = is_red ? 0 : (unsigned char)ink;
            acc_full[y * W + x] = is_red ? (unsigned char)ink : 0;

            if (a > 8) {
                ++opaque;
                if (is_red) { ++reddish; ar += r; ag += g; ab += b; }
                else { rs += r; gs += g; bs += b; }
                x0 = (std::min)(x0, x); y0 = (std::min)(y0, y);
                x1 = (std::max)(x1, x); y1 = (std::max)(y1, y);
            } else ++transparent;
        }
    }
    printf("alpha: opaque=%d transparent=%d reddish=%d\n", opaque, transparent, reddish);
    printf("bbox: (%d,%d)-(%d,%d)  %dx%d  aspect=%.3f\n",
           x0, y0, x1, y1, x1 - x0 + 1, y1 - y0 + 1,
           (double)(x1 - x0 + 1) / (double)(y1 - y0 + 1));
    if (opaque > reddish)
        printf("avg ink color: R=%lld G=%lld B=%lld\n",
               rs / (opaque - reddish), gs / (opaque - reddish), bs / (opaque - reddish));
    const int acc_r = reddish ? (int)(ar / reddish) : 235;
    const int acc_g = reddish ? (int)(ag / reddish) : 75;
    const int acc_b = reddish ? (int)(ab / reddish) : 65;
    printf("avg accent color: R=%d G=%d B=%d\n", acc_r, acc_g, acc_b);

    // 下采样到固定高度 64（比 48 保留更多边缘信息，缩到任务栏尺寸时更清晰），保持宽高比
    const int kH = 64;
    const int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
    const int kW = (std::max)(1, (int)(kH * (double)bw / bh + 0.5));
    std::vector<unsigned char> ink(kW * kH, 0), acc(kW * kH, 0);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            int sx0 = x0 + (int)((double)x * bw / kW), sx1 = x0 + (int)((double)(x + 1) * bw / kW);
            int sy0 = y0 + (int)((double)y * bh / kH), sy1 = y0 + (int)((double)(y + 1) * bh / kH);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sy1 <= sy0) sy1 = sy0 + 1;
            long long si_ = 0, sa = 0; int n = 0;
            for (int sy = sy0; sy < sy1 && sy < H; ++sy)
                for (int sx = sx0; sx < sx1 && sx < W; ++sx) {
                    si_ += ink_full[sy * W + sx];
                    sa += acc_full[sy * W + sx];
                    ++n;
                }
            if (n > 0) {
                ink[y * kW + x] = (unsigned char)(si_ / n);
                acc[y * kW + x] = (unsigned char)(sa / n);
            }
        }
    }
    // 线稿缩到 1:1 任务栏尺寸时会被插值冲淡，做一次轻度膨胀把细线加粗。
    // 四邻域权重高于对角，得到接近圆形的笔触而不是方块。
    auto dilate = [&](std::vector<unsigned char>& m) {
        std::vector<unsigned char> out(m.size(), 0);
        for (int y = 0; y < kH; ++y) {
            for (int x = 0; x < kW; ++x) {
                int best = m[y * kW + x];
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        const int nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= kW || ny >= kH) continue;
                        const int w = (std::abs(dx) + std::abs(dy) == 1) ? 205 : 150;
                        const int v = m[ny * kW + nx] * w / 255;
                        if (v > best) best = v;
                    }
                }
                out[y * kW + x] = (unsigned char)((std::min)(255, best));
            }
        }
        m.swap(out);
    };
    // 只有细线稿才需要膨胀加粗；本身已经是粗线条的图标再膨胀会糊成一团。
    // 用下采样后「实心墨点」的占比自动判断，换任何素材都不用手动调参数。
    int strong = 0, accent_pixels = 0;
    for (int i = 0; i < kW * kH; ++i) {
        if (ink[i] > 128) ++strong;
        if (acc[i] > 32) ++accent_pixels;
    }
    const double density = (double)strong / (double)(kW * kH);
    const bool need_dilate = (density < 0.20);
    if (need_dilate) { dilate(ink); dilate(acc); }
    const bool has_accent = (accent_pixels > 4);
    printf("ink density=%.3f (%s), accent pixels=%d (%s)\n",
           density, need_dilate ? "dilated" : "kept original stroke",
           accent_pixels, has_accent ? "has color layer" : "monochrome");
    printf("mask: %dx%d (%d bytes/layer)\n", kW, kH, kW * kH);

    // 写头文件
    FILE* fh = fopen("src/mouse_icon_data.h", "wb");
    fprintf(fh, "// mouse_icon_data.h - 由 probe/make_mouse_icon.cpp 从用户提供的鼠标 PNG 生成\n"
                "// 请勿手工编辑；重新生成：make_mouse_icon.exe <png路径>\n"
                "#pragma once\n#include <cstddef>\nnamespace icon {\n"
                "constexpr int kMouseW = %d;\nconstexpr int kMouseH = %d;\n"
                "constexpr unsigned char kMouseAccentR = %d;\n"
                "constexpr unsigned char kMouseAccentG = %d;\n"
                "constexpr unsigned char kMouseAccentB = %d;\n"
                "constexpr bool kMouseHasAccent = %s;\n"
                "extern const unsigned char kMouseInk[kMouseW * kMouseH];\n"
                "extern const unsigned char kMouseAccent[kMouseW * kMouseH];\n"
                "} // namespace icon\n", kW, kH, acc_r, acc_g, acc_b,
                has_accent ? "true" : "false");
    fclose(fh);

    auto emit = [&](FILE* fp, const char* name, const std::vector<unsigned char>& data) {
        fprintf(fp, "const unsigned char %s[kMouseW * kMouseH] = {\n", name);
        for (int y = 0; y < kH; ++y) {
            fprintf(fp, "    ");
            for (int x = 0; x < kW; ++x) fprintf(fp, "%3d,", data[y * kW + x]);
            fprintf(fp, "\n");
        }
        fprintf(fp, "};\n\n");
    };
    FILE* fc = fopen("src/mouse_icon_data.cpp", "wb");
    fprintf(fc, "// mouse_icon_data.cpp - 自动生成，勿手工编辑\n"
                "#include \"mouse_icon_data.h\"\nnamespace icon {\n\n");
    emit(fc, "kMouseInk", ink);
    emit(fc, "kMouseAccent", acc);
    fprintf(fc, "} // namespace icon\n");
    fclose(fc);
    printf("written: src/mouse_icon_data.h / .cpp\n");
    return 0;
}
