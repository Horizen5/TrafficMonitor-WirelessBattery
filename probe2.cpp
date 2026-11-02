// probe2.cpp — 聚焦：充电(有线)模式下的鼠标还能不能读到电量
// 1) 正确打印设备字符串（UTF-8）
// 2) 不发心跳直接 GetFeature(0x05)
// 3) 发心跳后连读 3 次，看数据是否变化
// 4) 长听 input report 2 秒
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <cstdio>
#include <string>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

static const GUID GUID_DEVINTERFACE_HID =
{ 0x4D1E55B2, 0xF16F, 0x11CF, { 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };

static std::string U8(const wchar_t* w)
{
    if (!w || !*w) return "(empty)";
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

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

static void Hex(const char* tag, const BYTE* p, int n)
{
    printf("    %-18s", tag);
    for (int i = 0; i < n; ++i) printf("%02X ", p[i]);
    printf("\n");
}

int main(int argc, char** argv)
{
    HDEVINFO di = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_HID, nullptr, nullptr,
                                       DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (di == INVALID_HANDLE_VALUE) return 1;

    SP_DEVICE_INTERFACE_DATA did{}; did.cbSize = sizeof(did);
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(di, nullptr, &GUID_DEVINTERFACE_HID, i, &did); ++i) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(di, &did, nullptr, 0, &need, nullptr);
        if (!need) continue;
        BYTE* buf = (BYTE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, need);
        auto* det = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)buf;
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (SetupDiGetDeviceInterfaceDetailW(di, &did, det, need, &need, nullptr)) {
            HANDLE h = CreateFileW(det->DevicePath, GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                HIDD_ATTRIBUTES a{}; a.Size = sizeof(a);
                if (HidD_GetAttributes(h, &a) && a.VendorID == 0x3151) {
                    wchar_t prod[128] = L"", manu[128] = L"", serial[128] = L"";
                    HidD_GetProductString(h, prod, sizeof(prod));
                    HidD_GetManufacturerString(h, manu, sizeof(manu));
                    HidD_GetSerialNumberString(h, serial, sizeof(serial));

                    HIDP_CAPS caps{};
                    bool has_caps = false;
                    PHIDP_PREPARSED_DATA ppd = nullptr;
                    if (HidD_GetPreparsedData(h, &ppd)) {
                        has_caps = (HidP_GetCaps(ppd, &caps) == HIDP_STATUS_SUCCESS);
                        HidD_FreePreparsedData(ppd);
                    }

                    printf("\n[%04X:%04X] page=%04X usage=%04X feat=%u in=%u  ver=%04X\n",
                           a.VendorID, a.ProductID,
                           has_caps ? caps.UsagePage : 0, has_caps ? caps.Usage : 0,
                           has_caps ? caps.FeatureReportByteLength : 0,
                           has_caps ? caps.InputReportByteLength : 0,
                           a.VersionNumber);
                    printf("    product=\"%s\" manu=\"%s\" serial=\"%s\"\n",
                           U8(prod).c_str(), U8(manu).c_str(), U8(serial).c_str());

                    // 只处理带 feature 65 的那个"厂商控制"集合
                    if (has_caps && caps.FeatureReportByteLength >= 64) {
                        BYTE rep[65] = {0};
                        rep[0] = 0x05;
                        BOOL ok = HidD_GetFeature(h, rep, sizeof(rep));
                        Hex(("GetFeature(05) 无心跳 ok=" + std::to_string(ok)).c_str(), rep, 12);

                        BYTE hb[65] = {0}; hb[0] = 0x00; hb[1] = 0xF7;
                        HidD_SetFeature(h, hb, sizeof(hb));
                        for (int k = 1; k <= 3; ++k) {
                            Sleep(120);
                            BYTE r2[65] = {0}; r2[0] = 0x05;
                            BOOL ok2 = HidD_GetFeature(h, r2, sizeof(r2));
                            Hex(("心跳后 #" + std::to_string(k)).c_str(), r2, 12);
                        }

                        // 也试试其它厂商常用命令
                        for (BYTE cmd : { 0x01, 0x02, 0x03, 0x04, 0x06, 0x10, 0x80 }) {
                            BYTE w[65] = {0}; w[0] = 0x00; w[1] = cmd;
                            HidD_SetFeature(h, w, sizeof(w));
                            Sleep(60);
                            BYTE r3[65] = {0}; r3[0] = cmd;
                            if (HidD_GetFeature(h, r3, sizeof(r3))) {
                                bool nz = false;
                                for (int j = 2; j < 12; ++j) if (r3[j]) { nz = true; break; }
                                if (nz) Hex(("cmd " + std::to_string(cmd)).c_str(), r3, 12);
                            }
                        }
                    }

                    if (has_caps && caps.InputReportByteLength > 0) {
                        printf("    -- 监听 input report 2s --\n");
                        int got_any = 0;
                        DWORD t0 = GetTickCount();
                        while (GetTickCount() - t0 < 2000) {
                            BYTE r[128] = {0}; DWORD got = 0;
                            if (OverlappedIO(h, false, r, caps.InputReportByteLength, 300, &got) && got) {
                                if (got_any < 6) Hex("in", r, got > 12 ? 12 : (int)got);
                                ++got_any;
                            }
                        }
                        printf("    共收到 %d 个 input report\n", got_any);
                    }
                }
                CloseHandle(h);
            }
        }
        HeapFree(GetProcessHeap(), 0, buf);
    }
    SetupDiDestroyDeviceInfoList(di);
    return 0;
}
