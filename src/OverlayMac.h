#pragma once
#include <cstdint>
#include <vector>

struct SDL_Window;

// The overlay's macOS side (OverlayMac.mm, plain Cocoa, no TPT headers): a clear borderless window over the screen's
// usable area (under the menu bar, above the Dock), the frame shown through a Core Animation layer, clicks taken only
// where something is drawn, and a menu bar item in place of the Windows tray.
namespace OverlayMac
{
	void Init(SDL_Window *window);
	void ScreenSize(int &w, int &h); // the main screen's usable area, in points
	// Shows the frame (0xAARRGGBB, premultiplied, w x h) stretched over the window. Clicks land on drawn pixels, on
	// every pixel when catchAll, on none when catchNone; the rest go to the apps underneath.
	void Present(const uint32_t *argb, int w, int h, bool catchAll, bool catchNone);
	bool Active(); // TPT is the app in front

	struct Rect
	{
		uint32_t id;
		double l, t, r, b; // screen points, top-left origin
	};
	Rect Window(); // ours
	std::vector<Rect> OtherWindows(); // other apps' normal windows on screen
}

// The menu bar item's entries (tags): 0 .. 2 input modes, 10 pause, 11 clear, 99 quit. In OverlayWindows.cpp.
void OverlayMacMenu(int item);
bool OverlayMacChecked(int item);
