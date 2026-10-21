"""Draws icon.jpg (256x256, the hbmenu icon): the BrowseNX compass on a dark tile.

Run from the repo root: python tools/make_icon.py
"""
import math

from PIL import Image, ImageDraw, ImageFont

S = 4  # supersample, then shrink for smooth edges
W = 256 * S
img = Image.new("RGB", (W, W), (14, 17, 22))
d = ImageDraw.Draw(img)

# soft vertical glow behind the dial
for y in range(W):
    t = y / W
    c = (int(14 + 20 * (1 - t)), int(17 + 28 * (1 - t)), int(22 + 60 * (1 - t)))
    d.line([(0, y), (W, y)], fill=c)

cx, cy, r = W / 2, W * 0.43, W * 0.30
d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(61, 123, 255))
rr = r * 0.82
d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], outline=(170, 196, 255), width=int(r * 0.05))
for i in range(12):  # tick marks
    a = i * math.pi / 6
    r0, r1 = rr * (0.80 if i % 3 == 0 else 0.88), rr * 0.97
    d.line([(cx + r0 * math.sin(a), cy - r0 * math.cos(a)), (cx + r1 * math.sin(a), cy - r1 * math.cos(a))],
           fill=(220, 230, 255), width=int(r * 0.035))

c45 = math.sqrt(0.5)
n, w = r * 0.62, r * 0.17
tip, tail = (cx + n * c45, cy - n * c45), (cx - n * c45, cy + n * c45)
left, right = (cx - w * c45, cy - w * c45), (cx + w * c45, cy + w * c45)
d.polygon([tip, right, left], fill=(255, 77, 77))
d.polygon([tail, left, right], fill=(255, 255, 255))
d.ellipse([cx - r * 0.07, cy - r * 0.07, cx + r * 0.07, cy + r * 0.07], fill=(14, 17, 22))

try:
    font = ImageFont.truetype("romfs/fonts/Inter-Bold.ttf", int(W * 0.13))
except OSError:
    font = ImageFont.load_default()
text = "BrowseNX"
tw = d.textlength(text, font=font)
d.text(((W - tw) / 2, W * 0.79), text, font=font, fill=(240, 246, 252))

img.resize((256, 256), Image.LANCZOS).save("icon.jpg", quality=95)
print("wrote icon.jpg")
