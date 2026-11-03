# Builds skin_<look>.png and src/OverlaySkins.h: the controls of a look, from a very faithful recreation of it (the
# B00merang Project's GTK themes, github.com/B00merang-Project, GPL-3). Each state of each part is its own image in the
# PNG (no bigger than 612x384: TDPT's PNG reader refuses anything bigger than the stock window), with the GTK theme's
# sizing border (left, right, top, bottom, kept unstretched at the edges; one number: all four).
# Usage: skins.py <dir with the theme repos checked out> <resources dir> <OverlaySkins.h>
import sys
from PIL import Image

# Parts and their states, as OverlayWindows.cpp's SkinPart lists them: Button normal, hot, pressed; Check and Radio
# off, off-hot, on, on-hot; Tab off, hot, on; caption buttons normal, hot, pressed.
PARTS = ['Button', 'Check', 'Radio', 'Tab', 'Close', 'Min', 'Max']
AERO = lambda repo: {  # Vista and 7 share their GTK theme's layout
    'Button': ([f'{repo}/gtk-2.0/Buttons/button-{s}.png' for s in ('normal', 'prelight', 'pressed')], 4),
    'Check': ([f'{repo}/gtk-2.0/Check-Radio/check{i}.png' for i in (1, 3, 2, 4)], 0),
    'Radio': ([f'{repo}/gtk-2.0/Check-Radio/option{i}.png' for i in (1, 1, 2, 4)], 0),
    'Tab': ([f'{repo}/gtk-2.0/Tabs/tab-top{s}.png' for s in ('', '', '-active')], 5),
}
LOOKS = {
    'vista': AERO('Windows-Vista'),
    # (its xfwm4 caption buttons are 7 Basic's, all one width: the glass look keeps its own)
    'seven': dict(AERO('Windows-7'), Radio=([f'Windows-7/gtk-2.0/Check-Radio/option{i}.png' for i in (1, 3, 2, 4)], 0)),
    'eight': {
        'Button': ([f'Windows-8.1/gtk-2.0/Buttons/button-{s}.png' for s in ('default', 'prelight', 'pressed')], 4),
        'Check': ([f'Windows-8.1/gtk-2.0/Check-Radio/checkbox-{s}.png' for s in ('unchecked', 'unchecked-over', 'checked', 'checked-over')], 0),
        'Radio': ([f'Windows-8.1/gtk-2.0/Check-Radio/radio-{s}.png' for s in ('unselected', 'unselected-over', 'selected', 'selected-over')], 0),
        'Tab': ([f'Windows-8.1/gtk-2.0/Tabs/tab-top{s}.png' for s in ('', '', '-active')], 3),
    },
    # (its buttons are the Settings app's, not a desktop program's: the look keeps its own)
    'ten': {
        'Check': ([f'Windows-10/gtk-2.0/assets/checkbox-{s}.png' for s in ('unchecked', 'unchecked-over', 'checked', 'checked-over')], 0),
        'Radio': ([f'Windows-10/gtk-2.0/assets/radio-{s}.png' for s in ('unchecked', 'unchecked-over', 'checked', 'checked-over')], 0),
    },
    # Macs had no hover: their normal state twice
    'system6': {  # System-4: the same black and white as System 1 to 6
        'Button': ([f'System-4/gtk-2.0/assets/button-{s}.png' for s in ('default', 'default', 'pressed')], 4),
        'Check': ([f'System-4/gtk-2.0/assets/checkbox-{s}.png' for s in ('unchecked', 'unchecked', 'checked', 'checked')], 0),
        'Radio': ([f'System-4/gtk-2.0/assets/radio-{s}.png' for s in ('unchecked', 'unchecked', 'checked', 'checked')], 0),
    },
    'mac9': {  # Platinum
        'Button': ([f'Mac-OS-9/gtk-2.0/button{s}alpha.png' for s in (12, 12, 11)], 9),
        'Check': ([f'Mac-OS-9/gtk-2.0/check{i}.png' for i in (1, 1, 2, 2)], 0),
        'Radio': ([f'Mac-OS-9/gtk-2.0/option{i}.png' for i in (1, 1, 2, 2)], 0),
        'Tab': ([f'Mac-OS-9/gtk-2.0/notebook{i}.png' for i in (3, 3, 2)], 10),
    },
    'aqua': {  # Leopard's Aqua
        'Button': ([f'OS-X-Leopard/gtk-2.0/Buttons/button-{s}.png' for s in ('normal', 'normal', 'pressed')], (15, 15, 12, 12)),
        'Check': ([f'OS-X-Leopard/gtk-2.0/Check-Radio/check{i}.png' for i in (1, 1, 2, 2)], 0),
        'Radio': ([f'OS-X-Leopard/gtk-2.0/Check-Radio/option{i}.png' for i in (1, 1, 2, 2)], 0),
        'Tab': ([f'OS-X-Leopard/gtk-2.0/Tabs/tab-top{s}.png' for s in ('', '', '-active')], (6, 6, 9, 0)),
    },
    'lion': {  # Mavericks (its buttons are GTK 3 only: the look keeps its own)
        'Check': ([f'OS-X-Mavericks/gtk-2.0/assets/l-checkbox-{s}-18.png' for s in ('unchecked', 'unchecked', 'checked', 'checked')], 0),
        'Radio': ([f'OS-X-Mavericks/gtk-2.0/assets/l-radio-{s}-18.png' for s in ('unselected', 'unselected', 'selected', 'selected')], 0),
    },
    **{look: {
        'Button': ([f'{repo}/gtk-2.0/assets/button-{s}.png' for s in ('default', 'default', 'pressed')], 4),
        'Check': ([f'{repo}/gtk-2.0/assets/checkbox-{s}.png' for s in ('unchecked', 'unchecked', 'checked', 'checked')], 0),
        'Radio': ([f'{repo}/gtk-2.0/assets/radio-{s}.png' for s in ('unchecked', 'unchecked', 'checked', 'checked')], 0),
    } for look, repo in (('yosemite', 'OS-X-Yosemite'), ('macos', 'macOS'))},
}

