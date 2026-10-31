// Overlay mode's own desktop windows (OVERLAY.md): each menu is a real top-level layered window, drawn here in
// software with per-pixel alpha. The look is Windows Vista Aero; other OS themes will replace the vista:: drawing.
#include "OverlayWindows.h"
#ifdef _WIN32
#include "Overlay.h"
#include "gui/game/GameController.h"
#include "gui/game/GameModel.h"
#include "gui/game/Notification.h"
#include "client/SaveFile.h"
#include "client/SaveInfo.h"
#include "gui/game/GameView.h"
#include "graphics/VideoBuffer.h"
#include "simulation/Simulation.h"
#include "simulation/Air.h"
#include "simulation/ElementGraphics.h"
#include "simulation/SimulationData.h"
#include "SimulationConfig.h"
#include "gui/game/Menu.h"
#include "gui/game/Brush.h"
#include "gui/game/tool/Tool.h"
#include "prefs/GlobalPrefs.h"
#include "common/platform/Platform.h"
#include "graphics/Renderer.h"
#include "gui/game/RenderPreset.h"
#include "gui/game/IntroText.h"
#include "gui/interface/Engine.h"
#include "graphics/Graphics.h"
#include "gui/interface/Window.h"
#include "gui/interface/Button.h"
#include "gui/interface/Checkbox.h"
#include "gui/interface/DropDown.h"
#include "gui/interface/Label.h"
#include "gui/interface/Panel.h"
#include "gui/interface/ProgressBar.h"
#include "gui/interface/Slider.h"
#include "gui/interface/Textbox.h"
#include <algorithm>
#include <atomic>
#include <thread>
#include <tuple>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#undef small       // rpcndr.h, via dwmapi.h
#undef GetUserName // SaveInfo::GetUserName

namespace
{
	float uiScale = 1;
	bool windowWalls = true;            // open windows are walls to particles (Overlay.WindowWalls)
	std::vector<uint8_t> ourWalls;      // XCELLS x YCELLS: 1 where a window put a wall (not the user)
	std::string recording;        // Overlay.Keys.<name> being recorded in the Overlay tab, or empty
	bool rebuildPalette = false;  // the UI scale changed: remake the side window (not from inside its own click)
	int reopenTab = -1;
	int S(float v)
	{
		return int(std::lround(v * uiScale));
	}

	// Premultiplied BGRA, the format UpdateLayeredWindow wants.
	struct Canvas
	{
		int w = 0, h = 0;
		std::vector<uint32_t> px;

		void Resize(int nw, int nh)
		{
			w = nw;
			h = nh;
			px.assign(size_t(w) * h, 0);
		}

		// Blends rgb (0xRRGGBB) at alpha a (0-255) over pixel (x, y).
		void Blend(int x, int y, uint32_t rgb, int a)
		{
			if (x < 0 || y < 0 || x >= w || y >= h || a <= 0)
			{
				return;
			}
			auto &d = px[size_t(y) * w + x];
			int inv = 255 - a;
			auto ch = [&](int shift) {
				int s = (rgb >> shift) & 0xFF;
				int dv = (d >> shift) & 0xFF;
				return uint32_t((s * a + dv * inv) / 255) << shift;
			};
			uint32_t da = d >> 24;
			d = ch(16) | ch(8) | ch(0) | (uint32_t(a + da * inv / 255) << 24);
		}

		// A rounded rectangle with a vertical gradient (top -> bottom colour), antialiased corners.
		void RoundRect(int x, int y, int rw, int rh, int r, uint32_t top, uint32_t bottom, int a, bool squareBottom = false)
		{
			for (int j = 0; j < rh; j++)
			{
				float t = rh > 1 ? float(j) / (rh - 1) : 0;
				uint32_t c = Mix(top, bottom, t);
				for (int i = 0; i < rw; i++)
				{
					float cover = 1;
					int cx = i < r ? r - 1 - i : (i >= rw - r ? i - (rw - r) : -1);
					int cy = j < r ? r - 1 - j : (!squareBottom && j >= rh - r ? j - (rh - r) : -1);
					if (cx >= 0 && cy >= 0)
					{
						float d = std::hypot(cx + 0.5f, cy + 0.5f);
						cover = std::clamp(r - d + 0.5f, 0.f, 1.f);
					}
					Blend(x + i, y + j, c, int(a * cover));
				}
			}
		}

		// A 1px rounded outline: an outer rounded rect minus an inner one, approximated by drawing the ring.
		void RoundFrame(int x, int y, int rw, int rh, int r, uint32_t c, int a, bool squareBottom = false)
		{
			for (int j = 0; j < rh; j++)
			{
				for (int i = 0; i < rw; i++)
				{
					int cx = i < r ? r - 1 - i : (i >= rw - r ? i - (rw - r) : -1);
					int cy = j < r ? r - 1 - j : (!squareBottom && j >= rh - r ? j - (rh - r) : -1);
					float cover;
					if (cx >= 0 && cy >= 0)
					{
						float d = std::hypot(cx + 0.5f, cy + 0.5f);
						cover = std::clamp(r - d + 0.5f, 0.f, 1.f) - std::clamp(r - 1 - d + 0.5f, 0.f, 1.f);
					}
					else
					{
						cover = (i == 0 || j == 0 || i == rw - 1 || j == rh - 1) ? 1.f : 0.f;
					}
					Blend(x + i, y + j, c, int(a * cover));
				}
			}
		}

		static uint32_t Mix(uint32_t a, uint32_t b, float t)
		{
			auto ch = [&](int shift) {
				float va = float((a >> shift) & 0xFF), vb = float((b >> shift) & 0xFF);
				return uint32_t(va + (vb - va) * t + 0.5f) << shift;
			};
			return ch(16) | ch(8) | ch(0);
		}
	};

	// Text: GDI draws white-on-black into a scratch DIB, whose brightness is the coverage we blend with.
	struct Text
	{
		HDC dc = CreateCompatibleDC(nullptr);
		HFONT font = nullptr, bold = nullptr;
		std::map<int, HFONT> styled;

		void SetScale()
		{
			if (font) DeleteObject(font);
			if (bold) DeleteObject(bold);
			for (auto &[key, f] : styled) DeleteObject(f);
			styled.clear();
			font = CreateFontW(-S(12), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI");
			bold = CreateFontW(-S(12), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI");
		}

		SIZE Measure(const std::wstring &s, bool isBold = false)
		{
			SelectObject(dc, isBold ? bold : font);
			SIZE size{};
			GetTextExtentPoint32W(dc, s.c_str(), int(s.size()), &size);
			return size;
		}

		// Segoe UI at size (12 = the normal UI text, before UI scale), weight FW_*, italic, underline.
		struct Style
		{
			int size = 12, weight = FW_NORMAL;
			bool italic = false, underline = false;
			uint32_t rgb = 0x000000;
			int px = 0; // a height in pixels instead of size (ignores UI scale)
		};
		HFONT Font(const Style &st)
		{
			auto &f = styled[(st.px ? 1000 + st.px : st.size) * 10000 + st.weight * 4 + st.italic * 2 + st.underline];
			if (!f)
			{
				f = CreateFontW(st.px ? -st.px : -S(float(st.size)), 0, 0, 0, st.weight, st.italic, st.underline, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI");
			}
			return f;
		}
		SIZE Measure(const std::wstring &s, const Style &st)
		{
			auto normal = font;
			font = Font(st);
			auto size = Measure(s);
			font = normal;
			return size;
		}
		void Draw(Canvas &cv, int x, int y, const std::wstring &s, const Style &st)
		{
			auto normal = font;
			font = Font(st);
			Draw(cv, x, y, s, st.rgb);
			font = normal;
		}

		// Draws s with its top-left at (x, y), clipped to maxW. glow > 0 adds Vista's soft white halo behind it.
		void Draw(Canvas &cv, int x, int y, const std::wstring &s, uint32_t rgb, bool isBold = false, int maxW = 1 << 20, int glow = 0)
		{
			auto size = Measure(s, isBold);
			int pad = glow;
			int tw = std::min(int(size.cx), maxW) + 2 * pad, th = size.cy + 2 * pad;
			if (tw <= 0 || th <= 0)
			{
				return;
			}
			BITMAPINFO bi{};
			bi.bmiHeader = { sizeof(bi.bmiHeader), tw, -th, 1, 32, BI_RGB };
			uint32_t *bits;
			auto dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&bits, nullptr, 0);
			auto old = SelectObject(dc, dib);
			SelectObject(dc, isBold ? bold : font);
			SetBkMode(dc, TRANSPARENT);
			SetTextColor(dc, RGB(255, 255, 255));
			RECT rc{ pad, pad, tw - pad, th - pad };
			DrawTextW(dc, s.c_str(), int(s.size()), &rc, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
			GdiFlush();
			std::vector<int> cover(size_t(tw) * th);
			for (int i = 0; i < tw * th; i++)
			{
				cover[i] = (bits[i] >> 8) & 0xFF;
			}
			if (glow)
			{
				// A box blur of the coverage, brightened: the halo.
				std::vector<int> tmp(cover.size()), halo(cover.size());
				for (int j = 0; j < th; j++)
					for (int i = 0; i < tw; i++)
					{
						int sum = 0;
						for (int k = -glow; k <= glow; k++) sum += cover[j * tw + std::clamp(i + k, 0, tw - 1)];
						tmp[j * tw + i] = sum / (2 * glow + 1);
					}
				for (int j = 0; j < th; j++)
					for (int i = 0; i < tw; i++)
					{
						int sum = 0;
						for (int k = -glow; k <= glow; k++) sum += tmp[std::clamp(j + k, 0, th - 1) * tw + i];
						halo[j * tw + i] = std::min(255, sum / (2 * glow + 1) * 4);
					}
				for (int j = 0; j < th; j++)
					for (int i = 0; i < tw; i++) cv.Blend(x - pad + i, y - pad + j, 0xFFFFFF, halo[j * tw + i] * 3 / 4);
			}
			for (int j = 0; j < th; j++)
				for (int i = 0; i < tw; i++) cv.Blend(x - pad + i, y - pad + j, rgb, cover[j * tw + i]);
			SelectObject(dc, old);
			DeleteObject(dib);
		}
	};
	Text *text;
	bool showTPTMenus = false;

	std::wstring Wide(const String &s)
	{
		return Platform::WinWiden(s.ToUtf8());
	}

	// Walls have no name in TPT (it draws them as icons), so they get their ID's tail: DEFAULT_WL_CNDTW -> CNDTW.
	std::wstring Label(Tool *tool)
	{
		if (tool->Name.size())
		{
			return Wide(tool->Name);
		}
		auto id = tool->Identifier;
		auto cut = id.rfind('_');
		return Platform::WinWiden(cut == id.npos ? id : id.substr(cut + 1));
	}

	// Windows Vista Aero. ponytail: one theme; when a second OS theme lands, these become a Theme interface.
	namespace vista
	{
		constexpr uint32_t glassTop = 0x7FA8D0, glassBottom = 0x4C7BAA; // the default Aero "sky" tint
		constexpr int glassAlpha = 205;

		// The glass frame: a translucent tinted body, a glossy upper half on the title bar, dark outer and light
		// inner edges. The client area (below the title bar, inset by the frame) is left for the caller.
		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title)
		{
			int r = S(6);
			cv.RoundRect(0, 0, cv.w, cv.h, r, glassTop, glassBottom, glassAlpha);
			cv.RoundRect(1, 1, cv.w - 2, titleH / 2, r, 0xFFFFFF, 0xFFFFFF, 60, true); // gloss
			cv.RoundFrame(0, 0, cv.w, cv.h, r, 0x000000, 170);
			cv.RoundFrame(1, 1, cv.w - 2, cv.h - 2, r - 1, 0xFFFFFF, 110);
			text->Draw(cv, S(10), (titleH - text->Measure(title).cy) / 2, title, 0x000000, false, cv.w - S(80), S(5));
			// The client area's sunken edge.
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, 0x000000, 90);
		}

		// A caption button: Vista's glossy red close pill, or the light glass minimise button. hover brightens it.
		enum CaptionKind { CaptionMin, CaptionMax, CaptionClose };
		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind)
		{
			bool close = kind == CaptionClose;
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			uint32_t top = close ? (hover ? 0xF3A08A : 0xE0907C) : (hover ? 0xF0F7FF : 0xE3ECF6);
			uint32_t bottom = close ? (hover ? 0xD8341A : 0xC1361D) : (hover ? 0x5FA3E8 : 0x9AB5D3);
			cv.RoundRect(rc.left, rc.top, w, h, S(3), top, bottom, close ? 255 : 235);
			cv.RoundRect(rc.left + 1, rc.top + 1, w - 2, h / 2, S(3), 0xFFFFFF, 0xFFFFFF, 70, true);
			cv.RoundFrame(rc.left, rc.top, w, h, S(3), close ? 0x5A1E12 : 0x1F3B5A, 220);
			int cx = rc.left + w / 2, cy = rc.top + h / 2;
			if (close)
			{
				// The white x.
				int k = S(3.5f);
				for (int i = -k; i <= k; i++)
					for (int t = 0; t < S(2); t++)
					{
						cv.Blend(cx + i + t, cy + i, 0xFFFFFF, 255);
						cv.Blend(cx + i + t, cy - i, 0xFFFFFF, 255);
					}
			}
			else if (kind == CaptionMax)
			{
				// A white window outline with a thick top, outlined dark.
				int bw = S(9), bh = S(7), bx = cx - bw / 2, by = cy - bh / 2;
				cv.RoundFrame(bx - 1, by - 1, bw + 2, bh + 2, 0, 0x1F3B5A, 200);
				cv.RoundFrame(bx, by, bw, bh, 0, 0xFFFFFF, 255);
				cv.RoundRect(bx, by, bw, S(2), 0, 0xFFFFFF, 0xFFFFFF, 255);
			}
			else
			{
				// The white bar, outlined dark like Vista's.
				int bw = S(9), bh = S(3), bx = cx - bw / 2, by = cy + S(1);
				cv.RoundRect(bx - 1, by - 1, bw + 2, bh + 2, 0, 0x1F3B5A, 0x1F3B5A, 200);
				cv.RoundRect(bx, by, bw, bh, 0, 0xFFFFFF, 0xFFFFFF, 255);
			}
		}

		// A Vista push button. state: 0 normal, 1 hover, 2 pressed/selected.
		void Button(Canvas &cv, int x, int y, int w, int h, int state)
		{
			static const uint32_t top[] = { 0xF2F2F2, 0xEAF6FD, 0xE5F4FC }, mid1[] = { 0xEBEBEB, 0xD9F0FC, 0xC4E5F6 },
				mid2[] = { 0xDDDDDD, 0xBEE6FD, 0x98D1EF }, bottom[] = { 0xCFCFCF, 0xA7D9F5, 0x68B3DB },
				edge[] = { 0x707070, 0x3C7FB1, 0x2C628B };
			int r = S(3);
			cv.RoundRect(x, y, w, h / 2, r, top[state], mid1[state], 255, true);
			cv.RoundRect(x, y + h / 2, w, h - h / 2, r, mid2[state], bottom[state], 255);
			// Square off the seam between the halves.
			for (int i = 0; i < w; i++) cv.Blend(x + i, y + h / 2 - 1, mid1[state], 255);
			cv.RoundFrame(x, y, w, h, r, edge[state], 255);
			cv.RoundFrame(x + 1, y + 1, w - 2, h - 2, r - 1, 0xFFFFFF, 120);
		}

