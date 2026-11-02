#include "OverlayBuddy.h"
#include "Overlay.h"
#include "OverlayWindows.h"
#include "SimulationConfig.h"
#include "prefs/GlobalPrefs.h"
#include "../buddy/tdpt_buddy.h"
#ifdef _WIN32
#include "common/platform/Platform.h"
#include <SDL.h>
#include <SDL_syswm.h>
#include <windows.h>
#include <vector>
#endif

namespace
{
	Buddy *buddy = nullptr;
	ByteString failed;            // the file that last failed to open: not retried until another is chosen
	const uint32_t *frame = nullptr; // his newest frame, XRES x YRES premultiplied (null: nothing shown)
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