src, res, header = sys.argv[1:4]
out = ['// Made by resources/skins.py: where each part\'s states are in skin_<look>.png, with their sizing borders.',
       '// x, y, w, h, then the border left, right, top, bottom; w 0: the look draws that part itself.', '#pragma once', '']
for look, parts in LOOKS.items():
    ims = {(p, i): Image.open(f'{src}/{f}').convert('RGBA') for p, (fs, _) in parts.items() for i, f in enumerate(fs)}
    # shelves, tallest first
    order, place, x, y, shelf, W, H = sorted(ims, key=lambda k: -ims[k].height), {}, 0, 0, 0, 612, 384
    for k in order:
        w, h = ims[k].size
        if x + w > W:
            x, y, shelf = 0, y + shelf, 0
        place[k], x, shelf = (x, y), x + w, max(shelf, h)
    assert y + shelf <= H, look
    atlas = Image.new('RGBA', (max(px + ims[k].width for k, (px, _) in place.items()), y + shelf))
    for k, p in place.items():
        atlas.paste(ims[k], p)
    atlas.save(f'{res}/skin_{look}.png', optimize=True)
    out.append(f'constexpr int skin_{look}_parts[][4][8] = {{')
    for p in PARTS:
        fs, b = parts.get(p, ([], 0))
        b = ', '.join(map(str, (b,) * 4 if isinstance(b, int) else b))
        cells = [f'{{ {place[p, i][0]}, {place[p, i][1]}, {ims[p, i].width}, {ims[p, i].height}, {b} }}' for i in range(len(fs))]
        out.append(f'\t{{ {", ".join(cells) or "{}"} }}, // {p}')
    out.append('};')
open(header, 'w', newline='\n').write('\n'.join(out) + '\n')
