#pragma once
// The little of Win32 that OverlayWindows.cpp's sim-side code (Fun, window walls, marks, the play area) leans on, on top
// of OverlayMac: that code is ~1800 lines of plain sim logic with a call here and there to get the time or the pointer,
// and one copy of it is worth more than two that drift apart. The Windows side keeps its own calls untouched. A "window"
// here is a CGWindowID in a pointer, with two made-up ones for the overlay and the Dock. Screen units are points.
#include "OverlayMac.h"
#include <cstdint>
#include <ctime>

using HWND = void *;
using DWORD = uint32_t;
using LONG = long;
using ULONGLONG = unsigned long long;
struct RECT { long left, top, right, bottom; };
struct POINT { long x, y; };

inline HWND const macOverlay = HWND(uintptr_t(0xFFFFFFF0));
inline HWND const macDock = HWND(uintptr_t(0xFFFFFFF1));

inline RECT MacRect(const OverlayMac::Rect &r)
{
	return { long(std::lround(r.l)), long(std::lround(r.t)), long(std::lround(r.r)), long(std::lround(r.b)) };
}

inline ULONGLONG GetTickCount64() { return ULONGLONG(OverlayMac::Now() * 1000); }
inline DWORD GetTickCount() { return DWORD(GetTickCount64()); }

struct LASTINPUTINFO { DWORD cbSize, dwTime; };
inline bool GetLastInputInfo(LASTINPUTINFO *in)
{
	in->dwTime = GetTickCount() - DWORD(OverlayMac::IdleSeconds() * 1000);
	return true;
}

inline int localtime_s(tm *out, const time_t *t) { return localtime_r(t, out) ? 0 : 1; }

enum { VK_LBUTTON = 1, VK_RBUTTON = 2, VK_CONTROL = 0x11 };
inline short GetAsyncKeyState(int key)
{
	bool down = key == VK_LBUTTON ? OverlayMac::ButtonDown(0) : key == VK_RBUTTON ? OverlayMac::ButtonDown(1) : OverlayMac::ControlDown();
	return down ? short(0x8000) : 0;
}

inline bool GetCursorPos(POINT *p)
{
	double x, y;
	OverlayMac::MouseFrom(x, y);
	*p = { long(std::lround(x)), long(std::lround(y)) };
	return true;
}

// Only ever compared with the overlay: "does the overlay take clicks under the pointer"
enum { GA_ROOT = 2 };
inline HWND WindowFromPoint(POINT) { return OverlayMac::CatchesMouse() ? macOverlay : nullptr; }
inline HWND GetAncestor(HWND h, int) { return h; }

inline HWND GetForegroundWindow()
{
	int pid = OverlayMac::FrontPid();
	for (auto &r : OverlayMac::OtherWindows())
		if (r.pid == pid) return HWND(uintptr_t(r.id));
	return nullptr;
}

inline HWND FindWindowW(const wchar_t *, const wchar_t *) { return macDock; } // only ever asked for the taskbar

bool GetWindowRect(HWND hwnd, RECT *out); // OverlayWindows.cpp: the Dock needs the sim's size

inline bool IsWindow(HWND hwnd)
{
	for (auto &r : OverlayMac::OtherWindows())
		if (r.id == uint32_t(uintptr_t(hwnd))) return true;
	return false;
}

inline bool IsZoomed(HWND) { return false; } // ponytail: a Mac window has no maximised state to leave alone

enum { SWP_NOSIZE = 1, SWP_NOZORDER = 4, SWP_NOACTIVATE = 16, SWP_ASYNCWINDOWPOS = 0x4000 };
inline bool SetWindowPos(HWND hwnd, HWND, long x, long y, int, int, int)
{
	return OverlayMac::MoveWindow(uint32_t(uintptr_t(hwnd)), x, y);
}

inline bool PtInRect(const RECT *r, POINT p)
{
	return p.x >= r->left && p.y >= r->top && p.x < r->right && p.y < r->bottom;
}
