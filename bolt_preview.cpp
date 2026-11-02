// bolt_preview.cpp — 对比"充电时图标要不要加闪电"
// 变体：0 现状（只有轮廓） 1 轮廓+内部闪电 2 轮廓+闪电替掉中缝/滚轮
#include <windows.h>
#include <gdiplus.h>
#include <cstdio>
using namespace Gdiplus;

static const int ICONSZ = 14;
static const int SS = 4;           // 与插件一致的 4x 超采样

static void Capsule(GraphicsPath& p)
{
    p.AddBezier(32.0f, 6.5f, 20.7f, 6.5f, 13.5f, 14.6f, 13.5f, 26.7f);
    p.AddLine(13.5f, 26.7f, 13.5f, 37.3f);
    p.AddBezier(13.5f, 37.3f, 13.5f, 49.4f, 20.7f, 57.5f, 32.0f, 57.5f);
    p.AddBezier(32.0f, 57.5f, 43.3f, 57.5f, 50.5f, 49.4f, 50.5f, 37.3f);
    p.AddLine(50.5f, 37.3f, 50.5f, 26.7f);
    p.AddBezier(50.5f, 26.7f, 50.5f, 14.6f, 43.3f, 6.5f, 32.0f, 6.5f);
    p.CloseFigure();
}

// 闪电：设计坐标，落在鼠标体内（x 22~42, y 16~46）
static void Bolt(GraphicsPath& p, float cx, float cy, float s)
{
    // 归一化点集（-1..1），再按 s 缩放、平移到 (cx, cy)
    const float pts[12][2] = {
        { 0.15f, -1.00f }, { -0.85f, 0.05f }, { -0.15f, 0.05f },
        { -0.35f, 1.00f }, { 0.85f, -0.10f }, { 0.15f, -0.10f }
    };
    PointF q[6];
    for (int i = 0; i < 6; ++i)
        q[i] = PointF(cx + pts[i][0] * s, cy + pts[i][1] * s);
    p.AddPolygon(q, 6);
    p.CloseFigure();
}

static void RenderVariant(int variant, BYTE* alpha)
{
    memset(alpha, 0, ICONSZ * ICONSZ);
    const int BIG = ICONSZ * SS;
    Bitmap bmp(BIG, BIG, PixelFormat32bppARGB);
    {
        Graphics g(&bmp);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        g.Clear(Color(0, 0, 0, 0));

        GraphicsPath body, seam, wheel, bolt;
        Capsule(body);
        if (variant == 0) {
            seam.AddLine(PointF(32, 8), PointF(32, 26));
            wheel.AddLine(PointF(32, 14), PointF(32, 20.5f));
        } else if (variant == 1) {
            seam.AddLine(PointF(32, 8), PointF(32, 15));
            Bolt(bolt, 32.0f, 32.0f, 9.0f);
        } else {
            Bolt(bolt, 32.0f, 30.0f, 10.0f);
        }

        // 包围盒（与插件同算法）
        RectF b, t; bool has = false;
        auto uni = [&](GraphicsPath& p, float pen) {
            if (p.GetPointCount() < 2) return;
            if (p.GetBounds(&t) != Ok) return;
            float pad = pen * 0.5f;
            t.X -= pad; t.Y -= pad; t.Width += pad * 2; t.Height += pad * 2;
            if (!has) { b = t; has = true; } else RectF::Union(b, b, t);
        };
        uni(body, 5.3f); uni(seam, 6.0f); uni(wheel, 7.4f); uni(bolt, 0.0f);

        const float fill_w = 0.80f, fill_h = 0.98f;
        float s = ICONSZ * fill_w / b.Width;
        float sy = ICONSZ * fill_h / b.Height;
        if (sy < s) s = sy;
        float ox = (ICONSZ - b.Width * s) * 0.5f - b.X * s;
        float oy = (ICONSZ - b.Height * s) * 0.5f - b.Y * s + 0.5f;

        Matrix m(s * SS, 0, 0, s * SS, ox * SS, oy * SS);
        g.SetTransform(&m);

        Pen pen(Color(255, 255, 255, 255), 5.3f);
        pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        pen.SetLineJoin(LineJoinRound);
        g.DrawPath(&pen, &body);

        if (seam.GetPointCount() >= 2) {
            Pen p1(Color(255, 255, 255, 255), 6.0f);
            p1.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
            g.DrawPath(&p1, &seam);
        }
        if (wheel.GetPointCount() >= 2) {
            Pen p2(Color(255, 255, 255, 255), 7.4f);
            p2.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
            g.DrawPath(&p2, &wheel);
        }
        if (variant > 0) {
            SolidBrush br(Color(255, 255, 255, 255));
            g.FillPath(&br, &bolt);
        }
    }

    BitmapData bd;
    Rect rc(0, 0, BIG, BIG);
    if (bmp.LockBits(&rc, ImageLockModeRead, PixelFormat32bppARGB, &bd) == Ok) {
        for (int y = 0; y < ICONSZ; ++y)
            for (int x = 0; x < ICONSZ; ++x) {
                int acc = 0;
                for (int dy = 0; dy < SS; ++dy) {
                    const BYTE* src = (const BYTE*)bd.Scan0 + (size_t)(y * SS + dy) * bd.Stride;
                    for (int dx = 0; dx < SS; ++dx) acc += src[(x * SS + dx) * 4 + 3];
                }
                alpha[y * ICONSZ + x] = (BYTE)(acc / (SS * SS));
            }
        bmp.UnlockBits(&bd);
    }
}

