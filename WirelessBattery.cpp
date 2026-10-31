// WirelessBattery - TrafficMonitor 插件
// 直接在 DLL 内通过 Windows HID API 读取无线鼠标/耳机电量，无需 Python 常驻
//
// 鼠标: AJAZZ 3151:402D  usage page 0xFFFF / usage 0x02
//       先发 0xF7 心跳唤醒 2.4G 链路，再读 feature report 0x05，第 3 字节为电量
// 耳机: MCHOSE 291D:385D  usage page 0xFF90
//       写 64 字节 55 65 01 ...，读回 64 字节，第 2 字节(0基)为电量
//
// 显示方式: 自绘（图标 + 数值）。图标为「细线描边」风格，用 GDI+ GraphicsPath
//           按 64x64 设计坐标矢量绘制，再缩放到实际像素（96 DPI 下 14px），
//           得到 32bpp 带 alpha 的 HICON。线宽与居中偏移都经过像素级校准，
//           详见 README 的「图标渲染」一节。

#include "PluginInterface.h"
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <mutex>
#include <cmath>
#include <cstdio>
#include <objidl.h>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

// ---------------------------------------------------------------- 全局状态

static CRITICAL_SECTION g_lock;
static bool g_lock_ready = false;

static wchar_t g_mouse_text[16] = L"--";
static wchar_t g_headset_text[16] = L"--";
static int g_mouse_val = -1;      // -1 表示读不到
static int g_headset_val = -1;
static wchar_t g_tooltip[256] = L"无线设备电量";

static HANDLE g_thread = nullptr;
static HANDLE g_stop_event = nullptr;
static volatile LONG g_started = 0;

static const DWORD REFRESH_SECONDS = 15;

// 主程序通过 OnExtenedInfo 传来的数值文字颜色（0x00BBGGRR）
static volatile LONG g_value_color = 0;
static volatile LONG g_color_valid = 0;

// ---------------------------------------------------------------- 工具函数

static void SetText(wchar_t* dst, size_t n, const wchar_t* text)
{
    EnterCriticalSection(&g_lock);
    wcsncpy_s(dst, n, text, _TRUNCATE);
    LeaveCriticalSection(&g_lock);
}

// 带超时的重叠 IO，避免设备不应答时把线程卡死
static bool OverlappedIO(HANDLE h, bool isWrite, void* buf, DWORD len, DWORD timeout_ms, DWORD* transferred)
{
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return false;

    BOOL ok = isWrite ? WriteFile(h, buf, len, nullptr, &ov)
                      : ReadFile(h, buf, len, nullptr, &ov);

    bool result = false;
    if (!ok && GetLastError() != ERROR_IO_PENDING)
    {
        CloseHandle(ov.hEvent);
        return false;
    }

    if (WaitForSingleObject(ov.hEvent, timeout_ms) == WAIT_OBJECT_0)
    {
        DWORD got = 0;
        if (GetOverlappedResult(h, &ov, &got, FALSE))
        {
            if (transferred) *transferred = got;
            result = true;
        }
    }
    else
    {
        CancelIoEx(h, &ov);
    }

    CloseHandle(ov.hEvent);
    return result;
}

// 按 VID/PID 和 usage page 找设备并打开，找不到返回 INVALID_HANDLE_VALUE
static HANDLE OpenHidDevice(unsigned short vid, unsigned short pid, unsigned short usage_page, unsigned short usage, bool check_usage)
{
    GUID guid = { 0x4D1E55B2, 0xF16F, 0x11CF, { 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } }; // GUID_DEVINTERFACE_HID

    HDEVINFO dev_info = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (dev_info == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    HANDLE found = INVALID_HANDLE_VALUE;
    SP_DEVICE_INTERFACE_DATA did{};
    did.cbSize = sizeof(did);

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(dev_info, nullptr, &guid, i, &did); ++i)
    {
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(dev_info, &did, nullptr, 0, &needed, nullptr);
        if (needed == 0) continue;

        BYTE* buffer = (BYTE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, needed);
        if (!buffer) continue;

        PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)buffer;
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (SetupDiGetDeviceInterfaceDetailW(dev_info, &did, detail, needed, &needed, nullptr))
        {
            HANDLE h = CreateFileW(detail->DevicePath,
                                   GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING,
                                   FILE_FLAG_OVERLAPPED, nullptr);
            if (h != INVALID_HANDLE_VALUE)
            {
                HIDD_ATTRIBUTES attr{};
                attr.Size = sizeof(attr);
                if (HidD_GetAttributes(h, &attr) &&
                    attr.VendorID == vid && attr.ProductID == pid)
                {
                    bool usage_ok = true;
                    if (check_usage)
                    {
                        usage_ok = false;
                        PHIDP_PREPARSED_DATA ppd = nullptr;
                        if (HidD_GetPreparsedData(h, &ppd))
                        {
                            HIDP_CAPS caps{};
                            if (HidP_GetCaps(ppd, &caps) == HIDP_STATUS_SUCCESS)
                            {
                                if (caps.UsagePage == usage_page)
                                    usage_ok = true;   // 固件偶有 usage 偏差，page 对即接受
                            }
                            HidD_FreePreparsedData(ppd);
                        }
                    }

                    if (usage_ok)
                    {
                        found = h;
                        HeapFree(GetProcessHeap(), 0, buffer);
                        break;
                    }
                }
                CloseHandle(h);
            }
        }
        HeapFree(GetProcessHeap(), 0, buffer);
    }

    SetupDiDestroyDeviceInfoList(dev_info);
    return found;
}

