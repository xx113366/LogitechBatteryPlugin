// battery_icon.cpp - 任务栏图标绘制
//
// 设计目标：像手机电池那样一眼看出剩余电量，同时能分辨出「这是鼠标的电量」。
//   style 0：左边是用户提供的鼠标图标（内嵌蒙版，随主题染色），右边横向电池
//   style 1：鼠标轮廓本身就是电池外壳（右侧带正极），电量自下而上填充
// 充电中叠加闪电；低电量自动变橙/红；读不到数据时画灰色问号。
//
// 全部用 GDI+ 抗锯齿绘制，插件与预览程序共用同一份代码。

#include "battery_icon.h"
#include "mouse_icon_data.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <vector>

namespace icon {
namespace {

    struct GdiplusSession {
        ULONG_PTR token = 0;
        bool ok = false;
        GdiplusSession() {
            Gdiplus::GdiplusStartupInput input;
            ok = (Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok);
        }
        // 故意不调用 GdiplusShutdown：DLL 卸载发生在 DllMain 的 loader lock 中，
        // 在那里关闭 GDI+ 是被禁止的；进程退出时由系统回收，无实际泄漏影响。
    };

    const GdiplusSession& Session() {
        static GdiplusSession s;      // C++11 保证线程安全，且首次调用发生在 DllMain 之后
        return s;
    }

    inline Gdiplus::Color ToColor(COLORREF c) {
        return Gdiplus::Color(GetRValue(c), GetGValue(c), GetBValue(c));
    }

    void AddRoundRect(Gdiplus::GraphicsPath& p, float x, float y, float w, float h, float r) {
        if (r <= 0.f) {
            p.AddRectangle(Gdiplus::RectF(x, y, w, h));
            return;
        }
        r = (std::min)(r, (std::min)(w, h) / 2.f);
        float d = 2.f * r;
        p.AddArc(x,         y,         d, d, 180.f, 90.f);
        p.AddArc(x + w - d, y,         d, d, 270.f, 90.f);
        p.AddArc(x + w - d, y + h - d, d, d,   0.f, 90.f);
        p.AddArc(x,         y + h - d, d, d,  90.f, 90.f);
        p.CloseFigure();
    }

    Gdiplus::Color FillColor(const Status& st) {
        if (!st.valid || st.percent < 0) return Gdiplus::Color(160, 160, 160);
        if (st.percent <= 15) return Gdiplus::Color(230, 68, 58);     // 红：快没电了
        if (st.percent <= 35) return Gdiplus::Color(238, 155, 40);     // 橙：该充电了
        return Gdiplus::Color(62, 182, 102);                            // 绿：健康
    }

    // 上窄下宽的水滴形轮廓：比胶囊更接近俯视的鼠标
    void AddMouseOutline(Gdiplus::GraphicsPath& p, float x, float y, float w, float h) {
        const float l = x, r = x + w, cx = x + w / 2.f;
        // GDI+ 的 AddBezier 需要显式起点；每段起点都写成上一段终点，路径即可连续
        p.AddBezier(l + 0.04f * w, y + 0.86f * h,
                    l - 0.01f * w, y + 0.22f * h,
                    l + 0.17f * w, y,
                    cx, y);
        p.AddBezier(cx, y,
                    r - 0.17f * w, y,
                    r + 0.01f * w, y + 0.22f * h,
                    r - 0.04f * w, y + 0.86f * h);
        p.AddBezier(r - 0.04f * w, y + 0.86f * h,
                    r - 0.04f * w, y + h,
                    l + 0.04f * w, y + h,
                    l + 0.04f * w, y + 0.86f * h);
        p.CloseFigure();
    }

    // 鼠标的两处识别特征：左右键中缝 + 滚轮
    void DrawMouseDetails(Gdiplus::Graphics& g, Gdiplus::Pen& pen, Gdiplus::Brush& ink,
                          float cx, float top, float s) {
        g.DrawLine(&pen, cx, top + 1.0f * s, cx, top + 7.2f * s);
        Gdiplus::GraphicsPath wheel;
        AddRoundRect(wheel, cx - 1.0f * s, top + 1.6f * s, 2.0f * s, 3.4f * s, 0.9f * s);
        g.FillPath(&ink, &wheel);
    }