		// An Explorer navigation-pane item: plain, or a soft blue highlight.
		void NavItem(Canvas &cv, int x, int y, int w, int h, int state)
		{
			if (state == 0) return;
			cv.RoundRect(x, y, w, h, S(2), state == 2 ? 0xDCEBFC : 0xF2F8FE, state == 2 ? 0xC1DBFC : 0xE3EFFD, 255);
			cv.RoundFrame(x, y, w, h, S(2), state == 2 ? 0x7DA2CE : 0xB8D6FB, 255);
		}
	}

	// A top-level layered window that repaints its Canvas whenever Invalidate() is called.
	struct Panel
	{
		HWND hwnd = nullptr;
		Canvas cv;
		ByteString name;
		std::wstring title;
		bool dirty = true;
		int opacity = 255;
		// An app window: taskbar button, normal z-order, resizable, minimise/maximise. Otherwise a small on-top popup
		// (notifications).
		bool appWindow = false;
		int grip = 0; // resize border width
		Vec2<int> minSize{ 0, 0 };
		HWND owner = nullptr; // an owned window: no taskbar button, normal z-order, minimises with its owner

		virtual ~Panel()
		{
			if (hwnd)
			{
				SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
				DestroyWindow(hwnd);
			}
		}
		virtual void Paint() = 0;
		virtual LRESULT HitTest(int, int) { return HTCLIENT; }
		virtual void Mouse(UINT, int, int, WPARAM) {}

