# Generate HarmonyOS layered app icon from the TheXTech logo's blue mushroom.
from PIL import Image

SRC = r"C:/Users/Administrator/AppData/Local/Temp/thextech-logo.png"
OUT_DIRS = [
    r"E:/TheXTechOH/hap/AppScope/resources/base/media",
    r"E:/TheXTechOH/hap/entry/src/main/resources/base/media",
]
BG_COLOR = (18, 26, 48, 255)      # deep navy behind the mushroom
ICON_SIZE = 1024
CONTENT_BOX = 640                  # mushroom max dimension inside the canvas

im = Image.open(SRC).convert("RGBA")

# Mushroom region located by saturated-blue pixel analysis: (139,16)-(408,233).
crop = im.crop((133, 10, 414, 239))
bbox = crop.getbbox()             # tight alpha crop
mushroom = crop.crop(bbox)
print("mushroom size:", mushroom.size)

def place_center(mush, size, box):
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    scale = min(box / mush.width, box / mush.height)
    w = max(1, round(mush.width * scale))
    h = max(1, round(mush.height * scale))
    m = mush.resize((w, h), Image.LANCZOS)
    canvas.paste(m, ((size - w) // 2, (size - h) // 2), m)
    return canvas

foreground = place_center(mushroom, ICON_SIZE, CONTENT_BOX)
background = Image.new("RGBA", (ICON_SIZE, ICON_SIZE), BG_COLOR)

for d in OUT_DIRS:
    foreground.save(d + "/foreground.png")
    background.save(d + "/background.png")
    foreground.save(d + "/startIcon.png")
    print("wrote", d)

# Preview strip for a quick visual check
prev = Image.new("RGBA", (ICON_SIZE * 2 + 24, ICON_SIZE), (240, 240, 240, 255))
prev.paste(background, (0, 0))
prev.paste(foreground, (ICON_SIZE + 24, 0), foreground)
prev = prev.resize((824, 256), Image.LANCZOS)
prev.save(r"C:/Users/Administrator/AppData/Local/Temp/icon_preview.png")
print("preview written")
