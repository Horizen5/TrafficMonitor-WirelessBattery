// 预览工具：把图标按多种尺寸光栅化后导出原始 RGBA，供外部拼成 PNG 检查造型
// 直接包含插件源文件以复用同一套形状定义，保证预览和实际 DLL 完全一致
#include "WirelessBattery.cpp"
#include <cstdio>
#include <vector>

int main()
{
    struct Rec { int h; int is_mouse; };
    const Rec recs[] = { {16,1},{16,0},{20,1},{20,0},{32,1},{32,0},{64,1},{64,0} };

    FILE* f = fopen("icon_preview.raw", "wb");
    if (!f) { printf("open failed\n"); return 1; }

    int count = 0;
    for (const auto& r : recs)
    {
        int h = r.h;
        int w = MulDiv(h, CanvasWidth(r.is_mouse != 0), 16);
        std::vector<BYTE> buf((size_t)w * h * 4, 0);
        RenderIconRGBA(r.is_mouse != 0, w, h, RGB(255, 255, 255), buf.data());
        int mouse = r.is_mouse;
        fwrite(&w, sizeof(int), 1, f);
        fwrite(&h, sizeof(int), 1, f);
        fwrite(&mouse, sizeof(int), 1, f);
        fwrite(buf.data(), 1, buf.size(), f);
        ++count;
    }
    fclose(f);
    printf("wrote %d records\n", count);
    return 0;
}