		void Create(int x, int y, int w, int h)
		{
			static bool registered = false;
			if (!registered)
			{
				WNDCLASSW wc{};
				wc.lpfnWndProc = Proc;
				wc.hInstance = GetModuleHandleW(nullptr);
				wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
				wc.lpszClassName = L"TPTOverlayPanel";
				RegisterClassW(&wc);
				registered = true;
			}
			cv.Resize(w, h);
			// NOACTIVATE either way: the keyboard stays with TPT. ponytail: windows with text fields will need focus.
			DWORD ex = WS_EX_LAYERED | WS_EX_NOACTIVATE | (appWindow ? WS_EX_APPWINDOW : owner ? 0 : WS_EX_TOOLWINDOW | WS_EX_TOPMOST);
			DWORD style = WS_POPUP | (appWindow ? WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_THICKFRAME : 0);
			hwnd = CreateWindowExW(ex, L"TPTOverlayPanel", title.empty() ? Platform::WinWiden(name).c_str() : title.c_str(), style, x, y, w, h,
			                       owner, nullptr, GetModuleHandleW(nullptr), nullptr);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, LONG_PTR(this));
			BOOL noFade = TRUE;
			DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &noFade, sizeof(noFade));
			// Creation measured the client area before Proc could say "no frame"; measure again, or the canvas
			// comes out smaller than the window.
			SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
			if (appWindow)
			{
				wchar_t exe[MAX_PATH];
				GetModuleFileNameW(nullptr, exe, MAX_PATH);
				HICON big = nullptr, small = nullptr;
				ExtractIconExW(exe, 0, &big, &small, 1);
				if (big) SendMessageW(hwnd, WM_SETICON, ICON_BIG, LPARAM(big));
				if (small) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, LPARAM(small));
			}
		}

		void Present()
		{
			if (dirty)
			{
				std::fill(cv.px.begin(), cv.px.end(), 0);
				Paint();
				dirty = false;
			}
			HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
			BITMAPINFO bi{};
			bi.bmiHeader = { sizeof(bi.bmiHeader), cv.w, -cv.h, 1, 32, BI_RGB };
			void *bits;
			auto dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
			memcpy(bits, cv.px.data(), cv.px.size() * 4);
			auto old = SelectObject(dc, dib);
			POINT origin{ 0, 0 };
			SIZE size{ cv.w, cv.h };
			BLENDFUNCTION blend{ AC_SRC_OVER, 0, BYTE(opacity), AC_SRC_ALPHA };
			UpdateLayeredWindow(hwnd, screen, nullptr, &size, dc, &origin, 0, &blend, ULW_ALPHA);
			SelectObject(dc, old);
			DeleteObject(dib);
			DeleteDC(dc);
			ReleaseDC(nullptr, screen);
		}

		void SavePosition()
		{
			if (IsIconic(hwnd) || IsZoomed(hwnd))
			{
				return;
			}
			RECT rc;
			GetWindowRect(hwnd, &rc);
			auto &prefs = GlobalPrefs::Ref();
			prefs.Set("Overlay.Windows." + name + ".X", int(rc.left));
			prefs.Set("Overlay.Windows." + name + ".Y", int(rc.top));
			prefs.Set("Overlay.Windows." + name + ".W", int(rc.right - rc.left));
			prefs.Set("Overlay.Windows." + name + ".H", int(rc.bottom - rc.top));
		}

		// Resize borders and corners, then the window's own areas.
		LRESULT FrameHitTest(int x, int y)
		{
			if (appWindow && !IsZoomed(hwnd))
			{
				bool l = x < grip, r = x >= cv.w - grip, t = y < grip, b = y >= cv.h - grip;
				if (t && l) return HTTOPLEFT;
				if (t && r) return HTTOPRIGHT;
				if (b && l) return HTBOTTOMLEFT;
				if (b && r) return HTBOTTOMRIGHT;
				if (l) return HTLEFT;
				if (r) return HTRIGHT;
				if (t) return HTTOP;
				if (b) return HTBOTTOM;
			}
			return HitTest(x, y);
		}

		static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
		{
			auto *self = (Panel *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
			if (self)
			{
				switch (msg)
				{
				case WM_NCHITTEST:
				{
					POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
					ScreenToClient(hwnd, &p);
					return self->FrameHitTest(p.x, p.y);
				}
				case WM_NCCALCSIZE:
					if (self->appWindow && wp)
					{
						return 0; // we draw the whole window, frame included
					}
					break;
				case WM_SIZE:
					if (wp != SIZE_MINIMIZED && (LOWORD(lp) != self->cv.w || HIWORD(lp) != self->cv.h))
					{
						self->cv.Resize(LOWORD(lp), HIWORD(lp));
						self->dirty = true;
						self->Present();
					}
					return 0;
				case WM_GETMINMAXINFO:
				{
					auto *mm = (MINMAXINFO *)lp;
					mm->ptMinTrackSize = { self->minSize.X, self->minSize.Y };
					// Maximised = the monitor's work area, not over the taskbar.
					MONITORINFO mi{ sizeof(mi) };
					if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi))
					{
						mm->ptMaxPosition = { mi.rcWork.left - mi.rcMonitor.left, mi.rcWork.top - mi.rcMonitor.top };
						mm->ptMaxSize = { mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top };
					}
					return 0;
				}
				case WM_MOUSEACTIVATE:
					if (self->appWindow)
					{
						// Come to the front like any window, without taking the keyboard from TPT.
						SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
					}
					return MA_NOACTIVATE;
				case WM_EXITSIZEMOVE:
					self->SavePosition();
					return 0;
				case WM_MOUSEMOVE:
				{
					TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
					TrackMouseEvent(&tme);
					self->Mouse(msg, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), wp);
					return 0;
				}
				case WM_MOUSELEAVE:
				case WM_LBUTTONDOWN:
				case WM_LBUTTONUP:
				case WM_RBUTTONDOWN:
				case WM_RBUTTONUP:
				case WM_MBUTTONDOWN:
				case WM_MBUTTONUP:
					self->Mouse(msg, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), wp);
					return 0;
				case WM_MOUSEWHEEL:
				{
					POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
					ScreenToClient(hwnd, &p);
					self->Mouse(msg, p.x, p.y, wp);
					return 0;
				}
				}
			}
			return DefWindowProcW(hwnd, msg, wp, lp);
		}
	};

	bool Inside(const RECT &rc, int x, int y)
	{
		return x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom;
	}

	// The side window: every TPT menu as a tab. Elements has TPT's menu sections down the left (Explorer's
	// navigation pane) and the section's tools as Vista buttons; left click picks the primary tool, right click the
	// secondary, as in TPT. The other tabs are immediate-mode Vista controls: each paint rebuilds the list of
	// clickable rectangles (hits) that the mouse handler uses.
	struct SideWindow : Panel
	{
		struct Hit
		{
			RECT rc;
			std::function<void()> left, right;
			std::wstring tip;
		};
		std::vector<Hit> hits;
		int hover = -1;
		int tab = 0, section = 0, scroll = 0;
		int titleH, border, tabH, navW, statusH, tileW, tileH, gap;
		uint64_t shownState = 0;
		static constexpr const wchar_t *tabNames[] = { L"Elements", L"Tools", L"Simulation", L"Display", L"Saves", L"Overlay", L"Welcome" };
		static constexpr int toolsTab = 1, welcomeTab = 6;

		SideWindow()
		{
			name = "Palette"; // keeps the saved position of the old Elements window
			title = L"The Powder Toy";
			appWindow = true;
			tab = reopenTab >= 0 ? reopenTab : welcomeTab; // the overlay's start page, in place of TPT's text over the game
			titleH = S(28), border = S(6), tabH = S(24), navW = S(118), statusH = S(22), tileW = S(92), tileH = S(24), gap = S(4);
		}

		std::vector<Menu *> Sections()
		{
			std::vector<Menu *> out;
			for (auto *menu : GameController::Ref().GetMenuList())
			{
				if (menu->GetVisible())
				{
					out.push_back(menu);
				}
			}
			return out;
		}

		std::vector<Tool *> Tools()
		{
			auto sections = Sections();
			std::vector<Tool *> out;
			if (section < int(sections.size()))
			{
				for (auto *tool : sections[section]->GetToolList())
				{
					if (tool->MenuVisible)
					{
						out.push_back(tool);
					}
				}
			}
			return out;
		}

		RECT CloseRect()
		{
			return { cv.w - border - S(43), 1, cv.w - border, 1 + S(18) };
		}
		RECT MaxRect()
		{
			auto close = CloseRect();
			return { close.left - S(27), close.top, close.left, close.bottom };
		}
		RECT MinRect()
		{
			auto max = MaxRect();
			return { max.left - S(27), max.top, max.left, max.bottom };
		}
		// Below the tabs, above the status bar.
		RECT Content()
		{
			return { border, titleH + tabH, cv.w - border, cv.h - border - statusH };
		}
		RECT ToolArea()
		{
			auto c = Content();
			return { c.left + navW + gap, c.top + gap, c.right - gap, c.bottom };
		}
		int Columns()
		{
			auto a = ToolArea();
			return std::max(1, int((a.right - a.left + gap) / (tileW + gap)));
		}
		RECT TileRect(int i)
		{
			auto a = ToolArea();
			int c = Columns();
			int x = a.left + (i % c) * (tileW + gap), y = a.top + (i / c) * (tileH + gap) - scroll;
			return { x, y, x + tileW, y + tileH };
		}
		RECT NavRect(int i)
		{
			auto c = Content();
			int h = S(22);
			return { c.left + S(3), c.top + S(4) + i * h, c.left + navW - S(3), c.top + S(4) + (i + 1) * h - S(1) };
		}

		bool Hovered(const RECT &rc)
		{
			return hover >= 0 && hover < int(hits.size()) && EqualRect(&hits[hover].rc, &rc);
		}

		RECT Add(RECT rc, std::function<void()> left, std::wstring tip = L"", std::function<void()> right = nullptr)
		{
			hits.push_back({ rc, std::move(left), std::move(right), std::move(tip) });
			return rc;
		}

		// --- Vista controls; each draws itself and registers its hit rectangle.

		void PushButton(int x, int y, int w, const std::wstring &label, std::function<void()> action, const std::wstring &tip = L"", bool pressed = false)
		{
			RECT rc{ x, y, x + w, y + S(23) };
			vista::Button(cv, x, y, w, S(23), pressed ? 2 : (Hovered(rc) ? 1 : 0));
			auto size = text->Measure(label);
			text->Draw(cv, x + std::max(S(3), int(w - size.cx) / 2), y + (S(23) - size.cy) / 2, label, 0x000000, false, w - S(6));
			Add(rc, std::move(action), tip);
		}

		// A check box (round = radio button) with its label; returns the width used.
		int Toggle(int x, int y, const std::wstring &label, bool on, bool round, std::function<void()> action, const std::wstring &tip = L"")
		{
			int b = S(13), w = b + S(6) + text->Measure(label).cx;
			RECT rc{ x, y, x + w, y + S(20) };
			bool hot = Hovered(rc);
			int by = y + (S(20) - b) / 2, r = round ? b / 2 : S(2);
			cv.RoundRect(x, by, b, b, r, hot ? 0xDEF3FD : 0xF4F4F4, hot ? 0xB1DFFD : 0xDEDEDE, 255);
			cv.RoundFrame(x, by, b, b, r, hot ? 0x3C7FB1 : 0x8E8F8F, 255);
			if (on && round)
			{
				int d = b / 2;
				cv.RoundRect(x + (b - d) / 2, by + (b - d) / 2, d, d, d / 2, 0x8AD0F2, 0x1C64B4, 255);
			}
			if (on && !round)
			{
				int cx = x + b / 2, cy = by + b / 2, k = S(2.5f);
				for (int t = 0; t < S(1.6f); t++)
				{
					for (int i = 0; i <= k; i++) cv.Blend(cx - k - 1 + i, cy + i - 1 + t, 0x1C3D6E, 255);
					for (int i = 0; i <= 2 * k; i++) cv.Blend(cx - 1 + i, cy + k - 1 - i + t, 0x1C3D6E, 255);
				}
			}
			text->Draw(cv, x + b + S(6), y + (S(20) - text->Measure(label).cy) / 2, label, 0x000000);
			Add(rc, std::move(action), tip);
			return w;
		}

		// A Vista group box: a rounded pale outline with a blue heading; returns the top of its inside.
		int Group(int x, int y, int w, int h, const std::wstring &title)
		{
			cv.RoundFrame(x, y + S(8), w, h - S(8), S(3), 0xD5DFE5, 255);
			auto size = text->Measure(title);
			cv.RoundRect(x + S(8), y, size.cx + S(8), size.cy, 0, 0xFFFFFF, 0xFFFFFF, 255);
			text->Draw(cv, x + S(12), y, title, 0x1E395B);
			return y + size.cy + S(4);
		}

		void Caption(int x, int y, const std::wstring &s, uint32_t rgb = 0x000000)
		{
			text->Draw(cv, x, y + (S(20) - text->Measure(s).cy) / 2, s, rgb);
		}

		// --- tabs

		void PaintElements()
		{
			auto c = Content();
			cv.RoundRect(c.left, c.top, navW, c.bottom - c.top, 0, 0xF1F5FB, 0xE5EDF8, 255);
			for (int j = c.top; j < c.bottom; j++) cv.Blend(c.left + navW, j, 0xA0AFC3, 255);
			auto sections = Sections();
			for (int i = 0; i < int(sections.size()); i++)
			{
				auto rc = NavRect(i);
				vista::NavItem(cv, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, i == section ? 2 : (Hovered(rc) ? 1 : 0));
				text->Draw(cv, rc.left + S(6), rc.top + S(3), Wide(sections[i]->GetDescription()), 0x1E395B, i == section, rc.right - rc.left - S(10));
				Add(rc, [this, i] { section = i, scroll = 0; });
			}
			auto tools = Tools();
			auto area = ToolArea();
			auto &gc = GameController::Ref();
			auto *primary = gc.GetActiveTool(0), *secondary = gc.GetActiveTool(1);
			for (int i = 0; i < int(tools.size()); i++)
			{
				auto rc = TileRect(i);
				if (rc.bottom <= area.top || rc.top >= area.bottom)
				{
					continue;
				}
				auto *tool = tools[i];
				bool selected = tool == primary || tool == secondary;
				Canvas tile;
				tile.Resize(tileW, tileH);
				vista::Button(tile, 0, 0, tileW, tileH, selected ? 2 : (Hovered(rc) ? 1 : 0));
				if (tool == primary) tile.RoundFrame(0, 0, tileW, tileH, S(3), 0xD02020, 255);
				if (tool == secondary) tile.RoundFrame(1, 1, tileW - 2, tileH - 2, S(3), 0x2050D0, 255);
				auto col = tool->Colour;
				uint32_t rgb = (col.Red << 16) | (col.Green << 8) | col.Blue;
				int sw = tileH - S(10);
				// TPT's own icon (walls, tools, some elements), scaled up; otherwise a colour swatch.
				if (auto icon = tool->GetTexture(Vec2(26, 14)))
				{
					int iw = S(26), ih = S(14), iy = (tileH - ih) / 2;
					auto size = icon->Size();
					for (int y = 0; y < ih; y++)
						for (int x = 0; x < iw; x++) tile.Blend(S(5) + x, iy + y, icon->Data()[(y * size.Y / ih) * size.X + x * size.X / iw] & 0xFFFFFF, 255);
					tile.RoundFrame(S(5) - 1, iy - 1, iw + 2, ih + 2, 0, 0x404040, 200);
					sw = iw;
				}
				else
				{
					tile.RoundRect(S(5), S(5), sw, sw, S(2), rgb, rgb, 255);
					tile.RoundFrame(S(5), S(5), sw, sw, S(2), 0x404040, 200);
				}
				text->Draw(tile, S(9) + sw, (tileH - text->Measure(Label(tool)).cy) / 2, Label(tool), 0x000000, false, tileW - sw - S(12));
				// Copy the tile in, clipped to the tool area.
				for (int y = 0; y < tileH; y++)
				{
					int dy = rc.top + y;
					if (dy < area.top || dy >= area.bottom) continue;
					for (int x = 0; x < tileW; x++)
					{
						auto p = tile.px[y * tileW + x];
						cv.Blend(rc.left + x, dy, p & 0xFFFFFF, p >> 24);
					}
				}
				RECT visible{ rc.left, std::max(rc.top, area.top), rc.right, std::min(rc.bottom, area.bottom) };
				Add(visible, [tool] { GameController::Ref().SetActiveTool(0, tool); }, Wide(tool->Description),
				    [tool] { GameController::Ref().SetActiveTool(1, tool); });
			}
		}

		void PaintSimulation()
		{
			auto c = Content();
			auto &gc = GameController::Ref();
			auto *model = gc.GetModel();
			auto *sim = model->GetSimulation();
			int x = c.left + S(10), w = c.right - c.left - S(20), y = c.top + S(8);

			// The playable area, in cells of 4 pixels.
			int top = Group(x, y, w, S(118), L"Play area");
			auto a = sim->playArea;
			Caption(x + S(12), top, L"Size: " + std::to_wstring(a.size.X * CELL) + L" \u00D7 " + std::to_wstring(a.size.Y * CELL) +
			      L" pixels   (largest " + std::to_wstring(XRES) + L" \u00D7 " + std::to_wstring(YRES) + L")");
			int step = 8; // cells
			auto set = [](Rect<int> r) { OverlayWindows::SetPlayArea(r.pos.X, r.pos.Y, r.size.X, r.size.Y); };
			// Grow or shrink about the centre.
			auto resize = [a, set](int dw, int dh) {
				auto r = a;
				r.size.X = std::clamp(r.size.X + dw, 8, CELLS.X);
				r.size.Y = std::clamp(r.size.Y + dh, 8, CELLS.Y);
				r.pos.X = std::clamp(a.pos.X - (r.size.X - a.size.X) / 2, 0, CELLS.X - r.size.X);
				r.pos.Y = std::clamp(a.pos.Y - (r.size.Y - a.size.Y) / 2, 0, CELLS.Y - r.size.Y);
				set(r);
			};
			int by = top + S(24), bw = S(34);
			Caption(x + S(12), by, L"Width");
			PushButton(x + S(60), by, bw, L"\u2212", [=] { resize(-step, 0); }, L"Narrower by 32 pixels");
			PushButton(x + S(60) + bw + S(4), by, bw, L"+", [=] { resize(step, 0); }, L"Wider by 32 pixels");
			Caption(x + S(160), by, L"Height");
			PushButton(x + S(210), by, bw, L"\u2212", [=] { resize(0, -step); }, L"Shorter by 32 pixels");
			PushButton(x + S(210) + bw + S(4), by, bw, L"+", [=] { resize(0, step); }, L"Taller by 32 pixels");
			by += S(30);
			int pw = (w - S(24) - 2 * S(6)) / 3;
			PushButton(x + S(12), by, pw, L"Whole screen", [=] { set(CELLS.OriginRect()); }, L"Use the whole sim");
			PushButton(x + S(12) + pw + S(6), by, pw, L"Stock TPT size", [=] {
				auto stock = Vec2(std::min(153, CELLS.X), std::min(96, CELLS.Y));
				set(RectSized((CELLS - stock) / 2, stock));
			}, L"612 \u00D7 384, centred: saves made at this size open in normal TPT");
			PushButton(x + S(12) + 2 * (pw + S(6)), by, pw, L"Centre", [=] { set(RectSized((CELLS - a.size) / 2, a.size)); }, L"Move the area to the middle");
			y += S(126);

			top = Group(x, y, w, S(60), L"Running");
			bool paused = gc.GetPaused();
			int bw4 = (w - S(24) - 3 * S(6)) / 4;
			PushButton(x + S(12), top, bw4, paused ? L"Resume" : L"Pause", [] { auto &g = GameController::Ref(); g.SetPaused(!g.GetPaused()); }, L"Space", paused);
			PushButton(x + S(12) + bw4 + S(6), top, bw4, L"Step", [] { GameController::Ref().FrameStep(); }, L"One frame (F)");
			PushButton(x + S(12) + 2 * (bw4 + S(6)), top, bw4, L"Reload", [] { GameController::Ref().ReloadSim(); }, L"Back to the last loaded save (F5)");
			PushButton(x + S(12) + 3 * (bw4 + S(6)), top, bw4, L"Clear", [] { GameController::Ref().ClearSim(); }, L"Empty the sim");
			y += S(68);

			top = Group(x, y, w, S(76), L"Edges");
			Toggle(x + S(12), top + S(24), L"Open windows are walls (sand piles on title bars)", windowWalls, false, [] {
				windowWalls = !windowWalls;
				GlobalPrefs::Ref().Set("Overlay.WindowWalls", windowWalls);
			}, L"Only while the sim is on top of every window");
			const wchar_t *edges[] = { L"Void", L"Solid", L"Loop" };
			const wchar_t *edgeTips[] = { L"Particles and air leave at the edges", L"The edges are walls", L"Particles wrap around (whole screen only for now)" };
			int ex = x + S(12);
			for (int i = 0; i < 3; i++)
			{
				ex += Toggle(ex, top, edges[i], gc.GetEdgeMode() == i, true, [i] { GameController::Ref().SetEdgeMode(i); }, edgeTips[i]) + S(18);
			}
			y += S(84);

			top = Group(x, y, w, S(76), L"Gravity");
			const wchar_t *gravs[] = { L"Vertical", L"Off", L"Radial", L"Custom" };
			int gx = x + S(12);
			for (int i = 0; i < 4; i++)
			{
				gx += Toggle(gx, top, gravs[i], sim->gravityMode == i, true, [sim, i] { sim->gravityMode = i; }) + S(18);
			}
			Toggle(x + S(12), top + S(24), L"Newtonian gravity", model->GetNewtonianGrvity(), false,
			       [model] { model->SetNewtonianGravity(!model->GetNewtonianGrvity()); }, L"Heavy particles pull on each other (N)");
			y += S(84);

			top = Group(x, y, w, S(100), L"Air");
			const wchar_t *airs[] = { L"On", L"Pressure off", L"Velocity off", L"Off", L"No update" };
			int ax = x + S(12), ay = top;
			for (int i = 0; i < 5; i++)
			{
				int tw = text->Measure(airs[i]).cx + S(19);
				if (ax + tw > x + w - S(12))
				{
					ax = x + S(12), ay += S(24);
				}
				ax += Toggle(ax, ay, airs[i], sim->air->airMode == i, true, [sim, i] { sim->air->airMode = i; }) + S(18);
			}
			Toggle(x + S(12), ay + S(24), L"Ambient heat", gc.GetAHeatEnable(), false, [] { GameController::Ref().ToggleAHeat(); }, L"The air carries heat (U)");
		}

		void PaintDisplay()
		{
			auto c = Content();
			auto &gc = GameController::Ref();
			auto *model = gc.GetModel();
			int x = c.left + S(10), w = c.right - c.left - S(20), y = c.top + S(8);
			auto &presets = Renderer::renderModePresets;
			int rows = (int(presets.size()) + 3) / 4;
			int top = Group(x, y, w, S(20) + rows * S(29), L"View mode");
			int bw = (w - S(24) - 3 * S(6)) / 4;
			for (int i = 0; i < int(presets.size()); i++)
			{
				PushButton(x + S(12) + (i % 4) * (bw + S(6)), top + (i / 4) * S(29), bw, Wide(presets[i].Name.EndsWith(" Display") ? presets[i].Name.Substr(0, presets[i].Name.size() - 8) : presets[i].Name),
				           [i] { GameController::Ref().LoadRenderPreset(i); }, L"Number key " + std::to_wstring(i == 9 ? 0 : i + 1));
			}
			y += S(28) + rows * S(29);
			int cw = (w - S(24)) / 3; // three columns
			auto &rs = model->GetRendererSettings();
			top = Group(x, y, w, S(68), L"Show");
			Toggle(x + S(12), top, L"HUD", gc.GetHudEnable(), false, [] { auto &g = GameController::Ref(); g.SetHudEnable(!g.GetHudEnable()); }, L"Frame rate, particle count and cursor info (H)");
			Toggle(x + S(12) + cw, top, L"Decorations", model->GetDecoration(), false, [] { GameController::Ref().SetDecoration(); }, L"Painted colours (Ctrl+B)");
			Toggle(x + S(12) + 2 * cw, top, L"Gravity field", model->GetGravityGrid(), false, [] { GameController::Ref().ShowGravityGrid(); }, L"Newtonian gravity lines (Ctrl+G)");
			struct Extra { const wchar_t *name; uint32_t flag; const wchar_t *tip; } extras[] = {
				{ L"Fancy", DISPLAY_EFFE, L"Moving solids, stickman guns and other extras" },
				{ L"Lensing", DISPLAY_WARP, L"Newtonian gravity bends light" },
				{ L"Persistent", DISPLAY_PERS, L"Paths stay on screen for a while" },
			};
			for (int i = 0; i < 3; i++)
			{
				bool on = rs.displayMode & extras[i].flag;
				Toggle(x + S(12) + i * cw, top + S(24), extras[i].name, on, false, [&rs, flag = extras[i].flag] { rs.displayMode ^= flag; }, extras[i].tip);
			}
			y += S(76);

			// TPT's render options (its whole-screen view sits under the taskbar here), as switches. The view-mode
			// presets above set all three groups at once.
			struct Mode { const wchar_t *name; uint32_t flag; const wchar_t *tip; };
			auto grid = [&](const wchar_t *title, std::initializer_list<Mode> modes, uint32_t &field, int kind) {
				int rows = (int(modes.size()) + 2) / 3;
				int gt = Group(x, y, w, S(20) + rows * S(24), title);
				int i = 0;
				for (auto &m : modes)
				{
					bool on = kind == 2 ? field == m.flag : (field & m.flag) == m.flag;
					Toggle(x + S(12) + (i % 3) * cw, gt + (i / 3) * S(24), m.name, on, kind != 0, [&field, flag = m.flag, kind, on] {
						if (kind == 2) field = on ? 0 : flag;                                   // colours: one or none
						else if (kind == 1) field = (field & ~DISPLAY_AIR & ~flag) | (on ? 0 : flag); // air views: one at a time
						else field = on ? field & ~flag : field | flag;
					}, m.tip);
					i++;
				}
				y += S(28) + rows * S(24);
			};
			grid(L"Effects", {
				{ L"Effects", RENDER_EFFE, L"Special flare effects on some elements" },
				{ L"Fire", RENDER_FIRE, L"Fire effect for gases" },
				{ L"Glow", RENDER_GLOW, L"Glow on some elements" },
				{ L"Blur", RENDER_BLUR, L"Blur for liquids" },
				{ L"Blob", RENDER_BLOB, L"Draws everything as blobs" },
				{ L"Basic", RENDER_BASC, L"Basic drawing; without it most things are invisible" },
				{ L"Spark glow", RENDER_SPRK, L"Glow on sparks" },
			}, rs.renderMode, 0);
			grid(L"Air (one at a time)", {
				{ L"Pressure", DISPLAY_AIRP, L"Red is positive pressure, blue negative" },
				{ L"Velocity", DISPLAY_AIRV, L"Air velocity and positive pressure" },
				{ L"Both", DISPLAY_AIRC, L"Pressure as red and blue, velocity as white" },
				{ L"Air heat", DISPLAY_AIRH, L"Air temperature, like heat display" },
				{ L"Vorticity", DISPLAY_AIRW, L"Red is clockwise, blue anticlockwise" },
			}, rs.displayMode, 1);
			grid(L"Colours (one at a time)", {
				{ L"Heat", COLOUR_HEAT, L"Temperature: dark blue is coldest, pink hottest" },
				{ L"Life", COLOUR_LIFE, L"Life value in greys" },
				{ L"Heat gradient", COLOUR_GRAD, L"Tints colours to show heat spreading" },
				{ L"Plain", COLOUR_BASC, L"No effects at all; overrides everything, deco too" },
			}, rs.colorMode, 2);
		}

		// Brush, replace mode and decoration colour (TPT keeps them in its tool strip and keys).
		void PaintTools()
		{
			auto c = Content();
			auto &gc = GameController::Ref();
			auto *model = gc.GetModel();
			int x = c.left + S(10), w = c.right - c.left - S(20), y = c.top + S(8);
			int n = model->BrushListSize(), rows = (n + 2) / 3;
			int top = Group(x, y, w, S(56) + rows * S(29), L"Brush");
			int bw = (w - S(24) - 2 * S(6)) / 3;
			for (int i = 0; i < n; i++)
			{
				std::wstring name = i == 0 ? L"Circle" : i == 1 ? L"Square" : i == 2 ? L"Triangle" : L"Custom " + std::to_wstring(i - 2);
				PushButton(x + S(12) + (i % 3) * (bw + S(6)), top + (i / 3) * S(29), bw, name,
				           [i] { GameController::Ref().GetModel()->SetBrushID(i); }, L"Tab cycles the shapes", model->GetBrushID() == i);
			}
			int sy = top + rows * S(29) + S(4);
			auto r = model->GetBrush().GetRadius();
			Caption(x + S(12), sy, L"Size " + std::to_wstring(r.X) + L" \u00D7 " + std::to_wstring(r.Y));
			PushButton(x + S(12) + bw + S(6), sy, S(40), L"\u2212", [] { GameController::Ref().AdjustBrushSize(-1); }, L"Smaller ([ or the wheel)");
			PushButton(x + S(12) + bw + S(52), sy, S(40), L"+", [] { GameController::Ref().AdjustBrushSize(1); }, L"Bigger (] or the wheel)");
			y += S(64) + rows * S(29);

			top = Group(x, y, w, S(76), L"Drawing over particles");
			int flags = gc.GetReplaceModeFlags();
			Toggle(x + S(12), top, L"Replace mode", flags & REPLACE_MODE, false, [] {
				auto &g = GameController::Ref();
				g.SetReplaceModeFlags((g.GetReplaceModeFlags() ^ REPLACE_MODE) & ~SPECIFIC_DELETE);
			}, L"Draw over particles instead of only empty space (Insert)");
			Toggle(x + S(12), top + S(24), L"Erase only the right-click material", flags & SPECIFIC_DELETE, false, [] {
				auto &g = GameController::Ref();
				g.SetReplaceModeFlags((g.GetReplaceModeFlags() ^ SPECIFIC_DELETE) & ~REPLACE_MODE);
			}, L"Specific delete (Delete)");
			y += S(84);

			top = Group(x, y, w, S(84), L"Decoration");
			Toggle(x + S(12), top, L"Show decorations", model->GetDecoration(), false, [] { GameController::Ref().SetDecoration(); }, L"Ctrl+B");
			auto col = model->GetColourSelectorColour();
			uint32_t rgb = (col.Red << 16) | (col.Green << 8) | col.Blue;
			cv.RoundRect(x + S(12), top + S(28), S(40), S(22), S(3), rgb, rgb, 255);
			cv.RoundFrame(x + S(12), top + S(28), S(40), S(22), S(3), 0x707070, 255);
			PushButton(x + S(60), top + S(26), S(140), L"Pick colour\u2026", [] { GameController::Ref().OpenColourPicker(); }, L"The colour the deco tools paint with");
		}

		void PaintSaves()
		{
			auto c = Content();
			int x = c.left + S(10), w = c.right - c.left - S(20), y = c.top + S(8);
			auto *model = GameController::Ref().GetModel();
			int top = Group(x, y, w, S(72), L"Open save");
			std::wstring name = L"Untitled simulation", info = L"Not saved yet";
			if (auto *save = model->GetSave())
			{
				name = Wide(save->GetName());
				info = L"by " + Platform::WinWiden(save->GetUserName()) + L"   \u25B2 " + std::to_wstring(save->GetVotesUp()) +
				       L"  \u25BC " + std::to_wstring(save->GetVotesDown());
				std::wstring tags;
				for (auto &tag : save->GetTags()) tags += (tags.empty() ? L"   Tags: " : L", ") + Platform::WinWiden(tag);
				info += tags;
			}
			else if (auto *file = model->GetSaveFile())
			{
				name = Platform::WinWiden(file->GetName());
				info = L"Saved on this computer";
			}
			text->Draw(cv, x + S(12), top, name, Text::Style{ 13, FW_SEMIBOLD, false, false, 0x1E395B });
			text->Draw(cv, x + S(12), top + S(24), info, Text::Style{ 11, FW_NORMAL, false, false, 0x6D6D6D });
			y += S(80);
			top = Group(x, y, w, S(96), L"Saves");
			int bw = (w - S(24) - S(6)) / 2;
			PushButton(x + S(12), top, bw, L"Open\u2026", [] { GameController::Ref().OpenLocalBrowse(); }, L"Your local saves");
			PushButton(x + S(12) + bw + S(6), top, bw, L"Save\u2026", [] { GameController::Ref().OpenLocalSaveWindow(false); },
			           L"Saves the play area; at 612 \u00D7 384 or smaller it opens in normal TPT");
			PushButton(x + S(12), top + S(29), bw, L"Stamps\u2026", [] { GameController::Ref().OpenStamps(); }, L"Saved selections (K)");
			PushButton(x + S(12) + bw + S(6), top + S(29), bw, L"Reload", [] { GameController::Ref().ReloadSim(); }, L"Back to the last loaded save (F5)");
			y += S(104);
			// The rest of what TPT's bottom button row had (it's hidden while the side window is open).
			top = Group(x, y, w, S(125), L"Online");
			PushButton(x + S(12), top, bw, L"Browse\u2026", [] { GameController::Ref().OpenSearch(""); }, L"Saves on powdertoy.co.uk");
			PushButton(x + S(12) + bw + S(6), top, bw, L"Upload\u2026", [] { GameController::Ref().OpenSaveWindow(); }, L"Save to powdertoy.co.uk");
			PushButton(x + S(12), top + S(29), bw, L"\u25B2 Vote up", [] { GameController::Ref().Vote(1); }, L"Vote for the save you opened");
			PushButton(x + S(12) + bw + S(6), top + S(29), bw, L"\u25BC Vote down", [] { GameController::Ref().Vote(-1); }, L"Vote against the save you opened");
			PushButton(x + S(12), top + S(58), bw, L"Tags\u2026", [] { GameController::Ref().OpenTags(); }, L"The open save's tags");
			PushButton(x + S(12) + bw + S(6), top + S(58), bw, L"Sign in\u2026", [] { GameController::Ref().OpenLogin(); }, L"Your powdertoy.co.uk account");
			y += S(133);
			top = Group(x, y, w, S(52), L"TPT");
			PushButton(x + S(12), top, bw, L"Options\u2026", [] { GameController::Ref().OpenOptions(); }, L"TPT's own settings window");
		}

		// The start page, in place of TPT's text over the game at startup: the same help, laid out like a Vista
		// welcome page. Drawn on its own canvas, then the visible part is copied in (it scrolls).
		int welcomeH = 0;
		void PaintWelcome()
		{
			using Style = Text::Style;
			auto c = Content();
			int pw = c.right - c.left, ph = S(1700);
			Canvas page;
			page.Resize(pw, ph);
			page.RoundRect(0, 0, pw, ph, 0, 0xFFFFFF, 0xFFFFFF, 255);
			std::swap(cv, page);
			struct Link { RECT rc; std::function<void()> go; std::wstring tip; };
			std::vector<Link> links;

			int x0 = S(18), x1 = pw - S(18), y = S(14);
			const Style title{ 22, FW_LIGHT, false, false, 0x1E395B }, subtitle{ 12, FW_NORMAL, true, false, 0x6D6D6D },
			            link{ 12, FW_NORMAL, false, true, 0x0066CC }, body{ 12, FW_NORMAL, false, false, 0x1A1A1A },
			            note{ 11, FW_NORMAL, true, false, 0x6D6D6D }, footer{ 10, FW_NORMAL, false, false, 0x8A8A8A };
			// Words wrapped to [x, x1); returns the y after the last line.
			auto para = [&](int x, int y, int right, const std::wstring &s, const Style &st) {
				int cx = x, lineH = text->Measure(L"Ag", st).cy, space = text->Measure(L" ", st).cx;
				size_t at = 0;
				while (at < s.size())
				{
					auto end = s.find(L' ', at);
					auto word = s.substr(at, end == s.npos ? s.npos : end - at);
					at = end == s.npos ? s.size() : end + 1;
					int ww = text->Measure(word, st).cx;
					if (cx > x && cx + ww > right)
					{
						cx = x;
						y += lineH;
					}
					text->Draw(cv, cx, y, word, st);
					cx += ww + space;
				}
				return y + lineH;
			};

			text->Draw(cv, x0, y, L"Welcome to The Powder Toy", title);
			y += text->Measure(L"Ag", title).cy + S(2);
			text->Draw(cv, x0, y, L"Version " + std::to_wstring(DISPLAY_VERSION[0]) + L"." + std::to_wstring(DISPLAY_VERSION[1]) +
			                          L"  \u00B7  running on your desktop", subtitle);
			y += text->Measure(L"Ag", subtitle).cy + S(8);
			int lx = x0;
			for (auto [label, url] : { std::pair{ L"powdertoy.co.uk", L"https://powdertoy.co.uk" },
			                           std::pair{ L"Discord", L"https://tpt.io/discord" },
			                           std::pair{ L"Create an account", L"https://powdertoy.co.uk/Register.html" } })
			{
				auto size = text->Measure(label, link);
				text->Draw(cv, lx, y, label, link);
				links.push_back({ RECT{ lx, y, lx + int(size.cx), y + int(size.cy) },
				                  [url = std::wstring(url)] { ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }, url });
				lx += size.cx + S(18);
			}
			y += S(30);

			// TPT's notifications (update news, server messages): a yellow row each, with Open and Dismiss.
			for (auto *n : GameController::Ref().GetModel()->GetNotifications())
			{
				int rh = S(30);
				cv.RoundRect(x0, y, x1 - x0, rh, S(3), 0xFFFBE5, 0xFFF4C4, 255);
				cv.RoundFrame(x0, y, x1 - x0, rh, S(3), 0xE5C365, 255);
				cv.RoundRect(x0 + S(8), y + S(7), S(16), S(16), S(8), 0xF0B030, 0xC08000, 255);
				text->Draw(cv, x0 + S(14), y + S(7), L"!", Style{ 11, FW_BOLD, false, false, 0xFFFFFF });
				int lx = x1 - S(10);
				for (auto [label, dismiss] : { std::pair{ L"Dismiss", true }, std::pair{ L"Open", false } })
				{
					auto size = text->Measure(label, link);
					lx -= size.cx;
					text->Draw(cv, lx, y + S(6), label, link);
					links.push_back({ RECT{ lx, y + S(6), lx + int(size.cx), y + S(6) + int(size.cy) }, [n, dismiss] {
						if (dismiss) GameController::Ref().RemoveNotification(n);
						else n->Action();
					}, dismiss ? L"Remove this notice" : L"Open it" });
					lx -= S(14);
				}
				text->Draw(cv, x0 + S(32), y + S(6), Wide(n->Message), 0x1A1A1A, false, lx - x0 - S(36));
				y += rh + S(6);
			}
			if (!GameController::Ref().GetModel()->GetNotifications().empty()) y += S(10);

			// Headers: H1 is the title, H2 a part of the page (with a rule), H3 a group of shortcuts.
			auto header = [&](const wchar_t *name) {
				const Style h2{ 17, FW_SEMIBOLD, false, false, 0x1E395B };
				text->Draw(cv, x0, y, name, h2);
				y += text->Measure(L"Ag", h2).cy + S(3);
				for (int i = x0; i < x1; i++) cv.Blend(i, y, 0xB9CDE5, 255);
				y += S(12);
			};

			header(L"Getting started");
			// The tip: a Vista information bar.
			{
				int top = y, tx = x0 + S(30);
				int bottom = para(tx + S(32), top + S(8), x1 - S(10),
				                  L"Pick a material in the Elements tab, then draw anywhere on your desktop. Ctrl+Alt+E shows or hides this window.", body) + S(8);
				cv.RoundRect(x0, top, x1 - x0, bottom - top, S(3), 0xFFFBE5, 0xFFF4C4, 255);
				cv.RoundFrame(x0, top, x1 - x0, bottom - top, S(3), 0xE5C365, 255);
				cv.RoundRect(x0 + S(10), top + S(9), S(16), S(16), S(8), 0x3C8DDE, 0x1E5FAA, 255);
				text->Draw(cv, x0 + S(16), top + S(9), L"i", Style{ 11, FW_BOLD, true, false, 0xFFFFFF });
				text->Draw(cv, tx, top + S(8), L"Tip", Style{ 12, FW_BOLD, false, false, 0x7A5B00 });
				para(tx + S(32), top + S(8), x1 - S(10),
				     L"Pick a material in the Elements tab, then draw anywhere on your desktop. Ctrl+Alt+E shows or hides this window.", body);
				y = bottom + S(18);
			}

			// Shortcut sections: a coloured heading and rule, then keycaps and what they do. "|" separates
			// alternatives, "+" keys pressed together.
			struct Row { const wchar_t *keys, *what, *aside; };
			auto section = [&](const wchar_t *name, uint32_t accent, std::initializer_list<Row> rows) {
				// An accent block, then the name in the accent colour.
				const Style h3{ 14, FW_SEMIBOLD, false, false, accent };
				int hh = text->Measure(L"Ag", h3).cy;
				cv.RoundRect(x0, y + S(3), S(4), hh - S(6), 0, accent, accent, 255);
				text->Draw(cv, x0 + S(10), y, name, h3);
				y += hh + S(6);
				int keyW = S(200);
				for (auto &row : rows)
				{
					int kx = x0 + S(4), ky = y;
					std::wstring keys = row.keys;
					size_t at = 0;
					bool first = true;
					while (at <= keys.size())
					{
						auto end = keys.find(L'|', at);
						auto alt = keys.substr(at, end == keys.npos ? keys.npos : end - at);
						at = end == keys.npos ? keys.size() + 1 : end + 1;
						if (!first)
						{
							text->Draw(cv, kx, ky + S(2), L"or", note);
							kx += text->Measure(L"or", note).cx + S(5);
						}
						first = false;
						size_t k = 0;
						bool firstKey = true;
						while (k <= alt.size())
						{
							auto plus = alt.find(L'+', k);
							auto key = alt.substr(k, plus == alt.npos ? alt.npos : plus - k);
							k = plus == alt.npos ? alt.size() + 1 : plus + 1;
							if (!firstKey)
							{
								text->Draw(cv, kx, ky + S(2), L"+", Style{ 12, FW_NORMAL, false, false, 0x8A8A8A });
								kx += text->Measure(L"+").cx + S(3);
							}
							firstKey = false;
							const Style cap{ 11, FW_SEMIBOLD, false, false, 0x2B2B2B };
							int cw = text->Measure(key, cap).cx + S(12), ch = S(20);
							cv.RoundRect(kx, ky, cw, ch, S(3), 0xFDFDFD, 0xE4E7EB, 255);
							cv.RoundFrame(kx, ky, cw, ch, S(3), 0xA9AFB8, 255);
							for (int i = kx + S(2); i < kx + cw - S(2); i++) cv.Blend(i, ky + ch - 2, 0x8E959E, 120);
							text->Draw(cv, kx + S(6), ky + (ch - text->Measure(key, cap).cy) / 2, key, cap);
							kx += cw + S(3);
						}
						kx += S(4);
					}
					int dx = std::max(x0 + keyW, kx + S(8));
					int end = para(dx, y + S(1), x1, row.what, body);
					if (row.aside)
					{
						end = para(dx, end, x1, row.aside, note);
					}
					y = std::max(end, y + S(20)) + S(7);
				}
				y += S(14);
			};
			header(L"Keyboard and mouse");
			section(L"Drawing", 0x2E7D32, {
				{ L"Left drag|Right drag", L"Draw with the left or right material", nullptr },
				{ L"Shift+drag", L"A straight line", L"Add Alt for horizontal, vertical or diagonal only" },
				{ L"Ctrl+drag", L"A filled rectangle", L"Add Alt for a square" },
				{ L"Ctrl+Shift+click", L"Flood-fill a closed area", nullptr },
				{ L"Middle click|Alt+click", L"Sample: pick up the material under the mouse", nullptr },
				{ L"Wheel|[|]", L"Brush size", nullptr },
				{ L"Tab", L"Brush shape", nullptr },
			});
			section(L"Editing", 0x6A1B9A, {
				{ L"Ctrl+C|Ctrl+X|Ctrl+V", L"Copy, cut and paste", nullptr },
				{ L"R", L"Rotate while pasting", L"Shift+R and Shift+Ctrl+R mirror it" },
				{ L"Ctrl+Z", L"Undo", nullptr },
				{ L"Ctrl+Y|Ctrl+Shift+Z", L"Redo", nullptr },
				{ L"S", L"Save part of the screen as a stamp", nullptr },
				{ L"L|K", L"Load the last stamp, or open the stamp library", nullptr },
			});
			section(L"Simulation", 0xC25100, {
				{ L"Space", L"Pause and resume", nullptr },
				{ L"F", L"Step one frame", nullptr },
				{ L"F5", L"Reload the simulation", nullptr },
			});
			section(L"View", 0x00796B, {
				{ L"0 \u2013 9", L"View modes", nullptr },
				{ L"H|D", L"Show the HUD, or debug details in it", nullptr },
				{ L"Z", L"Zoom", L"Click to keep the zoom window; the wheel changes its strength" },
				{ L"Ctrl+F", L"Highlight the selected element", nullptr },
			});
			text->Draw(cv, x0, y, Wide(VersionInfo().FromUtf8()), footer);
			y += S(30);

			std::swap(cv, page);
			welcomeH = y;
			int viewH = c.bottom - c.top - 1;
			scroll = std::clamp(scroll, 0, std::max(0, welcomeH - viewH));
			for (int row = 0; row < viewH && row + scroll < ph; row++)
			{
				std::copy_n(page.px.data() + size_t(row + scroll) * pw, pw, cv.px.data() + size_t(c.top + 1 + row) * cv.w + c.left);
			}
			for (auto &l : links)
			{
				RECT on{ l.rc.left + c.left, l.rc.top + c.top + 1 - scroll, l.rc.right + c.left, l.rc.bottom + c.top + 1 - scroll };
				if (on.top >= c.top && on.bottom <= c.bottom)
				{
					Add(on, l.go, l.tip);
				}
			}
		}

		void PaintOverlay()
		{
			auto c = Content();
			int x = c.left + S(10), w = c.right - c.left - S(20), y = c.top + S(8);
			auto &prefs = GlobalPrefs::Ref();
			auto key = [&](const char *name, const char *fallback) {
				return Platform::WinWiden(prefs.Get(ByteString("Overlay.Keys.") + name, ByteString(fallback)));
			};
			int top = Group(x, y, w, S(100), L"Clicks go to");
			const wchar_t *modes[] = { L"Auto: particles and menus to TPT, empty space to the desktop", L"Pass-through: everything to the desktop", L"Locked: everything to TPT" };
			for (int i = 0; i < int(OverlayInput::Count); i++)
			{
				Toggle(x + S(12), top + i * S(24), modes[i], int(OverlayGetInput()) == i, true, [i] { OverlaySetInput(OverlayInput(i)); });
			}
			y += S(108);
			top = Group(x, y, w, S(76), L"Sim layer");
			const wchar_t *layers[] = { L"On top of every window", L"Under windows, over the desktop icons",
			                            L"Behind the desktop icons, like a wallpaper (look only: clicks go to the desktop)" };
			for (int i = 0; i < int(OverlayLayer::Count); i++)
			{
				Toggle(x + S(12), top + i * S(24), layers[i], int(OverlayGetLayer()) == i, true, [i] { OverlaySetLayer(OverlayLayer(i)); });
			}
			y += S(84);
			top = Group(x, y, w, S(68), L"Window");
			Toggle(x + S(12), top, L"Show TPT's own menus too", showTPTMenus, false, [] {
				showTPTMenus = !showTPTMenus;
				GlobalPrefs::Ref().Set("Overlay.ShowTPTMenus", showTPTMenus);
			});
			Toggle(x + S(12), top + S(24), L"Pass clicks through while a full-screen app or game is in front", overlayAutoPass, false, [] {
				overlayAutoPass = !overlayAutoPass;
				GlobalPrefs::Ref().Set("Overlay.AutoPassThrough", overlayAutoPass);
			});
			y += S(76);
			top = Group(x, y, w, S(140), L"Shortcuts (any app)");
			std::wstring rows[][2] = {
				{ L"Next input mode", key("CycleInput", "Ctrl+Alt+O") },
				{ L"Pass-through on/off", key("PassThrough", "Ctrl+Alt+P") },
				{ L"Locked on/off", key("Locked", "Ctrl+Alt+L") },
				{ L"Show/hide this window", key("Palette", "Ctrl+Alt+E") },
			};
			const char *names[] = { "CycleInput", "PassThrough", "Locked", "Palette" };
			for (int i = 0; i < 4; i++)
			{
				int ry = top + i * S(26);
				Caption(x + S(12), ry, rows[i][0]);
				bool rec = recording == names[i];
				Caption(x + S(200), ry, rec ? L"Press the new keys (Esc cancels)" : rows[i][1], rec ? 0xC25100 : 0x1E395B);
				PushButton(x + w - S(100), ry, S(88), rec ? L"Cancel" : L"Change\u2026", [rec, name = std::string(names[i])] {
					recording = rec ? "" : name;
				}, L"Ctrl, Alt, Shift or Win plus a letter, digit or F key");
			}
			y += S(148);
			top = Group(x, y, w, S(52), L"Size");
			wchar_t scale[32];
			swprintf(scale, 32, L"UI scale %g\u00D7", uiScale);
			Caption(x + S(12), top, scale);
			auto setScale = [this](float v) {
				float old = uiScale;
				uiScale = std::clamp(v, 1.f, 4.f);
				auto &prefs = GlobalPrefs::Ref();
				RECT wr;
				GetWindowRect(hwnd, &wr);
				prefs.Set("Overlay.Windows.Palette.W", int(std::lround((wr.right - wr.left) * uiScale / old)));
				prefs.Set("Overlay.Windows.Palette.H", int(std::lround((wr.bottom - wr.top) * uiScale / old)));
				GlobalPrefs::Ref().Set("Overlay.UIScale", uiScale);
				rebuildPalette = true;
			};
			PushButton(x + S(200), top, S(40), L"\u2212", [setScale] { setScale(uiScale - 0.25f); }, L"Smaller windows and text");
			PushButton(x + S(246), top, S(40), L"+", [setScale] { setScale(uiScale + 0.25f); }, L"Bigger windows and text");
		}

		void Paint() override
		{
			hits.clear();
			vista::Frame(cv, titleH, border, L"The Powder Toy");
			vista::CaptionButton(cv, MinRect(), Hovered(MinRect()), vista::CaptionMin);
			vista::CaptionButton(cv, MaxRect(), Hovered(MaxRect()), vista::CaptionMax);
			vista::CaptionButton(cv, CloseRect(), Hovered(CloseRect()), vista::CaptionClose);
			Add(MinRect(), [this] { ShowWindow(hwnd, SW_MINIMIZE); }, L"Minimise to the taskbar");
			Add(MaxRect(), [this] { ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE); }, IsZoomed(hwnd) ? L"Restore" : L"Maximise");
			Add(CloseRect(), [] { OverlayWindows::TogglePalette(); }, L"Close (the tray icon or Ctrl+Alt+E brings it back)");

			// The tab strip: grey tabs on the glass, the selected one white and joined to the page.
			int tx = border + S(4);
			cv.RoundRect(border, titleH, cv.w - 2 * border, tabH, 0, 0xF0F0F0, 0xF0F0F0, 255);
			for (int i = 0; i < int(std::size(tabNames)); i++)
			{
				int tw = text->Measure(tabNames[i]).cx + S(20);
				bool on = i == tab;
				RECT rc{ tx, titleH + (on ? S(2) : S(4)), tx + tw, titleH + tabH + (on ? 1 : 0) };
				bool hot = !on && Hovered(rc);
				cv.RoundRect(rc.left, rc.top, tw, rc.bottom - rc.top, S(3), on ? 0xFFFFFF : (hot ? 0xEAF6FD : 0xF2F2F2), on ? 0xFFFFFF : (hot ? 0xA7D9F5 : 0xE3E3E3), 255, true);
				cv.RoundFrame(rc.left, rc.top, tw, rc.bottom - rc.top + S(3), S(3), on ? 0x898C95 : (hot ? 0x3C7FB1 : 0x898C95), 255, true);
				text->Draw(cv, tx + S(10), rc.top + (rc.bottom - rc.top - text->Measure(tabNames[i]).cy) / 2, tabNames[i], 0x000000);
				Add(rc, [this, i] { tab = i, scroll = 0; });
				tx += tw + S(1);
			}
			// The page.
			auto c = Content();
			cv.RoundRect(c.left, c.top, c.right - c.left, c.bottom - c.top, 0, 0xFFFFFF, 0xFFFFFF, 255);
			for (int i = c.left; i < c.right; i++) cv.Blend(i, c.top, 0x898C95, 255);
			// The selected tab's bottom edge opens into the page.
			int sx = border + S(4);
			for (int i = 0; i < tab; i++) sx += text->Measure(tabNames[i]).cx + S(21);
			for (int i = sx + 1; i < sx + text->Measure(tabNames[tab]).cx + S(19); i++) cv.Blend(i, c.top, 0xFFFFFF, 255);

			switch (tab)
			{
			case 0: PaintElements(); break;
			case 2: PaintSimulation(); break;
			case 3: PaintDisplay(); break;
			case 4: PaintSaves(); break;
			case 5: PaintOverlay(); break;
			case welcomeTab: PaintWelcome(); break;
			case toolsTab: PaintTools(); break;
			}

			// The status bar: what the hovered control does.
			int sy = cv.h - border - statusH;
			cv.RoundRect(border, sy, cv.w - 2 * border, statusH, 0, 0xF7F9FC, 0xDCE5F1, 255);
			for (int i = border; i < cv.w - border; i++) cv.Blend(i, sy, 0xA0AFC3, 255);
			if (hover >= 0 && hover < int(hits.size()))
			{
				text->Draw(cv, border + S(8), sy + S(3), hits[hover].tip, 0x1E395B, false, cv.w - 2 * border - S(16));
			}
		}

		LRESULT HitTest(int x, int y) override
		{
			return y < titleH && !Inside(CloseRect(), x, y) && !Inside(MaxRect(), x, y) && !Inside(MinRect(), x, y) ? HTCAPTION : HTCLIENT;
		}

		void Mouse(UINT msg, int x, int y, WPARAM wp) override
		{
			int at = -1;
			if (msg != WM_MOUSELEAVE)
			{
				for (int i = 0; i < int(hits.size()); i++)
					if (Inside(hits[i].rc, x, y)) at = i;
			}
			if (at != hover)
			{
				hover = at;
				dirty = true;
			}
			if (at >= 0 && msg == WM_LBUTTONUP && hits[at].left)
			{
				auto action = hits[at].left; // the action may repaint and rebuild hits
				action();
				dirty = true;
			}
			if (at >= 0 && msg == WM_RBUTTONUP && hits[at].right)
			{
				auto action = hits[at].right;
				action();
				dirty = true;
			}
			if (msg == WM_MOUSEWHEEL && tab == welcomeTab)
			{
				auto c = Content();
				int maxScroll = std::max(0, welcomeH - int(c.bottom - c.top));
				scroll = std::clamp(scroll - GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * S(51), 0, maxScroll);
				dirty = true;
			}
			if (msg == WM_MOUSEWHEEL && tab == 0)
			{
				auto tools = Tools();
				auto area = ToolArea();
				int rows = (int(tools.size()) + Columns() - 1) / Columns();
				int maxScroll = std::max(0, rows * (tileH + gap) - int(area.bottom - area.top));
				scroll = std::clamp(scroll - GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * (tileH + gap), 0, maxScroll);
				dirty = true;
			}
		}

		// Repaint when anything shown changes, from here, from TPT's keys, or from Lua.
		void Tick()
		{
			auto &gc = GameController::Ref();
			auto *model = gc.GetModel();
			auto *sim = model->GetSimulation();
			size_t count = 0;
			for (auto *menu : gc.GetMenuList()) count += menu->GetToolList().size();
			auto a = sim->playArea;
			uint64_t state = 1469598103934665603ULL;
			auto mix = [&](uint64_t v) { state = (state ^ v) * 1099511628211ULL; };
			mix(uint64_t(uintptr_t(gc.GetActiveTool(0))));
			mix(uint64_t(uintptr_t(gc.GetActiveTool(1))));
			for (uint64_t v : { uint64_t(count), uint64_t(gc.GetPaused()), uint64_t(gc.GetEdgeMode()), uint64_t(sim->gravityMode),
			                    uint64_t(sim->air->airMode), uint64_t(model->GetNewtonianGrvity()), uint64_t(gc.GetAHeatEnable()),
			                    uint64_t(gc.GetHudEnable()), uint64_t(model->GetDecoration()), uint64_t(model->GetGravityGrid()),
			                    uint64_t(OverlayGetInput()), uint64_t(showTPTMenus), uint64_t(a.pos.X), uint64_t(a.pos.Y),
			                    uint64_t(a.size.X), uint64_t(a.size.Y) })
			{
				mix(v);
			}
			if (state != shownState)
			{
				shownState = state;
				dirty = true;
			}
		}
	};

	// A small glass notice ("Input: Locked") that fades out.
	struct Toast : Panel
	{
		std::wstring message;
		DWORD shownAt = 0;

		Toast()
		{
			name = "Toast";
		}

		void Paint() override
		{
			int r = S(6);
			cv.RoundRect(0, 0, cv.w, cv.h, r, vista::glassTop, vista::glassBottom, vista::glassAlpha);
			cv.RoundRect(1, 1, cv.w - 2, cv.h / 2, r, 0xFFFFFF, 0xFFFFFF, 60, true);
			cv.RoundFrame(0, 0, cv.w, cv.h, r, 0x000000, 170);
			cv.RoundFrame(1, 1, cv.w - 2, cv.h - 2, r - 1, 0xFFFFFF, 110);
			auto size = text->Measure(message, true);
			text->Draw(cv, (cv.w - size.cx) / 2, (cv.h - size.cy) / 2, message, 0x000000, true, cv.w, S(5));
		}

		LRESULT HitTest(int, int) override
		{
			return HTTRANSPARENT;
		}

		void Show(HWND over, const std::wstring &newMessage)
		{
			message = newMessage;
			auto size = text->Measure(message, true);
			int w = size.cx + S(48), h = S(40);
			RECT rc;
			GetWindowRect(over, &rc);
			int x = (rc.left + rc.right - w) / 2, y = rc.top + S(40);
			if (!hwnd)
			{
				Create(x, y, w, h);
				SetWindowLongPtrW(hwnd, GWL_EXSTYLE, GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_TRANSPARENT);
			}
			cv.Resize(w, h);
			SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
			shownAt = GetTickCount();
			dirty = true;
		}

		void Tick()
		{
			if (!hwnd || !shownAt) return;
			auto age = GetTickCount() - shownAt;
			if (age > 1800)
			{
				ShowWindow(hwnd, SW_HIDE);
				shownAt = 0;
				return;
			}
			opacity = age < 1200 ? 255 : int(255 * (1800 - age) / 600);
			Present();
		}
	};

	HWND mainWindow = nullptr;
	std::unique_ptr<SideWindow> palette;
	std::unique_ptr<Toast> toast;
	bool passThrough = false;

	// A TPT window (dialog, browser, menu) in its own app window. TPT still draws it (Engine::Hosted, every frame);
	// we show it at a whole-pixel scale in a Vista frame and send the mouse back. Menus get no frame.
	// A TPT dialog's controls as plain data, read each frame (TPT can delete a window between frames, so the
	// host never keeps pointers into it). Rects are in the dialog's own pixels.
	struct SkinItem
	{
		ui::Component::Kind kind;
		int x, y, w, h, clipT, clipB;
		std::wstring text;
		uint32_t rgb = 0; // a label's colour
		int align = 0;    // 0 left, 1 centre, 2 right
		bool enabled = true, hover = false, down = false, checked = false, focused = false, multiline = false;
		int value = 0, steps = 0;
		int caret = 0, selA = 0, selB = 0; // text boxes: cursor and selection, in characters
		bool operator==(const SkinItem &) const = default;
	};

	// TPT text minus its formatting codes; \x01 marks a switch to or from grey (TPT's "\bg").
	std::wstring Plain(const String &s, bool &tptGlyphs)
	{
		String out;
		for (size_t i = 0; i < s.size(); i++)
		{
			auto c = s[i];
			if (c == 0x0F) { i += 3; continue; }
			if (c == '\b' && i + 1 < s.size())
			{
				out.push_back(s[++i] == 'g' ? 1 : 2);
				continue;
			}
			if (c == 0x0E || c == 0x01) continue;
			if (c >= 0xE000 && c <= 0xF8FF) tptGlyphs = true; // TPT's own icon font
			out += c;
		}
		return Wide(out);
	}

	void SkinWalk(ui::Window *window, const std::vector<ui::Component *> &list, int clipT, int clipB, std::vector<SkinItem> &out)
	{
		using K = ui::Component::Kind;
		for (auto *c : list)
		{
			if (!c->Visible) continue;
			auto pos = c->GetScreenPos() - window->Position;
			SkinItem it{ c->GetKind(), pos.X, pos.Y, c->Size.X, c->Size.Y, clipT, clipB };
			it.enabled = c->Enabled;
			it.hover = c->MouseInside;
			it.down = c->MouseDownInside;
			it.focused = window->IsFocused(c);
			it.align = c->Appearance.HorizontalAlign == ui::Appearance::AlignCentre ? 1 : c->Appearance.HorizontalAlign == ui::Appearance::AlignRight ? 2 : 0;
			bool glyphs = false;
			switch (it.kind)
			{
			case K::Panel:
			{
				auto *p = static_cast<ui::Panel *>(c);
				std::vector<ui::Component *> kids;
				for (int i = 0; i < p->GetChildCount(); i++) kids.push_back(p->GetChild(i));
				// a scroll bar for a panel taller than its view
				it.value = -p->ViewportPosition.Y;
				it.steps = p->InnerSize.Y;
				out.push_back(it);
				SkinWalk(window, kids, std::max(clipT, pos.Y), std::min(clipB, pos.Y + c->Size.Y), out);
				continue;
			}
			case K::Label:
			case K::Textbox:
			{
				auto *l = static_cast<ui::Label *>(c);
				it.text = Plain(l->GetText(), glyphs);
				auto col = l->GetTextColour();
				it.rgb = (col.Red << 16) | (col.Green << 8) | col.Blue;
				it.multiline = l->GetMultiline();
				if (it.kind == K::Textbox)
				{
					auto *t = static_cast<ui::Textbox *>(c);
					if (t->GetHidden()) it.text = std::wstring(it.text.size(), L'\u25CF');
					it.caret = std::clamp(t->GetCursor(), 0, int(it.text.size()));
					if (t->HasSelection())
					{
						it.selA = std::clamp(t->GetSelectionStart(), 0, int(it.text.size()));
						it.selB = std::clamp(t->GetSelectionEnd(), 0, int(it.text.size()));
					}
				}
				break;
			}
			case K::Button:
			{
				auto *b = static_cast<ui::Button *>(c);
				it.text = Plain(b->GetText(), glyphs);
				it.checked = b->GetTogglable() && b->GetToggleState();
				if (c->Appearance.icon != NoIcon) glyphs = true;
				auto bg = c->Appearance.BackgroundInactive;
				if (bg.Red || bg.Green || bg.Blue) glyphs = true; // a colour swatch
				break;
			}
			case K::Checkbox:
				it.text = Plain(static_cast<ui::Checkbox *>(c)->GetText(), glyphs);
				it.checked = static_cast<ui::Checkbox *>(c)->GetChecked();
				break;
			case K::DropDown:
				it.text = Plain(static_cast<ui::DropDown *>(c)->GetOption().first, glyphs);
				break;
			case K::Slider:
				it.value = static_cast<ui::Slider *>(c)->GetValue();
				it.steps = static_cast<ui::Slider *>(c)->GetSteps();
				break;
			case K::ProgressBar:
				it.value = static_cast<ui::ProgressBar *>(c)->GetProgress();
				break;
			default:
				break;
			}
			if (c->Appearance.icon != NoIcon) glyphs = true;
			if (glyphs) it.kind = K::Other; // keep TPT's pixels for icon buttons and the like
			out.push_back(it);
		}
	}

	struct HostWindow : Panel
	{
		ui::Window *window;
		bool framed;
		int scale = 1, titleH = 0, border = 0;
		ui::Point pos{ 0, 0 }, size{ 0, 0 };
		std::vector<pixel> pixels;
		std::vector<SkinItem> items; // empty: show TPT's pixels as they are
		bool hoverClose = false;

		HostWindow(ui::Window *window) : window(window)
		{
			framed = window->Framed;
			name = "Host";
			title = window->Title.empty() ? L"The Powder Toy" : Wide(window->Title);
			if (framed)
			{
				titleH = S(28);
				border = S(6);
			}
		}

		ui::Point ClientOrigin() { return { border, titleH }; }
		RECT CloseRect() { return { cv.w - border - S(43), 1, cv.w - border, 1 + S(18) }; }
		bool OnTop() { return ui::Engine::Ref().GetWindow() == window; }

		// TPT's pixels for one rect (dialog pixels), scaled, between rows clipT and clipB.
		void Blit(int x, int y, int w, int h, int clipT, int clipB)
		{
			for (int ty = std::max(y, clipT); ty < std::min(y + h, clipB); ty++)
			{
				if (ty < 0 || ty >= size.Y) continue;
				for (int sy = 0; sy < scale; sy++)
				{
					int dy = titleH + ty * scale + sy;
					if (dy >= cv.h) break;
					for (int tx = std::max(x, 0); tx < std::min(x + w, size.X); tx++)
					{
						auto p = pixels[size_t(ty) * size.X + tx] | 0xFF000000;
						for (int sx = 0; sx < scale; sx++)
						{
							int dx = border + tx * scale + sx;
							if (dx < cv.w) cv.px[size_t(dy) * cv.w + dx] = p;
						}
					}
				}
			}
		}

		// The dialog in Vista's colours and Segoe UI, from the controls' data; anything unknown keeps TPT's pixels.
		void PaintSkin()
		{
			using K = ui::Component::Kind;
			using Style = Text::Style;
			int k = scale, ox = border, oy = titleH;
			cv.RoundRect(ox, oy, size.X * k, size.Y * k, 0, 0xF0F0F0, 0xF0F0F0, 255);
			const Style ui{ 12, FW_NORMAL, false, false, 0x000000, 9 * k };
			int lineH = text->Measure(L"Ag", ui).cy;
			// A text run: \x01 toggles grey, \x02 back to the base colour.
			auto runs = [&](int x, int y, int right, const std::wstring &s, uint32_t base, int align, bool wrap) {
				std::vector<std::pair<std::wstring, uint32_t>> words;
				uint32_t col = base;
				std::wstring w;
				auto flush = [&] {
					if (!w.empty()) words.push_back({ w, col });
					w.clear();
				};
				for (auto ch : s)
				{
					if (ch == 1) { flush(); col = 0x6D6D6D; }
					else if (ch == 2) { flush(); col = base; }
					else if (ch == L' ' || ch == L'\n') { w += ch; flush(); }
					else w += ch;
				}
				flush();
				int total = 0;
				for (auto &[t, c] : words) total += text->Measure(t, ui).cx;
				int cx = !wrap && align == 1 ? x + (right - x - total) / 2 : !wrap && align == 2 ? right - total : x;
				for (auto &[t, c] : words)
				{
					int tw = text->Measure(t, ui).cx;
					if (wrap && cx > x && (cx + tw > right || (!t.empty() && t.back() == L'\n')))
					{
						if (cx + tw > right) { cx = x; y += lineH; }
					}
					Style st = ui;
					st.rgb = c;
					text->Draw(cv, cx, y, t, st);
					cx += tw;
					if (wrap && !t.empty() && t.back() == L'\n') { cx = x; y += lineH; }
				}
			};
			// TPT's colours are for white-on-black; keep a hue, darken light ones.
			auto ink = [](uint32_t rgb) -> uint32_t {
				int r = rgb >> 16 & 255, g = rgb >> 8 & 255, b = rgb & 255;
				if (std::abs(r - g) < 24 && std::abs(g - b) < 24) return r > 200 ? 0x000000 : 0x6D6D6D;
				return (r * 45 / 100) << 16 | (g * 45 / 100) << 8 | (b * 45 / 100);
			};
			for (auto &it : items)
			{
				int x = ox + it.x * k, y = oy + it.y * k, w = it.w * k, h = it.h * k;
				bool inside = it.y >= it.clipT && it.y + it.h <= it.clipB;
				if (it.kind == K::Other)
				{
					Blit(it.x, it.y, it.w, it.h, it.clipT, it.clipB);
					continue;
				}
				if (it.kind == K::Panel)
				{
					if (it.steps > it.h) // scroll bar
					{
						int bw = 4 * k, bx = x + w - bw, th = std::max(8 * k, h * it.h / it.steps), ty = y + (h - th) * it.value / std::max(1, it.steps - it.h);
						cv.RoundRect(bx, y, bw, h, 0, 0xE8E8E8, 0xE8E8E8, 255);
						cv.RoundRect(bx, ty, bw, th, 2 * k, 0xD4D4D4, 0xBCBCBC, 255);
					}
					continue;
				}
				if (it.y + it.h <= it.clipT || it.y >= it.clipB) continue;
				std::vector<std::pair<int, std::vector<uint32_t>>> keep; // rows outside the clip, as they were
				if (!inside)
				{
					int x0 = std::max(0, x - 2 * k), x1 = std::min(cv.w, x + w + 2 * k);
					for (int row = std::max(0, y - 2 * k); row < std::min(cv.h, y + h + 2 * k); row++)
					{
						if (row >= oy + it.clipT * k && row < oy + it.clipB * k) continue;
						keep.push_back({ row, std::vector<uint32_t>(cv.px.begin() + size_t(row) * cv.w + x0, cv.px.begin() + size_t(row) * cv.w + x1) });
					}
				}
				struct Restore
				{
					Canvas &cv;
					std::vector<std::pair<int, std::vector<uint32_t>>> &keep;
					int x0;
					~Restore()
					{
						for (auto &[row, px] : keep) std::copy(px.begin(), px.end(), cv.px.begin() + size_t(row) * cv.w + x0);
					}
				} restore{ cv, keep, std::max(0, x - 2 * k) };
				uint32_t fg = it.enabled ? 0x000000 : 0x8D8D8D;
				int ty = y + (h - lineH) / 2;
				switch (it.kind)
				{
				case K::Label:
					if (it.multiline || h > 20 * k) runs(x + 2 * k, y + 2 * k, x + w - 2 * k, it.text, ink(it.rgb), it.align, true);
					else runs(x + 2 * k, ty, x + w - 2 * k, it.text, ink(it.rgb), it.align, false);
					break;
				case K::Textbox:
					cv.RoundRect(x, y, w, h, 0, 0xFFFFFF, 0xFFFFFF, 255);
					cv.RoundFrame(x, y, w, h, 0, it.focused ? 0x3399FF : 0xABADB3, 255);
					if (it.selB > it.selA && !it.multiline)
					{
						int sx = x + 3 * k + int(text->Measure(it.text.substr(0, it.selA), ui).cx);
						int sw = int(text->Measure(it.text.substr(it.selA, it.selB - it.selA), ui).cx);
						cv.RoundRect(sx, ty, sw, lineH, 0, 0xCCE8FF, 0xCCE8FF, 255);
					}
					runs(x + 3 * k, it.multiline ? y + 2 * k : ty, x + w - 3 * k, it.text, fg, 0, it.multiline);
					if (it.focused && !it.multiline)
					{
						int cx = std::min(x + w - 3 * k, x + 3 * k + int(text->Measure(it.text.substr(0, it.caret), ui).cx));
						cv.RoundRect(cx, ty, std::max(1, k / 2), lineH, 0, 0x000000, 0x000000, 255);
					}
					break;
				case K::Button:
				{
					bool pressed = it.down || it.checked;
					uint32_t t = !it.enabled ? 0xF4F4F4 : pressed ? 0xE5F4FC : it.hover ? 0xEAF6FD : 0xF2F2F2;
					uint32_t b = !it.enabled ? 0xF4F4F4 : pressed ? 0x98D1EF : it.hover ? 0xA7D9F5 : 0xD5D5D5;
					uint32_t edge = !it.enabled ? 0xADB2B5 : pressed ? 0x2C628B : it.hover ? 0x3C7FB1 : 0x707070;
					cv.RoundRect(x + k / 2, y + k / 2, w - k, h - k, 3 * k / 2, t, b, 255);
					cv.RoundFrame(x + k / 2, y + k / 2, w - k, h - k, 3 * k / 2, edge, 255);
					runs(x + 4 * k, ty, x + w - 4 * k, it.text, fg, 1, false); // Vista centres button text
					break;
				}
				case K::Checkbox:
				{
					int bs = 10 * k, by = y + (h - bs) / 2;
					cv.RoundRect(x, by, bs, bs, 0, 0xFFFFFF, it.hover ? 0xDEF2FC : 0xF4F4F4, 255);
					cv.RoundFrame(x, by, bs, bs, 0, it.hover ? 0x3C7FB1 : 0x8E8F8F, 255);
					if (it.checked)
					{
						text->Draw(cv, x + 2 * k, by, L"\u2714", Style{ 12, FW_BOLD, false, false, 0x1C5180, 7 * k });
					}
					runs(x + bs + 4 * k, ty, x + w, it.text, fg, 0, false);
					break;
				}
				case K::DropDown:
				{
					cv.RoundRect(x + k / 2, y + k / 2, w - k, h - k, 3 * k / 2, it.hover ? 0xEAF6FD : 0xF2F2F2, it.hover ? 0xA7D9F5 : 0xD5D5D5, 255);
					cv.RoundFrame(x + k / 2, y + k / 2, w - k, h - k, 3 * k / 2, it.hover ? 0x3C7FB1 : 0x707070, 255);
					runs(x + 4 * k, ty, x + w - 14 * k, it.text, fg, 0, false);
					text->Draw(cv, x + w - 11 * k, ty, L"\u25BC", Style{ 12, FW_NORMAL, false, false, 0x404040, 6 * k });
					break;
				}
				case K::Separator:
					cv.RoundRect(x, y + h / 2, w, std::max(1, k / 2), 0, 0xD5DFE5, 0xD5DFE5, 255);
					break;
				case K::Slider:
				{
					int mid = y + h / 2, tx = x + (w - 6 * k) * it.value / std::max(1, it.steps);
					cv.RoundRect(x, mid - k, w, 2 * k, k, 0xE7EAEA, 0xE7EAEA, 255);
					cv.RoundFrame(x, mid - k, w, 2 * k, k, 0xB0B0B0, 255);
					cv.RoundRect(tx, y + k, 6 * k, h - 2 * k, 2 * k, it.hover ? 0xEAF6FD : 0xF2F2F2, it.hover ? 0xA7D9F5 : 0xD5D5D5, 255);
					cv.RoundFrame(tx, y + k, 6 * k, h - 2 * k, 2 * k, it.hover ? 0x3C7FB1 : 0x707070, 255);
					break;
				}
				case K::ProgressBar:
					cv.RoundRect(x, y, w, h, 0, 0xE6E6E6, 0xE6E6E6, 255);
					cv.RoundRect(x, y, w * std::clamp(it.value, 0, 100) / 100, h, 0, 0x37D33A, 0x06B025, 255);
					cv.RoundFrame(x, y, w, h, 0, 0xBCBCBC, 255);
					break;
				default:
					break;
				}
			}
		}

		void Paint() override
		{
			if (framed)
			{
				vista::Frame(cv, titleH, border, title);
				vista::CaptionButton(cv, CloseRect(), hoverClose, vista::CaptionClose);
			}
			if (!items.empty())
			{
				PaintSkin();
				return;
			}
			for (int y = 0; y < size.Y * scale && titleH + y < cv.h; y++)
			{
				auto *src = pixels.data() + size_t(y / scale) * size.X;
				auto *dst = cv.px.data() + size_t(titleH + y) * cv.w + border;
				for (int x = 0; x < size.X * scale && border + x < cv.w; x++)
					dst[x] = src[x / scale] | 0xFF000000;
			}
		}

		LRESULT HitTest(int x, int y) override
		{
			return framed && y < titleH && !Inside(CloseRect(), x, y) ? HTCAPTION : HTCLIENT;
		}

		void Mouse(UINT msg, int x, int y, WPARAM wp) override
		{
			bool overClose = framed && msg != WM_MOUSELEAVE && Inside(CloseRect(), x, y);
			if (overClose != hoverClose)
			{
				hoverClose = overClose;
				dirty = true;
				Present();
			}
			if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN)
			{
				SetForegroundWindow(mainWindow); // TPT's window has the keyboard, and the dialog gets its keys from it
			}
			auto &engine = ui::Engine::Ref();
			if (!OnTop() || msg == WM_MOUSELEAVE)
			{
				return; // modal, as in TPT: only the top window takes input
			}
			if (overClose)
			{
				if (msg == WM_LBUTTONUP)
				{
					// The dialog's own way out (cancel), as if Escape was pressed.
					engine.onKeyPress(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, false, false, false, false);
					engine.onKeyRelease(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, false, false, false, false);
				}
				return;
			}
			auto origin = ClientOrigin();
			int ex = pos.X + (x - origin.X) / scale, ey = pos.Y + (y - origin.Y) / scale;
			switch (msg)
			{
			case WM_MOUSEMOVE: engine.onMouseMove(ex, ey); break;
			case WM_LBUTTONDOWN: SetCapture(hwnd); engine.onMouseDown(ex, ey, SDL_BUTTON_LEFT); break;
			case WM_RBUTTONDOWN: SetCapture(hwnd); engine.onMouseDown(ex, ey, SDL_BUTTON_RIGHT); break;
			case WM_MBUTTONDOWN: SetCapture(hwnd); engine.onMouseDown(ex, ey, SDL_BUTTON_MIDDLE); break;
			case WM_LBUTTONUP: ReleaseCapture(); engine.onMouseUp(ex, ey, SDL_BUTTON_LEFT); break;
			case WM_RBUTTONUP: ReleaseCapture(); engine.onMouseUp(ex, ey, SDL_BUTTON_RIGHT); break;
			case WM_MBUTTONUP: ReleaseCapture(); engine.onMouseUp(ex, ey, SDL_BUTTON_MIDDLE); break;
			case WM_MOUSEWHEEL: engine.onMouseWheel(ex, ey, GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA); break;
			}
		}
	};
	std::vector<std::unique_ptr<HostWindow>> hosts;
	bool skinDialogs = true; // ponytail: no setting yet; false shows TPT's own look

	// A TPT drop-down list or context menu, shown as a real Windows menu. TrackPopupMenu runs its own message loop,
	// so it runs on a thread of its own (with its own hidden owner window) and the sim keeps going meanwhile.
	struct NativeMenu
	{
		ui::Window *window = nullptr;
		std::thread thread;
		std::atomic<int> picked{ -1 }; // -1 = still open, 0 = cancelled, n = item n - 1
		~NativeMenu()
		{
			if (thread.joinable()) thread.join();
		}
	};
	std::unique_ptr<NativeMenu> menu;

	void OpenMenu(ui::Window *window, POINT at)
	{
		menu = std::make_unique<NativeMenu>();
		menu->window = window;
		std::vector<std::tuple<std::wstring, bool, bool>> items;
		for (auto &it : window->MenuItems) items.push_back({ Wide(it.text), it.enabled, it.checked });
		menu->thread = std::thread([m = menu.get(), items = std::move(items), at] {
			HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr);
			HMENU popup = CreatePopupMenu();
			for (size_t i = 0; i < items.size(); i++)
			{
				auto &[text, enabled, checked] = items[i];
				AppendMenuW(popup, MF_STRING | (enabled ? 0 : MF_GRAYED) | (checked ? MF_CHECKED : 0), i + 1, text.c_str());
				if (checked)
				{
					CheckMenuRadioItem(popup, UINT(i + 1), UINT(i + 1), UINT(i + 1), MF_BYCOMMAND);
				}
			}
			SetForegroundWindow(owner); // or the menu won't close on a click elsewhere
			int cmd = TrackPopupMenu(popup, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN, at.x, at.y, 0, owner, nullptr);
			DestroyMenu(popup);
			DestroyWindow(owner);
			m->picked = cmd;
		});
	}

	// Called each tick: hands a picked item (or a cancel) back to TPT on its own thread.
	void TickMenu()
	{
		if (!menu || menu->picked < 0) return;
		int picked = menu->picked;
		auto *window = menu->window;
		menu.reset();
		auto &engine = ui::Engine::Ref();
		if (engine.GetWindow() != window) return;
		if (picked > 0 && picked <= int(window->MenuItems.size()))
		{
			auto pick = window->MenuItems[picked - 1].pick;
			pick();
		}
		else
		{
			engine.onMouseDown(-(1 << 20), -(1 << 20), 1); // a click outside closes it
			engine.onMouseUp(-(1 << 20), -(1 << 20), 1);
		}
	}

	// One host per TPT window above the game: made when the window opens, gone when it closes, repainted when its
	// picture changes.
	void SyncHosts()
	{
		auto &engine = ui::Engine::Ref();
		std::vector<std::unique_ptr<HostWindow>> kept;
		HostWindow *below = nullptr;
		for (auto &hosted : engine.Hosted)
		{
			if (!hosted.window->MenuItems.empty())
			{
				if (!menu || menu->window != hosted.window)
				{
					RECT sr;
					GetWindowRect(mainWindow, &sr);
					POINT at;
					if (below)
					{
						RECT br;
						GetWindowRect(below->hwnd, &br);
						at = { br.left + below->border + (hosted.window->MenuAt.X - below->pos.X) * below->scale,
						       br.top + below->titleH + (hosted.window->MenuAt.Y - below->pos.Y) * below->scale };
					}
					else
					{
						int scale = std::max(1, int((sr.right - sr.left) / engine.g->Size().X));
						at = { sr.left + hosted.window->MenuAt.X * scale, sr.top + hosted.window->MenuAt.Y * scale };
					}
					OpenMenu(hosted.window, at);
				}
				continue;
			}
			auto it = std::find_if(hosts.begin(), hosts.end(), [&](auto &h) { return h && h->window == hosted.window; });
			std::unique_ptr<HostWindow> host;
			if (it != hosts.end())
			{
				host = std::move(*it);
			}
			if (host && host->size != hosted.size)
			{
				host.reset(); // ponytail: a window that changes size is remade (and re-placed)
			}
			if (!host)
			{
				host = std::make_unique<HostWindow>(hosted.window);
				host->size = hosted.size;
				host->owner = palette ? palette->hwnd : nullptr;
				// Where TPT would have drawn it: over the window below it, or on the sim layer.
				RECT sr;
				GetWindowRect(mainWindow, &sr);
				auto screen = engine.g->Size();
				int anchorScale = below ? below->scale : std::max(1, int((sr.right - sr.left) / screen.X));
				POINT at = below ? POINT{ 0, 0 } : POINT{ sr.left, sr.top };
				if (below)
				{
					RECT br;
					GetWindowRect(below->hwnd, &br);
					at = { br.left + below->border + (hosted.pos.X - below->pos.X) * anchorScale,
					       br.top + below->titleH + (hosted.pos.Y - below->pos.Y) * anchorScale };
				}
				else
				{
					at = { sr.left + hosted.pos.X * anchorScale, sr.top + hosted.pos.Y * anchorScale };
				}
				MONITORINFO mi{ sizeof(mi) };
				GetMonitorInfoW(MonitorFromPoint(at, MONITOR_DEFAULTTONEAREST), &mi);
				auto work = mi.rcWork;
				int chromeW = 2 * host->border, chromeH = host->titleH + host->border;
				host->scale = std::max(1, std::min({ engine.GetScale(), int(work.right - work.left - chromeW) / std::max(1, hosted.size.X),
				                                      int(work.bottom - work.top - chromeH) / std::max(1, hosted.size.Y) }));
				int w = hosted.size.X * host->scale + chromeW, h = hosted.size.Y * host->scale + chromeH;
				int x = at.x - host->border, y = at.y - host->titleH;
				if (host->framed && !below)
				{
					// A dialog that TPT centres on its screen: centre it on the monitor instead, the sim layer's
					// scale can differ from the dialog's.
					x = (work.left + work.right - w) / 2;
					y = (work.top + work.bottom - h) / 2;
				}
				x = std::clamp(x, int(work.left), std::max(int(work.left), int(work.right) - w));
				y = std::clamp(y, int(work.top), std::max(int(work.top), int(work.bottom) - h));
				host->Create(x, y, w, h);
				ShowWindow(host->hwnd, SW_SHOWNOACTIVATE);
			}
			host->pos = hosted.pos;
			std::vector<SkinItem> items;
			if (host->framed && skinDialogs && hosted.size != engine.g->Size())
			{
				SkinWalk(hosted.window, hosted.window->GetComponents(), 0, hosted.size.Y, items);
				for (auto &[pos, size] : hosted.window->RawAreas)
				{
					items.push_back({ ui::Component::Kind::Other, pos.X, pos.Y, size.X, size.Y, 0, hosted.size.Y });
				}
			}
			if (host->pixels != hosted.pixels || host->items != items)
			{
				host->items = std::move(items);
				host->pixels = hosted.pixels;
				host->dirty = true;
				host->Present();
			}
			below = host.get();
			kept.push_back(std::move(host));
		}
		hosts = std::move(kept); // the rest close
	}

	// The notification-area (tray) icon: left click shows or hides the Elements window, right click opens a menu.
	NOTIFYICONDATAW tray{};
	constexpr UINT trayMessage = WM_APP + 1;

	LRESULT CALLBACK TrayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
	{
		static const UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
		if (msg == taskbarCreated)
		{
			Shell_NotifyIconW(NIM_ADD, &tray); // Explorer restarted and lost the icon
			return 0;
		}
		if (msg != trayMessage)
		{
			return DefWindowProcW(hwnd, msg, wp, lp);
		}
		if (lp == WM_LBUTTONUP)
		{
			OverlayWindows::TogglePalette();
		}
		if (lp == WM_RBUTTONUP)
		{
			// The plain Windows menu: settings and the way into the Vista windows.
			enum { elements = 1, inputBase = 10, showMenus = 20, exitItem = 99 };
			auto &prefs = GlobalPrefs::Ref();
			auto label = [&](const wchar_t *text, const char *keyName, const char *fallback) {
				return std::wstring(text) + L"\t" + Platform::WinWiden(prefs.Get(ByteString("Overlay.Keys.") + keyName, ByteString(fallback)));
			};
			HMENU menu = CreatePopupMenu();
			bool open = palette && IsWindowVisible(palette->hwnd);
			AppendMenuW(menu, MF_STRING | (open ? MF_CHECKED : 0), elements, label(L"Elements window", "Palette", "Ctrl+Alt+E").c_str());
			AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
			std::wstring modes[] = { L"Input: Auto", label(L"Input: Pass-through", "PassThrough", "Ctrl+Alt+P"), label(L"Input: Locked to TPT", "Locked", "Ctrl+Alt+L") };
			for (int i = 0; i < int(OverlayInput::Count); i++)
			{
				AppendMenuW(menu, MF_STRING | (int(OverlayGetInput()) == i ? MF_CHECKED : 0), inputBase + i, modes[i].c_str());
			}
			AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
			AppendMenuW(menu, MF_STRING | (showTPTMenus ? MF_CHECKED : 0), showMenus, L"Show TPT's own menus");
			AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
			AppendMenuW(menu, MF_STRING, exitItem, L"Exit");
			POINT p;
			GetCursorPos(&p);
			SetForegroundWindow(hwnd); // so the menu closes when you click elsewhere
			int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, p.x, p.y, 0, hwnd, nullptr);
			PostMessageW(hwnd, WM_NULL, 0, 0);
			DestroyMenu(menu);
			if (cmd == elements)
			{
				OverlayWindows::TogglePalette();
			}
			if (cmd >= inputBase && cmd < inputBase + int(OverlayInput::Count))
			{
				OverlaySetInput(OverlayInput(cmd - inputBase));
			}
			if (cmd == showMenus)
			{
				showTPTMenus = !showTPTMenus;
				prefs.Set("Overlay.ShowTPTMenus", showTPTMenus);
			}
			if (cmd == exitItem)
			{
				SDL_Event quit{};
				quit.type = SDL_QUIT;
				SDL_PushEvent(&quit);
			}
		}
		return 0;
	}

	void AddTrayIcon()
	{
		WNDCLASSW wc{};
		wc.lpfnWndProc = TrayProc;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = L"TPTOverlayTray";
		RegisterClassW(&wc);
		tray.cbSize = sizeof(tray);
		// A hidden top-level window, not a message-only one: those miss the TaskbarCreated broadcast.
		tray.hWnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
		tray.uID = 1;
		tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
		tray.uCallbackMessage = trayMessage;
		wchar_t exe[MAX_PATH];
		GetModuleFileNameW(nullptr, exe, MAX_PATH);
		tray.hIcon = ExtractIconW(wc.hInstance, exe, 0);
		if (!tray.hIcon || tray.hIcon == HICON(1))
		{
			tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
		}
		wcscpy_s(tray.szTip, L"The Powder Toy (overlay)");
		Shell_NotifyIconW(NIM_ADD, &tray);
		std::atexit([] { Shell_NotifyIconW(NIM_DELETE, &tray); });
		// On a crash too. A forced kill (Task Manager's End task) can't be caught: Windows clears that icon on hover.
		static LPTOP_LEVEL_EXCEPTION_FILTER previous = SetUnhandledExceptionFilter([](EXCEPTION_POINTERS *e) -> LONG {
			Shell_NotifyIconW(NIM_DELETE, &tray);
			return previous ? previous(e) : EXCEPTION_CONTINUE_SEARCH;
		});
	}

	void ApplyPassThrough(Panel &panel)
	{
		auto ex = GetWindowLongPtrW(panel.hwnd, GWL_EXSTYLE);
		SetWindowLongPtrW(panel.hwnd, GWL_EXSTYLE, passThrough ? ex | WS_EX_TRANSPARENT : ex & ~LONG_PTR(WS_EX_TRANSPARENT));
		panel.opacity = passThrough ? 110 : 255;
		panel.Present();
	}
}

