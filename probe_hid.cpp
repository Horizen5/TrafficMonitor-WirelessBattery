// probe_hid.cpp — 枚举 HID 设备并 dump 鼠标/耳机的原始 report，用来判断有没有充电标志位
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <cstdio>
#include <cwchar>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

static const GUID GUID_DEVINTERFACE_HID =
{ 0x4D1E55B2, 0xF16F, 0x11CF, { 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };

static bool OverlappedIO(HANDLE h, bool isWrite, void* buf, DWORD len, DWORD timeout_ms, DWORD* transferred)
{
    OVERLAPPED ov{}; ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) return false;
    BOOL ok = isWrite ? WriteFile(h, buf, len, nullptr, &ov) : ReadFile(h, buf, len, nullptr, &ov);
    bool result = false;
    if (!ok && GetLastError() != ERROR_IO_PENDING) { CloseHandle(ov.hEvent); return false; }
    if (WaitForSingleObject(ov.hEvent, timeout_ms) == WAIT_OBJECT_0) {
        DWORD got = 0;
        if (GetOverlappedResult(h, &ov, &got, FALSE)) { if (transferred) *transferred = got; result = true; }
    } else CancelIoEx(h, &ov);
    CloseHandle(ov.hEvent);
    return result;
}

static void DumpBytes(const char* tag, const BYTE* p, int n)
{
    printf("  %-14s", tag);
    for (int i = 0; i < n; ++i) {
        printf("%02X ", p[i]);
        if ((i + 1) % 16 == 0 && i + 1 < n) printf("\n                ");
    }
    printf("\n");
}

static void ProbeDevice(const wchar_t* path, unsigned short vid, unsigned short pid)
{
    HANDLE h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) { wprintf(L"  !! 打不开: %s (err=%lu)\n", path, GetLastError()); return; }

    HIDD_ATTRIBUTES attr{}; attr.Size = sizeof(attr);
    HidD_GetAttributes(h, &attr);

    wchar_t product[128] = L"";
    HidD_GetProductString(h, product, sizeof(product));
    wprintf(L"  产品名: %s\n", product[0] ? product : L"(无)");
    printf("  VID:PID = %04X:%04X  version=%04X\n", attr.VendorID, attr.ProductID, attr.VersionNumber);

    // 输出报告长度（决定能不能读 input report）
    PHIDP_PREPARSED_DATA ppd = nullptr;
    if (HidD_GetPreparsedData(h, &ppd)) {
        HIDP_CAPS caps{};
        if (HidP_GetCaps(ppd, &caps) == HIDP_STATUS_SUCCESS) {
            printf("  UsagePage=%04X Usage=%04X  in=%u out=%u feat=%u  inReport=%u featReport=%u\n",
                   caps.UsagePage, caps.Usage, caps.InputReportByteLength,
                   caps.OutputReportByteLength, caps.FeatureReportByteLength,
                   caps.InputReportByteLength, caps.FeatureReportByteLength);
        }
        HidD_FreePreparsedData(ppd);
    }

    // 鼠标：先发心跳再读
    if (vid == 0x3151) {
        BYTE hb[65] = {0}; hb[0] = 0x00; hb[1] = 0xF7;
        printf("  [心跳] SetFeature(00 F7 ...) = %d\n", HidD_SetFeature(h, hb, sizeof(hb)));
        Sleep(60);
    }

    // 逐个 feature report ID 探测
    for (int id = 0x00; id <= 0x10; ++id) {
        BYTE rep[65] = {0};
        rep[0] = (BYTE)id;
        if (HidD_GetFeature(h, rep, sizeof(rep))) {
            bool all_zero = true;
            for (int i = 0; i < 65; ++i) if (rep[i]) { all_zero = false; break; }
            if (!all_zero) {
                char tag[32]; sprintf(tag, "feat 0x%02X:", id);
                DumpBytes(tag, rep, 24);
            }
        }
    }

    // 非阻塞读 input report（2.4G 接收器会主动上报）
    printf("  [input reports]\n");
    for (int k = 0; k < 6; ++k) {
        BYTE rep[65] = {0}; DWORD got = 0;
        if (OverlappedIO(h, false, rep, sizeof(rep), 250, &got) && got > 0) {
            char tag[32]; sprintf(tag, "in #%d (%lu B):", k, got);
            DumpBytes(tag, rep, got > 20 ? 20 : (int)got);
        } else {
            printf("  (超时，无更多上报)\n");
            break;
        }
    }

    CloseHandle(h);
}

int main()
{
    printf("=== 枚举 GUID_DEVINTERFACE_HID ===\n");
    HDEVINFO di = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_HID, nullptr, nullptr,
                                       DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (di == INVALID_HANDLE_VALUE) { printf("SetupDiGetClassDevs 失败 %lu\n", GetLastError()); return 1; }

    SP_DEVICE_INTERFACE_DATA did{}; did.cbSize = sizeof(did);
    int interesting = 0;
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(di, nullptr, &GUID_DEVINTERFACE_HID, i, &did); ++i) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(di, &did, nullptr, 0, &need, nullptr);
        if (!need) continue;
        BYTE* buf = (BYTE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, need);
        if (!buf) continue;
        auto* det = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)buf;
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (SetupDiGetDeviceInterfaceDetailW(di, &did, det, need, &need, nullptr)) {
            HANDLE h = CreateFileW(det->DevicePath, 0,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, 0, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                HIDD_ATTRIBUTES a{}; a.Size = sizeof(a);
                if (HidD_GetAttributes(h, &a)) {
                    if (a.VendorID == 0x3151 || a.VendorID == 0x291D) {
                        printf("\n>>> [%d] %04X:%04X\n", ++interesting, a.VendorID, a.ProductID);
                        wprintf(L"    path=%s\n", det->DevicePath);
                        CloseHandle(h);
                        ProbeDevice(det->DevicePath, a.VendorID, a.ProductID);
                    }
                }
                CloseHandle(h);
            }
        }
        HeapFree(GetProcessHeap(), 0, buf);
    }
    SetupDiDestroyDeviceInfoList(di);
    if (!interesting) printf("\n没找到 3151 / 291D 的设备（鼠标可能没连上或没电关机）\n");
    return 0;
}
