"""Placeholder tile art for the dev package: a pale moon on a dark field. Usage: make_logos.py OUTDIR"""
import sys, os
from PIL import Image, ImageDraw
out = sys.argv[1]; os.makedirs(out, exist_ok=True)
def tile(name, w, h):
    im = Image.new("RGBA", (w, h), (11, 11, 20, 255)); d = ImageDraw.Draw(im)
    r = min(w, h) * 0.32; cx, cy = w / 2, h / 2
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(226, 224, 210, 255))
    d.ellipse((cx - r * 0.55, cy - r * 1.15, cx + r * 1.35, cy + r * 0.85), fill=(11, 11, 20, 255))
    im.save(os.path.join(out, name))
for n, w, h in [("StoreLogo.png", 50, 50), ("Square44x44Logo.png", 44, 44), ("Square150x150Logo.png", 150, 150),
                ("Wide310x150Logo.png", 310, 150), ("SplashScreen.png", 620, 300)]:
    tile(n, w, h)