// ---------------------------------------------------------------- 鼠标：AJAZZ

static int ReadMouseBattery()
{
    HANDLE h = OpenHidDevice(0x3151, 0x402D, 0xFFFF, 0x02, true);
    if (h == INVALID_HANDLE_VALUE) return -1;

    int value = -1;

    BYTE heartbeat[65] = { 0 };
    heartbeat[0] = 0x00;   // report ID 0
    heartbeat[1] = 0xF7;   // 厂商驱动的心跳，唤醒 2.4G 遥测链路

    for (int attempt = 0; attempt < 3 && value < 0; ++attempt)
    {
        if (!HidD_SetFeature(h, heartbeat, sizeof(heartbeat))) break;
        WaitForSingleObject(g_stop_event, 45);   // 等一次空中往返

        BYTE report[65] = { 0 };
        report[0] = 0x05;   // feature report 0x05
        if (HidD_GetFeature(h, report, sizeof(report)))
        {
            // report[0] 是回显的 report ID，真实数据从 report[1] 开始
            // 布局: 05 00 00 <电量> ...
            if (report[0] == 0x05 && report[1] == 0x00 && report[2] == 0x00)
            {
                int level = report[3];
                if (level >= 0 && level <= 100)
                    value = level;
            }
        }
        WaitForSingleObject(g_stop_event, 100);
    }

    CloseHandle(h);
    return value;
}

// ---------------------------------------------------------------- 耳机：MCHOSE

static int ReadHeadsetBattery()
{
    HANDLE h = OpenHidDevice(0x291D, 0x385D, 0xFF90, 0x01, true);
    if (h == INVALID_HANDLE_VALUE) return -1;

    int value = -1;
    BYTE request[64] = { 0 };
    request[0] = 0x55;
    request[1] = 0x65;
    request[2] = 0x01;

    for (int attempt = 0; attempt < 2 && value < 0; ++attempt)
    {
        DWORD sent = 0;
        if (!OverlappedIO(h, true, request, sizeof(request), 400, &sent)) break;

        for (int read_try = 0; read_try < 3; ++read_try)
        {
            BYTE reply[64] = { 0 };
            DWORD got = 0;
            if (!OverlappedIO(h, false, reply, sizeof(reply), 400, &got)) break;

            if (got >= 4 && reply[0] == 0x55 && reply[1] == 0x65)
            {
                int level = reply[2];
                if (level >= 0 && level <= 100)
                {
                    value = level;
                    break;
                }
            }
        }
        if (value < 0) WaitForSingleObject(g_stop_event, 200);
    }

    CloseHandle(h);
    return value;
}

// ---------------------------------------------------------------- 图标渲染（GDI+ 矢量）
//
// 图形完全照搬 ChatGPT 提供的 SVG 设计（64x64 设计画布），用 GDI+ 按贝塞尔
// 路径矢量绘制，不依赖位图拉伸，任何图标尺寸都清晰。
//   鼠标：竖向胶囊轮廓 + 顶部中缝 + 中段加粗的滚轮
//   耳机：半圆头梁（线描）+ 两侧实心耳罩
//
// 对齐要点：两个图标各自按"实际墨迹包围盒"等比缩放并居中到同一个正方形框内，
// 因此鼠标/耳机在行内共享同一视觉中线，不会出现一个偏上一个偏下。

