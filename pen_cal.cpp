// 线宽校准：把鼠标 / 耳机放在同一 pen 序列下渲染 14px，测"笔画横截面实际厚度"。
// 用途：两只图标缩放方式不同（鼠标高度受限、耳机宽度受限），同一个 pen 值
//       落到屏幕上并不一样粗；要视觉等重，必须按实测厚度配对。
// 编译：g++ -O2 -o pen_cal.exe pen_cal.cpp -lgdi32 -lgdiplus

#include <windows.h>
#include <gdiplus.h>
#include <cstdio>
#include <cmath>
#include <cstring>
using namespace Gdiplus;

// ---------------- 路径（与 WirelessBattery.cpp 完全一致） ----------------
static const float CUP_SPAN    = 0.85f;
static const float BAND_SQUASH = 0.85f;
static const float CUP_INNER   = 6.5f;
static inline float Hx(float x) { return 32.0f + (x - 32.0f) * CUP_SPAN; }
static inline float Vy(float y) { return 34.0f - (34.0f - y) * BAND_SQUASH; }

static void AddMousePaths(GraphicsPath& body, GraphicsPath& seam, GraphicsPath& wheel)
{
    body.AddBezier(32.0f, 6.5f, 20.7f, 6.5f, 13.5f, 14.6f, 13.5f, 26.7f);
    body.AddLine(13.5f, 26.7f, 13.5f, 37.3f);
    body.AddBezier(13.5f, 37.3f, 13.5f, 49.4f, 20.7f, 57.5f, 32.0f, 57.5f);
    body.AddBezier(32.0f, 57.5f, 43.3f, 57.5f, 50.5f, 49.4f, 50.5f, 37.3f);
    body.AddLine(50.5f, 37.3f, 50.5f, 26.7f);
    body.AddBezier(50.5f, 26.7f, 50.5f, 14.6f, 43.3f, 6.5f, 32.0f, 6.5f);
    body.CloseFigure();
    seam.AddLine(32.0f, 8.0f, 32.0f, 26.0f);
    wheel.AddLine(32.0f, 14.0f, 32.0f, 20.5f);
}

static void AddCupPath(GraphicsPath& p, float cx)
{
    const float w  = 11.0f + CUP_INNER;
    const float x0 = cx - w * 0.5f;
    const float x1 = x0 + w;
    const float yt = 31.5f, yb = 51.3f;
    const float r  = w * 0.5f;
    const float k  = 0.5523f * r;
    const float mx = x0 + r;

    p.AddBezier(Hx(mx), yt, Hx(mx + k), yt, Hx(x1), yt + r - k, Hx(x1), yt + r);
    p.AddLine(Hx(x1), yt + r, Hx(x1), yb - r);
    p.AddBezier(Hx(x1), yb - r, Hx(x1), yb - r + k, Hx(mx + k), yb, Hx(mx), yb);
    p.AddBezier(Hx(mx), yb, Hx(mx - k), yb, Hx(x0), yb - r + k, Hx(x0), yb - r);
    p.AddLine(Hx(x0), yb - r, Hx(x0), yt + r);
    p.AddBezier(Hx(x0), yt + r, Hx(x0), yt + r - k, Hx(mx - k), yt, Hx(mx), yt);
    p.CloseFigure();
}

static void AddHeadsetPaths(GraphicsPath& band, GraphicsPath& cupL, GraphicsPath& cupR)
{
    band.AddLine(Hx(8.5f), Vy(34.0f), Hx(8.5f), Vy(30.5f));
    band.AddBezier(Hx(8.5f), Vy(30.5f), Hx(8.5f), Vy(16.8f), Hx(18.8f), Vy(6.5f), 32.0f, Vy(6.5f));
    band.AddBezier(32.0f, Vy(6.5f), Hx(45.2f), Vy(6.5f), Hx(55.5f), Vy(16.8f), Hx(55.5f), Vy(30.5f));
    band.AddLine(Hx(55.5f), Vy(30.5f), Hx(55.5f), Vy(34.0f));
    AddCupPath(cupL, 8.5f);
    AddCupPath(cupR, 55.5f);
}

static void UnionPathBounds(RectF& acc, bool& has, GraphicsPath& p, float pen_w)
{
    if (p.GetPointCount() < 2) return;
    RectF b;
    if (p.GetBounds(&b) != Ok) return;
    float pad = pen_w * 0.5f;
    b.X -= pad;  b.Y -= pad;
    b.Width += pad * 2.0f;  b.Height += pad * 2.0f;
    if (!has) { acc = b; has = true; }
    else RectF::Union(acc, acc, b);
}

static const float PEN_SEAM  = 6.0f;
static const float PEN_WHEEL = 7.4f;

static float g_extra = -1.0f;   // <0 表示用插件里的默认偏移