void OverlayWindows::Init(void *mainHwnd)
{
	mainWindow = HWND(mainHwnd);
	auto &prefs = GlobalPrefs::Ref();
	windowWalls = prefs.Get("Overlay.WindowWalls", true);
	// Default UI scale: the larger of Windows' display scaling (TPT is DPI aware) and screen height / 1080, in
	// quarter steps. Overridable.
	HDC screen = GetDC(nullptr);
	float dpiScale = GetDeviceCaps(screen, LOGPIXELSY) / 96.f;
	ReleaseDC(nullptr, screen);
	float heightScale = GetSystemMetrics(SM_CYSCREEN) / 1080.f;
	uiScale = prefs.Get("Overlay.UIScale", std::max(1.f, std::round(std::max(dpiScale, heightScale) * 4) / 4));
	showTPTMenus = prefs.Get("Overlay.ShowTPTMenus", false);
	text = new Text;
	text->SetScale();
	AddTrayIcon();
}

static void TickRecording();
static void TickWindowWalls();

void OverlayWindows::Tick()
{
	if (!mainWindow || !&GameController::Ref())
	{
		return;
	}
	TickWindowWalls();
	static std::vector<Notification *> seen;
	auto notes = GameController::Ref().GetModel()->GetNotifications();
	if (notes != seen)
	{
		for (auto *n : notes)
			if (std::find(seen.begin(), seen.end(), n) == seen.end()) Notify(n->Message.ToUtf8().c_str());
		seen = notes;
		if (palette) palette->dirty = true;
	}
	static bool areaRestored = false;
	if (!areaRestored)
	{
		auto &prefs = GlobalPrefs::Ref();
		SetPlayArea(prefs.Get("Overlay.PlayArea.X", 0), prefs.Get("Overlay.PlayArea.Y", 0),
		            prefs.Get("Overlay.PlayArea.W", CELLS.X), prefs.Get("Overlay.PlayArea.H", CELLS.Y));
		areaRestored = true;
		GameController::Ref().GetView()->SkipIntroText(); // it's in the side window's Welcome tab
	}
	if (!palette && GlobalPrefs::Ref().Get("Overlay.Windows.Palette.Open", true))
	{
		TogglePalette();
	}
	if (palette)
	{
		palette->Tick();
		if (palette->dirty)
		{
			palette->Present();
		}
	}
	if (toast)
	{
		toast->Tick();
	}
	SyncHosts();
	TickMenu();
	TickRecording();
	if (rebuildPalette)
	{
		rebuildPalette = false;
		reopenTab = palette ? palette->tab : -1;
		palette.reset();
		text->SetScale();
		TogglePalette();
	}
}

