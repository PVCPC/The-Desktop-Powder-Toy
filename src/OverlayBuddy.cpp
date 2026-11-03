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
#endif
#include <vector>
#include <filesystem>
#include <array>

namespace
{
	Buddy *buddy = nullptr;
	ByteString failed;            // the file that last failed to open: not retried until another is chosen
	const uint32_t *frame = nullptr; // his newest frame, fw x fh premultiplied (null: nothing shown)
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
	bool held = false;            // a press that went to him, until its release
	double heldX, heldY;          // where TPT's window last saw the mouse while held (it has the mouse captured)

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

bool OverlayBuddy::Draw(uint32_t *argb, int w, double mx, double my)
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
	// it); light (PHOT) warms him
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
			auto temp = sim->parts[ID(p)].temp - 273.15f;
			if (int f = temp >= 400 ? 0x40 : temp >= 60 ? 0x20 : temp <= -50 ? 0x60 : 0) c = uint16_t((c & ~0x60) | f);
			if (TYP(p) == PT_SPRK) c |= 0x80;
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
	uintptr_t id = 0;
	int el, er, et, eb;
	if (windows == 1 && OverlayWindows::ActiveEdges(id, el, er, et, eb))
		tdpt_buddy_ledge(buddy, id, el, er, et < 0 ? NAN : et, eb < 0 ? NAN : eb);
	else
		tdpt_buddy_ledge(buddy, 0, 0, 0, NAN, NAN);
	frame = held ? tdpt_buddy_frame(buddy, heldX, heldY, fw, fh) : tdpt_buddy_frame(buddy, mx, my, fw, fh);
	if (!frame) return true;
	bool looks = GlobalPrefs::Ref().Get("Overlay.Fun.BuddyLooks", false);
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
	// wet parts look darker, frosted ones paler blue, fading over 10 s: his own pixels there, recoloured in place
	for (size_t n = 0; looks && n < parts.size(); n++)
	{
		auto &p = parts[n];
		float dark = 0.3f * wet[n] / 600, ice = 0.5f * frost[n] / 600;
		if ((!dark && !ice) || !std::isfinite(p[0] + p[1] + p[2])) continue;
		for (int y = std::max(0, int(p[1] - p[2]) - 2); y <= std::min(fh - 1, int(p[1] + p[2]) + 2); y++)
			for (int x = std::max(0, int(p[0] - p[2]) - 2); x <= std::min(fw - 1, int(p[0] + p[2]) + 2); x++)
			{
				uint32_t s = frame[size_t(y) * fw + x], a = s >> 24;
				if (!a || std::hypot(x + 0.5 - p[0], y + 0.5 - p[1]) > p[2] + 2) continue;
				auto &d = argb[size_t(y) * w + x];
				uint32_t out = d & 0xFF000000;
				for (int c = 0; c < 3; c++)
				{
					float sc = float(s >> (c * 8) & 0xFF), want = sc * (1 - dark);
					want += (a * (c == 0 ? 0.95f : c == 1 ? 0.85f : 0.75f) - want) * ice; // toward pale blue (b, g, r)
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
	if (!buddy || !frame)
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
	for (int i : touching)
	{
		int t = sim->parts[i].type;
		if (t == PT_SPRK) spark = true;
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
		if (!frame || !tdpt_buddy_hit(buddy, x, y)) return false;
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