namespace gp = Gdiplus;

static ULONG_PTR g_gdiplus_token = 0;
static volatile LONG g_gdiplus_state = 0;   // 0 未初始化 / 1 可用 / 2 失败

static bool EnsureGdiplus()
{
    if (g_gdiplus_state == 1) return true;
    if (g_gdiplus_state == 2) return false;
    gp::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (gp::GdiplusStartup(&token, &input, nullptr) == gp::Ok)
    {
        g_gdiplus_token = token;
        InterlockedExchange(&g_gdiplus_state, 1);
        return true;
    }
    InterlockedExchange(&g_gdiplus_state, 2);
    return false;
}

// ---- 路径（坐标直接取自 SVG，画布 64x64） ----

static void AddMousePaths(gp::GraphicsPath& body, gp::GraphicsPath& seam, gp::GraphicsPath& wheel)
{
    // M32 6.5 c-11.3 0 -18.5 8.1 -18.5 20.2 v10.6
    //   c0 12.1 7.2 20.2 18.5 20.2 s18.5 -8.1 18.5 -20.2 V26.7
    //   C50.5 14.6 43.3 6.5 32 6.5 Z
    body.AddBezier(32.0f, 6.5f, 20.7f, 6.5f, 13.5f, 14.6f, 13.5f, 26.7f);
    body.AddLine(13.5f, 26.7f, 13.5f, 37.3f);
    body.AddBezier(13.5f, 37.3f, 13.5f, 49.4f, 20.7f, 57.5f, 32.0f, 57.5f);
    body.AddBezier(32.0f, 57.5f, 43.3f, 57.5f, 50.5f, 49.4f, 50.5f, 37.3f);
    body.AddLine(50.5f, 37.3f, 50.5f, 26.7f);
    body.AddBezier(50.5f, 26.7f, 50.5f, 14.6f, 43.3f, 6.5f, 32.0f, 6.5f);
    body.CloseFigure();

    seam.AddLine(32.0f, 8.0f, 32.0f, 26.0f);      // 左右键分界
    wheel.AddLine(32.0f, 14.0f, 32.0f, 20.5f);    // 滚轮（笔更粗）
}

// 耳机横向收拢：耳罩与头梁一起以 x=32 为轴往中间收。
// 1.00 = 原始设计；0.85 = 向中线靠拢 15%（用户要求 10%~20%）。
// 收拢后包围盒变窄，Fit() 会自动放大，图标整体略有增高、更饱满。
static const float CUP_SPAN = 0.85f;
static inline float Hx(float x) { return 32.0f + (x - 32.0f) * CUP_SPAN; }

// 头梁纵向压扁：以腿底 y=34 为基点把拱顶往下压，耳的罩位置不动。
// 1.00 = 原始拱高（27.5）；0.85 = 下压 15%（拱顶 6.5 -> 10.6），顶长得更平缓圆润。
static const float BAND_SQUASH = 0.85f;
static inline float Vy(float y) { return 34.0f - (34.0f - y) * BAND_SQUASH; }

// 耳罩内缘向中线加厚：外缘固定在 x=3，只把内缘往里推。
// 6.5 设计单位 ≈ 在 14px 下加宽 0.6px；同时包围盒变宽 → Fit 宽度受限 → 整体再缩 3.4%。
static const float CUP_INNER = 6.5f;

static void AddCupPath(gp::GraphicsPath& p, float cx)
{
    // 左耳罩 cx=8.5，右耳罩 cx=55.5（关于 x=32 镜像）。
    // 罩的"宽度中心"必须落在头梁腿的 x 上，否则只推内缘会让中心外移 2.8 单位，
    // 耳罩就往腿的外侧长出一块 —— 看起来就是外八。这里改成以腿为轴两边对称扩。
    const float w  = 11.0f + CUP_INNER;        // 11 -> 17.5
    const float x0 = cx - w * 0.5f;            // 外缘
    const float x1 = x0 + w;                   // 内缘
    const float yt = 31.5f, yb = 51.3f;
    const float r  = w * 0.5f;                 // 两端半圆
    const float k  = 0.5523f * r;
    const float mx = x0 + r;                   // 圆心 == cx

    p.AddBezier(Hx(mx), yt, Hx(mx + k), yt, Hx(x1), yt + r - k, Hx(x1), yt + r);
    p.AddLine(Hx(x1), yt + r, Hx(x1), yb - r);
    p.AddBezier(Hx(x1), yb - r, Hx(x1), yb - r + k, Hx(mx + k), yb, Hx(mx), yb);
    p.AddBezier(Hx(mx), yb, Hx(mx - k), yb, Hx(x0), yb - r + k, Hx(x0), yb - r);
    p.AddLine(Hx(x0), yb - r, Hx(x0), yt + r);
    p.AddBezier(Hx(x0), yt + r, Hx(x0), yt + r - k, Hx(mx - k), yt, Hx(mx), yt);
    p.CloseFigure();
}

