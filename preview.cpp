// 图标变体渲染：与插件完全相同的 GDI+ 绘制逻辑，只输出 14/16px 的原始位图。
// 拼图和标注交给 Python（render_sheet.py），避免在 GDI+ 里做缩放/文字。
// 编译：g++ -O2 -o preview.exe preview.cpp -lgdi32 -lgdiplus

#include <windows.h>
#include <gdiplus.h>
#include <cstdio>
#include <cstdlib>

using namespace Gdiplus;

static const float PEN_MAIN  = 5.5f;
static const float PEN_SEAM  = 6.0f;
static const float PEN_WHEEL = 7.0f;
static const float PEN_BAND  = 5.0f;

struct MouseSpec
{
    const char* tag;
    float rTop;                     // <=0 用 SVG 原始胶囊轮廓；>0 用上窄下宽的蛋形
    float seamA, seamB;
    float wheelA, wheelB, wheelW;   // wheelW <= 0 表示不画滚轮
};

static void BuildMouse(GraphicsPath& body, GraphicsPath& seam, GraphicsPath& wheel, const MouseSpec& s)
{
    body.Reset();

    if (s.rTop <= 0.0f)
    {
        // SVG 原始：(17..47) x (10..54)，上下都是半径 15 的半圆 → 胶囊
        body.AddBezier(32.0f, 10.0f, 23.716f, 10.0f, 17.0f, 16.716f, 17.0f, 25.0f);
        body.AddLine(17.0f, 25.0f, 17.0f, 39.0f);
        body.AddBezier(17.0f, 39.0f, 17.0f, 47.284f, 23.716f, 54.0f, 32.0f, 54.0f);
        body.AddBezier(32.0f, 54.0f, 40.284f, 54.0f, 47.0f, 47.284f, 47.0f, 39.0f);
        body.AddLine(47.0f, 39.0f, 47.0f, 25.0f);
        body.AddBezier(47.0f, 25.0f, 47.0f, 16.716f, 40.284f, 10.0f, 32.0f, 10.0f);
        body.CloseFigure();
    }
    else
    {
        // 外框仍是 (17..47) x (10..54)，只把顶部收窄成半径 rTop 的圆角，
        // 腰部平滑过渡到底部半圆 r=15 —— 上窄下宽，14px 下才读得出"鼠标"。
        const float rt = s.rTop;
        const float k = 0.5523f * rt;
        const float bx = 32.0f - rt;      // 顶部左边 x
        const float rx = 32.0f + rt;      // 顶部右边 x
        const float by = 10.0f + rt;      // 顶部圆角中心 y

        body.StartFigure();
        body.AddBezier(32.0f, 10.0f, 32.0f - k, 10.0f, bx, 10.0f + rt - k, bx, by);
        body.AddBezier(bx, by, bx, by + 7.0f, 17.0f, 31.0f, 17.0f, 39.0f);
        body.AddBezier(17.0f, 39.0f, 17.0f, 47.284f, 23.716f, 54.0f, 32.0f, 54.0f);
        body.AddBezier(32.0f, 54.0f, 40.284f, 54.0f, 47.0f, 47.284f, 47.0f, 39.0f);
        body.AddBezier(47.0f, 39.0f, 47.0f, 31.0f, rx, by + 7.0f, rx, by);
        body.AddBezier(rx, by, rx, 10.0f + rt - k, 32.0f + k, 10.0f, 32.0f, 10.0f);
        body.CloseFigure();
    }

    seam.Reset();
    seam.AddLine(32.0f, s.seamA, 32.0f, s.seamB);

    wheel.Reset();
    if (s.wheelW > 0.0f)
        wheel.AddLine(32.0f, s.wheelA, 32.0f, s.wheelB);
}

static void BuildCup(GraphicsPath& p, float dx)
{
    p.Reset();
    p.AddBezier(22.0f + dx, 34.0f, 19.239f + dx, 34.0f, 17.0f + dx, 36.239f, 17.0f + dx, 39.0f);
    p.AddLine(17.0f + dx, 39.0f, 17.0f + dx, 49.0f);
    p.AddBezier(17.0f + dx, 49.0f, 17.0f + dx, 51.761f, 19.239f + dx, 54.0f, 22.0f + dx, 54.0f);
    p.AddBezier(22.0f + dx, 54.0f, 24.761f + dx, 54.0f, 27.0f + dx, 51.761f, 27.0f + dx, 49.0f);
    p.AddLine(27.0f + dx, 49.0f, 27.0f + dx, 39.0f);
    p.AddBezier(27.0f + dx, 39.0f, 27.0f + dx, 36.239f, 24.761f + dx, 34.0f, 22.0f + dx, 34.0f);
    p.CloseFigure();
}

static void AddBounds(RectF& acc, bool& has, GraphicsPath& p, float penW)
{
    if (p.GetPointCount() < 2) return;
    RectF b;
    if (p.GetBounds(&b) != Ok) return;
    float pad = penW * 0.5f;
    b.X -= pad; b.Y -= pad; b.Width += pad * 2.0f; b.Height += pad * 2.0f;
    if (!has) { acc = b; has = true; }
    else RectF::Union(acc, acc, b);
}

