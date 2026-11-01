#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct SDL_Window;

// The overlay's macOS side (OverlayMac.mm, plain Cocoa, no TPT headers): a clear borderless window over the screen's
// usable area (under the menu bar, above the Dock), the frame drawn on a CAMetalLayer by its own thread at the screen's
// rate, clicks taken only where something is drawn, a menu bar item for the Windows tray and an AppKit settings window
// for the Windows side window.
namespace OverlayMac
{
	void Init(SDL_Window *window);
	void InitMenu();                 // the menu bar item alone: all overlay:window wants of the above
	void SetScreen(int index);       // which screen the overlay lives on, before anything asks its size
	std::vector<std::string> ScreenNames();
	void ScreenSize(int &w, int &h); // that screen's usable area, in points
	void SetLayer(int layer);        // OverlayLayer as an int: on top, under windows, behind the icons

	// The drawing thread carries a particle on from (x, y) at (vx, vy) sim pixels a frame, or lifts it with the window
	// `ride` (non-zero). colour is 0xAARRGGBB, premultiplied.
	struct Mover
	{
		float x, y, vx, vy;
		uint32_t colour, ride;
	};
	// A window that moved since the frame: where the sim had it (sim pixels), how far it has gone since.
	struct Shift
	{
		int x0, y0, x1, y1;
		float dx, dy;
		uint32_t id;
	};
	// Asked by the drawing thread at every refresh.
	void SetShiftSource(std::vector<Shift> (*source)());
	// Hands the frame (0xAARRGGBB, premultiplied, w x h) to the drawing thread, swapping `frame` with its spare.
	void Present(std::vector<uint32_t> &frame, int w, int h, std::vector<Mover> movers, double frameAt, double period);
	// Clicks land on drawn pixels, on every pixel when catchAll, on none when catchNone; the rest go to the apps
	// underneath. True if the mouse is over a pixel that catches.
	bool UpdateClicks(const uint32_t *argb, int w, int h, bool catchAll, bool catchNone);
	bool CatchesMouse();            // our window takes the mouse right now
	bool Active();                  // the overlay is what's in front (the settings window doesn't count)
	void MouseFrom(double &x, double &y); // the pointer, screen points, top-left origin
	double Now();                   // seconds, monotonic

	struct Rect
	{
		uint32_t id;
		double l, t, r, b; // screen points, top-left origin
		int pid = 0;
		std::string owner; // the app's name
	};
	Rect Window();                    // ours
	std::vector<Rect> OtherWindows(); // other apps' normal windows on screen, front to back
	int FrontPid();                   // the frontmost app's process
	std::string FrontName();          // its name, lower case
	bool FrontFillsScreen();          // it's another app with a window covering the whole screen

	double IdleSeconds(bool keysOnly = false); // since the last input
	bool ButtonDown(int button);               // 0 left, 1 right
	bool ControlDown();
	float CpuLoad();                  // the whole Mac, 0-1, since the last call
	double ProcessCpuSeconds(int pid); // CPU time this process has used; < 0 if it can't be read
	float AudioPeak();                // what's playing, 0-1 (macOS 14.2+; only the level is read)

	// Accessibility: Finder's desktop icons, the Dock's items and the text caret, read on a worker thread.
	struct Icon
	{
		double l, t, r, b;
		uint64_t id;
		std::shared_ptr<const std::vector<uint8_t>> shape; // size x size, 1 = solid; null = the whole square
		int size;
	};
	struct DockItem
	{
		std::string name;
		double l, t, r, b;
		int badge;
		bool trash, running;
	};
	void WantDesktop(bool icons, bool dock, bool caret);
	std::vector<Icon> Icons();
	std::vector<DockItem> DockItems();
	bool Caret(double &x, double &y); // the text cursor, screen points; false when the focused app doesn't report one
	bool MoveWindow(uint32_t id, double x, double y); // a real window (Explosion shake)

	bool AccessibilityGranted();
	void AskAccessibility();
	bool ScreenCaptureGranted();
	void AskScreenCapture();
	// What's on screen under the overlay, scaled to pw x ph, 0x00RRGGBB
	bool Grab(double l, double t, double w, double h, int pw, int ph, uint32_t *out);
	// What's behind a window (for holes in it), a few times a second on a worker thread; px 0x00RRGGBB, w x h sim pixels
	struct Shot
	{
		int w = 0, h = 0;
		std::vector<uint32_t> px;
	};
	void WantShots(const std::vector<uint32_t> &ids, double simPerPoint);
	std::shared_ptr<const Shot> TakeShot(uint32_t id);
	// The wallpaper filling the screen, seen through a w x h buffer laid over the window (0x00RRGGBB)
	void Wallpaper(int w, int h, uint32_t *out);

	void SetHotkeys(const std::vector<std::string> &keys); // "Ctrl+Alt+O"; index = the id TakeHotkey gives
	int TakeHotkey();                                      // the next one pressed, or -1
	void Toast(const std::string &message);                // a short fading notice
	bool Relaunch(const char *extraArg);                   // starts this app again; false if it couldn't

	// The settings window. A row is laid out by kind; everything goes through the functions, so this file knows no TPT.
	struct Row
	{
		enum Kind { Header, Note, Toggle, Slider, Choice, Text, Button } kind = Header;
		std::string label, tip;
		std::function<float()> get;       // Toggle, Slider, Choice (index)
		std::function<void(float)> set;
		float lo = 0, hi = 1;
		std::function<std::string(float)> format; // Slider's value
		std::vector<std::string> options;         // Choice
		std::function<std::string()> getText;     // Text; Note if set
		std::function<void(std::string)> setText;
		std::function<void()> press;              // Button
	};
	struct Tab
	{
		std::string name;
		std::vector<Row> rows;
	};
	void ToggleSettings(const std::vector<Tab> &tabs);
	bool SettingsOpen();
}

// The menu bar item's entries (tags): 0 .. 2 input modes, 10 pause, 11 clear, 12 settings, 99 quit. In OverlayWindows.cpp.
void OverlayMacMenu(int item);
bool OverlayMacChecked(int item);
