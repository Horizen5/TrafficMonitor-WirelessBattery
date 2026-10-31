// 扫描耳机图标的垂直偏移：找出让位图内墨迹行数为奇数（中心落整数）的 oy
#include <windows.h>
#include <gdiplus.h>
#include <cstdio>

using namespace Gdiplus;

static const float CUP_SPAN = 0.85f, BAND_SQUASH = 0.85f, PEN_BAND = 5.85f, CUP_INNER = 6.5f;
static inline float Hx(float x) { return 32.0f + (x - 32.0f) * CUP_SPAN; }
static inline float Vy(float y) { return 34.0f - (34.0f - y) * BAND_SQUASH; }

static void AddCupPath(GraphicsPath& p, float cx)
{
    const float w = 11.0f + CUP_INNER, x0 = cx - w * 0.5f, x1 = x0 + w;
    const float yt = 31.5f, yb = 51.3f, r = w * 0.5f, k = 0.5523f * r, mx = x0 + r;
    p.AddBezier(Hx(mx), yt, Hx(mx + k), yt, Hx(x1), yt + r - k, Hx(x1), yt + r);
    p.AddLine(Hx(x1), yt + r, Hx(x1), yb - r);
    p.AddBezier(Hx(x1), yb - r, Hx(x1), yb - r + k, Hx(mx + k), yb, Hx(mx), yb);
    p.AddBezier(Hx(mx), yb, Hx(mx - k), yb, Hx(x0), yb - r + k, Hx(x0), yb - r);
    p.AddLine(Hx(x0), yb - r, Hx(x0), yt + r);
    p.AddBezier(Hx(x0), yt + r, Hx(x0), yt + r - k, Hx(mx - k), yt, Hx(mx), yt);
    p.CloseFigure();
}

int main()
{
    GdiplusStartupInput gsi; ULONG_PTR tok = 0;
    if (GdiplusStartup(&tok, &gsi, nullptr) != Ok) return 1;

    const int size = 14;
    for (int step = 0; step <= 40; ++step)
    {
        float extra = step * 0.025f;
        Bitmap bmp(size, size, PixelFormat32bppARGB);
        {
            Graphics g(&bmp);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
            g.Clear(Color(0, 0, 0, 0));

            GraphicsPath band, cupL, cupR;
            band.AddLine(Hx(8.5f), Vy(34.0f), Hx(8.5f), Vy(30.5f));
            band.AddBezier(Hx(8.5f), Vy(30.5f), Hx(8.5f), Vy(16.8f), Hx(18.8f), Vy(6.5f), 32.0f, Vy(6.5f));
            band.AddBezier(32.0f, Vy(6.5f), Hx(45.2f), Vy(6.5f), Hx(55.5f), Vy(16.8f), Hx(55.5f), Vy(30.5f));
            band.AddLine(Hx(55.5f), Vy(30.5f), Hx(55.5f), Vy(34.0f));
            AddCupPath(cupL, 8.5f); AddCupPath(cupR, 55.5f);

            RectF b;
            band.GetBounds(&b);
            b.X -= PEN_BAND * 0.5f; b.Y -= PEN_BAND * 0.5f;
            b.Width += PEN_BAND; b.Height += PEN_BAND;
            RectF c1, c2; cupL.GetBounds(&c1); cupR.GetBounds(&c2);
            RectF::Union(b, b, c1); RectF::Union(b, b, c2);

            float s = size * 0.92f / b.Width;
            float sy = size * 0.86f / b.Height;
            if (sy < s) s = sy;
            float ox = (size - b.Width * s) * 0.5f - b.X * s;
            float oy = (size - b.Height * s) * 0.5f - b.Y * s + extra;
            Matrix m(s, 0, 0, s, ox, oy);
            g.SetTransform(&m);
            Pen pen(Color(255, 255, 255, 255), PEN_BAND);
            pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
            pen.SetLineJoin(LineJoinRound);
            g.DrawPath(&pen, &band);
            SolidBrush br(Color(255, 255, 255, 255));
            g.FillPath(&br, &cupL); g.FillPath(&br, &cupR);
        }
        BitmapData bd; Rect rc(0, 0, size, size);
        int top = -1, bot = -1;
        if (bmp.LockBits(&rc, ImageLockModeRead, PixelFormat32bppARGB, &bd) == Ok)
        {
            for (int y = 0; y < size; ++y)
            {
                const BYTE* row = (const BYTE*)bd.Scan0 + (size_t)y * bd.Stride;
                bool hit = false;
                for (int x = 0; x < size; ++x) if (row[x * 4 + 3] > 120) { hit = true; break; }
                if (hit) { if (top < 0) top = y; bot = y; }
            }
            bmp.UnlockBits(&bd);
        }
        printf("extra=%.3f  rows=%d..%d  n=%d  centerInBmp=%.1f\n",
               extra, top, bot, bot - top + 1, (top + bot) * 0.5f);
    }
    GdiplusShutdown(tok);
    return 0;
}