// 渲染成 alpha 数组（只取 alpha 通道，等价于插件的 RenderIconBGRA）
static void RenderOne(bool is_mouse, float pen_w, int size, BYTE* alpha)
{
    memset(alpha, 0, size * size);
    Bitmap bmp(size, size, PixelFormat32bppARGB);
    {
        Graphics g(&bmp);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        g.Clear(Color(0, 0, 0, 0));

        GraphicsPath mainPath, sub1, sub2;
        RectF bounds; bool has_bounds = false;

        if (is_mouse)
        {
            AddMousePaths(mainPath, sub1, sub2);
            UnionPathBounds(bounds, has_bounds, mainPath, pen_w);
            UnionPathBounds(bounds, has_bounds, sub1, PEN_SEAM);
            UnionPathBounds(bounds, has_bounds, sub2, PEN_WHEEL);
        }
        else
        {
            AddHeadsetPaths(mainPath, sub1, sub2);
            UnionPathBounds(bounds, has_bounds, mainPath, pen_w);
            UnionPathBounds(bounds, has_bounds, sub1, 0.0f);
            UnionPathBounds(bounds, has_bounds, sub2, 0.0f);
        }

        const float fill_w = is_mouse ? 0.80f : 0.92f;
        const float fill_h = is_mouse ? 0.98f : 0.86f;
        float s  = size * fill_w / bounds.Width;
        float sy = size * fill_h / bounds.Height;
        if (sy < s) s = sy;

        float ox = (size - bounds.Width * s) * 0.5f - bounds.X * s;
        float oy = (size - bounds.Height * s) * 0.5f - bounds.Y * s;
        oy += (g_extra >= 0.0f) ? g_extra : (is_mouse ? 0.5f : 0.16f);

        Matrix m(s, 0, 0, s, ox, oy);
        g.SetTransform(&m);

        Pen pen(Color(255, 255, 255, 255), pen_w);
        pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        pen.SetLineJoin(LineJoinRound);
        g.DrawPath(&pen, &mainPath);

        if (is_mouse)
        {
            Pen p1(Color(255, 255, 255, 255), PEN_SEAM);
            p1.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
            g.DrawPath(&p1, &sub1);
            Pen p2(Color(255, 255, 255, 255), PEN_WHEEL);
            p2.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
            g.DrawPath(&p2, &sub2);
        }
        else
        {
            SolidBrush brush(Color(255, 255, 255, 255));
            g.FillPath(&brush, &sub1);
            g.FillPath(&brush, &sub2);
        }
        printf("  s=%.4f penPx=%.3f\n", s, pen_w * s);
    }

    BitmapData bd;
    Rect rc(0, 0, size, size);
    if (bmp.LockBits(&rc, ImageLockModeRead, PixelFormat32bppARGB, &bd) == Ok)
    {
        for (int y = 0; y < size; ++y)
        {
            const BYTE* src = (const BYTE*)bd.Scan0 + (size_t)y * bd.Stride;
            for (int x = 0; x < size; ++x) alpha[y * size + x] = src[x * 4 + 3];
        }
        bmp.UnlockBits(&bd);
    }
}

// 沿一行从左扫，返回第一段墨迹的 alpha 积分（= 该笔画的等效厚度，单位 px）
static double ThickRow(const BYTE* a, int size, int y)
{
    double sum = 0; bool started = false;
    for (int x = 0; x < size; ++x)
    {
        BYTE v = a[y * size + x];
        if (!started) { if (v > 6) { started = true; sum += v / 255.0; } }
        else { if (v <= 6) break; sum += v / 255.0; }
    }
    return sum;
}

// 沿一列从上往下扫
static double ThickCol(const BYTE* a, int size, int x)
{
    double sum = 0; bool started = false;
    for (int y = 0; y < size; ++y)
    {
        BYTE v = a[y * size + x];
        if (!started) { if (v > 6) { started = true; sum += v / 255.0; } }
        else { if (v <= 6) break; sum += v / 255.0; }
    }
    return sum;
}

static double Ink(const BYTE* a, int size)
{
    double s = 0;
    for (int i = 0; i < size * size; ++i) s += a[i] / 255.0;
    return s;
}

int main(int argc, char** argv)
{
    GdiplusStartupInput gsi; ULONG_PTR tok = 0;
    if (GdiplusStartup(&tok, &gsi, nullptr) != Ok) return 1;

    const int SZ = 14;
    BYTE am[256], ah[256];

    // 模式 2：扫耳机在 pen=6.5 下的垂直偏移 → 位图内墨迹行范围与中心
    if (argc > 1)
    {
        printf("== oy 扫描：耳机 pen=6.5 ==\n");
        float prevc = -1.0f;
        for (float e = 0.0f; e <= 0.601f; e += 0.01f)
        {
            g_extra = e;
            RenderOne(false, 6.5f, SZ, ah);
            int top = -1, bot = -1;
            for (int y = 0; y < SZ; ++y)
            {
                bool any = false;
                for (int x = 0; x < SZ; ++x) if (ah[y * SZ + x] > 120) { any = true; break; }
                if (any) { if (top < 0) top = y; bot = y; }
            }
            float c = (top + bot) * 0.5f;
            if (c != prevc)
                printf("extra %.2f  行 %d..%d  (共 %d 行)  位图内中心 %.1f  屏幕中心 %.1f\n",
                       e, top, bot, bot - top + 1, c, 1417.0f + c);
            prevc = c;
        }
        GdiplusShutdown(tok);
        return 0;
    }

    printf("pen | mouse thick  ink  | headset thick  ink\n");
    for (float p = 5.4f; p <= 7.45f; p += 0.1f)
    {
        RenderOne(true,  p, SZ, am);
        double mt = ThickRow(am, SZ, SZ / 2);
        double mi = Ink(am, SZ);

        RenderOne(false, p, SZ, ah);
        double ht = ThickCol(ah, SZ, SZ / 2);
        double hi = Ink(ah, SZ);

        printf("%.1f | %.2f  %6.2f | %.2f  %6.2f   %s\n", p, mt, mi, ht, hi,
               (mt > 0 && fabs(mt - ht) < 0.06) ? "<== 厚度接近" : "");
    }

    GdiplusShutdown(tok);
    return 0;
}
