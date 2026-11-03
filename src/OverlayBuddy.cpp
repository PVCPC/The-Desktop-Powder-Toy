#include "OverlayBuddy.h"
#include "Overlay.h"
#include "OverlayWindows.h"
#include "SimulationConfig.h"
#include "OverlayWalls.h"
#include <cmath>
#include "gui/game/GameController.h"
#include "gui/game/GameModel.h"
#include "simulation/Simulation.h"
#include "simulation/SimulationData.h"
#include "simulation/ElementClasses.h"
#include <algorithm>
#include "prefs/GlobalPrefs.h"
#include "../buddy/tdpt_buddy.h"
#ifdef _WIN32
#include "common/platform/Platform.h"
#include <SDL.h>
#include <SDL_syswm.h>
#include <windows.h>
#include <dwmapi.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#endif
#include <vector>
#include <filesystem>
#include <array>

namespace
{
	Buddy *buddy = nullptr;
	ByteString failed;            // the file that last failed to open: not retried until another is chosen
	const uint32_t *frame = nullptr; // his newest frame, fw x fh premultiplied (null: nothing shown), drawn into the overlay's
	bool drawn = false;              // he has a frame up (in the overlay's or in his own window)
	int fw = XRES, fh = YRES;     // his stage: TDPT's play area (from the top left), so his floor is the sand's floor
	int wb = YRES;                // the taskbar's top (sim pixels): he stands on it, as in the desktop app
	std::vector<uint16_t> ground; // the sim around him, for tdpt_buddy_ground
	std::vector<std::array<double, 6>> parts; // his body: x, y, radius, xv, yv, 1 burning (tdpt_buddy_parts)
	bool Mine(int x, int y, double margin = 1) // in or near one of his own parts (his walls are not ground to himself)
	{
		for (auto &p : parts)
			if ((x + 0.5 - p[0]) * (x + 0.5 - p[0]) + (y + 0.5 - p[1]) * (y + 0.5 - p[1]) <= (p[2] + margin) * (p[2] + margin)) return true;
		return false;
	}
	std::vector<float> air;       // and its air (velocity, pressure), for tdpt_buddy_air
	int wet[8], frost[8];         // ticks each part still looks wet / frosted (Overlay.Fun.BuddyLooks)
	int dirt[8] = {};             // how dirty each part is, 0-3600 (Overlay.Fun.BuddyDirt)
	int zapped = 0;               // ticks he still glows from a shock (Overlay.Fun.BuddyGlow)
	int fleeRest = 0;             // ticks before he runs from fire again (Overlay.Fun.BuddyFlee)
	int hidden = -1;              // the game's own effects last told to hide (hide: command)
	double lastSpeed[8];          // each part's speed last tick (a splash: fast, then stopped by sand or water)
	int hose = 0, hoseRest = 0;   // fireman: ticks left spraying, and before he looks for fire again
	float hoseX, hoseY;           // the fire he is putting out
	std::vector<Particle> handful; // what his hand carries (Overlay.Fun.BuddyCarry); in the sim each tick, by carried
	std::vector<int> carried;      // their ids this tick (-1: no room for that one this tick)
	int carryTicks = 0, carryRest = 0; // ticks he has held it, and before he scoops again
	bool held = false;            // a press that went to him, until its release
	double heldX, heldY;          // where TPT's window last saw the mouse while held (it has the mouse captured)

#ifdef _WIN32
	// His own window (Windows, the overlay over everything as layered window): his frames go to the screen from a
	// thread of their own at the screen's rate, as in the desktop app, not at TDPT's own frame rate (~30 a second at
	// 5120x1440). Click-through; the overlay still catches clicks on his pixels (alpha 1 there, from mask).
	// UpdateLayeredWindowIndirect: only the changed part goes to the screen (Vista and later; the build targets XP's headers)
	struct UlwInfo { DWORD cbSize; HDC hdcDst; const POINT *pptDst; const SIZE *psize; HDC hdcSrc; const POINT *pptSrc; COLORREF crKey; const BLENDFUNCTION *pblend; DWORD dwFlags; const RECT *prcDirty; };
	extern "C" __declspec(dllimport) BOOL WINAPI UpdateLayeredWindowIndirect(HWND, const UlwInfo *);

	struct Shown
	{
		std::thread thread;
		std::atomic<bool> quit{ false };
		std::atomic<HWND> window{ nullptr };
		std::mutex m; // guards the rest
		HWND overlay = nullptr;
		int w = 1, h = 1, fw = XRES, fh = YRES, windows = 1; // the overlay frame's size, his stage, Overlay.Buddy.Windows
		bool held = false, has = false;
		double heldX = 0, heldY = 0;
		std::vector<uint32_t> copy; // his last frame shown (fw2 wide), and where it has pixels (tdpt_buddy_boxes)
		int fw2 = 0, k = 1;             // its width, and how many of its pixels to one of the overlay frame's
		std::vector<RECT> boxes;
	};
	std::unique_ptr<Shown> shown;