static void AddHeadsetPaths(gp::GraphicsPath& band, gp::GraphicsPath& cupL, gp::GraphicsPath& cupR)
{
    band.AddLine(Hx(8.5f), Vy(34.0f), Hx(8.5f), Vy(30.5f));
    band.AddBezier(Hx(8.5f), Vy(30.5f), Hx(8.5f), Vy(16.8f), Hx(18.8f), Vy(6.5f), 32.0f, Vy(6.5f));
    band.AddBezier(32.0f, Vy(6.5f), Hx(45.2f), Vy(6.5f), Hx(55.5f), Vy(16.8f), Hx(55.5f), Vy(30.5f));
    band.AddLine(Hx(55.5f), Vy(30.5f), Hx(55.5f), Vy(34.0f));

    AddCupPath(cupL, 8.5f);
    AddCupPath(cupR, 55.5f);
}

static void UnionPathBounds(gp::RectF& acc, bool& has, gp::GraphicsPath& p, float pen_w)
{
    if (p.GetPointCount() < 2) return;
    gp::RectF b;
    if (p.GetBounds(&b) != gp::Ok) return;
    float pad = pen_w * 0.5f;
    b.X -= pad;  b.Y -= pad;
    b.Width += pad * 2.0f;  b.Height += pad * 2.0f;
    if (!has) { acc = b; has = true; }
    else gp::RectF::Union(acc, acc, b);
}

// 线宽按"最终 14px 下正好落在整数像素"反推：
// 鼠标设计外框宽 37、高 51，缩放 s ≈ 14 * 0.98 / (51 + pen)
//   pen 4.0 → 1.00px（发飘）   pen 6.0 → 1.44px（居中偏实）   pen 8.0 → 1.86px（最重）
// 当前：鼠标轮廓取 D(8.0) 的一半 = 4.0，中缝/滚轮沿用 C 的 6.0 / 7.4。
static const float PEN_MAIN  = 6.0f;   // 与 PEN_BAND 保持一致
static const float PEN_SEAM  = 6.0f;
static const float PEN_WHEEL = 7.4f;
static const float PEN_CUP   = 0.0f;   // 耳罩只填充不描边
// 耳机头梁单独一档：4.0 在浅底上呈 0.97px 几乎断线，加粗到 6.5（1.58px）才立得住。
// 宽度方向由耳罩撑开、不是约束项，所以加粗头梁不会让整体缩水。
static const float PEN_BAND  = 6.0f;

