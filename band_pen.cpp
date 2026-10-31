// 耳机头梁线宽扫描：其他一切不变，只改 band 的笔宽
// 编译：g++ -O2 -o band_pen.exe band_pen.cpp -lgdi32 -lgdiplus

#include <windows.h>
#include <gdiplus.h>
#include <cstdio>
#include <cstdlib>

using namespace Gdiplus;

static const float CUP_SPAN     = 0.85f;
static const float BAND_SQUASH  = 0.85f;
static const float CUP_INNER    = 6.5f;
static inline float Hx(float x) { return 32.0f + (x - 32.0f) * CUP_SPAN; }
static inline float Vy(float y) { return 34.0f - (34.0f - y) * BAND_SQUASH; }

static void AddCupPath(GraphicsPath& p, float cx)
{
    // 以头梁腿 cx 为轴两侧对称加宽（与插件 AddCupPath 一致）
    const float w  = 11.0f + CUP_INNER;
    const float x0 = cx - w * 0.5f;
    const float x1 = x0 + w;
    const float yt = 31.5f, yb = 51.3f;
    const float r  = w * 0.5f;
    const float k  = 0.5523f * r;
    const float mx = cx;

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

static RectF BoundsOf(GraphicsPath& p, float penW)
{
    RectF b;
    p.GetBounds(&b);
    float pad = penW * 0.5f;
    b.X -= pad; b.Y -= pad; b.Width += pad * 2.0f; b.Height += pad * 2.0f;
    return b;
}

// 与插件完全相同的拟合逻辑
static float FitTransform(Graphics& g, int size, const RectF& b, float fillW, float fillH)
{
    float s = size * fillW / b.Width;
    float sy = size * fillH / b.Height;
    if (sy < s) s = sy;
    float ox = (size - b.Width * s) * 0.5f - b.X * s;
    float oy = (size - b.Height * s) * 0.5f - b.Y * s;
    Matrix m(s, 0, 0, s, ox, oy);
    g.SetTransform(&m);
    return s;
}

static const float BAND_W[] = { 5.85f, 6.0f, 6.3f, 6.5f, 7.0f };
static const int   NW = 5;

static int GetEncoderClsid(const WCHAR* mime, CLSID* pClsid)
{
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    ImageCodecInfo* info = (ImageCodecInfo*)malloc(size);
    if (!info) return -1;
    GetImageEncoders(num, size, info);
    int ret = -1;
    for (UINT i = 0; i < num; ++i)
        if (wcscmp(info[i].MimeType, mime) == 0) { *pClsid = info[i].Clsid; ret = 0; break; }
    free(info);
    return ret;
}

static bool Save(int idx, bool dark, int size)
{
    Bitmap bmp(size, size, PixelFormat32bppARGB);
    {
        Graphics g(&bmp);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        g.Clear(dark ? Color(255, 32, 32, 32) : Color(255, 250, 250, 248));

        GraphicsPath band, cupL, cupR;
        AddHeadsetPaths(band, cupL, cupR);

        float bw = BAND_W[idx];
        RectF b = BoundsOf(band, bw);
        RectF b1 = BoundsOf(cupL, 0.0f);
        RectF b2 = BoundsOf(cupR, 0.0f);
        RectF::Union(b, b, b1);
        RectF::Union(b, b, b2);
        float sc = FitTransform(g, size, b, 0.92f, 0.86f);

        Color fg = dark ? Color(255, 255, 255, 255) : Color(255, 20, 20, 20);
        Pen pen(fg, bw);
        pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        pen.SetLineJoin(LineJoinRound);
        g.DrawPath(&pen, &band);

        SolidBrush brush(fg);
        g.FillPath(&brush, &cupL);
        g.FillPath(&brush, &cupR);

        printf("idx=%d band=%.1f  s=%.4f  bandPx=%.2f\n", idx, bw, sc, bw * sc);
    }

    CLSID png;
    if (GetEncoderClsid(L"image/png", &png) != 0) return false;
    WCHAR path[256];
    wsprintfW(path, L"bp%d_%s_%d.png", idx, dark ? L"d" : L"l", size);
    return bmp.Save(path, &png, nullptr) == Ok;
}

int main()
{
    GdiplusStartupInput gsi;
    ULONG_PTR token = 0;
    if (GdiplusStartup(&token, &gsi, nullptr) != Ok) return 1;

    int ok = 0;
    for (int i = 0; i < NW; ++i)
        for (int d = 0; d < 2; ++d)
            if (Save(i, d == 1, 14)) ++ok;

    printf("saved %d\n", ok);
    GdiplusShutdown(token);
    return 0;
}