bool OverlayWindows::RecordingKey()
{
	return !recording.empty();
}

// While the Overlay tab records a shortcut: the first letter, digit or F key pressed with the modifiers held.
static void TickRecording()
{
	if (recording.empty()) return;
	auto down = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
	if (down(VK_ESCAPE))
	{
		recording.clear();
		if (palette) palette->dirty = true;
		return;
	}
	int key = 0;
	for (int vk = '0'; vk <= 'Z' && !key; vk++)
		if ((vk <= '9' || vk >= 'A') && down(vk)) key = vk;
	for (int vk = VK_F1; vk <= VK_F24 && !key; vk++)
		if (down(vk)) key = vk;
	bool ctrl = down(VK_CONTROL), alt = down(VK_MENU), shift = down(VK_SHIFT), win = down(VK_LWIN) || down(VK_RWIN);
	if (!key || (key < VK_F1 && !ctrl && !alt && !win)) return; // a bare letter would fire while typing
	ByteString combo;
	if (ctrl) combo += "Ctrl+";
	if (alt) combo += "Alt+";
	if (shift) combo += "Shift+";
	if (win) combo += "Win+";
	combo += key >= VK_F1 ? ByteString::Build("F", key - VK_F1 + 1) : ByteString(1, char(key));
	GlobalPrefs::Ref().Set("Overlay.Keys." + recording, combo);
	recording.clear();
	OverlayLoadKeys();
	if (palette) palette->dirty = true;
}

