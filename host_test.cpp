// 模拟 TrafficMonitor 的加载与调用流程，验证插件不会崩溃、能返回电量
#include "PluginInterface.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef ITMPlugin* (*PFN_GET_INSTANCE)();

static void PrintW(const wchar_t* w)
{
    char buf[512];
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, sizeof(buf), nullptr, nullptr);
    if (n > 0) fwrite(buf, 1, (size_t)(n - 1), stdout);
}

int main()
{
    HMODULE h = LoadLibraryW(L"WirelessBattery.dll");
    if (!h)
    {
        printf("LoadLibrary 失败, err=%lu\n", GetLastError());
        return 1;
    }
    printf("LoadLibrary 成功\n");

    PFN_GET_INSTANCE fn = (PFN_GET_INSTANCE)GetProcAddress(h, "TMPluginGetInstance");
    if (!fn)
    {
        printf("找不到导出函数\n");
        return 1;
    }

    ITMPlugin* p = fn();
    printf("TMPluginGetInstance 返回 %p\n", (void*)p);
    printf("API 版本 = %d\n", p->GetAPIVersion());

    printf("名称: "); PrintW(p->GetInfo(ITMPlugin::TMI_NAME)); printf("\n");
    printf("描述: "); PrintW(p->GetInfo(ITMPlugin::TMI_DESCRIPTION)); printf("\n");

    p->OnInitialize(nullptr);

    for (int round = 0; round < 4; ++round)
    {
        Sleep(3000);
        p->DataRequired();

        IPluginItem* m = p->GetItem(0);
        IPluginItem* e = p->GetItem(1);
        IPluginItem* bad = p->GetItem(2);

        printf("第%d次: ", round + 1);
        PrintW(m->GetItemLableText()); printf("="); PrintW(m->GetItemValueText());
        printf("   ");
        PrintW(e->GetItemLableText()); printf("="); PrintW(e->GetItemValueText());
        printf("   (GetItem(2)=%s)\n", bad == nullptr ? "nullptr 正确" : "非空 错误");
        fflush(stdout);
    }

    printf("卸载 DLL...\n");
    fflush(stdout);
    FreeLibrary(h);
    printf("卸载完成，未崩溃\n");
    return 0;
}