    void DrawBolt(Gdiplus::Graphics& g, float cx, float cy, float s, Gdiplus::Color ink) {
        const float z = 0.78f * s;
        const float px[6] = {  0.9f, -2.4f, -0.2f, -1.0f,  2.5f,  0.3f };
        const float py[6] = { -4.3f,  0.4f,  0.4f,  4.3f, -0.9f, -0.9f };
        Gdiplus::PointF pts[6];
        for (int i = 0; i < 6; ++i) pts[i] = Gdiplus::PointF(cx + px[i] * z, cy + py[i] * z);

        Gdiplus::SolidBrush white(Gdiplus::Color(248, 255, 255, 255));
        g.FillPolygon(&white, pts, 6);
        Gdiplus::Pen edge(Gdiplus::Color(110, ink.GetR(), ink.GetG(), ink.GetB()), 0.6f * s);
        edge.SetLineJoin(Gdiplus::LineJoinRound);
        g.DrawPolygon(&edge, pts, 6);
    }

    void DrawUnknown(Gdiplus::Graphics& g, const Gdiplus::RectF& box, float s,
                     Gdiplus::Color ink) {
        Gdiplus::Font font(L"Arial", 8.f * s, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush br(Gdiplus::Color(190, ink.GetR(), ink.GetG(), ink.GetB()));
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        g.DrawString(L"?", 1, &font, box, &sf, &br);
    }

    // 用内嵌的 PNG 蒙版画鼠标：主体染成任务栏文字色（深浅主题通用）。
    // 若素材带彩色强调层（例如红色滚轮），再叠一层保留原色。
    // 蒙版由 probe/make_mouse_icon.cpp 从 assets/mouse.png 生成，已裁剪到紧边界。
    void DrawMouseBitmap(Gdiplus::Graphics& g, float x, float y, float w, float h,
                         Gdiplus::Color ink) {
        const int n = kMouseW * kMouseH;
        std::vector<BYTE> base(n * 4);
        for (int i = 0; i < n; ++i) {
            base[i * 4 + 0] = ink.GetB();
            base[i * 4 + 1] = ink.GetG();
            base[i * 4 + 2] = ink.GetR();
            // 缩到任务栏尺寸时边缘会被插值冲淡，轻微提升 alpha 让线条保持实心
            base[i * 4 + 3] = (BYTE)(std::min)(255, (int)kMouseInk[i] * 7 / 5);
        }
        // PixelFormat32bppARGB 在 MinGW 的 gdiplus 头里是宏，不能加命名空间限定
        Gdiplus::Bitmap b1(kMouseW, kMouseH, kMouseW * 4,
                           PixelFormat32bppARGB, base.data());
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        const Gdiplus::RectF dest(x, y, w, h);
        g.DrawImage(&b1, dest);

        if constexpr (kMouseHasAccent) {
            std::vector<BYTE> accent(n * 4);
            for (int i = 0; i < n; ++i) {
                accent[i * 4 + 0] = kMouseAccentB;
                accent[i * 4 + 1] = kMouseAccentG;
                accent[i * 4 + 2] = kMouseAccentR;
                accent[i * 4 + 3] = (BYTE)(std::min)(255, (int)kMouseAccent[i] * 7 / 5);
            }
            Gdiplus::Bitmap b2(kMouseW, kMouseH, kMouseW * 4,
                               PixelFormat32bppARGB, accent.data());
            g.DrawImage(&b2, dest);
        }
    }

    // 电池内数值：文字用主题色，描边取反色。
    // 这样浅色主题是「黑字+白描边」、深色主题是「白字+黑描边」，
    // 无论压在绿色/红色填充上还是落在空电池区域都清晰。
    void DrawBatteryNumber(Gdiplus::Graphics& g, const Gdiplus::RectF& box,
                           const wchar_t* text, float s, Gdiplus::Color ink) {
        Gdiplus::FontFamily family(L"Segoe UI");
        Gdiplus::GraphicsPath path;
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        // 常见两位数用最大字号，三/四位（如 100、100+）自动收紧，避免「+」被裁掉
        const int n = (int)wcslen(text);
        const float em = (n >= 4) ? 7.4f * s : (n == 3) ? 8.6f * s : 9.5f * s;
        if (path.AddString(text, -1, &family, Gdiplus::FontStyleBold,
                           em, box, &sf) != Gdiplus::Ok) return;
        const int lum = (ink.GetR() * 299 + ink.GetG() * 587 + ink.GetB() * 114) / 1000;
        Gdiplus::Color edge_col = (lum > 128)
            ? Gdiplus::Color(190, 0, 0, 0)          // 亮色文字 → 深色描边
            : Gdiplus::Color(190, 255, 255, 255);   // 暗色文字 → 亮色描边
        Gdiplus::SolidBrush fill(ink);
        Gdiplus::Pen edge(edge_col, (std::max)(0.6f * s, em * 0.085f));
        edge.SetLineJoin(Gdiplus::LineJoinRound);
        // 先描边再填字：描边在底层，不会侵蚀笔画，浅色主题下细笔画才不发灰
        g.DrawPath(&edge, &path);
        g.FillPath(&fill, &path);
    }

    // style 0：用户鼠标图标 + 留白 + 横向电池
    void DrawMouseAndBattery(Gdiplus::Graphics& g, float left, float cy, float s,
                             Gdiplus::Color ink, const Status& st, float gap,
                             float mouse_scale, bool number_inside) {
        Gdiplus::Pen pen(ink, 1.15f * s);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        Gdiplus::SolidBrush ink_brush(ink);

        const float mh = 14.5f * s * mouse_scale;   // 默认略高于电池（11*s），比上一版 13*s 放大一点
        const float mw = mh * (float)kMouseW / (float)kMouseH;   // 保持原图宽高比
        // 只显示纯数字（不带 %），电池不用太长；24*13 配合自适应字号足以放下「100+」
        const float bw = (number_inside ? 24.f : 18.f) * s;
        const float bh = (number_inside ? 13.f : 11.f) * s;
        const float cap_w = 2.4f * s, cap_h = 5.f * s;

        const float mx = left + 1.5f * s;
        const float my = cy - mh / 2.f;
        const float bx = mx + mw + gap * s;
        const float by = cy - bh / 2.f;

        // ---- 鼠标（内嵌 PNG 蒙版，随主题染色）----
        DrawMouseBitmap(g, mx, my, mw, mh, ink);

        // ---- 电池外壳 + 正极 ----
        Gdiplus::GraphicsPath body;
        AddRoundRect(body, bx, by, bw, bh, 2.2f * s);
        g.DrawPath(&pen, &body);

        Gdiplus::GraphicsPath cap;
        AddRoundRect(cap, bx + bw - 0.3f * s, by + bh / 2.f - cap_h / 2.f,
                     cap_w, cap_h, 0.8f * s);
        g.FillPath(&ink_brush, &cap);

        // ---- 电量填充（裁剪在圆角外壳内）----
        if (st.valid && st.percent >= 0) {
            const float inner = 1.7f * s;
            const float ratio = (std::min)(100, (std::max)(0, st.percent)) / 100.f;
            const float fw = (bw - 2.f * inner) * ratio;
            if (fw > 0.3f * s) {
                Gdiplus::SolidBrush fill(FillColor(st));
                g.SetClip(&body);
                g.FillRectangle(&fill, Gdiplus::RectF(bx + inner, by + inner,
                                                      fw, bh - 2.f * inner));
                g.ResetClip();
            }
        }

        const Gdiplus::RectF box(bx, by, bw, bh);
        if (number_inside) {
            wchar_t txt[16] = {};
            if (!st.valid || st.percent < 0)
                swprintf_s(txt, L"?");
            else
                swprintf_s(txt, L"%d%s",   // 不带 %：省下两个字符，数字能放大一号
                           (std::min)(100, (std::max)(0, st.percent)),
                           st.charging ? L"+" : L"");
            DrawBatteryNumber(g, box, txt, s, ink);
        } else {
            if (st.charging) DrawBolt(g, bx + bw * 0.47f, by + bh / 2.f, s, ink);
            else if (!st.valid) DrawUnknown(g, box, s, ink);
        }
    }

    // style 1：鼠标造型的电池，电量自下而上填充
    void DrawMouseShapedBattery(Gdiplus::Graphics& g, float left, float cy, float s,
                                Gdiplus::Color ink, const Status& st) {
        Gdiplus::Pen pen(ink, 1.15f * s);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        Gdiplus::SolidBrush ink_brush(ink);

        const float mw = 16.f * s, mh = 19.f * s;
        const float cap_w = 2.2f * s, cap_h = 6.f * s;
        const float mx = left + 1.5f * s + 0.6f * s;
        const float my = cy - mh / 2.f;

        Gdiplus::GraphicsPath body;
        AddMouseOutline(body, mx, my, mw, mh);

        if (st.valid && st.percent >= 0) {
            const float inner = 1.6f * s;
            const float ratio = (std::min)(100, (std::max)(0, st.percent)) / 100.f;
            const float max_h = mh - 2.f * inner;
            const float fh = max_h * ratio;
            if (fh > 0.3f * s) {
                Gdiplus::SolidBrush fill(FillColor(st));
                g.SetClip(&body);
                g.FillRectangle(&fill, Gdiplus::RectF(mx + inner, my + mh - inner - fh,
                                                      mw - 2.f * inner, fh));
                g.ResetClip();
            }
        }

        g.DrawPath(&pen, &body);

        // 正极：右侧中部的小凸起，让它明确是「电池」
        Gdiplus::GraphicsPath cap;
        AddRoundRect(cap, mx + mw - 0.3f * s, my + mh / 2.f - cap_h / 2.f,
                     cap_w, cap_h, 0.8f * s);
        g.FillPath(&ink_brush, &cap);

        DrawMouseDetails(g, pen, ink_brush, mx + mw / 2.f, my, s);

        if (st.charging) DrawBolt(g, mx + mw / 2.f, my + mh * 0.58f, 0.9f * s, ink);
        else if (!st.valid) DrawUnknown(g, Gdiplus::RectF(mx, my, mw, mh), s, ink);
    }

    // GDI+ 万一不可用时的极简兜底（不会让任务栏出现一片空白）
    void DrawFallback(HDC hdc, int x, int y, int w, int h, COLORREF outline, const Status& st) {
        int bw = 18, bh = 10;
        if (w < bw + 4) bw = w - 4;
        if (h < bh + 4) bh = h - 4;
        if (bw < 6 || bh < 5) return;
        int bx = x + (w - bw) / 2, by = y + (h - bh) / 2;
        HPEN p = CreatePen(PS_SOLID, 1, outline);
        HGDIOBJ op = SelectObject(hdc, p);
        HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, bx, by, bx + bw, by + bh);
        Rectangle(hdc, bx + bw, by + bh / 3, bx + bw + 2, by + bh - bh / 3);
        if (st.valid && st.percent > 0) {
            COLORREF fill = (st.percent <= 15) ? RGB(230, 68, 58)
                          : (st.percent <= 35) ? RGB(238, 155, 40)
                                               : RGB(62, 182, 102);
            HBRUSH br = CreateSolidBrush(fill);
            RECT r{ bx + 1, by + 1,
                    bx + 1 + (bw - 2) * (std::min)(100, st.percent) / 100,
                    by + bh - 1 };
            FillRect(hdc, &r, br);
            DeleteObject(br);
        }
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        DeleteObject(p);
    }

} // namespace

