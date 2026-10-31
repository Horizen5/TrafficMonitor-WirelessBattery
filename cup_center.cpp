// 耳罩对位方案扫描：外缘固定 vs 以头梁腿为宽度中心
// 编译：g++ -O2 -o cup_center.exe cup_center.cpp -lgdi32 -lgdiplus
#include <windows.h>
#include <gdiplus.h>
#include <cstdio>
#include <cstdlib>

using namespace Gdiplus;

static const float CUP_SPAN    = 0.85f;
static const float BAND_SQUASH = 0.85f;
static const float PEN_BAND    = 6.5f;
static inline float Hx(float x) { return 32.0f + (x - 32.0f) * CUP_SPAN; }
static inline float Vy(float y) { return 34.0f - (34.0f - y) * BAND_SQUASH; }

static void AddCupPath(GraphicsPath& p, float cx, float w, bool centered)
{
    const float x0 = centered ? (cx - w * 0.5f) : (3.0f + (cx - 8.5f));
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

static void AddHeadsetPaths(GraphicsPath& band, GraphicsPath& cupL, GraphicsPath& cupR, float w, bool centered)
{
    band.AddLine(Hx(8.5f), Vy(34.0f), Hx(8.5f), Vy(30.5f));
    band.AddBezier(Hx(8.5f), Vy(30.5f), Hx(8.5f), Vy(16.8f), Hx(18.8f), Vy(6.5f), 32.0f, Vy(6.5f));
    band.AddBezier(32.0f, Vy(6.5f), Hx(45.2f), Vy(6.5f), Hx(55.5f), Vy(16.8f), Hx(55.5f), Vy(30.5f));
    band.AddLine(Hx(55.5f), Vy(30.5f), Hx(55.5f), Vy(34.0f));
    AddCupPath(cupL, 8.5f, w, centered);
    AddCupPath(cupR, 55.5f, w, centered);
}

static RectF BoundsOf(GraphicsPath& p, float penW)
{
    RectF b;
    p.GetBounds(&b);
    float pad = penW * 0.5f;
    b.X -= pad; b.Y -= pad; b.Width += pad * 2.0f; b.Height += pad * 2.0f;
    return b;
}

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

struct Var { const char* tag; float w; bool centered; };

static const Var VARS[] = {
    { "A", 17.5f, false },   // 现在：外缘固定，宽度中心偏右 2.8
    { "B", 17.5f, true  },   // 居中，宽度不变
    { "C", 15.0f, true  },   // 居中，罩略窄
    { "D", 13.0f, true  },   // 居中，罩更窄
};
static const int NV = 4;

static bool Save(int idx, bool dark, int size)
{
    Bitmap bmp(size, size, PixelFormat32bppARGB);
    {
        Graphics g(&bmp);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        g.Clear(dark ? Color(255, 32, 32, 32) : Color(255, 250, 250, 248));

        GraphicsPath band, cupL, cupR;
        AddHeadsetPaths(band, cupL, cupR, VARS[idx].w, VARS[idx].centered);

        RectF b = BoundsOf(band, PEN_BAND);
        RectF b1 = BoundsOf(cupL, 0.0f);
        RectF b2 = BoundsOf(cupR, 0.0f);
        RectF::Union(b, b, b1);
        RectF::Union(b, b, b2);
        float s = size * 0.92f / b.Width;
        float sy = size * 0.86f / b.Height;
        if (sy < s) s = sy;
        float ox = (size - b.Width * s) * 0.5f - b.X * s;
        float oy = (size - b.Height * s) * 0.5f - b.Y * s + 0.25f;
        Matrix m(s, 0, 0, s, ox, oy);
        g.SetTransform(&m);

        Color fg = dark ? Color(255, 255, 255, 255) : Color(255, 20, 20, 20);
        Pen pen(fg, PEN_BAND);
        pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        pen.SetLineJoin(LineJoinRound);
        g.DrawPath(&pen, &band);
        SolidBrush brush(fg);
        g.FillPath(&brush, &cupL);
        g.FillPath(&brush, &cupR);

        fprintf(stderr, "%d s=%.4f inkW=%.1f inkH=%.1f\n", idx, s, b.Width * s, b.Height * s);
        fflush(stderr);
    }
    CLSID png;
    if (GetEncoderClsid(L"image/png", &png) != 0) return false;
    WCHAR path[256];
    wsprintfW(path, L"cc%d_%s_%d.png", idx, dark ? L"d" : L"l", size);
    return bmp.Save(path, &png, nullptr) == Ok;
}

int main()
{
    GdiplusStartupInput gsi;
    ULONG_PTR token = 0;
    if (GdiplusStartup(&token, &gsi, nullptr) != Ok) return 1;
    int ok = 0;
    for (int i = 0; i < NV; ++i)
        for (int d = 0; d < 2; ++d)
            if (Save(i, d == 1, 14)) ++ok;
    printf("saved %d\n", ok);
    GdiplusShutdown(token);
    return 0;
}
