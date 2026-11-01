#pragma once
// The rest of Win32 that the Windows UI in OverlayWindows.cpp is written against, implemented on AppKit in
// OverlayMacWin32.mm: top-level layered windows with a message procedure, GDI text into a DIB, popup menus, monitors. The
// UI paints itself in software on its own canvas, so with this one source draws every theme the same on both systems.
#include "OverlayMacCompat.h"
#include <cstdint>
#include <cwchar>
#include <string>
#include <tuple>

using WBOOL = int;
#ifndef __OBJC__
using BOOL = WBOOL; // Objective-C has its own
#endif
using BYTE = uint8_t;
using UINT = unsigned;
using WORD = uint16_t;
using COLORREF = uint32_t;
using HRESULT = int32_t;
using LONG_PTR = intptr_t;
using WPARAM = uintptr_t;
using LPARAM = intptr_t;
using LRESULT = intptr_t;
using LPWSTR = wchar_t *;
using LPCWSTR = const wchar_t *;
using HDC = void *;
using HFONT = void *;
using HBITMAP = void *;
using HGDIOBJ = void *;
using HICON = void *;
using HCURSOR = void *;
using HMENU = void *;
using HINSTANCE = void *;
using HMODULE = void *;
using HDROP = void *;
using HMONITOR = void *;
#define TRUE 1
#define FALSE 0
#define CALLBACK
#define WINAPI
#define MAX_PATH 4096
#define S_OK 0
#define SUCCEEDED(hr) (HRESULT(hr) >= 0)
#define FAILED(hr) (HRESULT(hr) < 0)
struct SIZE { long cx, cy; };
struct MINMAXINFO { POINT ptReserved, ptMaxSize, ptMaxPosition, ptMinTrackSize, ptMaxTrackSize; };
struct MONITORINFO { DWORD cbSize; RECT rcMonitor, rcWork; DWORD dwFlags; };
struct BITMAPINFOHEADER { DWORD biSize; long biWidth, biHeight; WORD biPlanes, biBitCount; DWORD biCompression; };
struct BITMAPINFO { BITMAPINFOHEADER bmiHeader; };
struct BLENDFUNCTION { BYTE BlendOp, BlendFlags, SourceConstantAlpha, AlphaFormat; };
struct TRACKMOUSEEVENT { DWORD cbSize, dwFlags; HWND hwndTrack; DWORD dwHoverTime; };
using WNDPROC = LRESULT (*)(HWND, UINT, WPARAM, LPARAM);
struct WNDCLASSW { UINT style; WNDPROC lpfnWndProc; int cbClsExtra, cbWndExtra; HINSTANCE hInstance; HICON hIcon; HCURSOR hCursor; void *hbrBackground; LPCWSTR lpszMenuName, lpszClassName; };

