# Original icon (no official logos or characters): a ringed planet in a deep-blue sky over green hills,
# with a small delta-shaped flyer and its exhaust trail.   python3 mkicons.py  ->  arcade.png, arcade.ico
from PIL import Image, ImageDraw, ImageFilter
import random
S = 1024
def grad(top, bot):
    im = Image.new('RGB', (S, S)); d = ImageDraw.Draw(im)
    for y in range(S):
        t = y / (S - 1); d.line([(0, y), (S, y)], fill=tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3)))
    return im
im = grad((10, 18, 70), (70, 150, 235)); d = ImageDraw.Draw(im)
random.seed(11)
for _ in range(90):
    x, y = random.randrange(S), random.randrange(int(S * 0.5)); r = random.choice((2, 2, 3, 4))
    d.ellipse([x - r, y - r, x + r, y + r], fill=(255, 255, 255))
# planet with a ring (ring drawn behind, planet, then the front half of the ring)
ring = Image.new('RGBA', (S, S), (0, 0, 0, 0)); rd = ImageDraw.Draw(ring)
rd.ellipse([330, 250, 990, 430], outline=(255, 214, 120, 255), width=26)
ring = ring.rotate(-18, center=(660, 340), resample=Image.BICUBIC)
im.paste(ring, (0, 0), ring)
d = ImageDraw.Draw(im)
d.ellipse([500, 180, 820, 500], fill=(240, 120, 60))
d.chord([500, 180, 820, 500], 200, 340, fill=(250, 160, 90))
front = Image.new('RGBA', (S, S), (0, 0, 0, 0)); fd = ImageDraw.Draw(front)
fd.arc([330, 250, 990, 430], 0, 180, fill=(255, 214, 120, 255), width=26)
front = front.rotate(-18, center=(660, 340), resample=Image.BICUBIC)
im.paste(front, (0, 0), front)
d = ImageDraw.Draw(im)
d.polygon([(0, 760), (200, 690), (430, 750), (660, 680), (880, 740), (1024, 700), (1024, 1024), (0, 1024)], fill=(40, 150, 80))
d.polygon([(0, 870), (260, 810), (540, 880), (800, 820), (1024, 870), (1024, 1024), (0, 1024)], fill=(24, 110, 60))
# flyer and trail
d.polygon([(150, 640), (430, 520), (330, 600), (360, 680)], fill=(235, 240, 250))
d.polygon([(330, 600), (430, 520), (360, 680)], fill=(190, 200, 220))
d.line([(40, 690), (190, 632)], fill=(255, 240, 160), width=14)
mask = Image.new('L', (S, S), 0); ImageDraw.Draw(mask).rounded_rectangle([24, 24, S - 24, S - 24], radius=170, fill=255)
out = Image.new('RGBA', (S, S), (0, 0, 0, 0)); out.paste(im, (0, 0), mask)
ImageDraw.Draw(out).rounded_rectangle([24, 24, S - 24, S - 24], radius=170, outline=(255, 205, 80, 255), width=22)
out.resize((256, 256), Image.LANCZOS).save('arcade.png')
out.save('arcade.ico', sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
