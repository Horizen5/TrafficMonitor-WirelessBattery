// probe3.cpp — 有线模式下全量扫命令字：找出任何"不原样回显"的响应
// 判据：回包 r[1] != 我们写进去的 cmd，或 r[2..] 有非零数据
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <cstdio>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

static const GUID GUID_DEVINTERFACE_HID =
{ 0x4D1E55B2, 0xF16F, 0x11CF, { 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };

int main()
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
                                   OPEN_EXISTING, 0, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                HIDD_ATTRIBUTES a{}; a.Size = sizeof(a);
                if (HidD_GetAttributes(h, &a) && a.VendorID == 0x3151) {
                    PHIDP_PREPARSED_DATA ppd = nullptr;
                    HIDP_CAPS caps{};
                    if (HidD_GetPreparsedData(h, &ppd)) {
                        HidP_GetCaps(ppd, &caps);
                        HidD_FreePreparsedData(ppd);
                    }
                    if (caps.FeatureReportByteLength < 64) { CloseHandle(h); HeapFree(GetProcessHeap(),0,buf); continue; }

                    printf("扫 %04X:%04X (feat=%u)\n", a.VendorID, a.ProductID, caps.FeatureReportByteLength);
                    printf("写 cmd 后判断：回包[1] != cmd 或 [2..7] 有非零 → 认为设备真的答话了\n\n");

                    int hits = 0;
                    for (int cmd = 0x00; cmd <= 0x7F; ++cmd) {
                        BYTE w[65] = {0};
                        w[0] = 0x00;
                        w[1] = (BYTE)cmd;
                        memset(w + 2, 0, 63);            // 清零，避免上一次写入残留
                        if (!HidD_SetFeature(h, w, sizeof(w))) { printf("cmd %02X SetFeature 失败\n", cmd); continue; }

                        BYTE r[65] = {0};
                        r[0] = 0x05;                     // 有些固件固定用 0x05 取回
                        if (!HidD_GetFeature(h, r, sizeof(r))) continue;

                        bool real = (r[1] != (BYTE)cmd);
                        for (int j = 2; j < 8 && !real; ++j) if (r[j]) real = true;

                        if (real) {
                            ++hits;
                            printf("cmd %02X -> ", cmd);
                            for (int j = 0; j < 12; ++j) printf("%02X ", r[j]);
                            printf("\n");
                        }
                    }
                    printf("\n可能答话的命令数：%d\n", hits);
                }
                CloseHandle(h);
            }
        }
        HeapFree(GetProcessHeap(), 0, buf);
    }
    SetupDiDestroyDeviceInfoList(di);
    return 0;
}
