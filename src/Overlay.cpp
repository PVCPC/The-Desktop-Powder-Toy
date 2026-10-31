#include "Overlay.h"
#include "SimulationConfig.h"
#include "simulation/ElementDefs.h"
#include <algorithm>
#include "OverlayWindows.h"
#include "SimulationConfig.h"
#include "prefs/GlobalPrefs.h"
#include "gui/interface/Engine.h"
#include "gui/interface/Window.h"
#include <vector>
#include <cstdio>
#include <utility>
#include <algorithm>
#ifdef _WIN32
# include <SDL_syswm.h>
# define WIN32_LEAN_AND_MEAN
# undef _WIN32_WINNT
# define _WIN32_WINNT 0x0601 // SHQueryUserNotificationState (the build targets XP)
# include <windows.h>
# include <shellapi.h>
#endif

bool overlayMode = false;
bool overlayWindowed = false;
bool overlayAutoPass = true;

#ifdef _WIN32
namespace
{
	HWND hwnd = nullptr;
	OverlayInput input = OverlayInput::Auto;
	bool fullScreenApp = false; // a full-screen app or game is in front: clicks pass through whatever the mode
	OverlayLayer layer = OverlayLayer::OnTop;

	// A global hotkey ("Ctrl+Alt+O"), polled each frame so it works while the window is unfocused.
	struct Hotkey
	{
		bool ctrl = false, alt = false, shift = false, win = false;
		int vk = 0;
		bool wasDown = false;

		static Hotkey Parse(ByteString text)
		{
			Hotkey k;
			for (auto &part : text.PartitionBy('+'))
			{
				auto p = part.ToUpper();
				if (p == "CTRL") k.ctrl = true;
				else if (p == "ALT") k.alt = true;
				else if (p == "SHIFT") k.shift = true;
				else if (p == "WIN") k.win = true;
				else if (p.size() == 1 && isalnum((unsigned char)p[0])) k.vk = p[0];
				else if (p.size() >= 2 && p[0] == 'F') k.vk = VK_F1 - 1 + atoi(p.c_str() + 1);
			}
			return k;
		}

		static bool Down(int vk)
		{
			return GetAsyncKeyState(vk) & 0x8000;
		}

		// True once per press.
		bool Pressed()
		{
			bool down = vk && Down(vk) && ctrl == Down(VK_CONTROL) && alt == Down(VK_MENU) && shift == Down(VK_SHIFT) &&
			            win == (Down(VK_LWIN) || Down(VK_RWIN));
			bool pressed = down && !wasDown;
			wasDown = down;
			return pressed;
		}
	};
	Hotkey cycleInput, passThroughKey, lockedKey, paletteKey;

	void ApplyPassThrough()
	{
		bool pass = input == OverlayInput::PassThrough || fullScreenApp || layer == OverlayLayer::BehindIcons;
		OverlayWindows::SetPassThrough(pass);
		auto ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
		ex = pass ? ex | WS_EX_TRANSPARENT : ex & ~LONG_PTR(WS_EX_TRANSPARENT);
		SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
	}

	void SetInput(OverlayInput newInput, bool announce = true)
	{
		input = newInput;
		ApplyPassThrough();
		if (announce)
		{
			static const char *names[] = { "Input: Auto", "Input: Pass-through", "Input: Locked to TPT" };
			OverlayWindows::Notify(names[int(input)]);
		}
		GlobalPrefs::Ref().Set("Overlay.Input", int(input));
	}

