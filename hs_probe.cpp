// hs_probe - 耳机电量口探针
// 1) 把 VID 291D(接收器) / 0D8C(声卡) 下所有 HID 接口列出来（usage page / 报告长度）
// 2) 对厂商页接口跑一次 55 65 01 协议，打印完整回包
// 3) 对答话的接口连读 10 轮，标出哪些字节在跳
// 编译: g++ -O2 -o hs_probe.exe hs_probe.cpp -lsetupapi -lhid

#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <cstdio>

static const GUID kHidGuid = { 0x4D1E55B2, 0xF16F, 0x11CF, { 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };

static void Dump(const char* tag, const BYTE* b, DWORD n)
{
    printf("  %s", tag);
    for (DWORD i = 0; i < n && i < 32; ++i) printf(" %02X", b[i]);
    printf("\n");
}

// 带超时的重叠 I/O，设备不应答时不会把探针卡死
static bool OverlappedIO(HANDLE h, bool isWrite, void* buf, DWORD len, DWORD timeout_ms, DWORD* transferred)
{
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return false;

    BOOL ok = isWrite ? WriteFile(h, buf, len, nullptr, &ov)
                      : ReadFile(h, buf, len, nullptr, &ov);
    if (!ok && GetLastError() != ERROR_IO_PENDING) { CloseHandle(ov.hEvent); return false; }

    bool result = false;
    if (WaitForSingleObject(ov.hEvent, timeout_ms) == WAIT_OBJECT_0)
    {
        DWORD got = 0;
        if (GetOverlappedResult(h, &ov, &got, FALSE))
        {
            if (transferred) *transferred = got;
            result = true;
        }
    }
    else CancelIoEx(h, &ov);

    CloseHandle(ov.hEvent);
    return result;
}

// 一次完整往返：写 64 字节 55 65 01 ...，读回 64 字节
static bool RoundTrip(HANDLE h, BYTE* reply, DWORD* got)
{
    BYTE req[64] = { 0 };
    req[0] = 0x55; req[1] = 0x65; req[2] = 0x01;

    DWORD written = 0;
    if (!OverlappedIO(h, true, req, sizeof(req), 400, &written)) return false;

    for (int i = 0; i < 3; ++i)
    {
        DWORD g = 0;
        if (OverlappedIO(h, false, reply, 64, 400, &g))
        {
            *got = g;
            return (g >= 4 && reply[0] == 0x55 && reply[1] == 0x65);
        }
    }
    return false;
}

int main()
{
    HDEVINFO di = SetupDiGetClassDevsW(&kHidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (di == INVALID_HANDLE_VALUE) { printf("SetupDi 失败\n"); return 1; }

    SP_DEVICE_INTERFACE_DATA did{};
    did.cbSize = sizeof(did);

    struct Cand { HANDLE h; wchar_t path[MAX_PATH]; USHORT vid, pid, page; char col[8]; };
    Cand cands[32];
    int ncand = 0;

    printf("=== HID 接口清单 ===\n");
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(di, nullptr, &kHidGuid, i, &did); ++i)
    {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(di, &did, nullptr, 0, &need, nullptr);
        if (!need) continue;

        BYTE* buf = (BYTE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, need);
        if (!buf) continue;
        auto* det = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)buf;
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (SetupDiGetDeviceInterfaceDetailW(di, &did, det, need, &need, nullptr))
        {
            HANDLE h = CreateFileW(det->DevicePath, GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (h != INVALID_HANDLE_VALUE)
            {
                HIDD_ATTRIBUTES attr{};
                attr.Size = sizeof(attr);
                if (HidD_GetAttributes(h, &attr) &&
                    (attr.VendorID == 0x291D || attr.VendorID == 0x0D8C))
                {
                    HIDP_CAPS caps{};
                    PHIDP_PREPARSED_DATA ppd = nullptr;
                    if (HidD_GetPreparsedData(h, &ppd))
                    {
                        HidP_GetCaps(ppd, &caps);
                        HidD_FreePreparsedData(ppd);
                    }

                    printf("%04X:%04X  page=%04X usage=%04X  in=%u out=%u feat=%u  open=%s\n    %ls\n",
                           attr.VendorID, attr.ProductID, caps.UsagePage, caps.Usage,
                           caps.InputReportByteLength, caps.OutputReportByteLength,
                           caps.FeatureReportByteLength,
                           (attr.ProductID == 0x385D ? "dongle" : "soundcard"),
                           det->DevicePath);

                    if (caps.UsagePage >= 0xFF00 && caps.OutputReportByteLength >= 64 && ncand < 32)
                    {
                        cands[ncand].h = h;
                        cands[ncand].vid = attr.VendorID;
                        cands[ncand].pid = attr.ProductID;
                        cands[ncand].page = caps.UsagePage;
                        wcsncpy_s(cands[ncand].path, det->DevicePath, _TRUNCATE);
                        ++ncand;
                        CloseHandle(h);
                        HeapFree(GetProcessHeap(), 0, buf);
                        continue;
                    }
                }
                CloseHandle(h);
            }
        }
        HeapFree(GetProcessHeap(), 0, buf);
    }
    SetupDiDestroyDeviceInfoList(di);

    printf("\n=== 厂商接口试探（%d 个候选） ===\n", ncand);
    int answered = -1;
    for (int i = 0; i < ncand; ++i)
    {
        HANDLE h = CreateFileW(cands[i].path, GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            printf("[%d] %04X:%04X page=%04X 打不开 (err=%lu)\n", i, cands[i].vid, cands[i].pid, cands[i].page, GetLastError());
            continue;
        }

        printf("[%d] %04X:%04X page=%04X\n", i, cands[i].vid, cands[i].pid, cands[i].page);
        BYTE reply[64] = { 0 };
        DWORD got = 0;
        if (RoundTrip(h, reply, &got))
        {
            Dump("回包:", reply, got);
            if (answered < 0) answered = i;
        }
        else
        {
            printf("  无有效回包 (got=%lu)\n", got);
        }
        CloseHandle(h);
    }

    if (answered < 0) { printf("\n没有任何厂商接口答话\n"); return 0; }

    // 连读 10 轮，看哪些字节在跳
    printf("\n=== 连读 10 轮：%04X:%04X page=%04X ===\n", cands[answered].vid, cands[answered].pid, cands[answered].page);
    HANDLE h = CreateFileW(cands[answered].path, GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) { printf("打不开\n"); return 1; }

    BYTE reps[10][64];
    int okCount = 0;
    for (int r = 0; r < 10; ++r)
    {
        BYTE reply[64] = { 0 };
        DWORD got = 0;
        if (RoundTrip(h, reply, &got))
        {
            memcpy(reps[okCount], reply, 64);
            printf("  #%d", okCount + 1);
            for (int i = 0; i < 12; ++i) printf(" %02X", reply[i]);
            printf("   ...  [2]=%d [3]=%d [4]=%d\n", reply[2], reply[3], reply[4]);
            ++okCount;
        }
        else
        {
            printf("  #%d 无回包\n", r + 1);
        }
        Sleep(700);
    }
    CloseHandle(h);

    if (okCount > 1)
    {
        printf("\n变化的字节位置: ");
        for (int i = 0; i < 32; ++i)
        {
            bool varies = false;
            for (int r = 1; r < okCount; ++r)
                if (reps[r][i] != reps[0][i]) { varies = true; break; }
            if (varies) printf("[%d] ", i);
        }
        printf("\n");
    }
    return 0;
}
