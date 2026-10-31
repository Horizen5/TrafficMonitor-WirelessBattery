// 6 套图标方案预览（纯代码绘制，不替换任务栏）
// 核心：设计里的线宽要让它在 14px 下正好落在整数像素上
//   鼠标设计框宽 37+pen，高 51+pen；缩放 s ≈ 14*0.98/(51+pen)
//   pen 4.0 -> 线宽 1.00px   pen 6.0 -> 1.44px   pen 8.0 -> 1.86px
// 编译：g++ -O2 -o design.exe design.cpp -lgdi32 -lgdiplus

#include <windows.h>
#include <gdiplus.h>
#include <cstdio>
#include <cstdlib>

using namespace Gdiplus;

// ---------------------------------------------------------------- 形状

static void MakeMouse(GraphicsPath& body, GraphicsPath& seam, GraphicsPath& wheel)
{
    // 用户给的 64x64 设计：外框 (13.5..50.5) x (6.5..57.5)
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

static const float CUP_SPAN = 0.85f;
static inline float Hx(float x) { return 32.0f + (x - 32.0f) * CUP_SPAN; }

static const float BAND_SQUASH = 0.85f;
static inline float Vy(float y) { return 34.0f - (34.0f - y) * BAND_SQUASH; }

static float g_cupInner = 0.0f;   // 耳罩内缘加厚量（设计单位）

static void MakeCup(GraphicsPath& p, float cx)
{
    const float d  = cx - 8.5f;
    const float x0 = 3.0f + d;
    const float w  = 11.0f + g_cupInner;
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

static void MakeHeadset(GraphicsPath& band, GraphicsPath& cupL, GraphicsPath& cupR)
{
    band.AddLine(Hx(8.5f), Vy(34.0f), Hx(8.5f), Vy(30.5f));
    band.AddBezier(Hx(8.5f), Vy(30.5f), Hx(8.5f), Vy(16.8f), Hx(18.8f), Vy(6.5f), 32.0f, Vy(6.5f));
    band.AddBezier(32.0f, Vy(6.5f), Hx(45.2f), Vy(6.5f), Hx(55.5f), Vy(16.8f), Hx(55.5f), Vy(30.5f));
    band.AddLine(Hx(55.5f), Vy(30.5f), Hx(55.5f), Vy(34.0f));
    MakeCup(cupL, 8.5f);
    MakeCup(cupR, 55.5f);
}

static void Fit(Graphics& g, int size, const RectF& b, float fillW, float fillH)
{
    float s = size * fillW / b.Width;
    float sy = size * fillH / b.Height;
    if (sy < s) s = sy;
    float ox = (size - b.Width * s) * 0.5f - b.X * s;
    float oy = (size - b.Height * s) * 0.5f - b.Y * s;
    Matrix m(s, 0, 0, s, ox, oy);
    g.SetTransform(&m);
}

static RectF BoundsOf(GraphicsPath& p, float penW)
{
    RectF b;
    p.GetBounds(&b);
    float pad = penW * 0.5f;
    b.X -= pad; b.Y -= pad; b.Width += pad * 2.0f; b.Height += pad * 2.0f;
    return b;
}

// ---------------------------------------------------------------- 样式表

struct StyleDef
{
    const char*        tag;
    const wchar_t*     name;
    const wchar_t*     desc;
    bool               solid;      // 实心剪影
    float              pen;        // 轮廓线宽（设计单位）
    float              penWheel;   // 滚轮线宽
    bool               withWheel;
    float              seamEnd;    // 中缝终点
    float              fillWm, fillHm;
    float              fillWh, fillHh;
    float              cutSeamW, cutWheelW;   // 实心模式的挖缝宽度
};

static const StyleDef STYLES[] = {
    { "0", L"+2", L"内缘加厚 2", false, 6.0f, 7.4f, true, 26.0f, 0.80f, 0.98f, 0.92f, 0.86f, 0,0 },
    { "1", L"+4（当前）", L"内缘加厚 4 ≈ 1px", false, 6.0f, 7.4f, true, 26.0f, 0.80f, 0.98f, 0.92f, 0.86f, 0,0 },
    { "2", L"+6", L"内缘加厚 6", false, 6.0f, 7.4f, true, 26.0f, 0.80f, 0.98f, 0.92f, 0.86f, 0,0 },
};
static const float CUP_INNERS[3] = { 2.0f, 4.0f, 6.0f };

static const int NS = 3;

// ---------------------------------------------------------------- 绘制

static void DrawMouse(Graphics& g, const StyleDef& st, int size, Color c)
{
    GraphicsPath body, seam, wheel;
    MakeMouse(body, seam, wheel);

    if (st.solid)
    {
        RectF b = BoundsOf(body, 0.0f);
        Fit(g, size, b, st.fillWm, st.fillHm);

        SolidBrush brush(c);
        g.FillPath(&brush, &body);

        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetCompositingMode(CompositingModeSourceCopy);

        Pen cut(Color(0, 0, 0, 0), st.cutSeamW);
        cut.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        g.DrawPath(&cut, &seam);

        if (st.withWheel && st.cutWheelW > 0.0f)
        {
            Pen cutW(Color(0, 0, 0, 0), st.cutWheelW);
            cutW.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
            g.DrawPath(&cutW, &wheel);
        }
        g.SetCompositingMode(CompositingModeSourceOver);
        return;
    }

    RectF b = BoundsOf(body, st.pen);
    RectF b2 = BoundsOf(seam, st.pen);
    RectF::Union(b, b, b2);
    if (st.withWheel)
    {
        RectF b3 = BoundsOf(wheel, st.penWheel);
        RectF::Union(b, b, b3);
    }
    Fit(g, size, b, st.fillWm, st.fillHm);

    Pen pen(c, st.pen);
    pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
    pen.SetLineJoin(LineJoinRound);
    g.DrawPath(&pen, &body);

    Pen ps(c, st.pen);
    ps.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
    g.DrawPath(&ps, &seam);

    if (st.withWheel)
    {
        Pen pw(c, st.penWheel);
        pw.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        g.DrawPath(&pw, &wheel);
    }
}

static void DrawHeadset(Graphics& g, const StyleDef& st, int size, Color c)
{
    GraphicsPath band, cupL, cupR;
    MakeHeadset(band, cupL, cupR);

    float bandW = st.solid ? 9.0f : st.pen;

    RectF b = BoundsOf(band, bandW);
    RectF b1 = BoundsOf(cupL, 0.0f);
    RectF b2 = BoundsOf(cupR, 0.0f);
    RectF::Union(b, b, b1);
    RectF::Union(b, b, b2);
    Fit(g, size, b, st.fillWh, st.fillHh);

    Pen pen(c, bandW);
    pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
    g.DrawPath(&pen, &band);

    SolidBrush brush(c);
    g.FillPath(&brush, &cupL);
    g.FillPath(&brush, &cupR);
}

// ---------------------------------------------------------------- 输出

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

static bool Save(int idx, bool isMouse, bool dark, int size)
{
    Bitmap bmp(size, size, PixelFormat32bppARGB);
    {
        Graphics g(&bmp);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        g.Clear(dark ? Color(255, 32, 32, 32) : Color(255, 255, 255, 255));

        g_cupInner = CUP_INNERS[idx];
        Color fg = dark ? Color(255, 255, 255, 255) : Color(255, 0, 0, 0);
        if (isMouse) DrawMouse(g, STYLES[idx], size, fg);
        else DrawHeadset(g, STYLES[idx], size, fg);
    }

    CLSID png;
    if (GetEncoderClsid(L"image/png", &png) != 0) return false;
    WCHAR path[256];
    wsprintfW(path, L"s%d_%s_%s_%d.png", idx,
              isMouse ? L"m" : L"h", dark ? L"d" : L"l", size);
    return bmp.Save(path, &png, nullptr) == Ok;
}

int main()
{
    GdiplusStartupInput gsi;
    ULONG_PTR token = 0;
    if (GdiplusStartup(&token, &gsi, nullptr) != Ok) return 1;

    int ok = 0;
    for (int s = 0; s < NS; ++s)
        for (int m = 0; m < 2; ++m)
            for (int d = 0; d < 2; ++d)
                if (Save(s, m == 0, d == 0, 14)) ++ok;

    printf("saved %d\n", ok);
    GdiplusShutdown(token);
    return 0;
}
