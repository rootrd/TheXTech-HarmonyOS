# Launch wallpaper v2: recognizable Mario scene
# sky-hills bg + brick ground + floating ?-blocks/bricks + full TheXTech logo
from PIL import Image

W, H = 2848, 1276
GD = r"E:/TheXTechOH/hap/entry/src/main/resources/resfile/gamedata/graphics"
BG = GD + "/background2/background2-2.png"
BRICK = GD + "/block/block-1.png"
QBLOCK = GD + "/block/block-90.png"
LOGO = r"C:/Users/Administrator/AppData/Local/Temp/thextech-logo.png"
OUT = r"E:/TheXTechOH/hap/entry/src/main/resources/base/media/launch_bg.jpg"

# 1) sky + hills base
bg0 = Image.open(BG).convert("RGB")
tile = bg0.resize((int(bg0.width * H / bg0.height), H), Image.LANCZOS)
canvas = Image.new("RGB", (W, H))
x = 0
while x < W:
    canvas.paste(tile.crop((0, 0, min(tile.width, W - x), H)), (x, 0))
    x += tile.width

def paste_rgba(canvas, img, pos):
    canvas.paste(img, pos, img)

# 2) brick ground strip at the bottom (2 rows of 3x bricks)
brick = Image.open(BRICK).convert("RGBA").resize((96, 96), Image.NEAREST)
gy = H - 192
for row in range(2):
    bx = 0
    while bx < W:
        paste_rgba(canvas, brick, (bx, gy + row * 96))
        bx += 96

# 3) floating decoration rows of bricks + question blocks
q = Image.open(QBLOCK).convert("RGBA").resize((96, 96), Image.NEAREST)

def platform(items, x0, y0):
    x = x0
    for kind in items:
        paste_rgba(canvas, q if kind == "q" else brick, (x, y0))
        x += 96

platform(["b", "b", "q", "b"], 170, 830)
platform(["b", "q", "b", "b", "q"], W - 170 - 5 * 96, 640)
platform(["q", "b", "b"], W // 2 - 144, 470)

# 4) full logo (mushroom + TheXTech text), drop shadow, upper-center
logo = Image.open(LOGO).convert("RGBA")
logo = logo.crop(logo.getbbox())
lw = 1150
lh = int(logo.height * lw / logo.width)
logo = logo.resize((lw, lh), Image.LANCZOS)
# soft shadow
sh = Image.new("RGBA", (lw + 40, lh + 40), (0, 0, 0, 0))
lm = logo.split()[3].point(lambda a: int(a * 0.55))
black = Image.new("RGBA", logo.size, (0, 0, 0, 255))
black.putalpha(lm)
sh.paste(black, (22, 22), black)
paste_rgba(canvas, sh, ((W - lw) // 2 - 20, 120 - 20))
paste_rgba(canvas, logo, ((W - lw) // 2, 120))

# 5) gentle dark gradient only at the very bottom (button readability)
from PIL import ImageDraw
overlay = Image.new("L", (1, H), 0)
for y in range(H):
    t = y / (H - 1)
    v = int(60 * t) if t > 0.55 else 0
    overlay.putpixel((0, y), v)
dark = Image.new("RGB", (W, H), (8, 12, 28))
canvas = Image.composite(dark, canvas, overlay.resize((W, H)))

canvas.save(OUT, quality=85)
print("saved", OUT, canvas.size)