	// The desktop window the wallpaper is drawn in, under the icons; parenting to it puts us behind the icons (the
	// trick live-wallpaper apps use). Returns the parent and the window to sit just under, if any.
	std::pair<HWND, HWND> WallpaperLayer()
	{
		HWND progman = FindWindowW(L"Progman", nullptr);
		SendMessageTimeoutW(progman, 0x052C, 0xD, 0x1, SMTO_NORMAL, 1000, nullptr); // makes Explorer split off a WorkerW
		HWND worker = nullptr;
		EnumWindows([](HWND top, LPARAM out) -> BOOL {
			if (FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr))
			{
				*(HWND *)out = FindWindowExW(nullptr, top, L"WorkerW", nullptr);
			}
			return TRUE;
		}, LPARAM(&worker));
		if (worker) return { worker, nullptr };                                       // up to Windows 11 23H2
		return { progman, FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr) }; // 24H2: under the icons' view
	}

	void ApplyLayer()
	{
		static RECT screen; // where the layer sits as a top-level window
		auto [desk, under] = layer == OverlayLayer::BehindIcons ? WallpaperLayer() : std::pair<HWND, HWND>{};
		auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
		if (desk && !GetParent(hwnd))
		{
			GetWindowRect(hwnd, &screen);
			SetWindowLongPtrW(hwnd, GWL_STYLE, (style | WS_CHILD) & ~LONG_PTR(WS_POPUP));
			SetParent(hwnd, desk);
		}
		else if (!desk && GetParent(hwnd))
		{
			SetWindowLongPtrW(hwnd, GWL_STYLE, (style & ~LONG_PTR(WS_CHILD)) | WS_POPUP); // before SetParent(nullptr)
			SetParent(hwnd, nullptr);
			SetWindowPos(hwnd, nullptr, screen.left, screen.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		}
		if (desk)
		{
			// Child coordinates are the desktop window's: put the layer back over this monitor.
			POINT origin{ screen.left, screen.top };
			ScreenToClient(desk, &origin);
			SetWindowPos(hwnd, under ? under : HWND_TOP, origin.x, origin.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
		}
		else if (layer == OverlayLayer::OnTop)
		{
			SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
		}
		else
		{
			SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
			SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}
		ApplyPassThrough();
	}

	// Under windows: clicking the layer brings it up; send it back down.
	void TickLayer()
	{
		static int frames = 0;
		if (layer == OverlayLayer::UnderWindows && !(++frames % 10) && GetWindow(hwnd, GW_HWNDNEXT))
		{
			// ponytail: anything after us in the z-order counts; fine as long as nothing else pins itself to the bottom
			HWND next = GetWindow(hwnd, GW_HWNDNEXT);
			bool onlyDesktop = true;
			for (; next; next = GetWindow(next, GW_HWNDNEXT))
				if (IsWindowVisible(next) && !IsIconic(next))
				{
					wchar_t cls[32];
					GetClassNameW(next, cls, 32);
					if (wcscmp(cls, L"Progman") && wcscmp(cls, L"WorkerW")) { onlyDesktop = false; break; }
				}
			if (!onlyDesktop) SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}
	}

	// Twice a second: is a full-screen app (a game, a video, a presentation) in front of the desktop?
	void TickFullScreen()
	{
		static int frames = 0;
		if (++frames % 30) return;
		bool now = false;
		QUERY_USER_NOTIFICATION_STATE state;
		if (overlayAutoPass && SHQueryUserNotificationState(&state) == S_OK)
		{
			DWORD pid = 0;
			GetWindowThreadProcessId(GetForegroundWindow(), &pid);
			now = (state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE) &&
			      pid != GetCurrentProcessId();
		}
		if (now == fullScreenApp) return;
		fullScreenApp = now;
		ApplyPassThrough();
		OverlayWindows::Notify(now ? "Full-screen app: clicks pass through" : "Full-screen app closed: clicks back to normal");
	}

	// A top-down 32-bit DIB the frame is scaled into, then handed to UpdateLayeredWindow.
	std::vector<uint32_t> argb;
	HDC memDC = nullptr;
	HBITMAP dib = nullptr;
	uint32_t *bits = nullptr;
	int dibW = 0, dibH = 0;
}
#endif

void OverlaySetLayer(OverlayLayer newLayer)
{
#ifdef _WIN32
	layer = newLayer;
	GlobalPrefs::Ref().Set("Overlay.Layer", int(layer));
	ApplyLayer();
#endif
}

OverlayLayer OverlayGetLayer()
{
#ifdef _WIN32
	return layer;
#else
	return OverlayLayer::OnTop;
#endif
}

void OverlaySetInput(OverlayInput newInput)
{
#ifdef _WIN32
	SetInput(newInput);
#else
	(void)newInput;
#endif
}

void OverlayLoadKeys()
{
#ifdef _WIN32
	auto &prefs = GlobalPrefs::Ref();
	cycleInput = Hotkey::Parse(prefs.Get("Overlay.Keys.CycleInput", ByteString("Ctrl+Alt+O")));
	passThroughKey = Hotkey::Parse(prefs.Get("Overlay.Keys.PassThrough", ByteString("Ctrl+Alt+P")));
	lockedKey = Hotkey::Parse(prefs.Get("Overlay.Keys.Locked", ByteString("Ctrl+Alt+L")));
	paletteKey = Hotkey::Parse(prefs.Get("Overlay.Keys.Palette", ByteString("Ctrl+Alt+E")));
#endif
}

OverlayInput OverlayGetInput()
{
#ifdef _WIN32
	return input;
#else
	return OverlayInput::Auto;
#endif
}

std::pair<Vec2<int>, int> OverlaySimSize(int scale)
{
#ifdef _WIN32
	Vec2<int> screen(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
	for (scale = std::max(scale, 1); ; scale++)
	{
		auto cells = (screen / scale - Vec2(BARSIZE, MENUSIZE)) / CELL;
		// pmap holds the particle index above PMAPBITS
		if (int64_t(cells.X) * cells.Y * CELL * CELL <= (int64_t(1) << (31 - PMAPBITS)))
		{
			return { cells, scale };
		}
	}
#else
	return { CELLS, scale };
#endif
}

void ApplyOverlay(SDL_Window *window)
{
#ifdef _WIN32
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	if (!SDL_GetWindowWMInfo(window, &info))
	{
		return;
	}
	hwnd = info.info.win.window;
	if (overlayWindowed)
	{
		OverlayLoadKeys();
		OverlayWindows::Init(hwnd);
		return;
	}
	SetWindowLongPtrW(hwnd, GWL_EXSTYLE, GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TOOLWINDOW);
	// SDL_WINDOW_ALWAYS_ON_TOP alone doesn't stick once the style changes above; set it on the window itself.
	SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	auto &prefs = GlobalPrefs::Ref();
	OverlayLoadKeys();
	OverlayWindows::Init(hwnd);
	overlayAutoPass = prefs.Get("Overlay.AutoPassThrough", true);
	SetInput(prefs.Get("Overlay.Input", OverlayInput::Count, OverlayInput::Auto), false);
	layer = prefs.Get("Overlay.Layer", OverlayLayer::Count, OverlayLayer::OnTop);
	if (layer != OverlayLayer::OnTop) ApplyLayer();
#else
	(void)window; // ponytail: Windows only; X11/Wayland/macOS each need their own shape/input-region code
#endif
}

void OverlayTickUI()
{
#ifdef _WIN32
	if (OverlayWindows::RecordingKey())
	{
		// the keys being recorded must not also fire
	}
	else if (cycleInput.Pressed())
	{
		SetInput(OverlayInput((int(input) + 1) % int(OverlayInput::Count)));
	}
	else if (passThroughKey.Pressed())
	{
		SetInput(input == OverlayInput::PassThrough ? OverlayInput::Auto : OverlayInput::PassThrough);
	}
	else if (lockedKey.Pressed())
	{
		SetInput(input == OverlayInput::Locked ? OverlayInput::Auto : OverlayInput::Locked);
	}
	else if (paletteKey.Pressed())
	{
		OverlayWindows::TogglePalette();
	}
	OverlayWindows::Tick();
#endif
}

void OverlayPresent(SDL_Window *window, const uint32_t *vid, int w, int h)
{
#ifdef _WIN32
	if (!hwnd)
	{
		return;
	}
	TickFullScreen();
	TickLayer();
	OverlayTickUI();

	int ww, wh;
	SDL_GetWindowSize(window, &ww, &wh);
	if (ww != dibW || wh != dibH)
	{
		if (dib) DeleteObject(dib);
		if (!memDC) memDC = CreateCompatibleDC(nullptr);
		BITMAPINFO bi{};
		bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
		bi.bmiHeader.biWidth = ww;
		bi.bmiHeader.biHeight = -wh;
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;
		dib = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, (void **)&bits, nullptr, 0);
		SelectObject(memDC, dib);
		dibW = ww;
		dibH = wh;
	}
	// Black is alpha 0: see-through, and clicks on it reach the desktop. Locked mode gives it alpha 1 instead:
	// still invisible, but it catches clicks. Black particles stay solid, and hidden menu bars are always clear.
	uint32_t empty = input == OverlayInput::Locked ? 0x01000000 : 0;
	argb.resize(size_t(w) * h);
	for (int i = 0; i < w * h; i++)
	{
		auto c = vid[i] & 0xFFFFFF;
		argb[i] = c ? c | 0xFF000000 : empty;
	}
	if (w == WINDOWW && h == WINDOWH)
	{
		OverlayWindows::ApplySimMasks(argb.data(), w);
		auto *top = ui::Engine::Ref().GetWindow();
		// TPT's menus: the side bar (x >= XRES), the tool strip under the sim and the bottom button row (which would
		// sit on the taskbar).
		static uint32_t lastUse = 0;
		static bool wasShown = false;
		bool shown = !(OverlayWindows::HideTPTMenus() && !(top && top->InPlace));
		if (shown && !wasShown) lastUse = SDL_GetTicks(); // just turned on: start visible
		wasShown = shown;
		float fade = 1;
		if (OverlayWindows::HideTPTMenus() && !(top && top->InPlace))
		{
			fade = 0; // all of them are in the side window
		}
		else if (!(top && top->InPlace))
		{
			// Shown: they fade out after a few seconds without the mouse over them, and come back on hover.
			POINT cur;
			RECT wr;
			GetCursorPos(&cur);
			GetWindowRect(hwnd, &wr);
			int cx = wr.right > wr.left ? (cur.x - wr.left) * w / (wr.right - wr.left) : 0;
			int cy = wr.bottom > wr.top ? (cur.y - wr.top) * h / (wr.bottom - wr.top) : 0;
			if (cx >= 0 && cy >= 0 && cx < w && cy < h && (cx >= XRES || cy >= YRES)) lastUse = SDL_GetTicks();
			fade = std::clamp(1 - (int(SDL_GetTicks() - lastUse) - 3000) / 1000.f, 0.f, 1.f);
		}
		if (fade < 1)
		{
			uint32_t k = uint32_t(fade * 256);
			for (int y = 0; y < WINDOWH; y++)
				for (int x = y < YRES ? XRES : 0; x < w; x++)
				{
					auto &p = argb[y * w + x];
					p = k ? ((((p & 0xFF00FF) * k) >> 8) & 0xFF00FF) | ((((p & 0xFF00) * k) >> 8) & 0xFF00) | ((((p >> 24) * k) >> 8) << 24) : 0;
				}
		}
	}
	OverlayWindows::ClearUnderWindows(argb.data(), w, h, ww, wh);
	for (int y = 0; y < wh; y++)
	{
		auto *src = argb.data() + (y * h / wh) * w;
		auto *dst = bits + y * ww;
		for (int x = 0; x < ww; x++)
		{
			dst[x] = src[x * w / ww];
		}
	}
	POINT origin{ 0, 0 };
	SIZE size{ ww, wh };
	BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
	if (!UpdateLayeredWindow(hwnd, nullptr, nullptr, &size, memDC, &origin, 0, &blend, ULW_ALPHA))
	{
		static bool told = false;
		if (!told) fprintf(stderr, "overlay: UpdateLayeredWindow failed (%lu)\n", GetLastError());
		told = true;
	}
#else
	(void)window, (void)vid, (void)w, (void)h;
#endif
}
