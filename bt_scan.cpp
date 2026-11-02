// bt_scan - 用 Win32 蓝牙 API 做一次经典蓝牙（BR/EDR）inquiry 扫描
// 作用：验证「电脑蓝牙到底能不能扫到周围设备」，而不是只看已配对列表。
// 编译: g++ -O2 -o bt_scan.exe bt_scan.cpp -lbthprops

#include <windows.h>
#include <bluetoothapis.h>
#include <cstdio>

static const char* ClassToString(ULONG c)
{
    // 取 major service class 的粗略判断
    if (c & 0x000004) return "audio/headset";
    if (c & 0x000100) return "computer";
    if (c & 0x000200) return "phone";
    if (c & 0x000010) return "peripheral(HID)";
    if (c & 0x000020) return "imaging";
    if (c & 0x000400) return "LAN/AP";
    return "-";
}

int main()
{
    BLUETOOTH_FIND_RADIO_PARAMS rp = { sizeof(rp) };
    HANDLE hRadio = nullptr;
    HBLUETOOTH_RADIO_FIND find = BluetoothFindFirstRadio(&rp, &hRadio);

    if (!find)
    {
        printf("[x] 没有找到蓝牙无线电, GetLastError=%lu\n", GetLastError());
        return 1;
    }

    int radio_index = 0;
    do
    {
        BLUETOOTH_RADIO_INFO ri = { sizeof(ri) };
        if (BluetoothGetRadioInfo(hRadio, &ri) == ERROR_SUCCESS)
        {
            printf("=== radio[%d] %ls ===\n", radio_index, ri.szName);
            printf("    地址 %02X:%02X:%02X:%02X:%02X:%02X  class=0x%08X  mfr=%u  subver=0x%04X\n",
                   ri.address.rgBytes[5], ri.address.rgBytes[4], ri.address.rgBytes[3],
                   ri.address.rgBytes[2], ri.address.rgBytes[1], ri.address.rgBytes[0],
                   ri.ulClassofDevice, ri.manufacturer, ri.lmpSubversion);

            BLUETOOTH_DEVICE_SEARCH_PARAMS sp = { sizeof(sp) };
            BLUETOOTH_DEVICE_INFO di = { sizeof(di) };
            sp.hRadio = hRadio;
            sp.fReturnAuthenticated = TRUE;
            sp.fReturnRemembered    = TRUE;
            sp.fReturnUnknown       = TRUE;
            sp.fReturnConnected     = TRUE;
            sp.fIssueInquiry        = TRUE;
            sp.cTimeoutMultiplier   = 10;   // 10 * 1.28s ≈ 12.8s

            printf("    开始 inquiry（约 12 秒）...\n");
            DWORD t0 = GetTickCount();

            HBLUETOOTH_DEVICE_FIND df = BluetoothFindFirstDevice(&sp, &di);
            int count = 0;
            if (!df)
            {
                printf("    [!] 一个都没扫到, GetLastError=%lu (%s)\n",
                       GetLastError(), GetLastError() == ERROR_NO_MORE_ITEMS ? "正常结束但结果为空" : "调用失败");
            }
            else
            {
                do
                {
                    ++count;
                    printf("    [%2d] %-32ls %02X:%02X:%02X:%02X:%02X:%02X  %s%s%s  [%s]\n",
                           count,
                           di.szName[0] ? di.szName : L"(无名)",
                           di.Address.rgBytes[5], di.Address.rgBytes[4], di.Address.rgBytes[3],
                           di.Address.rgBytes[2], di.Address.rgBytes[1], di.Address.rgBytes[0],
                           di.fConnected    ? "已连接 " : "",
                           di.fRemembered   ? "已配对 " : "",
                           (!di.fConnected && !di.fRemembered) ? "陌生设备 " : "",
                           ClassToString(di.ulClassofDevice));
                } while (BluetoothFindNextDevice(df, &di));
                BluetoothFindDeviceClose(df);
            }

            printf("    -> 共 %d 个设备，耗时 %lu ms\n\n", count, GetTickCount() - t0);
        }
        CloseHandle(hRadio);
        ++radio_index;
    } while (BluetoothFindNextRadio(find, &hRadio));

    BluetoothFindRadioClose(find);
    return 0;
}
