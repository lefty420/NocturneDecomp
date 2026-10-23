"""Tile art for the dev package. Usage: make_logos.py OUTDIR [ICON.png]
Uses ICON (a square picture) when given and present; otherwise draws a plain pale moon on a dark field."""
import sys, os
from PIL import Image, ImageDraw

out = sys.argv[1]
icon_path = sys.argv[2] if len(sys.argv) > 2 else None
os.makedirs(out, exist_ok=True)
BG = (11, 11, 20, 255)
icon = Image.open(icon_path).convert("RGBA") if icon_path and os.path.exists(icon_path) else None


def tile(name, w, h):
    im = Image.new("RGBA", (w, h), BG)
    if icon is not None:
        side = min(w, h)
        art = icon.resize((side, side), Image.LANCZOS)
        im.paste(art, ((w - side) // 2, (h - side) // 2))
    else:
        d = ImageDraw.Draw(im)
        r = min(w, h) * 0.32
        cx, cy = w / 2, h / 2
        d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(226, 224, 210, 255))
        d.ellipse((cx - r * 0.55, cy - r * 1.15, cx + r * 1.35, cy + r * 0.85), fill=BG)
    im.save(os.path.join(out, name))


for n, w, h in [("StoreLogo.png", 50, 50), ("Square44x44Logo.png", 44, 44), ("Square150x150Logo.png", 150, 150),
                ("Wide310x150Logo.png", 310, 150), ("SplashScreen.png", 620, 300)]:
    tile(n, w, h)