static void DrawIcon(Graphics& g, bool isMouse, int size, Color color,
                     const MouseSpec& spec, float fillW, float fillH)
{
    GraphicsPath mainPath, sub1, sub2;

    if (isMouse)
    {
        BuildMouse(mainPath, sub1, sub2, spec);
    }
    else
    {
        BuildCup(sub1, 0.0f);
        BuildCup(sub2, 20.0f);
        mainPath.AddArc(13.0f, 15.0f, 38.0f, 38.0f, 180.0f, 180.0f);
    }

    RectF bounds;
    bool has = false;
    if (isMouse)
    {
        AddBounds(bounds, has, mainPath, PEN_MAIN);
        AddBounds(bounds, has, sub1, PEN_SEAM);
        if (spec.wheelW > 0.0f) AddBounds(bounds, has, sub2, spec.wheelW);
    }
    else
    {
        AddBounds(bounds, has, mainPath, PEN_BAND);
        AddBounds(bounds, has, sub1, 0.0f);
        AddBounds(bounds, has, sub2, 0.0f);
    }
    if (!has || bounds.Width < 0.1f || bounds.Height < 0.1f) return;

    float s = size * fillW / bounds.Width;
    float sy = size * fillH / bounds.Height;
    if (sy < s) s = sy;

    float ox = (size - bounds.Width * s) * 0.5f - bounds.X * s;
    float oy = (size - bounds.Height * s) * 0.5f - bounds.Y * s;
    Matrix m(s, 0, 0, s, ox, oy);
    g.SetTransform(&m);

    if (isMouse)
    {
        Pen pen(color, PEN_MAIN);
        pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        pen.SetLineJoin(LineJoinRound);
        g.DrawPath(&pen, &mainPath);

        Pen p1(color, PEN_SEAM);
        p1.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        g.DrawPath(&p1, &sub1);

        if (spec.wheelW > 0.0f)
        {
            Pen p2(color, spec.wheelW);
            p2.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
            g.DrawPath(&p2, &sub2);
        }
    }
    else
    {
        SolidBrush brush(color);
        Pen bp(color, PEN_BAND);
        bp.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        g.DrawPath(&bp, &mainPath);
        g.FillPath(&brush, &sub1);
        g.FillPath(&brush, &sub2);
    }
    g.ResetTransform();
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

static bool SaveSmall(const char* path, bool isMouse, int size, bool dark,
                      const MouseSpec& spec, float fillW, float fillH)
{
    Bitmap bmp(size, size, PixelFormat32bppARGB);
    {
        Graphics g(&bmp);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        g.SetCompositingQuality(CompositingQualityHighQuality);
        g.Clear(dark ? Color(255, 32, 32, 32) : Color(255, 255, 255, 255));
        DrawIcon(g, isMouse, size, dark ? Color(255, 255, 255, 255) : Color(255, 0, 0, 0),
                 spec, fillW, fillH);
    }

    CLSID png;
    if (GetEncoderClsid(L"image/png", &png) != 0) return false;

    WCHAR wpath[512];
    MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, 512);
    return bmp.Save(wpath, &png, nullptr) == Ok;
}

int main()
{
    GdiplusStartupInput gsi;
    ULONG_PTR token = 0;
    if (GdiplusStartup(&token, &gsi, nullptr) != Ok) return 1;

    MouseSpec specs[] = {
        { "A",  0.0f, 12.0f, 30.0f, 22.0f, 28.0f, 7.0f },
        { "B",  0.0f, 12.0f, 22.0f,  0.0f,  0.0f, 0.0f },
        { "F", 11.0f, 12.0f, 24.0f,  0.0f,  0.0f, 0.0f },
        { "G", 11.0f, 12.0f, 21.0f, 26.0f, 33.0f, 6.5f },
        { "H", 13.0f, 12.0f, 24.0f,  0.0f,  0.0f, 0.0f },
        { "I", 11.0f, 12.0f, 20.0f,  0.0f,  0.0f, 0.0f },
    };

    char buf[256];
    int ok = 0;

    for (int i = 0; i < 6; ++i)
    {
        for (int sz = 14; sz <= 16; sz += 2)
        {
            sprintf(buf, "out_m%s_%d_dark.png", specs[i].tag, sz);
            if (SaveSmall(buf, true, sz, true, specs[i], 0.75f, 0.93f)) ++ok;
            sprintf(buf, "out_m%s_%d_light.png", specs[i].tag, sz);
            if (SaveSmall(buf, true, sz, false, specs[i], 0.75f, 0.93f)) ++ok;
        }
    }

    MouseSpec dummy = { "H", 12.0f, 22.0f, 0.0f, 0.0f, 0.0f };
    for (int sz = 14; sz <= 16; sz += 2)
    {
        sprintf(buf, "out_h_%d_dark.png", sz);
        if (SaveSmall(buf, false, sz, true, dummy, 0.92f, 0.85f)) ++ok;
        sprintf(buf, "out_h_%d_light.png", sz);
        if (SaveSmall(buf, false, sz, false, dummy, 0.92f, 0.85f)) ++ok;
    }

    printf("saved %d files\n", ok);
    GdiplusShutdown(token);
    return 0;
}