	void Show(Shown &s)
	{
		HWND bw = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST, L"STATIC", L"Buddy",
		                          WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, nullptr, nullptr);
		HDC dc = CreateCompatibleDC(nullptr);
		HBITMAP bmp = nullptr;
		uint32_t *bits = nullptr;
		RECT at{};               // the overlay's place on screen (his window's too)
		const uint32_t *seen = nullptr;
		std::vector<RECT> had;   // where the last frame shown had pixels (sim), for the size it was
		int hadW = 0, hadH = 0;
		s.window = bw;
		while (!s.quit)
		{
			MSG msg;
			while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
			HWND overlay;
			int w, h, fw, fh, windows;
			bool held;
			double mx, my;
			{
				std::lock_guard lock(s.m);
				overlay = s.overlay, w = s.w, h = s.h, fw = s.fw, fh = s.fh, windows = s.windows, held = s.held, mx = s.heldX, my = s.heldY;
			}
			RECT wr;
			if (!overlay || !GetWindowRect(overlay, &wr) || wr.right <= wr.left || wr.bottom <= wr.top)
			{
				Sleep(10);
				continue;
			}
			if (!IsWindowVisible(overlay)) // TPT minimised or the overlay hidden: he goes with it
			{
				if (IsWindowVisible(bw)) ShowWindow(bw, SW_HIDE);
				Sleep(10);
				continue;
			}
			int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
			if (!EqualRect(&wr, &at))
			{
				at = wr, seen = nullptr, hadW = 0; // all drawn again
				if (bmp) DeleteObject(bmp);
				BITMAPINFO bi{ { sizeof(BITMAPINFOHEADER), ww, -wh, 1, 32, BI_RGB } };
				bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&bits, nullptr, 0);
				SelectObject(dc, bmp);
				POINT src{}, dst{ wr.left, wr.top };
				SIZE size{ ww, wh };
				BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
				UpdateLayeredWindow(bw, nullptr, &dst, &size, dc, &src, 0, &blend, ULW_ALPHA);
			}
			if (!IsWindowVisible(bw)) ShowWindow(bw, SW_SHOWNOACTIVATE);
			if (!held)
			{
				POINT cur;
				GetCursorPos(&cur);
				mx = double(cur.x - wr.left) * w / ww, my = double(cur.y - wr.top) * h / wh;
			}
			// the window he stands on, every frame here so he moves with it smoothly
			uintptr_t id = 0;
			int el, er, et, eb;
			if (windows == 1 && OverlayWindows::ActiveEdges(id, el, er, et, eb))
				tdpt_buddy_ledge(buddy, id, el, er, et < 0 ? NAN : et, eb < 0 ? NAN : eb);
			else
				tdpt_buddy_ledge(buddy, 0, 0, 0, NAN, NAN);
			// drawn at the screen's own pixels (whole times the sim's), as sharp as in the desktop app: W x H of them
			// for the overlay frame's w x h, his frame FW x FH
			int k = std::max(1, ww / w), FW = fw * k, FH = fh * k, W = w * k, H = h * k;
			tdpt_buddy_scale(buddy, k);
			auto *f = tdpt_buddy_frame(buddy, mx, my, fw, fh);
			if (f && f != seen && bits)
			{
				seen = f;
				// where it has pixels (it's clear everywhere else): those and where the last one had them are all that
				// changed (the game's labels sit in the far corners: one box around everything was most of the screen)
				int32_t raw[64 * 4];
				size_t n = tdpt_buddy_boxes(buddy, raw, 64);
				std::vector<RECT> boxes;
				if (n > 64) boxes = { RECT{ 0, 0, FW, FH } };
				for (size_t i = 0; i < n && n <= 64; i++)
				{
					RECT r{ std::clamp(raw[i * 4], 0, FW), std::clamp(raw[i * 4 + 1], 0, FH), std::clamp(raw[i * 4 + 2], 0, FW), std::clamp(raw[i * 4 + 3], 0, FH) };
					if (!IsRectEmpty(&r)) boxes.push_back(r);
				}
				if (FW != hadW || FH != hadH) had = { RECT{ 0, 0, FW, FH } }, hadW = FW, hadH = FH; // all of it again
				auto dirty = boxes;
				dirty.insert(dirty.end(), had.begin(), had.end());
				had = boxes;
				for (bool merged = true; merged;) // overlapping ones as one
				{
					merged = false;
					for (size_t i = 0; i < dirty.size() && !merged; i++)
						for (size_t j = i + 1; j < dirty.size() && !merged; j++)
						{
							RECT both;
							if (IntersectRect(&both, &dirty[i], &dirty[j]))
							{
								UnionRect(&dirty[i], &dirty[i], &dirty[j]);
								dirty.erase(dirty.begin() + j);
								merged = true;
							}
						}
				}
				{
					// a copy for the overlay's click catching
					std::lock_guard lock(s.m);
					if (s.copy.size() != size_t(FW) * FH || s.fw2 != FW) s.copy.assign(size_t(FW) * FH, 0), s.fw2 = FW;
					for (auto &r : dirty)
						for (int y = r.top; y < r.bottom; y++) std::copy(f + size_t(y) * FW + r.left, f + size_t(y) * FW + r.right, s.copy.data() + size_t(y) * FW + r.left);
					s.boxes = boxes;
					s.k = k;
					s.has = true;
				}
				// each to the screen (nearest pixel, as the overlay scales the sim)
				for (auto &r : dirty)
				{
					RECT now{ LONG(r.left * ww / W), LONG(r.top * wh / H), LONG(std::min<int>(ww, (r.right * ww + W - 1) / W)), LONG(std::min<int>(wh, (r.bottom * wh + H - 1) / H)) };
					for (int y = now.top; y < now.bottom; y++)
					{
						auto *src = f + size_t(std::min(y * H / wh, FH - 1)) * FW;
						for (int x = now.left; x < now.right; x++) bits[size_t(y) * ww + x] = src[std::min(x * W / ww, FW - 1)];
					}
					POINT src{};
					SIZE size{ ww, wh };
					BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
					UlwInfo info{ sizeof(info), nullptr, nullptr, &size, dc, &src, 0, &blend, ULW_ALPHA, &now };
					UpdateLayeredWindowIndirect(bw, &info);
				}
			}
			DwmFlush(); // the next screen refresh
		}
		s.window = nullptr;
		DestroyWindow(bw);
		DeleteDC(dc);
		if (bmp) DeleteObject(bmp);
	}

	void Hide()
	{
		if (!shown) return;
		shown->quit = true;
		shown->thread.join();
		shown.reset();
	}
#endif

	// a blast in the sim at (x, y) sim pixels, power 1 about one of his game's bombs: pressure and a ball of fire
	void Blast(Simulation *sim, double bx, double by, double power)
	{
		sim->petBuddy.scare = sim->currentTick + 120;
		int R = std::max(2, int(4 * power)); // cells
		for (int y = int(by) / CELL - R; y <= int(by) / CELL + R; y++)
			for (int x = int(bx) / CELL - R; x <= int(bx) / CELL + R; x++)
				if (double d = std::hypot(x - bx / CELL, y - by / CELL); x >= 0 && y >= 0 && x < XCELLS && y < YCELLS && d < R)
					sim->pv[y][x] = std::min(256.0f, sim->pv[y][x] + float(40 * power * (1 - d / R)));
		int F = int(8 * power); // pixels
		for (int y = int(by) - F; y <= int(by) + F; y++)
			for (int x = int(bx) - F; x <= int(bx) + F; x++)
				if (x >= 0 && y >= 0 && x < XRES && y < YRES && std::hypot(x - bx, y - by) < F && sim->rng.chance(1, 3))
					sim->create_part(-1, x, y, PT_FIRE);
	}
}