void OverlayWindows::TogglePalette()
{
	if (!&GameController::Ref())
	{
		return; // the tray icon exists before the game does
	}
	bool open = !palette || !IsWindowVisible(palette->hwnd) || IsIconic(palette->hwnd);
	GlobalPrefs::Ref().Set("Overlay.Windows.Palette.Open", open);
	if (!open)
	{
		ShowWindow(palette->hwnd, SW_HIDE);
		return;
	}
	if (!palette)
	{
		palette = std::make_unique<SideWindow>();
		// Tall enough for every section in the navigation pane and for the Simulation tab.
		int w = S(560), h = std::max<int>(palette->NavRect(int(palette->Sections().size())).top + S(4) + palette->statusH + palette->border, S(560));
		// Default: just right of TPT's window, or its top-left corner if that's off screen.
		RECT rc;
		GetWindowRect(mainWindow, &rc);
		int x = rc.right + S(8), y = rc.top;
		if (x + w > GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN))
		{
			x = rc.left + S(8);
			y = rc.top + S(8);
		}
		auto &prefs = GlobalPrefs::Ref();
		POINT saved{ prefs.Get("Overlay.Windows.Palette.X", x), prefs.Get("Overlay.Windows.Palette.Y", y) };
		POINT grip{ saved.x + w / 2, saved.y + S(10) }; // the title bar must be on a monitor
		if (MonitorFromPoint(grip, MONITOR_DEFAULTTONULL))
		{
			x = saved.x;
			y = saved.y;
		}
		w = std::max(prefs.Get("Overlay.Windows.Palette.W", w), S(360));
		h = std::max(prefs.Get("Overlay.Windows.Palette.H", h), S(300));
		// The whole window on its monitor's work area (a saved spot can hang off the edge).
		MONITORINFO mi{ sizeof(mi) };
		GetMonitorInfoW(MonitorFromPoint({ x + w / 2, y + S(10) }, MONITOR_DEFAULTTONEAREST), &mi);
		auto work = mi.rcWork;
		w = std::min(w, int(work.right - work.left));
		h = std::min(h, int(work.bottom - work.top));
		x = std::clamp(x, int(work.left), int(work.right) - w);
		y = std::clamp(y, int(work.top), int(work.bottom) - h);
		palette->grip = palette->border;
		palette->minSize = { S(360), S(300) };
		palette->Create(x, y, w, h);
	}
	palette->dirty = true;
	palette->Present();
	ShowWindow(palette->hwnd, SW_SHOWNOACTIVATE);
	ApplyPassThrough(*palette);
}

