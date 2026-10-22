"""Lays the frames preview.sh rendered out as a contact sheet, with times."""
import glob, os, sys
from PIL import Image, ImageDraw
work, W, H, every, start, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]), sys.argv[6]
files = sorted(glob.glob(os.path.join(work, 'f*.rgb')))
C = max(3, min(10, 600 // W))
cols = max(1, min(5, 1800 // (W * C + 8)))
rows = (len(files) + cols - 1) // cols
sheet = Image.new('RGB', (cols * (W * C + 8), rows * (H * C + 20)), (40, 40, 40))
d = ImageDraw.Draw(sheet)
s = lambda v: int(255 * (v / 255) ** 0.55)
for k, fn in enumerate(files):
    px = open(fn, 'rb').read()
    im = Image.new('RGB', (W * C, H * C))
    dr = ImageDraw.Draw(im)
    for y in range(H):
        for x in range(W):
            i = (y * W + x) * 3
            c = (s(px[i]), s(px[i + 1]), s(px[i + 2]))
            dr.ellipse([x * C + 1, y * C + 1, x * C + C - 2, y * C + C - 2], fill=c if any(c) else (14, 14, 14))
    ox, oy = (k % cols) * (W * C + 8), (k // cols) * (H * C + 20)
    sheet.paste(im, (ox, oy + 16))
    d.text((ox + 2, oy + 2), f"{(start + k * every) / 1000:.1f}s", fill=(200, 200, 200))
sheet.save(out)
