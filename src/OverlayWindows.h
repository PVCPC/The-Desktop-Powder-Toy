#pragma once
#include <cstdint>

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
}
