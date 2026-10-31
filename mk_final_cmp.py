# 真实任务栏截图 vs 渲染档位，同一尺度并排
from PIL import Image, ImageDraw, ImageFont
import os
os.chdir(r"C:\Users\Administrator\WorkBuddy\电脑\tmplug")

Z = 16
PAD, TOP = 20, 58
CELL = 14 * Z

cands = [("bp0_d_14.png", "4.0"), ("bp2_d_14.png", "5.5"), ("bp3_d_14.png", "6.5")]
# 真实截图：耳机图标 14x14（从 2247,1417 起）
real = Image.open("shot_full.png").convert("RGB").crop((2247, 1417, 2261, 1431))

tiles = []
for f, lab in cands:
    im = Image.open(f).convert("RGB").resize((CELL, CELL), Image.NEAREST)
    tiles.append((im, lab + " 渲染"))
tiles.append((real.resize((CELL, CELL), Image.NEAREST), "6.5 实际任务栏"))

W = PAD + len(tiles) * (CELL + PAD)
H = TOP + CELL + 34 + PAD
img = Image.new("RGB", (W, H), (248, 248, 246))
d = ImageDraw.Draw(img)
try:
    f1 = ImageFont.truetype(r"C:\Windows\Fonts\msyhbd.ttc", 19)
    f2 = ImageFont.truetype(r"C:\Windows\Fonts\msyh.ttc", 15)
except Exception:
    f1 = f2 = ImageFont.load_default()

d.text((PAD, 14), "耳机头梁 4.0 / 5.5 / 6.5 渲染 vs 实际任务栏", fill=(20, 20, 20), font=f1)
for i, (im, lab) in enumerate(tiles):
    x = PAD + i * (CELL + PAD)
    img.paste(im, (x, TOP))
    d.rectangle([x - 1, TOP - 1, x + CELL, TOP + CELL], outline=(196, 196, 190))
    d.text((x, TOP + CELL + 8), lab, fill=(35, 35, 35), font=f2)

img.save("band_final_cmp.png")
print(img.size)