void OverlayWindows::SetPassThrough(bool on)
{
	passThrough = on;
	if (palette)
	{
		ApplyPassThrough(*palette);
	}
}

void OverlayWindows::Notify(const char *message)
{
	if (!toast)
	{
		toast = std::make_unique<Toast>();
	}
	toast->Show(mainWindow, Platform::WinWiden(message));
}
bool OverlayWindows::HideTPTMenus()
{
	return !showTPTMenus && palette && IsWindowVisible(palette->hwnd);
}

void OverlayWindows::ApplySimMasks(uint32_t *argb, int w)
{
	if (!&GameController::Ref())
	{
		return;
	}
	// ponytail: assumes the sim is drawn 1:1 at the top-left
	auto *model = GameController::Ref().GetModel();
	auto *sim = model->GetSimulation();
	auto area = sim->PlayRect();
	bool full = sim->playArea == CELLS.OriginRect();
	// The zoom window is a magnifier: opaque, its frame included, black particles and all.
	auto zoom = RectSized(model->GetZoomWindowPosition() - Vec2{ 2, 2 }, Vec2{ 1, 1 } * (model->GetZoomSize() * model->GetZoomFactor() + 4));
	bool zoomOn = model->GetZoomEnabled();
	for (int y = 0; y < YRES; y++)
		for (int x = 0; x < XRES; x++)
		{
			auto &p = argb[y * w + x];
			if (zoomOn && zoom.Contains({ x, y }))
			{
				p |= 0xFF000000;
			}
			else if (!ourWalls.empty() && ourWalls[size_t(y / CELL) * XCELLS + x / CELL])
			{
				p = 0; // a window's wall: the window shows through
			}
			else if (!area.Contains({ x, y }))
			{
				p = 0;
			}
			else if (!(p & 0xFFFFFF) && (sim->pmap[y][x] || sim->photons[y][x]))
			{
				p = 0xFF000000;
			}
		}
	if (!full)
	{
		// A faint frame just outside the area, so you can see where it ends.
		auto br = area.BottomRight();
		auto mark = [&](int x, int y) {
			if (x >= 0 && y >= 0 && x < XRES && y < YRES) argb[y * w + x] = 0x785080C0;
		};
		for (int x = area.pos.X - 1; x <= br.X + 1; x++) mark(x, area.pos.Y - 1), mark(x, br.Y + 1);
		for (int y = area.pos.Y; y <= br.Y; y++) mark(area.pos.X - 1, y), mark(br.X + 1, y);
	}
}