// 渲染成 32bpp BGRA（直通 alpha），backgroundColor 先铺成目标色、alpha=0
static void RenderIconBGRA(bool is_mouse, int size, COLORREF color, BYTE* out)
{
    if (size < 6) size = 6;
    const BYTE cr = GetRValue(color), cg = GetGValue(color), cb = GetBValue(color);
    const size_t total = (size_t)size * size;
    for (size_t i = 0; i < total; ++i)
    {
        out[i * 4 + 0] = cb;
        out[i * 4 + 1] = cg;
        out[i * 4 + 2] = cr;
        out[i * 4 + 3] = 0;
    }

    if (!EnsureGdiplus()) return;

    gp::Bitmap bmp(size, size, PixelFormat32bppARGB);
    {
        gp::Graphics g(&bmp);
        g.SetSmoothingMode(gp::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(gp::PixelOffsetModeHighQuality);
        g.Clear(gp::Color(0, 0, 0, 0));

        gp::GraphicsPath mainPath, sub1, sub2;
        float w1 = PEN_SEAM, w2 = PEN_WHEEL;
        bool hasSub2 = true;

        gp::RectF bounds;
        bool has_bounds = false;

        if (is_mouse)
        {
            AddMousePaths(mainPath, sub1, sub2);
            UnionPathBounds(bounds, has_bounds, mainPath, PEN_MAIN);
            UnionPathBounds(bounds, has_bounds, sub1, w1);
            UnionPathBounds(bounds, has_bounds, sub2, w2);
        }
        else
        {
            AddHeadsetPaths(mainPath, sub1, sub2);
            w1 = PEN_CUP;
            w2 = PEN_CUP;
            UnionPathBounds(bounds, has_bounds, mainPath, PEN_BAND);
            UnionPathBounds(bounds, has_bounds, sub1, 0.0f);
            UnionPathBounds(bounds, has_bounds, sub2, 0.0f);
        }

        if (has_bounds && bounds.Width > 0.1f && bounds.Height > 0.1f)
        {
            // 各自按墨迹包围盒等比缩放进"目标占比"框，再居中 —— 这是对齐的关键
            const float fill_w = is_mouse ? 0.80f : 0.92f;
            const float fill_h = is_mouse ? 0.98f : 0.86f;
            float s = size * fill_w / bounds.Width;
            float sy = size * fill_h / bounds.Height;
            if (sy < s) s = sy;

            // 居中的小数部分用四舍五入，避免长期偏半个像素
            float ox = (size - bounds.Width * s) * 0.5f - bounds.X * s;
            float oy = (size - bounds.Height * s) * 0.5f - bounds.Y * s;
            // 墨迹居中后的余数会被像素栅格量化成 0.5px 的台阶，且随线宽变化：
            //   耳机必须落在 extra 0.125~0.200 这一段（墨迹在位图内为 3..11 共 9 行、中心 7.0），取 0.16。
            // 每次改线宽/形状都要重新实测这两个值。
            oy += is_mouse ? 0.5f : 0.16f;
            gp::Matrix m(s, 0, 0, s, ox, oy);
            g.SetTransform(&m);

            gp::Pen pen(gp::Color(255, 255, 255, 255), is_mouse ? PEN_MAIN : PEN_BAND);
            pen.SetLineCap(gp::LineCapRound, gp::LineCapRound, gp::DashCapRound);
            pen.SetLineJoin(gp::LineJoinRound);
            g.DrawPath(&pen, &mainPath);

            if (is_mouse)
            {
                gp::Pen p1(gp::Color(255, 255, 255, 255), w1);
                p1.SetLineCap(gp::LineCapRound, gp::LineCapRound, gp::DashCapRound);
                g.DrawPath(&p1, &sub1);

                gp::Pen p2(gp::Color(255, 255, 255, 255), w2);
                p2.SetLineCap(gp::LineCapRound, gp::LineCapRound, gp::DashCapRound);
                g.DrawPath(&p2, &sub2);
            }
            else
            {
                gp::SolidBrush brush(gp::Color(255, 255, 255, 255));
                g.FillPath(&brush, &sub1);
                g.FillPath(&brush, &sub2);
            }
            (void)hasSub2;
        }
    }

    gp::BitmapData bd;
    gp::Rect rc(0, 0, size, size);
    if (bmp.LockBits(&rc, gp::ImageLockModeRead, PixelFormat32bppARGB, &bd) == gp::Ok)
    {
        for (int y = 0; y < size; ++y)
        {
            const BYTE* src = (const BYTE*)bd.Scan0 + (size_t)y * bd.Stride;
            for (int x = 0; x < size; ++x)
                out[((size_t)y * size + x) * 4 + 3] = src[x * 4 + 3];
        }
        bmp.UnlockBits(&bd);
    }
}

static HICON BuildIcon(bool is_mouse, int size, COLORREF color)
{
    int h = size, w = size;
    if (h < 6) h = 6;
    if (w < 4) w = 4;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;             // 自上而下
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdc = GetDC(nullptr);
    void* bits = nullptr;
    HBITMAP hbmColor = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, hdc);
    if (!hbmColor || !bits) return nullptr;

    BYTE* px = (BYTE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)w * h * 4);
    if (!px) { DeleteObject(hbmColor); return nullptr; }
    RenderIconBGRA(is_mouse, w, color, px);
    memcpy(bits, px, (size_t)w * h * 4);
    HeapFree(GetProcessHeap(), 0, px);

    HBITMAP hbmMask = CreateBitmap(w, h, 1, 1, nullptr);
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmMask = hbmMask;
    ii.hbmColor = hbmColor;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(hbmMask);
    DeleteObject(hbmColor);
    return icon;
}