enum : UINT
{
	WM_NULL = 0, WM_SIZE = 5, WM_KILLFOCUS = 8, WM_SETICON = 0x80, WM_GETMINMAXINFO = 0x24, WM_NCCALCSIZE = 0x83, WM_NCHITTEST = 0x84,
	WM_NCLBUTTONDOWN = 0xA1, WM_KEYDOWN = 0x100, WM_CHAR = 0x102, WM_MOUSEMOVE = 0x200, WM_LBUTTONDOWN = 0x201, WM_LBUTTONUP = 0x202,
	WM_RBUTTONDOWN = 0x204, WM_RBUTTONUP = 0x205, WM_MBUTTONDOWN = 0x207, WM_MBUTTONUP = 0x208, WM_MOUSEWHEEL = 0x20A, WM_MOUSEACTIVATE = 0x21,
	WM_MOVING = 0x216, WM_CAPTURECHANGED = 0x215, WM_EXITSIZEMOVE = 0x232, WM_DROPFILES = 0x233, WM_MOUSELEAVE = 0x2A3, WM_ENDSESSION = 0x16, WM_APP = 0x8000,
};
enum : DWORD
{
	WS_POPUP = 0x80000000u, WS_MINIMIZEBOX = 0x20000, WS_MAXIMIZEBOX = 0x10000, WS_SYSMENU = 0x80000, WS_THICKFRAME = 0x40000,
	WS_EX_TOPMOST = 8, WS_EX_TRANSPARENT = 0x20, WS_EX_TOOLWINDOW = 0x80, WS_EX_APPWINDOW = 0x40000, WS_EX_LAYERED = 0x80000, WS_EX_NOACTIVATE = 0x8000000,
};
enum { HTTRANSPARENT = -1, HTCLIENT = 1, HTCAPTION = 2, HTLEFT = 10, HTRIGHT, HTTOP, HTTOPLEFT, HTTOPRIGHT, HTBOTTOM, HTBOTTOMLEFT, HTBOTTOMRIGHT };
enum { GWL_EXSTYLE = -20, GWLP_USERDATA = -21, SIZE_MINIMIZED = 1, MA_NOACTIVATE = 3, ICON_SMALL = 0, ICON_BIG = 1, MK_LBUTTON = 1, WHEEL_DELTA = 120, TME_LEAVE = 2 };
enum { SW_HIDE = 0, SW_SHOWNORMAL = 1, SW_MAXIMIZE = 3, SW_SHOWNOACTIVATE = 4, SW_MINIMIZE = 6, SW_RESTORE = 9, SWP_NOMOVE = 2, SWP_FRAMECHANGED = 0x20, SWP_SHOWWINDOW = 0x40 };
enum { MF_STRING = 0, MF_GRAYED = 1, MF_CHECKED = 8, MF_DEFAULT = 0x1000, MF_SEPARATOR = 0x800, MF_BYCOMMAND = 0 };
enum { TPM_LEFTALIGN = 0, TPM_TOPALIGN = 0, TPM_RIGHTBUTTON = 2, TPM_NONOTIFY = 0x80, TPM_RETURNCMD = 0x100 };
enum { MONITOR_DEFAULTTONULL = 0, MONITOR_DEFAULTTONEAREST = 2 };
enum { VK_BACK = 8, VK_RETURN = 13, VK_SHIFT = 16, VK_MENU = 18, VK_ESCAPE = 27, VK_LEFT = 37, VK_UP, VK_RIGHT, VK_DOWN, VK_DELETE = 46, VK_LWIN = 91, VK_RWIN, VK_F1 = 112, VK_F24 = 135 };
enum { FW_LIGHT = 300, FW_NORMAL = 400, FW_SEMIBOLD = 600, FW_BOLD = 700, DEFAULT_CHARSET = 1, ANTIALIASED_QUALITY = 4, TRANSPARENT = 1 };
enum { BI_RGB = 0, DIB_RGB_COLORS = 0, SRCCOPY = 0, AC_SRC_OVER = 0, AC_SRC_ALPHA = 1, ULW_ALPHA = 2, LOGPIXELSY = 90 };
enum { DT_SINGLELINE = 0x20, DT_NOPREFIX = 0x800, DT_END_ELLIPSIS = 0x8000 };
enum { SM_CXVIRTUALSCREEN = 78, SM_CYVIRTUALSCREEN = 79, SM_XVIRTUALSCREEN = 76, SM_YVIRTUALSCREEN = 77, SM_CYSCREEN = 1 };
enum { DWMWA_TRANSITIONS_FORCEDISABLED = 3, MB_YESNO = 4, MB_ICONWARNING = 0x30, MB_DEFBUTTON2 = 0x100, IDYES = 6 };
#define HWND_TOP nullptr
#define HWND_TOPMOST HWND(intptr_t(-1))
#define IDC_ARROW nullptr
#define IDI_APPLICATION nullptr
#define RGB(r, g, b) (COLORREF((r) | ((g) << 8) | ((b) << 16)))
#define GetRValue(c) BYTE(c)
#define GetGValue(c) BYTE((c) >> 8)
#define GetBValue(c) BYTE((c) >> 16)
#define LOWORD(l) WORD(l)
#define HIWORD(l) WORD(uintptr_t(l) >> 16)
#define GET_X_LPARAM(l) int(short(LOWORD(l)))
#define GET_Y_LPARAM(l) int(short(HIWORD(l)))
#define GET_WHEEL_DELTA_WPARAM(w) short(HIWORD(w))
#define MAKELPARAM(l, h) LPARAM((WORD(l)) | (uint32_t(WORD(h)) << 16))