static int GetEncClsid(const WCHAR* mime, CLSID* pClsid)
{
    UINT num = 0, sz = 0;
    GetImageEncodersSize(&num, &sz);
    if (!sz) return -1;
    ImageCodecInfo* info = (ImageCodecInfo*)malloc(sz);
    GetImageEncoders(num, sz, info);
    int ret = -1;
    for (UINT i = 0; i < num; ++i)
        if (wcscmp(info[i].MimeType, mime) == 0) { *pClsid = info[i].Clsid; ret = 0; break; }
    free(info);
    return ret;
}

static void SaveAlpha(const WCHAR* path, const BYTE* a)
{
    Bitmap bmp(ICONSZ, ICONSZ, PixelFormat32bppARGB);
    BitmapData bd; Rect rc(0, 0, ICONSZ, ICONSZ);
    if (bmp.LockBits(&rc, ImageLockModeWrite, PixelFormat32bppARGB, &bd) == Ok) {
        for (int y = 0; y < ICONSZ; ++y) {
            BYTE* d = (BYTE*)bd.Scan0 + (size_t)y * bd.Stride;
            for (int x = 0; x < ICONSZ; ++x) {
                int al = a[y * ICONSZ + x];
                int v = (255 * al + 32 * (255 - al)) / 255;
                d[x * 4 + 0] = (BYTE)v; d[x * 4 + 1] = (BYTE)v; d[x * 4 + 2] = (BYTE)v; d[x * 4 + 3] = 255;
            }
        }
        bmp.UnlockBits(&bd);
    }
    CLSID png;
    if (GetEncClsid(L"image/png", &png) == 0) bmp.Save(path, &png, nullptr);
}

int main()
{
    GdiplusStartupInput in; ULONG_PTR tok = 0;
    GdiplusStartup(&tok, &in, nullptr);
    BYTE buf[ICONSZ * ICONSZ];
    for (int v = 0; v < 3; ++v) {
        RenderVariant(v, buf);
        WCHAR name[64];
        wsprintfW(name, L"bolt_%d.png", v);
        SaveAlpha(name, buf);
        printf("bolt_%d.png saved\n", v);
    }
    GdiplusShutdown(tok);
    return 0;
}