// 按 (尺寸, 颜色) 缓存图标
class CIconCache
{
public:
    HICON Get(bool is_mouse, int size, COLORREF color)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_icon && m_size == size && m_color == color) return m_icon;
        if (m_icon) { DestroyIcon(m_icon); m_icon = nullptr; }
        m_icon = BuildIcon(is_mouse, size, color);
        m_size = size;
        m_color = color;
        return m_icon;
    }

private:
    std::mutex m_mutex;
    HICON m_icon = nullptr;
    int m_size = 0;
    COLORREF m_color = CLR_INVALID;
};

// ---------------------------------------------------------------- 刷新线程

static DWORD WINAPI RefreshThread(LPVOID)
{
    while (WaitForSingleObject(g_stop_event, 0) == WAIT_TIMEOUT)
    {
        int mouse = ReadMouseBattery();
        int headset = ReadHeadsetBattery();

        EnterCriticalSection(&g_lock);
        g_mouse_val = mouse;
        g_headset_val = headset;

        if (mouse >= 0) wsprintfW(g_mouse_text, L"%d%%", mouse);
        else            wcsncpy_s(g_mouse_text, L"--", _TRUNCATE);

        if (headset >= 0) wsprintfW(g_headset_text, L"%d%%", headset);
        else              wcsncpy_s(g_headset_text, L"--", _TRUNCATE);

        wsprintfW(g_tooltip, L"鼠标 %s  |  耳机 %s  (%s)",
                  (mouse >= 0 ? g_mouse_text : L"读不到"),
                  (headset >= 0 ? g_headset_text : L"读不到"),
                  L"2.4G 直连读取");
        LeaveCriticalSection(&g_lock);

        if (WaitForSingleObject(g_stop_event, REFRESH_SECONDS * 1000) != WAIT_TIMEOUT)
            break;
    }
    return 0;
}

static void EnsureThread()
{
    if (InterlockedExchange(&g_started, 1) != 0) return;

    InitializeCriticalSection(&g_lock);
    g_lock_ready = true;
    g_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, RefreshThread, nullptr, 0, nullptr);
}

// ---------------------------------------------------------------- 插件项目

// 图标边长（像素）：96 DPI 下 15px，高 DPI 下按比例放大但压住上限，
// 保证在任何缩放下都不比文字高出一截。GetItemWidthEx 和 DrawItem 必须共用。
static int IconSizeForDpi(int dpi)
{
    if (dpi <= 0) dpi = 96;
    int s = MulDiv(14, dpi, 96);
    s &= ~1;                 // 取偶数：偶数尺寸才能让图标中心落在整数像素上
    if (s < 12) s = 12;
    if (s > 22) s = 22;
    return s;
}

class CBatteryItem : public IPluginItem
{
public:
    explicit CBatteryItem(bool is_mouse) : m_is_mouse(is_mouse) {}

    const wchar_t* GetItemName() const override { return m_is_mouse ? L"鼠标电量" : L"耳机电量"; }
    const wchar_t* GetItemId() const override   { return m_is_mouse ? L"WB_MOUSE_BATTERY" : L"WB_HEADSET_BATTERY"; }
    const wchar_t* GetItemLableText() const override { return m_is_mouse ? L"鼠标" : L"耳机"; }
    const wchar_t* GetItemValueSampleText() const override { return L"100%"; }

    const wchar_t* GetItemValueText() const override
    {
        if (!g_lock_ready) return L"--";
        EnterCriticalSection(&g_lock);
        int v = m_is_mouse ? g_mouse_val : g_headset_val;
        wcsncpy_s(m_buf, (m_is_mouse ? g_mouse_text : g_headset_text), _TRUNCATE);
        LeaveCriticalSection(&g_lock);
        if (v < 0) return L"--";
        return m_buf;
    }

    // 自绘：图标 + 数值（TrafficMonitor 1.8.6 只提供 GDI 的 DrawItem）
    bool IsCustomDraw() const override { return true; }

    int GetItemWidth() const override { return 44; }   // 96 DPI 下的兜底宽度

    int GetItemWidthEx(void* hDC) const override
    {
        HDC hdc = (HDC)hDC;
        int dpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
        int icon = IconSizeForDpi(dpi);
        SIZE sz{};
        int tw = MulDiv(26, dpi, 96);
        if (hdc && GetTextExtentPoint32W(hdc, L"100%", 5, &sz))
            tw = sz.cx;
        // 图标列宽固定 = 图标边长，鼠标/耳机完全一致，两行才会左对齐
        int gap = icon / 4;
        if (gap < 3) gap = 3;
        return icon + gap + tw + MulDiv(1, dpi, 96);
    }