bool OverlayBuddy::Draw(uint32_t *argb, int w, double mx, double my, void *layer, int h)
{
	auto path = GlobalPrefs::Ref().Get("Overlay.Fun.BuddySwf", ByteString(""));
	if (!GlobalPrefs::Ref().Get("Overlay.Fun.Buddy", false) || path.empty())
	{
		Close();
		failed.clear();
		return false;
	}
	if (!buddy && path != failed)
	{
		char error[256] = "";
		buddy = tdpt_buddy_open(path.c_str(), XRES, YRES, error, sizeof(error));
		if (!buddy)
		{
			failed = path;
			OverlayWindows::Notify(error);
		}
	}
	if (!buddy) return false;
	// what he stands on: powders and solids, and walls a powder can't pass (1); windows and icons (not their holes,
	// not the taskbar) stop him from the side (0x400), and he stands on the active window's edges (tdpt_buddy_ledge);
	// liquids as 2 + a quarter of their weight (water 30), which he floats in, acid as 31, VOID and black holes as 30,
	// portals in as 29; then how hot it is (0x20 60 C+, 0x40
	// 400 C+: sets him on fire, 0x60 -50 C or colder), 0x80 a spark or electron (shocks him), 0x100 caustic gas, radiation
	// or virus (hurts), 0x200 DEST, BOMB or a singularity (blows up on him), 0x800 a powder (buried, he digs up through
	// it), 0x1000 ice (closed in by it, he's held until it melts); light (PHOT) warms him
	int32_t r[4];
	tdpt_buddy_area(buddy, r);
	r[2] = std::clamp(r[2], 0, XRES * 2), r[3] = std::clamp(r[3], 0, YRES * 2);
	ground.assign(size_t(r[2]) * r[3], 0);
	auto *sim = GameController::Ref().GetModel()->GetSimulation();
	auto &elements = SimulationData::CRef().elements;
	int channel = -1; // of a portal in his reach
	// windows (Buddy tab): 0 he walks through them, 1 he stands on the active one (the desktop app's way, below),
	// 2 every window and icon is solid to him as to sand (not in overlay:window: they aren't where his stage is)
	int windows = overlayWindowed ? 0 : GlobalPrefs::Ref().Get("Overlay.Buddy.Windows", 1);
	for (int y = std::max(r[1], 0); y < std::min(r[1] + r[3], YRES); y++)
		for (int x = std::max(r[0], 0); x < std::min(r[0] + r[2], XRES); x++)
		{
			int p = sim->pmap[y][x];
			auto &c = ground[size_t(y - r[1]) * r[2] + x - r[0]];
			if (int e = TYP(sim->photons[y][x]); e == PT_ELEC) c |= 0x80;
			else if (e == PT_NEUT || e == PT_PROT) c |= 0x100;
			else if (e == PT_PHOT && !(c & 0x60)) c |= 0x20;
			if (windows == 2 && y < wb && sim->PixelWall(x, y) && !sim->HoleAt(x, y) && !Mine(x, y, 4)) c |= 0x400;
			if (sim->IsWallBlocking(x, y, PT_DUST)) c |= 1;
			else if (p && (elements[TYP(p)].Properties & (TYPE_PART | TYPE_SOLID))) c |= (elements[TYP(p)].Properties & TYPE_PART) ? 0x801 : 1;
			else if (p && (elements[TYP(p)].Properties & TYPE_LIQUID)) c |= uint16_t(2 + (std::clamp(elements[TYP(p)].Weight, 0, 100) + 2) / 4);
			if (!p) continue;
			if (TYP(p) == PT_ACID) c = (c & ~0x1F) | 31;
			if (TYP(p) == PT_VOID || TYP(p) == PT_BHOL) c = (c & ~0x1F) | 30;
			if (TYP(p) == PT_PRTI)
			{
				c = (c & ~0x1F) | 29;
				channel = sim->parts[ID(p)].tmp;
			}
			int t = TYP(p);
			if (t == PT_CAUS || t == PT_VIRS || t == PT_VRSS || t == PT_VRSG) c |= 0x100;
			if (t == PT_DEST || t == PT_BOMB || t == PT_SING) c |= 0x200;
			if (t == PT_ICEI) c |= 0x1000;
			auto temp = sim->parts[ID(p)].temp - 273.15f;
			if (int f = temp >= 400 ? 0x40 : temp >= 60 ? 0x20 : temp <= -50 ? 0x60 : 0) c = uint16_t((c & ~0x60) | f);
			if (TYP(p) == PT_SPRK || TYP(p) == PT_LIGH) c |= 0x80; // (lightning too)
		}
	tdpt_buddy_ground(buddy, r, ground.data());
	// where that portal comes out
	int has = 0;
	float ex = 0, ey = 0;
	for (int i = 0; channel >= 0 && i <= sim->parts.active; i++)
		if (sim->parts[i].type == PT_PRTO && sim->parts[i].tmp == channel)
		{
			has = 1, ex = sim->parts[i].x, ey = sim->parts[i].y;
			break;
		}
	tdpt_buddy_portal(buddy, has, ex, ey);
	// the air over the same area, in sim cells
	int32_t c[4] = { std::max(r[0], 0) / CELL, std::max(r[1], 0) / CELL, 0, 0 };
	c[2] = std::max(0, std::min((r[0] + r[2]) / CELL + 1, XCELLS) - c[0]);
	c[3] = std::max(0, std::min((r[1] + r[3]) / CELL + 1, YCELLS) - c[1]);
	air.resize(size_t(c[2]) * c[3] * 3);
	for (int y = 0; y < c[3]; y++)
		for (int x = 0; x < c[2]; x++)
		{
			auto *a = &air[(size_t(y) * c[2] + x) * 3];
			a[0] = sim->vx[c[1] + y][c[0] + x];
			a[1] = sim->vy[c[1] + y][c[0] + x];
			a[2] = sim->pv[c[1] + y][c[0] + x];
		}
	tdpt_buddy_air(buddy, CELL, c, air.data());
	// gravity where he is (the middle of that area), as a particle that falls like his game would (Newtonian
	// gravity x10: his normal fall is about ten times a powder's)
	float gx = 0, gy = 1;
	int cx = std::clamp(r[0] + r[2] / 2, 0, XRES - 1), cy = std::clamp(r[1] + r[3] / 2, 0, YRES - 1);
	if (r[2] > 0) sim->GetGravityField(cx, cy, 1.0f, 10.0f, gx, gy);
	tdpt_buddy_gravity(buddy, gx, gy);
	// the game's own explosions: a TPT blast there (pressure, power 1 about a bomb's, and a ball of fire)
	double booms[16 * 3];
	for (size_t i = 0, n = tdpt_buddy_booms(buddy, booms, 16); i < n; i++) Blast(sim, booms[i * 3], booms[i * 3 + 1], booms[i * 3 + 2]);
	auto play = sim->PlayRect();
	// his stage: the play area, ending 10 px into the taskbar as the desktop app's did (the game's floor is 7 px up: feet just on it)
	fw = std::clamp(play.pos.X + play.size.X, 1, XRES), fh = std::clamp(play.pos.Y + play.size.Y, 1, YRES);
	// (overlay:window: the window's own bottom, no taskbar or other windows: they aren't where his stage is)
	wb = overlayWindowed ? YRES : OverlayWindows::WorkBottom(parts.size() ? int(parts[0][0]) : XRES / 2);
	fh = std::min(fh, wb + 10);
	auto &prefs = GlobalPrefs::Ref();
	tdpt_buddy_settings(buddy, prefs.Get("Overlay.Buddy.Size", 100.f), prefs.Get("Overlay.Buddy.Speech", true), prefs.Get("Overlay.Buddy.SpeechSize", 100.f),
	                    int(prefs.Get("Overlay.Buddy.Toys", 6.f)), prefs.Get("Overlay.Buddy.Smooth", true));
	tdpt_buddy_volume(buddy, prefs.Get("Overlay.Buddy.Volume", 50.f) / 100.f);
	bool looks = GlobalPrefs::Ref().Get("Overlay.Fun.BuddyLooks", false), glows = GlobalPrefs::Ref().Get("Overlay.Fun.BuddyGlow", false);
	bool dirty = GlobalPrefs::Ref().Get("Overlay.Fun.BuddyDirt", false);
#ifdef _WIN32
	// in his own window (not with the wet/frosty looks, the glow or dirt: those are drawn into the overlay's frame)
	if (layer && !looks && !glows && !dirty)
	{
		if (!shown)
		{
			shown = std::make_unique<Shown>();
			shown->thread = std::thread(Show, std::ref(*shown));
		}
		auto &s = *shown;
		std::lock_guard lock(s.m);
		s.overlay = HWND(layer), s.w = w, s.h = h, s.fw = fw, s.fh = fh, s.windows = windows;
		s.held = held, s.heldX = heldX, s.heldY = heldY;
		frame = nullptr;
		drawn = s.has;
		// clicks on him: the overlay takes them over his pixels (alpha 1, as good as invisible)
		for (auto &r : s.boxes) // (his frame's pixels, s.k to one of ours)
			for (int y = r.top / s.k; y < (r.bottom + s.k - 1) / s.k; y++)
				for (int x = r.left / s.k; x < (r.right + s.k - 1) / s.k; x++)
					if (s.copy[size_t(y * s.k) * s.fw2 + x * s.k] >> 24 && !(argb[size_t(y) * w + x] >> 24)) argb[size_t(y) * w + x] = 0x01000000;
		// just above the overlay
		HWND bwin = s.window, above = GetWindow(HWND(layer), GW_HWNDPREV);
		if (bwin && above != bwin)
			SetWindowPos(bwin, above ? above : (GetWindowLongPtrW(HWND(layer), GWL_EXSTYLE) & WS_EX_TOPMOST ? HWND_TOPMOST : HWND_TOP), 0, 0, 0, 0,
			             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
		return true;
	}
	Hide();
#endif
	uintptr_t id = 0;
	int el, er, et, eb;
	if (windows == 1 && OverlayWindows::ActiveEdges(id, el, er, et, eb))
		tdpt_buddy_ledge(buddy, id, el, er, et < 0 ? NAN : et, eb < 0 ? NAN : eb);
	else
		tdpt_buddy_ledge(buddy, 0, 0, 0, NAN, NAN);
	tdpt_buddy_scale(buddy, 1);
	frame = held ? tdpt_buddy_frame(buddy, heldX, heldY, fw, fh) : tdpt_buddy_frame(buddy, mx, my, fw, fh);
	drawn = frame;
	if (!frame) return true;
	// his premultiplied pixels over the frame's
	OverlayBands(fh, [&](int y0, int y1) {
		for (int y = y0; y < y1; y++)
			for (int x = 0; x < fw; x++)
			{
				uint32_t s = frame[size_t(y) * fw + x], a = s >> 24;
				if (!a) continue;
				auto &d = argb[size_t(y) * w + x];
				uint32_t k = 256 - a;
				d = s + ((((d & 0xFF00FF) * k) >> 8) & 0xFF00FF) + ((((d & 0xFF00) * k) >> 8) & 0xFF00) + ((((d >> 24) * k) >> 8) << 24);
			}
	});
	// wet parts look darker, frosted ones paler blue, fading over 10 s; dirty ones have dark brown smudges (fixed spots
	// on each part, more of them the dirtier it is); shocked, all of him flickers bright blue-white for a moment: his
	// own pixels there, recoloured in place
	static int flicker = 0;
	float zap = glows && zapped ? (++flicker & 2 ? 0.9f : 0.5f) * std::min(1.f, zapped / 10.f) : 0;
	for (size_t n = 0; (looks || zap || dirty) && n < parts.size(); n++)
	{
		auto &p = parts[n];
		float dark = looks ? 0.3f * wet[n] / 600 : 0, ice = looks ? 0.5f * frost[n] / 600 : 0, cover = dirty ? 0.6f * std::min(1.f, dirt[n] / 1800.f) : 0;
		if ((!dark && !ice && !zap && !cover) || !std::isfinite(p[0] + p[1] + p[2])) continue;
		for (int y = std::max(0, int(p[1] - p[2]) - 2); y <= std::min(fh - 1, int(p[1] + p[2]) + 2); y++)
			for (int x = std::max(0, int(p[0] - p[2]) - 2); x <= std::min(fw - 1, int(p[0] + p[2]) + 2); x++)
			{
				uint32_t s = frame[size_t(y) * fw + x], a = s >> 24;
				if (!a || std::hypot(x + 0.5 - p[0], y + 0.5 - p[1]) > p[2] + 2) continue;
				auto &d = argb[size_t(y) * w + x];
				uint32_t out = d & 0xFF000000;
				uint32_t spot = uint32_t((x - int(p[0])) >> 1) * 73856093u ^ uint32_t((y - int(p[1])) >> 1) * 19349663u ^ uint32_t(n) * 83492791u;
				spot ^= spot >> 13, spot *= 0x5bd1e995u, spot ^= spot >> 15;
				float grime = (spot & 255) < cover * 256 ? 0.8f : 0;
				for (int c = 0; c < 3; c++)
				{
					float sc = float(s >> (c * 8) & 0xFF), want = sc * (1 - dark);
					want += (a * (c == 0 ? 0.12f : c == 1 ? 0.16f : 0.23f) - want) * grime; // toward dark brown
					want += (a * (c == 0 ? 0.95f : c == 1 ? 0.85f : 0.75f) - want) * ice; // toward pale blue (b, g, r)
					want += (a * (c == 0 ? 1.f : c == 1 ? 0.97f : 0.8f) - want) * zap;  // toward blue-white
					int v = int(d >> (c * 8) & 0xFF) - int(sc) + int(want);
					out |= uint32_t(std::clamp(v, 0, 255)) << (c * 8);
				}
				d = out;
			}
	}
	return true;
}

void OverlayBuddy::Walls(std::vector<OverlayWall> &walls)
{
	auto *sim = GameController::Ref().GetModel()->GetSimulation();
	sim->petBuddy.x = -1;
	if (!buddy || !drawn)
	{
		parts.clear();
		return;
	}
	parts.resize(8);
	static_assert(sizeof(parts[0]) == 6 * sizeof(double));
	parts.resize(tdpt_buddy_parts(buddy, parts[0].data(), parts.size()));
	if (parts.size() && GlobalPrefs::Ref().Get("Overlay.Fun.BuddyPets", false) && std::isfinite(parts[0][0] + parts[0][1]))
		sim->petBuddy.x = int(parts[0][0]), sim->petBuddy.y = int(parts[0][1]), sim->petBuddy.speed = float(std::hypot(parts[0][3], parts[0][4]));
	// he pushes the air he moves through fast (0.1 of his speed) and warms what touches him, slowly, toward 37 C; and what
	// touches him reacts: acid is used up on him, DEST and bombs go off, a spark runs through him into metal he
	// touches; and when he burns, so does what is next to him
	std::vector<int> touching;
	for (size_t k = 0; k < parts.size(); k++)
	{
		auto &p = parts[k];
		wet[k] = p[5] != 0 ? 0 : std::max(0, wet[k] - 1), frost[k] = std::max(0, frost[k] - 1); // fire dries him
		dirt[k] = std::max(0, dirt[k] - 1); // a minute to wear off
		if (!std::isfinite(p[0] + p[1] + p[2] + p[3] + p[4])) continue;
		int cx = int(p[0]) / CELL, cy = int(p[1]) / CELL;
		// only fast moves (kicks, throws): he feels the air too, so a slow drift would feed itself and float him away
		if (p[0] >= 0 && p[1] >= 0 && cx < XCELLS && cy < YCELLS && std::hypot(p[3], p[4]) > 4)
			sim->vx[cy][cx] += 0.1f * float(p[3]), sim->vy[cy][cx] += 0.1f * float(p[4]);
		for (int y = std::max(0, int(p[1] - p[2]) - 2); y <= std::min(YRES - 1, int(p[1] + p[2]) + 2); y++)
			for (int x = std::max(0, int(p[0] - p[2]) - 2); x <= std::min(XRES - 1, int(p[0] + p[2]) + 2); x++)
				if (int q = sim->pmap[y][x]; q && std::hypot(x + 0.5 - p[0], y + 0.5 - p[1]) <= p[2] + 2)
				{
					sim->parts[ID(q)].temp += (310.15f - sim->parts[ID(q)].temp) * 0.002f;
					touching.push_back(ID(q));
					auto &t = sim->parts[ID(q)];
					if (t.temp < 223.15f) frost[k] = 600;
					else if (t.type != PT_ACID && t.temp < 333.15f && (SimulationData::CRef().elements[t.type].Properties & TYPE_LIQUID)) wet[k] = 600;
					// dirt: coal, dust, gunpowder, clay, oil, broken metal, smoke rub off on him; water washes it off
					if (t.type == PT_COAL || t.type == PT_BCOL || t.type == PT_DUST || t.type == PT_GUNP || t.type == PT_CLST || t.type == PT_OIL || t.type == PT_BRMT || t.type == PT_SMKE)
						dirt[k] = std::min(3600, dirt[k] + 1); // each grain: buried, a few seconds to get filthy
					else if (t.type == PT_WATR || t.type == PT_DSTW || t.type == PT_SLTW || t.type == PT_CBNW)
						dirt[k] = std::max(0, dirt[k] - 3);
				}
		for (int k = 0; p[5] != 0 && k < 2; k++)
		{
			double a = sim->rng.uniform01() * 6.2832, d = p[2] + 1 + sim->rng.uniform01() * 2;
			int x = int(p[0] + std::cos(a) * d), y = int(p[1] + std::sin(a) * d);
			if (x >= 0 && y >= 0 && x < XRES && y < YRES && !sim->pmap[y][x] && sim->rng.chance(1, 3)) sim->create_part(-1, x, y, PT_FIRE);
		}
	}
	auto &elements = SimulationData::CRef().elements;
	bool spark = false;
	zapped = std::max(0, zapped - 1);
	for (int i : touching)
	{
		int t = sim->parts[i].type;
		if (t == PT_SPRK) spark = true;
		if (t == PT_SPRK || t == PT_ELEC || t == PT_LIGH) zapped = 30;
		if (t == PT_ACID && sim->rng.chance(1, 8)) sim->kill_part(i);
		if (t == PT_DEST || t == PT_BOMB || t == PT_SING)
		{
			float x = sim->parts[i].x, y = sim->parts[i].y;
			sim->kill_part(i);
			Blast(sim, x, y, 0.6);
		}
	}
	for (int i : touching)
		if (int t = sim->parts[i].type; spark && t != PT_SPRK && (elements[t].Properties & PROP_CONDUCTS) && sim->parts[i].life == 0)
		{
			sim->part_change_type(i, int(sim->parts[i].x + 0.5f), int(sim->parts[i].y + 0.5f), PT_SPRK);
			sim->parts[i].life = 4;
			sim->parts[i].ctype = t;
		}
	auto &prefs = GlobalPrefs::Ref();
	auto empty = [&](int x, int y) { return x >= 0 && y >= 0 && x < XRES && y < YRES && !sim->pmap[y][x] && !Mine(x, y); };
	auto isPowder = [&](int i) { return (elements[sim->parts[i].type].Properties & TYPE_PART) != 0; };
	auto isLiquid = [&](int i) { return (elements[sim->parts[i].type].Properties & TYPE_LIQUID) != 0; };
	// Footprints: a foot walking through powder pushes a grain out of its way, onto the pile beside it; a wet foot
	// leaves drops on dry ground
	if (prefs.Get("Overlay.Fun.BuddyTracks", false) && parts.size() == 6)
		for (size_t k = 4; k < 6; k++)
		{
			auto &p = parts[k];
			if (std::abs(p[3]) < 0.5) continue;
			int fx = int(p[0]), fy = int(p[1] + p[2]) + 1, side = p[3] > 0 ? -1 : 1; // pushed back, behind him
			if (fx < 0 || fy < 0 || fx >= XRES || fy >= YRES) continue;
			if (int q = sim->pmap[fy][fx]; q && isPowder(ID(q)) && sim->rng.chance(1, 3))
			{
				int to = fx + side * int(p[2] + 2), ty = fy;
				while (ty > 0 && to >= 0 && to < XRES && sim->pmap[ty][to]) ty--; // on top of the pile there
				if (empty(to, ty))
					{
						auto old = sim->parts[ID(q)];
						sim->kill_part(ID(q));
						if (int i = sim->create_part(-1, to, ty, old.type); i >= 0) sim->parts[i].temp = old.temp, sim->parts[i].ctype = old.ctype;
					}
			}
			else if (wet[k] > 300 && sim->rng.chance(1, 12) && empty(fx, fy - 1) && fy + 1 < YRES && sim->pmap[fy + 1][fx] && !isLiquid(ID(sim->pmap[fy + 1][fx])))
				sim->create_part(-1, fx, fy - 1, PT_WATR);
		}
	// Splash: a part that was flying (speed over 8) and is stopped by sand or water throws it up and out round it
	bool splash = prefs.Get("Overlay.Fun.BuddySplash", false);
	for (size_t k = 0; k < parts.size() && k < 8; k++)
	{
		auto &p = parts[k];
		double speed = std::hypot(p[3], p[4]), was = lastSpeed[k];
		lastSpeed[k] = std::isfinite(speed) ? speed : 0;
		if (!splash || was < 8 || speed > was * 0.5 || !std::isfinite(p[0] + p[1])) continue;
		for (int y = std::max(0, int(p[1] - p[2] - 4)); y <= std::min(YRES - 1, int(p[1] + p[2] + 4)); y++)
			for (int x = std::max(0, int(p[0] - p[2] - 4)); x <= std::min(XRES - 1, int(p[0] + p[2] + 4)); x++)
				if (int q = sim->pmap[y][x]; q && (isPowder(ID(q)) || isLiquid(ID(q))))
				{
					double dx = x + 0.5 - p[0], dy = y + 0.5 - p[1], d = std::max(1.0, std::hypot(dx, dy));
					auto &t = sim->parts[ID(q)];
					float f = float(std::min(was, 10.0) * (0.2 + 0.2 * sim->rng.uniform01())); // (harder flung it left the play area)
					t.vx += float(dx / d) * f, t.vy += float(std::min(dy / d, -0.3)) * f; // out, and up
				}
	}
	// Fireman: fire near him (and he isn't burning): he sprays water at it from his hand for 3 s, then rests 10 s
	if (prefs.Get("Overlay.Fun.BuddyFireman", false) && parts.size() == 6)
	{
		auto &body = parts[0], &hand = parts[3];
		bool burning = std::any_of(parts.begin(), parts.end(), [](auto &p) { return p[5] != 0; });
		if (hoseRest > 0) hoseRest--;
		else if (!hose && !burning && sim->currentTick % 10 == 0)
		{
			double best = 250 * 250;
			for (int i = 0; i <= sim->parts.active; i++)
				if (sim->parts[i].type == PT_FIRE || sim->parts[i].type == PT_PLSM)
					if (double d = std::pow(sim->parts[i].x - body[0], 2) + std::pow(sim->parts[i].y - body[1], 2); d < best && d > 40 * 40)
						best = d, hoseX = sim->parts[i].x, hoseY = sim->parts[i].y, hose = 90;
		}
		if (hose > 0 && --hose == 0) hoseRest = 300;
		for (int j = 0; hose > 0 && j < 3; j++)
		{
			double dx = hoseX - hand[0], dy = hoseY - hand[1], d = std::max(1.0, std::hypot(dx, dy));
			int x = int(hand[0] + dx / d * (hand[2] + 2)), y = int(hand[1] + dy / d * (hand[2] + 2));
			if (!empty(x, y)) continue;
			int i = sim->create_part(-1, x, y, PT_WATR);
			if (i < 0) continue;
			float v = 7, lob = float(std::min(4.0, d / 80)); // aimed a bit high: it falls on the way
			sim->parts[i].vx = float(dx / d) * v + float(sim->rng.uniform01() - 0.5), sim->parts[i].vy = float(dy / d) * v - lob;
		}
	}
	// His weapons (Overlay.Fun.BuddyWeapons): a bullet goes through gas, pushing it along; it stops in the first
	// powder or liquid it hits, spraying a little of it back out and up (a dent in a pile, a splash), and at a solid: a puff of
	// pressure there (glass breaks) and sparks off it. A flame from his flamethrower (or other fire of his) is TPT fire
	// where it is, so it lights what burns
	double shots[256 * 5];
	size_t shot = tdpt_buddy_shots(buddy, shots, 256);
	// he dances (to his radio): the pets dance with him
	for (size_t s = 0; s < shot && prefs.Get("Overlay.Fun.BuddyDance", false); s++)
		if (shots[s * 5 + 4] == 3) sim->petDance = sim->currentTick + 30;
	// his hoses (Overlay.Fun.BuddyWater): each new drop is a little TPT water flying on as the drop does (his frames to
	// TPT's ticks: 0.6 the speed); BuddyWaterOnly hides the game's own drops, so only TPT's are seen (BuddyFireOnly: his flames)
	bool water = prefs.Get("Overlay.Fun.BuddyWater", false);
	bool weapons = prefs.Get("Overlay.Fun.BuddyWeapons", false);
	if (int hide = (water && prefs.Get("Overlay.Fun.BuddyWaterOnly", false) ? 1 : 0) | (weapons && prefs.Get("Overlay.Fun.BuddyFireOnly", false) ? 2 : 0); hide != hidden)
		tdpt_buddy_command(buddy, ("hide:" + std::to_string(hide)).c_str()), hidden = hide;
	for (size_t s = 0; s < shot && water; s++)
		if (double *b = shots + s * 5; b[4] == 4)
			for (int k = 0; k < 3; k++)
				if (int x = int(b[0]) + sim->rng.between(-2, 2), y = int(b[1]) + sim->rng.between(-2, 2); empty(x, y))
					if (int i = sim->create_part(-1, x, y, PT_WATR); i >= 0)
						sim->parts[i].vx = float(std::clamp(b[2] * 0.6, -25.0, 25.0) + sim->rng.uniform01() - 0.5),
						sim->parts[i].vy = float(std::clamp(b[3] * 0.6, -25.0, 25.0) + sim->rng.uniform01() - 0.5);
	for (size_t s = 0; s < shot && weapons; s++)
	{
		double *b = shots + s * 5;
		if (b[4] >= 3) continue;
		if (b[4] == 2)
		{
			int x = int(b[0]) + sim->rng.between(-3, 3), y = int(b[1]) + sim->rng.between(-3, 3);
			if (sim->rng.chance(1, 2) && empty(x, y)) sim->create_part(-1, x, y, PT_FIRE);
			continue;
		}
		double dx = b[2] - b[0], dy = b[3] - b[1], d = std::hypot(dx, dy);
		if (!(d > 0 && d < 2000)) continue;
		dx /= d, dy /= d;
		for (int t = 0, px = -1, py = -1; t <= int(d); t++)
		{
			int x = int(b[0] + dx * t), y = int(b[1] + dy * t), q;
			if (x < 0 || y < 0 || x >= XRES || y >= YRES || !(q = sim->pmap[y][x]) || Mine(x, y))
			{
				px = x, py = y;
				continue;
			}
			auto &p = sim->parts[ID(q)];
			if (elements[p.type].Properties & TYPE_GAS)
			{
				p.vx += float(dx * 4), p.vy += float(dy * 4);
				continue;
			}
			if (isPowder(ID(q)) || isLiquid(ID(q)))
			{
				for (int y2 = std::max(0, y - 3); y2 <= std::min(YRES - 1, y + 3); y2++)
					for (int x2 = std::max(0, x - 3); x2 <= std::min(XRES - 1, x + 3); x2++)
						if (int q2 = sim->pmap[y2][x2]; q2 && (isPowder(ID(q2)) || isLiquid(ID(q2))))
						{
							auto &g = sim->parts[ID(q2)];
							float k = float(3 + 3 * sim->rng.uniform01());
							g.vx += float(-dx * k + (sim->rng.uniform01() - 0.5) * 4), g.vy += float(-dy * k - 2 - sim->rng.uniform01() * 3);
						}
				break;
			}
			sim->pv[y / CELL][x / CELL] = std::min(256.0f, sim->pv[y / CELL][x / CELL] + 2);
			for (int j = 0; j < 2 && px >= 0; j++)
				if (int e = empty(px, py) ? sim->create_part(-1, px, py, PT_EMBR) : -1; e >= 0)
					sim->parts[e].vx = float(-dx * 2 + sim->rng.uniform01() * 2 - 1), sim->parts[e].vy = float(-dy * 2 - sim->rng.uniform01()), sim->parts[e].life = 15;
			break;
		}
	}
	// Running from fire: fire, lava or plasma within 100 px of him and he runs 250 px away from (most of) it, with the game's
	// own run (its runToPoint action); again 2 s later if it's still there. Not while he burns: then the game's own panic
	if (fleeRest > 0) fleeRest--;
	else if (prefs.Get("Overlay.Fun.BuddyFlee", false) && parts.size() == 6 && sim->currentTick % 10 == 0 && std::isfinite(parts[0][0] + parts[0][1]) &&
		std::none_of(parts.begin(), parts.end(), [](auto &p) { return p[5] != 0; }))
	{
		auto &body = parts[0];
		double side = 0; // the fire round him: more to his right (+) or left (-)
		int n = 0;
		for (int i = 0; i <= sim->parts.active; i++)
			if (int t = sim->parts[i].type; t == PT_FIRE || t == PT_LAVA || t == PT_PLSM)
				if (std::pow(sim->parts[i].x - body[0], 2) + std::pow(sim->parts[i].y - body[1], 2) < 100 * 100)
					side += sim->parts[i].x - body[0], n++;
		if (n)
		{
			int to = int(std::clamp(body[0] + (side > 0 ? -250 : 250), 30.0, std::max(30.0, fw - 30.0)));
			tdpt_buddy_command(buddy, ("act:runToPoint:" + std::to_string(to)).c_str());
			fleeRest = 120;
		}
	}
	// Feeding: when he stands calm with a pet near him, every 10 s or so he drops a few seeds from his hand; the pets eat
	// them (any seeds near them, while this is on)
	sim->petEat = prefs.Get("Overlay.Fun.BuddyFeed", false);
	if (sim->petEat && parts.size() == 6 && sim->rng.chance(1, 600) && std::isfinite(parts[3][0] + parts[3][1]) &&
		std::all_of(parts.begin(), parts.end(), [](auto &p) { return p[5] == 0 && std::hypot(p[3], p[4]) < 2; }))
	{
		bool pet = false;
		for (int i = 0; i <= sim->parts.active && !pet; i++)
			pet = sim->parts[i].type == PT_FIGH && std::abs(sim->parts[i].x - parts[0][0]) < 150 && std::abs(sim->parts[i].y - parts[0][1]) < 80;
		for (int k = 0; pet && k < 4; k++)
		{
			int x = int(parts[3][0]) + sim->rng.between(-3, 3), y = int(parts[3][1] + parts[3][2]) + 2;
			if (x >= 0 && y >= 0 && x < XRES && y < YRES && !sim->pmap[y][x]) sim->create_part(-1, x, y, PT_SEED);
		}
	}
	// Carrying: his hand in a powder or liquid that isn't hot scoops up a handful (up to 40) that rides on it as real
	// particles; a fast swing or throw (hand over 6 px a frame) sends it off at the hand's speed, else he drops it after
	// 8 s; he scoops again 4 s later
	for (size_t j = 0; j < carried.size(); j++)
		if (int i = carried[j]; i >= 0 && sim->parts[i].type == handful[j].type)
			handful[j] = sim->parts[i], sim->kill_part(i); // as it is now (its heat, life...)
	carried.clear();
	if (!prefs.Get("Overlay.Fun.BuddyCarry", false) || parts.size() != 6 || !std::isfinite(parts[3][0] + parts[3][1] + parts[3][3] + parts[3][4]))
		handful.clear();
	else
	{
		auto &hand = parts[3];
		if (carryRest > 0) carryRest--;
		else if (handful.empty())
		{
			for (int y = std::max(0, int(hand[1] - hand[2]) - 2); y <= std::min(YRES - 1, int(hand[1] + hand[2]) + 2); y++)
				for (int x = std::max(0, int(hand[0] - hand[2]) - 2); x <= std::min(XRES - 1, int(hand[0] + hand[2]) + 2); x++)
					if (int q = sim->pmap[y][x]; q && handful.size() < 40 && (isPowder(ID(q)) || isLiquid(ID(q))) && sim->parts[ID(q)].temp < 333.15f)
						handful.push_back(sim->parts[ID(q)]), sim->kill_part(ID(q));
			carryTicks = 0;
		}
		double speed = std::hypot(hand[3], hand[4]);
		bool letGo = !handful.empty() && ((carryTicks > 15 && speed > 6) || ++carryTicks > 480);
		for (size_t j = 0; j < handful.size(); j++)
		{
			// a clump round the hand: a spiral just outside it, or further out that way when that's full (buried)
			double a = j * 2.39996, d = hand[2] + 1.5 + j * 0.15;
			int x = int(hand[0] + std::cos(a) * d), y = int(hand[1] + std::sin(a) * d), i = -1;
			for (int out = 1; !empty(x, y) && out < 25; out++) x = int(hand[0] + std::cos(a) * (d + out)), y = int(hand[1] + std::sin(a) * (d + out));
			if (empty(x, y) && (i = sim->create_part(-1, x, y, handful[j].type)) >= 0)
			{
				sim->parts[i] = handful[j];
				sim->parts[i].x = float(x), sim->parts[i].y = float(y);
				float k = letGo && speed > 6 ? float(std::min(1.0, 20 / speed)) : 0; // thrown (at most 20 px a tick), or dropped
				sim->parts[i].vx = float(hand[3]) * k + (k ? float(sim->rng.uniform01() - 0.5) : 0), sim->parts[i].vy = float(hand[4]) * k;
			}
			carried.push_back(i); // ponytail: one with no room the tick he lets go is lost; look further out if it shows
		}
		if (letGo) handful.clear(), carried.clear(), carryRest = 240;
	}
	std::vector<OverlayWall> mine;
	for (auto &p : parts)
	{
		OverlayWall w;
		w.x0 = std::clamp(int(std::floor(p[0] - p[2])), 0, XRES), w.x1 = std::clamp(int(std::ceil(p[0] + p[2])) + 1, 0, XRES);
		w.y0 = std::clamp(int(std::floor(p[1] - p[2])), 0, YRES), w.y1 = std::clamp(int(std::ceil(p[1] + p[2])) + 1, 0, YRES);
		w.mx = int(std::lround(p[3])), w.my = int(std::lround(p[4]));
		w.solid = [x = p[0], y = p[1], r2 = p[2] * p[2]](int px, int py) { return (px + 0.5 - x) * (px + 0.5 - x) + (py + 0.5 - y) * (py + 0.5 - y) <= r2; };
		if (w.x1 > w.x0 && w.y1 > w.y0) mine.push_back(w);
	}
	walls.insert(walls.begin(), mine.begin(), mine.end());
}

void OverlayBuddy::Close()
{
#ifdef _WIN32
	Hide(); // before he goes: it reads his frames
#endif
	drawn = false;
	tdpt_buddy_close(buddy);
	buddy = nullptr;
	frame = nullptr;
	held = false;
	parts.clear();
}

bool OverlayBuddy::Mouse(int x, int y, int what)
{
	if (!buddy) return false;
	heldX = x, heldY = y;
	if (what == 1)
	{
		// only a press on him, his toys or the game's windows; everywhere else stays TPT's
		if (!drawn || !tdpt_buddy_hit(buddy, x, y)) return false;
		held = true;
		tdpt_buddy_mouse(buddy, x, y, 1);
		return true;
	}
	if (what == 2 && held)
	{
		held = false;
		tdpt_buddy_mouse(buddy, x, y, 2);
		return true;
	}
	return held;
}

bool OverlayBuddy::Menu(int x, int y)
{
	if (!buddy || !tdpt_buddy_hit(buddy, x, y)) return false;
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
#ifdef _WIN32
	GameMenu(-1, SDL_GetMouseFocus() && SDL_GetWindowWMInfo(SDL_GetMouseFocus(), &info) ? info.info.win.window : nullptr);
	return true;
#else
	return false; // ponytail: Windows only; the Mac shim has no submenus yet
#endif
}

bool OverlayBuddy::GameMenu(int which, void *owner)
{
#ifdef _WIN32
	if (!buddy) return false;
	std::vector<char> text(tdpt_buddy_menus(buddy, nullptr, 0));
	tdpt_buddy_menus(buddy, text.data(), text.size());
	// the game's menus, as the right-click menu: lines of depth, flags, command, label (tdpt_buddy.h)
	std::vector<HMENU> open{ CreatePopupMenu() };
	std::vector<ByteString> commands;
	int top = -1; // which of the game's menus (File, Skins, Items, Modes, Settings, Help) the lines are in
	for (auto &line : ByteString(text.data()).PartitionBy('\n'))
	{
		auto f = line.PartitionBy('\t', true);
		if (f.size() < 4) continue;
		int d = f[0].ToNumber<int>(true);
		if (d == 0) top++;
		if (which >= 0 && (top != which || d == 0)) continue; // one of the menus: its items, without its own heading
		size_t depth = std::min(size_t(std::max(d - (which >= 0), 0)), open.size() - 1);
		open.resize(depth + 1);
		UINT flags = (f[1].Contains("g") ? MF_GRAYED : 0) | (f[1].Contains("c") ? MF_CHECKED : 0);
		auto label = Platform::WinWiden(f[3]);
		if (f[1].Contains("p"))
		{
			HMENU sub = CreatePopupMenu();
			AppendMenuW(open.back(), MF_POPUP | flags, UINT_PTR(sub), label.c_str());
			open.push_back(sub);
		}
		else if (f[1].Contains("s")) AppendMenuW(open.back(), MF_SEPARATOR, 0, nullptr);
		else
		{
			commands.push_back(f[2]);
			AppendMenuW(open.back(), MF_STRING | flags, commands.size(), label.c_str());
		}
	}
	POINT p;
	GetCursorPos(&p);
	int cmd = TrackPopupMenu(open.front(), TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, p.x, p.y, 0, HWND(owner), nullptr);
	DestroyMenu(open.front());
	if (cmd > 0 && size_t(cmd) <= commands.size()) tdpt_buddy_command(buddy, commands[cmd - 1].c_str());
	return true;
#else
	return false;
#endif
}

bool OverlayBuddy::ImportSave()
{
	// the desktop Interactive Buddy's save (Interactive Buddy Native: %LOCALAPPDATA%\Interactive Buddy Native\saves),
	// over his own here (kept as daBud.sol.bak); he is closed for it (writing his save first) and opens again
	auto *local = getenv("LOCALAPPDATA");
	if (!local) return false;
	std::error_code ec;
	std::filesystem::path from;
	for (auto &e : std::filesystem::recursive_directory_iterator(std::filesystem::path(local) / "Interactive Buddy Native" / "saves", ec))
		if (e.path().filename() == "daBud.sol") from = e.path();
	if (from.empty()) return false;
	Close();
	std::filesystem::path to = std::filesystem::path("buddy") / "localhost_tdpt_buddy.swf_daBud.sol";
	std::filesystem::create_directories("buddy", ec);
	if (std::filesystem::exists(to)) std::filesystem::copy_file(to, to.string() + ".bak", std::filesystem::copy_options::overwrite_existing, ec);
	return std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec) && !ec;
}