    int BaseWidth(const Options& opt) {
        float w;
        if (opt.style == kMouseShapedBattery) {
            w = 22.f;
        } else {
            const float mouse_w = 14.5f * opt.mouse_scale * (float)kMouseW / (float)kMouseH;
            const float battery_w = opt.number_inside ? 24.f : 18.f;
            w = std::ceil(1.5f + mouse_w + (float)opt.gap + battery_w + 2.4f + 1.5f);
        }
        if (opt.show_text) w += 36.f;
        return (int)w;
    }

    int PixelWidth(const Options& opt, int dpi) {
        if (dpi <= 0) dpi = 96;
        return (int)std::ceil(BaseWidth(opt) * dpi / 96.f);
    }

    void Draw(Gdiplus::Graphics& g, float x, float y, float w, float h, float scale,
              COLORREF outline_color, bool dark_mode, const Status& status, const Options& opt) {
        if (w <= 0 || h <= 0) return;
        Gdiplus::Color ink = (outline_color != CLR_INVALID)
                           ? ToColor(outline_color)
                           : (dark_mode ? Gdiplus::Color(240, 240, 240)
                                        : Gdiplus::Color(38, 38, 38));

        // 图标优先占满可用高度，同时不超出宽度；这样不管主程序用逻辑坐标还是物理坐标都不会被裁
        float s = h / 16.f;
        if (s < 0.6f) s = 0.6f;
        float cap_s = (scale > 0.f ? scale : 1.f) * 1.5f;
        if (cap_s < 1.f) cap_s = 1.f;
        if (s > cap_s) s = cap_s;
        // TrafficMonitor 会缓存插件项宽度（只在自己 ApplySettings 时重算），
        // 所以插件改大 mouse_gap 后传进来的 w 可能仍是旧值。此时若直接整体缩小，
        // 鼠标会跟着被压到 10px 以下、线条断掉，看起来像「显示不完全」。
        // 优先压缩纯留白 gap 来适配宽度，保住鼠标和电池的实际尺寸。
        Options draw_opt = opt;
        // 把可用宽度换算回 96 DPI 基准，只有「按 1:1 都放不下当前间距」时才压缩 gap；
        // 否则主程序正常给足宽度时会误把用户设的间距也吃掉。
        const float w_base = w / (scale > 0.f ? scale : 1.f);
        if (w_base > 0.f && (float)BaseWidth(opt) > w_base) {
            const float excess = (float)BaseWidth(opt) - w_base;
            draw_opt.gap = (int)std::ceil((std::max)(0.f, (float)opt.gap - excess));
        }
        float need = (float)BaseWidth(draw_opt) * s;
        if (need > w && need > 0.f) s *= w / need;
        if (s < 0.45f) s = 0.45f;

        Gdiplus::GraphicsState saved = g.Save();
        g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        float content_w = (float)BaseWidth(draw_opt) * s;
        float left = x + (w - content_w) / 2.f;
        float cy = y + h / 2.f;

        if (opt.style == kMouseShapedBattery)
            DrawMouseShapedBattery(g, left, cy, s, ink, status);
        else
            DrawMouseAndBattery(g, left, cy, s, ink, status,
                                (float)draw_opt.gap, opt.mouse_scale, opt.number_inside);

        // 可选的百分比文字（默认关闭）
        if (opt.show_text && status.text && *status.text) {
            float text_w = 36.f * s;
            float tx = left + (float)(BaseWidth(draw_opt) - 36) * s;
            Gdiplus::Font font(L"Segoe UI", 9.f * s, Gdiplus::FontStyleRegular,
                               Gdiplus::UnitPixel);
            Gdiplus::SolidBrush br(ink);
            Gdiplus::StringFormat sf;
            sf.SetAlignment(Gdiplus::StringAlignmentNear);
            sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            g.DrawString(status.text, -1, &font, Gdiplus::RectF(tx, y, text_w, h), &sf, &br);
        }

        g.Restore(saved);
    }

    void DrawToHDC(HDC hdc, int x, int y, int w, int h,
                   COLORREF outline_color, bool dark_mode,
                   const Status& status, const Options& opt) {
        if (!hdc || w <= 0 || h <= 0) return;
        if (!Session().ok) {
            DrawFallback(hdc, x, y, w, h,
                         (outline_color != CLR_INVALID)
                             ? outline_color : (dark_mode ? RGB(240, 240, 240) : RGB(38, 38, 38)),
                         status);
            return;
        }
        Gdiplus::Graphics g(hdc);
        int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
        if (dpi < 48) dpi = 96;
        Draw(g, (float)x, (float)y, (float)w, (float)h, dpi / 96.f,
             outline_color, dark_mode, status, opt);
    }

} // namespace icon
