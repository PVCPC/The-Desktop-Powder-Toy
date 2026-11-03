# Builds luna.png: the Windows XP Luna theme's own images (from luna.msstyles, via github.com/listumps/wine_themes),
# Blue, Olive Green (Homestead) and Silver (Metallic) side by side, each part at the same place in each colour's block
# (blocks of columns no taller than 384 px: TDPT's PNG reader refuses anything bigger than the stock window).
# Usage: luna.py <dir with the msstyles' bitmaps, as NAME_BMP.bmp> <out.png>; prints the table for OverlayWindows.cpp.
import struct, sys
from PIL import Image

PARTS = ['FRAMECAPTION', 'FRAMELEFT', 'FRAMERIGHT', 'FRAMEBOTTOM', 'CAPTIONBUTTON', 'CLOSEBUTTON', 'CLOSEGLYPH',
         'MINIMIZEGLYPH', 'MAXIMIZEGLYPH', 'RESTOREGLYPH', 'BUTTON', 'CHECKBOX13', 'RADIOBUTTON13', 'GROUPBOX', 'TABITEM',
         'TABPANEEDGE']
COLOURS = ['BLUE', 'HOMESTEAD', 'METALLIC']


def load(path):
    b = open(path, 'rb').read()
    w, h = struct.unpack_from('<ii', b, 18)
    if struct.unpack_from('<H', b, 28)[0] == 32:  # straight alpha, bottom-up BGRA (PIL drops the alpha)
        px = struct.unpack_from('<I', b, 10)[0]
        im = Image.frombuffer('RGBA', (w, abs(h)), b[px:px + w * abs(h) * 4], 'raw', 'BGRA', 0, -1 if h > 0 else 1)
        if not any(im.getchannel('A').get_flattened_data()):
            im.putalpha(255)
        return im
    im = Image.open(path).convert('RGBA')  # magenta is see-through
    im.putdata([(0, 0, 0, 0) if p[:3] == (255, 0, 255) else p for p in im.get_flattened_data()])
    return im


ims = {(c, p): load(f'{sys.argv[1]}/{c}_{p}_BMP.bmp') for c in COLOURS for p in PARTS}
places, x, y, colw, H = [], 0, 0, 0, 384
for p in PARTS:
    w, h = ims[COLOURS[0], p].size
    if y + h > H:
        x, y, colw = x + colw, 0, 0
    places.append((x, y))
    colw, y = max(colw, w), y + h
bw = x + colw
out = Image.new('RGBA', (bw * len(COLOURS), H))
for p, (x, y) in zip(PARTS, places):
    w, h = ims[COLOURS[0], p].size
    for i, c in enumerate(COLOURS):
        out.paste(ims[c, p], (i * bw + x, y))
    print(f'{{ {x}, {y}, {w}, {h} }}, // {p}')
print(f'block width {bw}')
out.save(sys.argv[2], optimize=True)
