# Recording scene for the README: a plain gradient backdrop over the real desktop (click-through, so it isn't a wall)
# and two dummy app windows to pile sand on. Starts the Desktop Powder Toy on top. Close Notes or Music to end it.
import ctypes, os, subprocess, tkinter as tk
from ctypes import wintypes

u = ctypes.windll.user32
u.GetWindowLongPtrW.restype = ctypes.c_ssize_t
u.SetProcessDPIAware()  # real pixels, so the backdrop covers the whole screen
exe = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build-rs', 'powder.exe')
sw, sh = u.GetSystemMetrics(0), u.GetSystemMetrics(1)  # the main screen

r = tk.Tk(); r.overrideredirect(True); r.geometry(f'{sw}x{sh}+0+0')
c = tk.Canvas(r, width=sw, height=sh, highlightthickness=0); c.pack()
for i in range(130):
    t = i / 129
    c.create_rectangle(0, sh * i // 130, sw, sh * (i + 1) // 130 + 1, width=0,
                       fill='#%02x%02x%02x' % (int(18 + 20 * t), int(32 + 70 * t), int(58 + 60 * t)))
r.update()
h = int(r.wm_frame(), 16)
u.SetWindowLongPtrW(h, -20, u.GetWindowLongPtrW(h, -20) | 0x80000 | 0x20 | 0x80)  # layered, click-through, no taskbar button
u.SetLayeredWindowAttributes(h, 0, 255, 2)
u.SetWindowPos(h, -1, 0, 0, 0, 0, 0x13)  # topmost: hides your own windows

def app(title, geo, text):
    t = tk.Toplevel(r); t.title(title); t.geometry(geo); t.configure(bg='white'); t.attributes('-topmost', True)
    tk.Label(t, text=text, bg='white', fg='#333', font=('Segoe UI', 11), justify='left', anchor='nw').pack(fill='both', padx=12, pady=10)
    t.protocol('WM_DELETE_WINDOW', r.destroy)
app('Notes', f'520x300+{sw * 55 // 100}+{sh * 50 // 100}', 'Shopping list\n- milk\n- sand (lots)')
app('Music', f'360x200+{sw * 72 // 100}+{sh * 30 // 100}', 'Now playing\nFalling Sand - Lo-fi mix')

# TPT's windows over the backdrop (it may already be running; otherwise start it)
EnumProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
def raise_tpt():
    pids = {p.pid} if p else set()
    def each(hwnd, _):
        pid = wintypes.DWORD(); u.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if u.IsWindowVisible(hwnd) and pid.value in pids: u.SetWindowPos(hwnd, -1, 0, 0, 0, 0, 0x13)
        return True
    u.EnumWindows(EnumProc(each), 0)
    r.after(2000, raise_tpt)
p = subprocess.Popen([exe, 'overlay:on'], cwd=os.path.dirname(exe)) if os.path.exists(exe) else None
r.after(3000, raise_tpt)
r.mainloop()
