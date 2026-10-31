# Recording scene for the README: two dummy app windows to pile sand on, and the Desktop Powder Toy started with a
# copy of your settings whose play area is the 16:9 middle of the main screen (record that part). Your desktop,
# icons and taskbar stay as they are: minimise anything you don't want in the shot. Esc in Notes or Music, or
# closing either, ends it.
import ctypes, json, os, subprocess, tempfile, tkinter as tk

u = ctypes.windll.user32
u.SetProcessDPIAware()  # real pixels
exe = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build-rs', 'powder.exe')
sw, sh = u.GetSystemMetrics(0), u.GetSystemMetrics(1)  # the main screen
bw, bh = min(sw, sh * 16 // 9), min(sh, sw * 9 // 16)  # the 16:9 middle
bx, by = (sw - bw) // 2, (sh - bh) // 2

r = tk.Tk(); r.withdraw()
def app(title, x, y, w, h, text):
    t = tk.Toplevel(r); t.title(title); t.geometry(f'{w}x{h}+{bx + x}+{by + y}'); t.configure(bg='white')
    tk.Label(t, text=text, bg='white', fg='#333', font=('Segoe UI', 11), justify='left', anchor='nw').pack(fill='both', padx=12, pady=10)
    t.protocol('WM_DELETE_WINDOW', r.destroy)
    t.bind('<Escape>', lambda e: r.destroy())
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
r.mainloop()
if p: p.terminate()
