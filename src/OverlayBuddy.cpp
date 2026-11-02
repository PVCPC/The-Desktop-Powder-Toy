#include "OverlayBuddy.h"
#include "Overlay.h"
#include "OverlayWindows.h"
#include "SimulationConfig.h"
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

namespace
{
	Buddy *buddy = nullptr;
	ByteString failed;            // the file that last failed to open: not retried until another is chosen
	const uint32_t *frame = nullptr; // his newest frame, XRES x YRES premultiplied (null: nothing shown)
	std::vector<uint8_t> ground;  // the sim around him, for tdpt_buddy_ground
	std::vector<float> air;       // and its air (velocity, pressure), for tdpt_buddy_air
	bool held = false;            // a press that went to him, until its release
	double heldX, heldY;          // where TPT's window last saw the mouse while held (it has the mouse captured)
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
	// what he stands on: windows and icons (not their holes), powders and solids, and walls a powder can't pass (1);
	// liquids as 2 + a quarter of their weight (water 30), which he floats in, acid as 31, VOID and black holes as 30,
	// portals in as 29; then how hot it is (0x20 60 C+, 0x40
	// 400 C+: sets him on fire, 0x60 -50 C or colder) and 0x80 a spark (shocks him)
	int32_t r[4];
	tdpt_buddy_area(buddy, r);
	r[2] = std::clamp(r[2], 0, XRES * 2), r[3] = std::clamp(r[3], 0, YRES * 2);
	ground.assign(size_t(r[2]) * r[3], 0);
	auto *sim = GameController::Ref().GetModel()->GetSimulation();
	auto &elements = SimulationData::CRef().elements;
	int channel = -1; // of a portal in his reach
	for (int y = std::max(r[1], 0); y < std::min(r[1] + r[3], YRES); y++)
		for (int x = std::max(r[0], 0); x < std::min(r[0] + r[2], XRES); x++)
		{
			int p = sim->pmap[y][x];
			auto &c = ground[size_t(y - r[1]) * r[2] + x - r[0]];
			if ((sim->PixelWall(x, y) && !sim->HoleAt(x, y)) || sim->IsWallBlocking(x, y, PT_DUST)) c = 1;
			else if (p && (elements[TYP(p)].Properties & (TYPE_PART | TYPE_SOLID))) c = 1;
			else if (p && (elements[TYP(p)].Properties & TYPE_LIQUID)) c = uint8_t(2 + (std::clamp(elements[TYP(p)].Weight, 0, 100) + 2) / 4);
			if (!p) continue;
			if (TYP(p) == PT_ACID) c = 31;
			if (TYP(p) == PT_VOID || TYP(p) == PT_BHOL) c = 30;
			if (TYP(p) == PT_PRTI)
			{
				c = 29;
				channel = sim->parts[ID(p)].tmp;
			}
			auto t = sim->parts[ID(p)].temp - 273.15f;
			c |= t >= 400 ? 0x40 : t >= 60 ? 0x20 : t <= -50 ? 0x60 : 0;
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
	frame = held ? tdpt_buddy_frame(buddy, heldX, heldY, XRES, YRES) : tdpt_buddy_frame(buddy, mx, my, XRES, YRES);
	if (!frame) return true;
	// his premultiplied pixels over the frame's
	OverlayBands(YRES, [&](int y0, int y1) {
		for (int y = y0; y < y1; y++)
			for (int x = 0; x < XRES; x++)
			{
				uint32_t s = frame[size_t(y) * XRES + x], a = s >> 24;
				if (!a) continue;
				auto &d = argb[size_t(y) * w + x];
				uint32_t k = 256 - a;
				d = s + ((((d & 0xFF00FF) * k) >> 8) & 0xFF00FF) + ((((d & 0xFF00) * k) >> 8) & 0xFF00) + ((((d >> 24) * k) >> 8) << 24);
			}
	});
	return true;
}

void OverlayBuddy::Close()
{
	tdpt_buddy_close(buddy);
	buddy = nullptr;
	frame = nullptr;
	held = false;
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
#ifdef _WIN32
	if (!buddy || !tdpt_buddy_hit(buddy, x, y)) return false;
	std::vector<char> text(tdpt_buddy_menus(buddy, nullptr, 0));
	tdpt_buddy_menus(buddy, text.data(), text.size());
	// the game's menus, as the right-click menu: lines of depth, flags, command, label (tdpt_buddy.h)
	std::vector<HMENU> open{ CreatePopupMenu() };
	std::vector<ByteString> commands;
	for (auto &line : ByteString(text.data()).PartitionBy('\n'))
	{
		auto f = line.PartitionBy('\t', true);
		if (f.size() < 4) continue;
		size_t depth = std::min(size_t(f[0].ToNumber<int>(true)), open.size() - 1);
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
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	HWND owner = SDL_GetMouseFocus() && SDL_GetWindowWMInfo(SDL_GetMouseFocus(), &info) ? info.info.win.window : nullptr;
	POINT p;
	GetCursorPos(&p);
	int cmd = TrackPopupMenu(open.front(), TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, p.x, p.y, 0, owner, nullptr);
	DestroyMenu(open.front());
	if (cmd > 0 && size_t(cmd) <= commands.size()) tdpt_buddy_command(buddy, commands[cmd - 1].c_str());
	return true;
#else
	return false; // ponytail: Windows only; the Mac shim has no submenus yet
#endif
}
