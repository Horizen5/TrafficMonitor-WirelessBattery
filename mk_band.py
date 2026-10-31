# 拼 band 线宽对比图
from PIL import Image, ImageDraw, ImageFont
import os

os.chdir(r"C:\Users\Administrator\WorkBuddy\电脑\tmplug")
Z = 8
CELL = 14 * Z
PAD = 16
TOP = 56
LBL = 42
label = ["4.0（现在）", "5.0", "5.5", "6.5", "7.5"]
px = ["0.97px", "1.22px", "1.34px", "1.58px", "1.82px"]

cols = 5
W = PAD + cols * (CELL + PAD)
H = TOP + 2 * (CELL + LBL + PAD) + PAD
img = Image.new("RGB", (W, H), (245, 245, 243))
d = ImageDraw.Draw(img)

try:
    f1 = ImageFont.truetype(r"C:\Windows\Fonts\msyhbd.ttc", 20)
    f2 = ImageFont.truetype(r"C:\Windows\Fonts\msyh.ttc", 15)
except Exception:
    f1 = f2 = ImageFont.load_default()

for i in range(cols):
    x0 = PAD + i * (CELL + PAD)
    y0 = TOP
    for r, suf in enumerate(["l", "d"]):
        p = "bp%d_%s_14.png" % (i, suf)
        if not os.path.exists(p):
            continue
        im = Image.open(p).convert("RGB").resize((CELL, CELL), Image.NEAREST)
        yy = y0 + r * (CELL + LBL + PAD)
        img.paste(im, (x0, yy))
        d.rectangle([x0 - 1, yy - 1, x0 + CELL, yy + CELL], outline=(190, 190, 185))
        d.text((x0, yy + CELL + 3), label[i], fill=(20, 20, 20), font=f2)
        d.text((x0, yy + CELL + 21), "墨迹 " + px[i], fill=(120, 120, 115), font=f2)

d.text((PAD, 12), "耳机头梁线宽扫描（其余参数完全不变，14px 实际渲染 ×8 放大）",
       fill=(20, 20, 20), font=f1)
img.save("band_pen_sheet.png")
print(img.size)
