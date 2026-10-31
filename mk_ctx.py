# 把各档耳机图标放进"任务栏一行"的上下文里看
from PIL import Image, ImageDraw, ImageFont
import os

os.chdir(r"C:\Users\Administrator\WorkBuddy\电脑\tmplug")
label = ["4.0（现在）", "5.0", "5.5", "6.5", "7.5"]
Z = 6

try:
    ftxt = ImageFont.truetype(r"C:\Windows\Fonts\msyh.ttc", 12)
    flbl = ImageFont.truetype(r"C:\Windows\Fonts\msyhbd.ttc", 16)
except Exception:
    ftxt = flbl = ImageFont.load_default()

TEXT = "40%"
tw = ftxt.getbbox(TEXT)[2]
ROW_W = 14 + 3 + tw + 6
ROW_H = 16

def strip(dark):
    bg = (32, 32, 32) if dark else (243, 243, 243)
    fg = (255, 255, 255) if dark else (0, 0, 0)
    out = []
    for i in range(5):
        im = Image.new("RGB", (ROW_W, ROW_H), bg)
        ic = Image.open("bp%d_%s_14.png" % (i, "d" if dark else "l")).convert("RGB")
        # 浅底用黑图标、深底用白图标；图标本身已带底，直接贴
        im.paste(ic, (0, (ROW_H - 14) // 2))
        d = ImageDraw.Draw(im)
        ty = (ROW_H - 15) // 2
        d.text((14 + 3, ty), TEXT, fill=fg, font=ftxt)
        out.append(im)
    return out

PAD, TOP, LBL = 18, 62, 34
CW = ROW_W * Z
W = PAD * 2 + 5 * (CW + PAD)
H = TOP + 2 * (ROW_H * Z + LBL + PAD) + PAD
img = Image.new("RGB", (W, H), (250, 250, 248))
d = ImageDraw.Draw(img)

for r, dark in enumerate([False, True]):
    strips = strip(dark)
    for i, s in enumerate(strips):
        x = PAD + i * (CW + PAD)
        y = TOP + r * (ROW_H * Z + LBL + PAD)
        big = s.resize((CW, ROW_H * Z), Image.NEAREST)
        img.paste(big, (x, y))
        d.rectangle([x - 1, y - 1, x + CW, y + ROW_H * Z], outline=(200, 200, 195))
        d.text((x, y + ROW_H * Z + 6), "头梁 " + label[i], fill=(25, 25, 25), font=flbl)

img.save("band_ctx.png")
print(img.size)