// A few times a second: every visible window's rectangle becomes wall cells, so particles pile on title bars and
// flow around windows. Only empty cells are taken, and only those are given back.
// ponytail: a save made meanwhile includes these walls; strip them in the save path if that bothers anyone.
static void TickWindowWalls()
{
	static int frames = 0;
	if (++frames % 20) return;
	auto *sim = GameController::Ref().GetModel()->GetSimulation();
	ourWalls.resize(size_t(XCELLS) * YCELLS);
	std::vector<uint8_t> want(ourWalls.size());
	RECT sr;
	GetWindowRect(mainWindow, &sr);
	int sw = sr.right - sr.left, sh = sr.bottom - sr.top;
	if (windowWalls && OverlayGetLayer() == OverlayLayer::OnTop && sw > 0 && sh > 0)
	{
		struct Ctx { std::vector<uint8_t> *want; RECT sr; int sw, sh; } ctx{ &want, sr, sw, sh };
		EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
			auto &c = *(Ctx *)lp;
			auto ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
			BOOL cloaked = FALSE;
			DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
			wchar_t cls[32];
			GetClassNameW(hwnd, cls, 32);
			if (hwnd == mainWindow || !IsWindowVisible(hwnd) || IsIconic(hwnd) || cloaked || (ex & WS_EX_TRANSPARENT) ||
			    ((ex & WS_EX_TOOLWINDOW) && GetWindowThreadProcessId(hwnd, nullptr) != GetCurrentThreadId()) ||
			    !wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd"))
			{
				return TRUE;
			}
			RECT r; // the visible frame, without the invisible resize borders
			if (DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)) != S_OK) GetWindowRect(hwnd, &r);
			// screen -> sim pixels -> cells, rounded to the nearest cell edge
			auto cell = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw / CELL)); };
			int x0 = std::max(0, cell(r.left - c.sr.left, WINDOWW, c.sw)), x1 = std::min(XCELLS, cell(r.right - c.sr.left, WINDOWW, c.sw));
			int y0 = std::max(0, cell(r.top - c.sr.top, WINDOWH, c.sh)), y1 = std::min(YCELLS, cell(r.bottom - c.sr.top, WINDOWH, c.sh));
			for (int y = y0; y < y1; y++)
				for (int x = x0; x < x1; x++) (*c.want)[size_t(y) * XCELLS + x] = 1;
			return TRUE;
		}, LPARAM(&ctx));
	}
	for (int y = 0; y < YCELLS; y++)
		for (int x = 0; x < XCELLS; x++)
		{
			auto i = size_t(y) * XCELLS + x;
			auto &b = sim->bmap[y][x];
			if (want[i] && !ourWalls[i] && !b)
			{
				b = WL_WALL;
				ourWalls[i] = 1;
			}
			else if (!want[i] && ourWalls[i])
			{
				if (b == WL_WALL) b = 0;
				ourWalls[i] = 0;
			}
		}
}

void OverlayWindows::ClearUnderWindows(uint32_t *argb, int w, int h, int ww, int wh)
{
	// ponytail: also clears where another app's window covers ours; walk the z-order if that ever matters.
	if (ww <= 0 || wh <= 0)
	{
		return;
	}
	RECT sr;
	GetWindowRect(mainWindow, &sr);
	auto map = [](long v, int n, int nw, bool up) { return int(std::clamp((v * n + (up ? nw - 1 : 0)) / nw, 0L, long(n))); };
	auto clear = [&](HWND hwnd) {
		if (!hwnd || !IsWindowVisible(hwnd) || IsIconic(hwnd))
		{
			return;
		}
		RECT pr;
		GetWindowRect(hwnd, &pr);
		int x0 = map(pr.left - sr.left, w, ww, false), x1 = map(pr.right - sr.left, w, ww, true);
		int y0 = map(pr.top - sr.top, h, wh, false), y1 = map(pr.bottom - sr.top, h, wh, true);
		for (int y = y0; y < y1; y++)
		{
			std::fill(argb + size_t(y) * w + x0, argb + size_t(y) * w + x1, 0);
		}
	};
	clear(palette ? palette->hwnd : nullptr);
	for (auto &host : hosts)
	{
		clear(host->hwnd);
	}
}

void OverlayWindows::SetPlayArea(int x, int y, int w, int h)
{
	if (!&GameController::Ref())
	{
		return;
	}
	auto area = RectSized(Vec2(x, y), Vec2(w, h)) & CELLS.OriginRect();
	if (area.size.X < 8 || area.size.Y < 8)
	{
		area = CELLS.OriginRect(); // too small to play in: back to the whole sim
	}
	GameController::Ref().GetModel()->GetSimulation()->playArea = area;
	auto &prefs = GlobalPrefs::Ref();
	prefs.Set("Overlay.PlayArea.X", area.pos.X);
	prefs.Set("Overlay.PlayArea.Y", area.pos.Y);
	prefs.Set("Overlay.PlayArea.W", area.size.X);
	prefs.Set("Overlay.PlayArea.H", area.size.Y);
}

#else
void OverlayWindows::Init(void *) {}
void OverlayWindows::Tick() {}
void OverlayWindows::TogglePalette() {}
void OverlayWindows::SetPassThrough(bool) {}
void OverlayWindows::Notify(const char *) {}
bool OverlayWindows::HideTPTMenus() { return false; }
void OverlayWindows::ApplySimMasks(uint32_t *, int) {}
void OverlayWindows::ClearUnderWindows(uint32_t *, int, int, int, int) {}
void OverlayWindows::SetPlayArea(int, int, int, int) {}
#endif