// windows: real NSWindows behind an HWND; a CGWindowID in an HWND still means another app's window
HINSTANCE GetModuleHandleW(LPCWSTR);
HCURSOR LoadCursorW(HINSTANCE, const void *);
WORD RegisterClassW(const WNDCLASSW *);
HWND CreateWindowExW(DWORD ex, LPCWSTR cls, LPCWSTR title, DWORD style, int x, int y, int w, int h, HWND owner, HMENU, HINSTANCE, void *);
bool DestroyWindow(HWND);
LRESULT DefWindowProcW(HWND, UINT, WPARAM, LPARAM);
LRESULT SendMessageW(HWND, UINT, WPARAM, LPARAM);
LONG_PTR GetWindowLongPtrW(HWND, int);
LONG_PTR SetWindowLongPtrW(HWND, int, LONG_PTR);
bool ShowWindow(HWND, int);
bool IsWindowVisible(HWND);
bool IsIconic(HWND);
bool ScreenToClient(HWND, POINT *);
bool ClientToScreen(HWND, POINT *);
HWND SetCapture(HWND);
bool ReleaseCapture();
bool TrackMouseEvent(TRACKMOUSEEVENT *);
bool SetForegroundWindow(HWND);
HWND SetFocus(HWND);
bool UpdateLayeredWindow(HWND, HDC, POINT *, SIZE *, HDC, POINT *, COLORREF, BLENDFUNCTION *, DWORD);
bool DragAcceptFiles(HWND, WBOOL);
UINT DragQueryFileW(HDROP, UINT, wchar_t *, UINT);
void DragFinish(HDROP);
HRESULT DwmSetWindowAttribute(HWND, DWORD, const void *, DWORD);
HRESULT DwmFlush();
void Sleep(DWORD);
// GDI: a DC is a bitmap with a font and a colour selected
HDC GetDC(HWND);
int ReleaseDC(HWND, HDC);
HDC CreateCompatibleDC(HDC);
bool DeleteDC(HDC);
HBITMAP CreateDIBSection(HDC, const BITMAPINFO *, UINT, void **, void *, DWORD);
HGDIOBJ SelectObject(HDC, HGDIOBJ);
bool DeleteObject(HGDIOBJ);
HFONT CreateFontW(int height, int, int, int, int weight, DWORD italic, DWORD underline, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPCWSTR face);
bool GetTextExtentPoint32W(HDC, const wchar_t *, int, SIZE *);
int SetBkMode(HDC, int);
COLORREF SetTextColor(HDC, COLORREF);
int DrawTextW(HDC, const wchar_t *, int, RECT *, UINT);
bool GdiFlush();
int GetDeviceCaps(HDC, int);
int GetSystemMetrics(int);
inline bool OffsetRect(RECT *r, int dx, int dy) { r->left += dx, r->right += dx, r->top += dy, r->bottom += dy; return true; }
inline bool EqualRect(const RECT *a, const RECT *b) { return a->left == b->left && a->top == b->top && a->right == b->right && a->bottom == b->bottom; }
// monitors
HMONITOR MonitorFromWindow(HWND, DWORD);
HMONITOR MonitorFromRect(const RECT *, DWORD);
HMONITOR MonitorFromPoint(POINT, DWORD);
bool GetMonitorInfoW(HMONITOR, MONITORINFO *);
// menus
HMENU CreatePopupMenu();
bool AppendMenuW(HMENU, UINT, uintptr_t, LPCWSTR);
bool DestroyMenu(HMENU);
bool SetMenuDefaultItem(HMENU, UINT, UINT);
int TrackPopupMenu(HMENU, UINT, int, int, int, HWND, const RECT *);
// shell
int MessageBoxW(HWND, LPCWSTR text, LPCWSTR title, UINT);
void *ShellExecuteW(HWND, LPCWSTR verb, LPCWSTR file, LPCWSTR params, LPCWSTR dir, int);
bool GetFullPathNameW_(const wchar_t *, wchar_t *);
#define GetFullPathNameW(path, n, out, part) GetFullPathNameW_(path, out)
DWORD GetModuleFileNameW(HMODULE, wchar_t *, DWORD);
inline UINT ExtractIconExW(const wchar_t *, int, HICON *big, HICON *small_, UINT) { if (big) *big = nullptr; if (small_) *small_ = nullptr; return 0; }
inline void *GetProcAddress(HMODULE, const char *) { return nullptr; }
inline HICON ExtractIconW(HINSTANCE, const wchar_t *, UINT) { return nullptr; }
inline HICON LoadIconW(HINSTANCE, const void *) { return nullptr; }
uint32_t MacWindowNumber(HWND); // the window server's number for one of ours
void MacDockFloor(int simW, int simH, int cell, int windowW, int windowH); // where GetWindowRect(macDock) reports the sim's floor
inline void *GetCurrentThread() { return nullptr; }
inline HRESULT SetThreadDescription(void *, const wchar_t *) { return 0; }
int DeleteFileW(const wchar_t *);
inline bool BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD) { return false; }
inline UINT RegisterWindowMessageW(const wchar_t *) { static UINT next = 0xC000; return next++; }
bool PostMessageW(HWND, UINT, WPARAM, LPARAM);
inline bool CheckMenuRadioItem(HMENU, UINT, UINT, UINT, UINT) { return true; } // MF_CHECKED already shows it
#define swscanf_s swscanf
template<size_t N> int wcscpy_s(wchar_t (&to)[N], const wchar_t *from) { wcsncpy(to, from, N - 1); to[N - 1] = 0; return 0; }
// the tray icon is a status item in the menu bar
enum { NIM_ADD = 0, NIM_MODIFY = 1, NIM_DELETE = 2, NIF_MESSAGE = 1, NIF_ICON = 2, NIF_TIP = 4, NIF_INFO = 16, NIIF_INFO = 1, NIN_BALLOONUSERCLICK = 0x405 };
enum { HSHELL_WINDOWCREATED = 1, HSHELL_FLASH = 0x8006, TPM_BOTTOMALIGN = 0x20 };
struct NOTIFYICONDATAW
{
	DWORD cbSize; HWND hWnd; UINT uID, uFlags, uCallbackMessage; HICON hIcon;
	wchar_t szTip[128], szInfo[256], szInfoTitle[64];
	DWORD dwInfoFlags;
};
bool Shell_NotifyIconW(DWORD, NOTIFYICONDATAW *);
// the colour dialog
enum { CC_RGBINIT = 1, CC_FULLOPEN = 2, CC_ANYCOLOR = 0x100 };
struct CHOOSECOLORW { DWORD lStructSize; HWND hwndOwner; COLORREF rgbResult, *lpCustColors; DWORD Flags; };
bool ChooseColorW(CHOOSECOLORW *);
// what Windows has as a native control or registry key, and the app-level glue
namespace OverlayMac
{
	int Form(const std::wstring &title, const std::wstring &prompt, const std::vector<std::wstring> &options, int &choice, std::wstring &text,
	         const std::vector<std::wstring> &extras, const std::function<bool(int, const std::wstring &)> &valid);
	struct ConsoleHooks
	{
		std::function<void(const std::wstring &)> run; // Enter
		std::function<std::wstring(int)> history;      // Up/Down, by steps from the newest; empty past the end
		std::function<void()> closed;                  // Esc or the close button
	};
	void ConsoleOpen(const ConsoleHooks &hooks);
	void ConsoleClose();
	void ConsoleAppend(const std::wstring &text, bool error);
	bool ConsoleVisible();
	bool StartsAtLogin();
	void SetStartsAtLogin(bool on);
	void OnMain(std::function<void()> f);
	// A drop-down that doesn't stop anything while it's open: items (text, enabled, checked), at screen points. done(i + 1), or
	// done(0) for a click elsewhere.
	void PopupList(const std::vector<std::tuple<std::wstring, bool, bool>> &items, double x, double y, std::function<void(int)> done);
}
