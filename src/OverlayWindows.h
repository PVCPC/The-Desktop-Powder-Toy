#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include "common/String.h"

// Overlay mode's own desktop windows (element palette, notices), Vista-styled. Windows only so far.
namespace OverlayWindows
{
	void Init(void *mainHwnd);
	void Tick(); // once per frame, from the main thread
	void TogglePalette();
	void SetPassThrough(bool on);
	void Notify(const char *message); // a short fading notice over TPT's window
	bool HideTPTMenus(); // TPT's own side bar and tool strip, replaced by the Elements window while it's open
	// Sim-aware touches to a w-wide 0xAARRGGBB frame whose top-left XRES x YRES is the sim: black particles opaque,
	// everything outside the playable area clear, a faint frame around the area.
	void ApplySimMasks(uint32_t *argb, int w);
	// Clears the w x h frame where our app windows are, the frame being shown at ww x wh: the sim layer sits on top,
	// so this keeps sand off them and lets clicks reach them.
	void ClearUnderWindows(uint32_t *argb, int w, int h, int ww, int wh);
	// The playable area in cells, saved in prefs (Overlay.PlayArea.*).
	void SetPlayArea(int x, int y, int w, int h);
	// Dragging the play area's edges with the mouse (sim pixels). what: 0 move, 1 left button down, 2 up.
	// True if the mouse is busy with an edge, so TPT shouldn't draw.
	bool AreaMouse(int x, int y, int what);
	bool RecordingKey(); // the Overlay tab is waiting for a new shortcut: global hotkeys are off
	// A native Windows dialog: a drop-down (options, choice) over a text field (text), then OK, Cancel and the extra
	// buttons. valid(choice, text) false greys out OK. 1 OK, 0 Cancel, 2 + i extra button i; -1 when the overlay
	// isn't running (use TPT's own window).
	int Form(const String &title, const String &prompt, const std::vector<String> &options, int &choice, String &text,
	         const std::vector<String> &extras, std::function<bool(int, const String &)> valid, int limit = 0);
	// The Lua console as a real window (TPT's ` key); false when the overlay isn't running.
	bool ShowConsole();
	void HideConsole();
}