    void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) override
    {
        HDC dc = (HDC)hDC;
        if (dc == nullptr) return;

        // 主程序会把配置里的数值文字颜色送进来；没有就用深浅模式兜底
        COLORREF color = g_color_valid ? (COLORREF)(g_value_color & 0x00FFFFFF)
                                       : (dark_mode ? RGB(255, 255, 255) : RGB(0, 0, 0));

        // 图标边长按 DPI 给，且与 GetItemWidthEx 用同一公式，否则文字起点会错位
        int dpi = GetDeviceCaps(dc, LOGPIXELSY);
        int isz = IconSizeForDpi(dpi);
        if (isz > h) isz = (h > 8) ? h : 8;   // 行高不够时再收

        int gap = isz / 4;
        if (gap < 3) gap = 3;

        // 垂直居中：图标边长取偶数时，(h-isz)/2 恰好让位图中心落在数字视觉中线上。
        // TrafficMonitor 两行的行高并不一致（实测鼠标行 15、耳机行 16），
        // 用奇偶不定的尺寸会让其中一行整体偏半像素。
        int iy = y + (h - isz) / 2;
        HICON icon = m_icon_cache.Get(m_is_mouse, isz, color);
        if (icon)
            DrawIconEx(dc, x, iy, icon, isz, isz, 0, nullptr, DI_NORMAL);

        int tx = x + isz + gap;
        int tw = w - isz - gap;
        if (tw < 8) tw = 8;

        EnterCriticalSection(&g_lock);
        wcsncpy_s(m_buf, (m_is_mouse ? g_mouse_text : g_headset_text), _TRUNCATE);
        LeaveCriticalSection(&g_lock);

        COLORREF old_color = SetTextColor(dc, color);
        int old_bk = SetBkMode(dc, TRANSPARENT);
        RECT r{ tx, y, tx + tw, y + h };
        DrawTextW(dc, m_buf, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
        SetBkMode(dc, old_bk);
        SetTextColor(dc, old_color);

    }

private:
    bool m_is_mouse;
    mutable wchar_t m_buf[16] = L"--";
    CIconCache m_icon_cache;
};

// ---------------------------------------------------------------- 插件主体

class CBatteryPlugin : public ITMPlugin
{
public:
    CBatteryPlugin() : m_mouse(true), m_headset(false) {}

    IPluginItem* GetItem(int index) override
    {
        switch (index)
        {
        case 0: return &m_mouse;
        case 1: return &m_headset;
        default: return nullptr;
        }
    }

    void DataRequired() override
    {
        // 数据由后台线程刷新，这里只确保线程已启动，绝不在这里做 HID 往返
        EnsureThread();
    }

    const wchar_t* GetInfo(PluginInfoIndex index) override
    {
        switch (index)
        {
        case TMI_NAME:        return L"无线设备电量";
        case TMI_DESCRIPTION: return L"以图标显示 AJAZZ 鼠标与 MCHOSE 耳机的电量（HID 直读）";
        case TMI_AUTHOR:      return L"WorkBuddy";
        case TMI_COPYRIGHT:   return L"";
        case TMI_VERSION:     return L"1.1";
        case TMI_URL:         return L"";
        default:              return L"";
        }
    }

    const wchar_t* GetTooltipInfo() override
    {
        return g_tooltip;
    }

    void OnInitialize(ITrafficMonitor* pApp) override
    {
        EnsureThread();
    }

    void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override
    {
        if (index == ITMPlugin::EI_VALUE_TEXT_COLOR && data != nullptr)
        {
            unsigned long v = wcstoul(data, nullptr, 10);
            g_value_color = (LONG)(v & 0x00FFFFFF);
            g_color_valid = 1;
        }
    }

private:
    CBatteryItem m_mouse;
    CBatteryItem m_headset;
};

static CBatteryPlugin g_plugin;

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance()
{
    return &g_plugin;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_DETACH)
    {
        if (g_stop_event)
        {
            SetEvent(g_stop_event);
            if (g_thread)
            {
                WaitForSingleObject(g_thread, 1500);
                CloseHandle(g_thread);
                g_thread = nullptr;
            }
            CloseHandle(g_stop_event);
            g_stop_event = nullptr;
        }
    }
    return TRUE;
}
