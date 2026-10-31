# Recording scene for the README: a 16:9 gradient backdrop in the middle of the main screen (it hides your own
# windows there and catches stray clicks) with two dummy app windows to pile sand on. Starts the Desktop Powder Toy
# with a copy of your settings whose play area is that same middle part. Esc, or closing Notes or Music, ends it.
import ctypes, json, os, subprocess, tempfile, tkinter as tk
from ctypes import wintypes

u = ctypes.windll.user32
u.GetWindowLongPtrW.restype = ctypes.c_ssize_t
u.SetProcessDPIAware()  # real pixels
exe = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build-rs', 'powder.exe')
sw, sh = u.GetSystemMetrics(0), u.GetSystemMetrics(1)  # the main screen
bw, bh = min(sw, sh * 16 // 9), min(sh, sw * 9 // 16)  # the 16:9 middle
bx, by = (sw - bw) // 2, (sh - bh) // 2

r = tk.Tk(); r.overrideredirect(True); r.geometry(f'{bw}x{bh}+{bx}+{by}')
c = tk.Canvas(r, width=bw, height=bh, highlightthickness=0); c.pack()
for i in range(130):
    t = i / 129
    c.create_rectangle(0, bh * i // 130, bw, bh * (i + 1) // 130 + 1, width=0,
                       fill='#%02x%02x%02x' % (int(18 + 20 * t), int(32 + 70 * t), int(58 + 60 * t)))
r.update()
h = int(r.wm_frame(), 16)
u.SetWindowLongPtrW(h, -20, u.GetWindowLongPtrW(h, -20) | 0x80)  # no taskbar button (and so not a wall for the sand)
u.SetWindowPos(h, -1, 0, 0, 0, 0, 0x13)  # topmost: hides your own windows
r.bind_all('<Escape>', lambda e: r.destroy())

def app(title, x, y, w, h, text):
    t = tk.Toplevel(r); t.title(title); t.geometry(f'{w}x{h}+{bx + x}+{by + y}'); t.configure(bg='white'); t.attributes('-topmost', True)
    tk.Label(t, text=text, bg='white', fg='#333', font=('Segoe UI', 11), justify='left', anchor='nw').pack(fill='both', padx=12, pady=10)
    t.protocol('WM_DELETE_WINDOW', r.destroy)
    apps.append(t)
apps = []
app('Notes', bw * 30 // 100, bh * 55 // 100, 520, 300, 'Shopping list\n- milk\n- sand (lots)')
app('Music', bw * 58 // 100, bh * 35 // 100, 360, 200, 'Now playing\nFalling Sand - Lo-fi mix')

# TPT with a copy of your settings: play area = the middle, side window inside it
ddir = os.path.join(tempfile.gettempdir(), 'tpt-demo-scene')
os.makedirs(ddir, exist_ok=True)
try:
    prefs = json.load(open(os.path.join(os.environ['APPDATA'], 'The Powder Toy', 'powder.pref'), encoding='utf-8'))
except OSError:
    prefs = {}
prefs.pop('User', None)  # no sign-in in the copy
cell = 4 * max(1, int(prefs.get('Scale', 1)))  # screen pixels per sim cell
o = prefs.setdefault('Overlay', {})
o['PlayArea'] = {'X': bx // cell, 'Y': by // cell, 'W': bw // cell, 'H': bh // cell}
o['Input'] = 0  # Auto
o['Layer'] = 0  # on top
o.setdefault('Windows', {})['Torn'] = 0
o['Windows']['Palette'] = {'X': bx + bw - 760, 'Y': by + 60, 'W': 700, 'H': 700, 'Open': True}
json.dump(prefs, open(os.path.join(ddir, 'powder.pref'), 'w', encoding='utf-8'), indent='\t')
p = subprocess.Popen([exe, 'overlay:on', 'ddir', ddir], cwd=os.path.dirname(exe)) if os.path.exists(exe) else None

# Every second, stack: backdrop, Notes and Music, then TPT, over any always-on-top windows of yours
EnumProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
def restack():
    for w in [h] + [int(a.wm_frame(), 16) for a in apps]: u.SetWindowPos(w, -1, 0, 0, 0, 0, 0x13)
    def each(hwnd, _):
        pid = wintypes.DWORD(); u.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if p and pid.value == p.pid and u.IsWindowVisible(hwnd): u.SetWindowPos(hwnd, -1, 0, 0, 0, 0, 0x13)
        return True
    u.EnumWindows(EnumProc(each), 0)
    r.after(1000, restack)
r.after(1000, restack)
r.mainloop()
if p: p.terminate()
