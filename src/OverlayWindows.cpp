// Overlay mode's own desktop windows (OVERLAY.md): each menu is a real top-level layered window, drawn here in
// software with per-pixel alpha. The look is a Theme (Windows Vista, 7, 8, 10, Classic Mac, macOS).
#include "OverlayWindows.h"
#include "OverlayWalls.h"
#ifdef _WIN32
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601 // IFolderView2 for the desktop icons (the build targets XP)
#include "Overlay.h"
#include "gui/game/GameController.h"
#include "gui/game/GameModel.h"
#include "gui/game/Notification.h"
#include "client/SaveFile.h"
#include "client/SaveInfo.h"
#include "client/http/SearchSavesRequest.h"
#include "client/http/ThumbnailRequest.h"
#include "client/http/Request.h"
#include "gui/SavePreviewType.h"
#include "client/GameSave.h"
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
#include "gui/options/OptionsModel.h"
#include "lua/CommandInterface.h"
#include "Format.h"
#include "gui/dialogues/ErrorMessage.h"
#include "client/Client.h"
#include "simulation/SaveRenderer.h"
#include "Config.h"
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
#include <cwctype>
#include <atomic>
#include <thread>
#include <mutex>
#include <tuple>
#include <cmath>
#include <functional>
#include <SDL.h>
#include <map>
#include <memory>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")
#include <shlobj.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <exdisp.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#include <d3d11.h>
#include <dxgi1_2.h>
// the Overlay tab's pass-through apps (Overlay.PassApps)
static std::vector<ByteString> PassApps();
static void SetPassApps(const std::vector<ByteString> &apps);
static std::vector<ByteString> RunningApps();
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#undef small       // rpcndr.h, via dwmapi.h
#undef GetUserName // SaveInfo::GetUserName

namespace
{
	float uiScale = 1;
	bool windowWalls = true;            // open windows are walls to particles (Overlay.WindowWalls)
	bool iconWalls = true;              // and so are the desktop icons (Overlay.IconWalls)
	HWND mainWindow = nullptr;    // TPT's own window
	std::string recording;        // Overlay.Keys.<name> being recorded in the Overlay tab, or empty
	std::function<void()> later; // run on the next tick, outside any window's message handling
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

	enum CaptionKind { CaptionMin, CaptionMax, CaptionClose };

	// A caption glyph centred on (cx, cy): an x (close), a square (maximise), a bar (minimise) or, with plus, a +.
	void Glyph(Canvas &cv, int cx, int cy, CaptionKind kind, uint32_t c, int size, int thick, bool plus = false)
	{
		int k = size / 2;
		if (kind == CaptionClose)
		{
			for (int i = -k; i <= k; i++)
				for (int t = 0; t < thick; t++)
				{
					cv.Blend(cx + i + t, cy + i, c, 255);
					cv.Blend(cx + i + t, cy - i, c, 255);
				}
			return;
		}
		if (kind == CaptionMax && !plus)
		{
			for (int t = 0; t < thick; t++) cv.RoundFrame(cx - k + t, cy - k + t, 2 * k + 1 - 2 * t, 2 * k + 1 - 2 * t, 0, c, 255);
			return;
		}
		cv.RoundRect(cx - k, cy - thick / 2, 2 * k + 1, thick, 0, c, c, 255);
		if (plus) cv.RoundRect(cx - thick / 2, cy - k, thick, 2 * k + 1, 0, c, c, 255);
	}

	// The desktop as the GPU composes it (DXGI desktop duplication, what screen recorders use), minus windows kept out
	// of captures. Grab copies one screen rectangle out at half size; the last frame is kept for when nothing has
	// changed.
	struct DesktopGrab
	{
		ID3D11Device *dev = nullptr;
		ID3D11DeviceContext *ctx = nullptr;
		IDXGIOutputDuplication *dup = nullptr;
		// The whole screen, and two read-back copies of the grabbed rectangle: each grab reads the one copied a refresh
		// ago (done by then, so no waiting on the GPU) while the GPU fills the other.
		ID3D11Texture2D *frame = nullptr, *staging[2] = {};
		ID3D11Texture2D *mip = nullptr; // the rectangle at full and half size: GenerateMips does the halving
		ID3D11ShaderResourceView *mipView = nullptr;
		ID3D11RenderTargetView *mipTarget = nullptr; // to clear the part that's off this screen
		POINT stagedAt[2] = {}; // the screen position each copy is of
		bool staged[2] = {};
		int cur = 0;
		RECT desk{};
		int sw = 0, sh = 0;
		bool have = false;

		~DesktopGrab()
		{
			Release();
		}

		void Release()
		{
			for (IUnknown *p : std::initializer_list<IUnknown *>{ mipTarget, mipView, mip, staging[0], staging[1], frame, dup, ctx, dev })
				if (p) p->Release();
			mipTarget = nullptr, mipView = nullptr, mip = nullptr;
			staging[0] = staging[1] = nullptr, frame = nullptr, dup = nullptr, ctx = nullptr, dev = nullptr;
			staged[0] = staged[1] = have = false;
		}

		bool Open(POINT at)
		{
			IDXGIFactory1 *factory;
			if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&factory))) return false;
			IDXGIAdapter1 *adapter;
			bool ok = false;
			for (UINT i = 0; !ok && factory->EnumAdapters1(i, &adapter) == S_OK; i++)
			{
				IDXGIOutput *output;
				for (UINT j = 0; !ok && adapter->EnumOutputs(j, &output) == S_OK; j++)
				{
					DXGI_OUTPUT_DESC d;
					output->GetDesc(&d);
					IDXGIOutput1 *output1;
					if (PtInRect(&d.DesktopCoordinates, at) && SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput1), (void **)&output1)))
					{
						if (SUCCEEDED(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx)))
							ok = SUCCEEDED(output1->DuplicateOutput(dev, &dup));
						desk = d.DesktopCoordinates;
						output1->Release();
					}
					output->Release();
				}
				adapter->Release();
			}
			factory->Release();
			if (!ok) Release();
			return ok;
		}

		// The screen rectangle at (x, y), bw x bh, into dst at half size (off this screen: black). False: no
		// duplication here.
		bool Grab(int x, int y, int bw, int bh, uint32_t *dst)
		{
			POINT c{ x + bw / 2, y + bh / 2 };
			if (dup && !PtInRect(&desk, c)) Release(); // moved to another screen
			if (!dup && !Open(c)) return false;
			DXGI_OUTDUPL_FRAME_INFO info;
			IDXGIResource *res = nullptr;
			HRESULT hr = dup->AcquireNextFrame(0, &info, &res);
			if (hr == DXGI_ERROR_ACCESS_LOST)
			{
				Release(); // mode change, secure desktop...: reopened next time
				return false;
			}
			if (SUCCEEDED(hr))
			{
				ID3D11Texture2D *tex;
				if (info.LastPresentTime.QuadPart && SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&tex)))
				{
					if (!frame)
					{
						D3D11_TEXTURE2D_DESC td;
						tex->GetDesc(&td);
						td.Usage = D3D11_USAGE_DEFAULT, td.BindFlags = 0, td.CPUAccessFlags = 0, td.MiscFlags = 0;
						dev->CreateTexture2D(&td, nullptr, &frame);
						sw = int(td.Width), sh = int(td.Height);
					}
					if (frame) ctx->CopyResource(frame, tex), have = true;
					tex->Release();
				}
				res->Release();
				dup->ReleaseFrame();
			}
			if (!have) return false;
			int hw = bw / 2, hh = bh / 2;
			D3D11_TEXTURE2D_DESC md;
			if (mip && (mip->GetDesc(&md), int(md.Width) != bw || int(md.Height) != bh))
			{
				mipTarget->Release(), mipView->Release(), mip->Release();
				mipTarget = nullptr, mipView = nullptr, mip = nullptr;
			}
			if (!mip)
			{
				D3D11_TEXTURE2D_DESC td{};
				td.Width = UINT(bw), td.Height = UINT(bh), td.MipLevels = 2, td.ArraySize = 1, td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				td.SampleDesc.Count = 1, td.Usage = D3D11_USAGE_DEFAULT, td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
				td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
				if (FAILED(dev->CreateTexture2D(&td, nullptr, &mip))) return false;
				dev->CreateShaderResourceView(mip, nullptr, &mipView);
				D3D11_RENDER_TARGET_VIEW_DESC rd{ td.Format, D3D11_RTV_DIMENSION_TEXTURE2D };
				dev->CreateRenderTargetView(mip, &rd, &mipTarget);
				if (!mipView || !mipTarget) return false;
			}
			for (auto &s : staging)
			{
				D3D11_TEXTURE2D_DESC sd;
				if (s && (s->GetDesc(&sd), int(sd.Width) != hw || int(sd.Height) != hh)) s->Release(), s = nullptr, staged[0] = staged[1] = false;
				if (s) continue;
				D3D11_TEXTURE2D_DESC td{};
				td.Width = UINT(hw), td.Height = UINT(hh), td.MipLevels = 1, td.ArraySize = 1, td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				td.SampleDesc.Count = 1, td.Usage = D3D11_USAGE_STAGING, td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				if (FAILED(dev->CreateTexture2D(&td, nullptr, &s))) return false;
			}
			// copy this rectangle (the part on this screen) in, halve it, and into one read-back copy...
			int sx = x - desk.left, sy = y - desk.top;
			int l = std::max(0, sx), t = std::max(0, sy), r = std::clamp(sx + bw, 0, sw), b = std::clamp(sy + bh, 0, sh);
			if (l != sx || t != sy || r != sx + bw || b != sy + bh)
			{
				const float black[4] = {};
				ctx->ClearRenderTargetView(mipTarget, black);
			}
			if (r > l && b > t)
			{
				D3D11_BOX box{ UINT(l), UINT(t), 0, UINT(r), UINT(b), 1 };
				ctx->CopySubresourceRegion(mip, 0, UINT(l - sx), UINT(t - sy), 0, frame, 0, &box);
			}
			ctx->GenerateMips(mipView);
			ctx->CopySubresourceRegion(staging[cur], 0, 0, 0, 0, mip, 1, nullptr);
			stagedAt[cur] = { x, y };
			staged[cur] = true;
			// ...and read the other, shifted by how far the window has moved since (the first time: this one)
			int read = staged[cur ^ 1] ? cur ^ 1 : cur;
			cur ^= 1;
			D3D11_MAPPED_SUBRESOURCE m;
			if (FAILED(ctx->Map(staging[read], 0, D3D11_MAP_READ, 0, &m))) return false;
			int dx = (x - stagedAt[read].x) / 2, dy = (y - stagedAt[read].y) / 2; // dst (i, j) is the copy's (i + dx, j + dy)
			int i0 = std::max(0, -dx), i1 = std::min(hw, hw - dx);
			std::fill(dst, dst + size_t(hw) * hh, 0);
			if (i1 > i0)
				for (int j = std::max(0, -dy); j < std::min(hh, hh - dy); j++)
					memcpy(dst + size_t(j) * hw + i0, (uint8_t *)m.pData + size_t(j + dy) * m.RowPitch + size_t(i0 + dx) * 4, size_t(i1 - i0) * 4);
			ctx->Unmap(staging[read], 0);
			return true;
		}
	};

	// An OS look (pref Overlay.Theme). The theme draws the frame, caption buttons, push buttons and popups; tabs,
	// list items, check boxes, group boxes and the status bar are drawn here in its colours. Sizes are unscaled.
	struct Theme
	{
		const wchar_t *name = L"";
		uint32_t page = 0xFFFFFF, ink = 0x000000, pressedInk = 0x000000, heading = 0x1E395B, navInk = 0x1E395B, navSelInk = 0xFFFFFFFF /* none: navInk */, edge = 0x898C95, groupEdge = 0xD5DFE5;
		uint32_t strip = 0xF0F0F0, tabOff[2] = { 0xF2F2F2, 0xE3E3E3 }, tabHot[2] = { 0xEAF6FD, 0xA7D9F5 }, tabHotEdge = 0x3C7FB1;
		uint32_t status[3] = { 0xF7F9FC, 0xDCE5F1, 0xA0AFC3 }, nav[3] = { 0xF1F5FB, 0xE5EDF8, 0xA0AFC3 }; // top, bottom, edge
		uint32_t navHot[3] = { 0xF2F8FE, 0xE3EFFD, 0xB8D6FB }, navSel[3] = { 0xDCEBFC, 0xC1DBFC, 0x7DA2CE };
		uint32_t box[3] = { 0xF4F4F4, 0xDEDEDE, 0x8E8F8F }, boxHot[3] = { 0xDEF3FD, 0xB1DFFD, 0x3C7FB1 };
		uint32_t mark = 0x1C3D6E, dot[2] = { 0x8AD0F2, 0x1C64B4 };
		uint32_t boxOn = 0; // non-zero: a ticked box or radio is filled with this (macOS), mark and dot then contrast
		int radius = 3, boxRadius = 2, glow = 0;
		bool segmentTabs = false; // macOS segmented control instead of tabs
		// Caption buttons: widths (minimise, maximise, close), height, top, gap, distance from the edge (-1: the
		// frame border) and the side each sits on.
		int capW[3] = { 27, 27, 43 }, capH = 18, capTop = 1, capGap = 0, capInset = -1;
		bool capLeft[3] = { false, false, false };

		virtual ~Theme() = default;
		virtual void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) = 0;
		virtual void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) = 0;
		virtual void Button(Canvas &cv, int x, int y, int w, int h, int state) = 0; // 0 normal, 1 hover, 2 pressed/selected
		int stripAlpha = 255; // the tab strip's background (0: the frame shows through)
		// Live glass (see Glass): the window is kept out of screen captures. A paint records the glass shapes it wants
		// and Bake turns them into per-pixel samples; every display refresh Prepare grabs what's behind the window and
		// Underlay puts the glass under the cached UI.
		struct Shape { int x, y, w, h, r; float frost; uint32_t tint; float lens; };
		struct Baked { uint32_t idx, src[3], tint; uint8_t cover, frost, dark, white; }; // src: backdrop pixel per channel
		// One window's view of what's behind it: px is the window plus a margin all round; scratch is the glass layer.
		struct Backdrop
		{
			DesktopGrab grab;
			std::vector<uint32_t> px, scratch, small; // small: px at half size, blurred (what the glass samples)
			std::vector<uint32_t> probe; // some of the small pixels seen through the glass
			int w = 0, h = 0;
		};
		std::vector<Shape> shapes;
		virtual bool Live() const { return false; }
		virtual void Bake(Backdrop &bd, const Canvas &ui, std::vector<Baked> &out) { (void)bd, (void)ui, (void)out; }
		// Grabs what's behind; true when the look should change with it (a repaint).
		virtual bool Prepare(Backdrop &bd, HWND hwnd, int w, int h) { (void)bd, (void)hwnd, (void)w, (void)h; return false; }
		virtual uint32_t TabInk(bool on) const { (void)on; return ink; }
		virtual void Underlay(Backdrop &bd, Canvas &cv, const Canvas &ui, const std::vector<Baked> &baked) { (void)bd, (void)cv, (void)ui, (void)baked; }
		virtual void Popup(Canvas &cv)
		{
			cv.RoundRect(0, 0, cv.w, cv.h, S(float(radius)), page, page, 250);
			cv.RoundFrame(0, 0, cv.w, cv.h, S(float(radius)), edge, 255);
		}

		RECT CaptionRect(int w, int border, CaptionKind k) const
		{
			// outward in: on the right close, maximise, minimise; on the left close, minimise, maximise
			static const CaptionKind order[2][3] = { { CaptionClose, CaptionMax, CaptionMin }, { CaptionClose, CaptionMin, CaptionMax } };
			int x = capInset < 0 ? border : S(float(capInset)), top = capTop <= 1 ? capTop : S(float(capTop));
			for (auto o : order[capLeft[k]])
			{
				if (capLeft[o] != capLeft[k]) continue;
				int bw = S(float(capW[o]));
				if (o == k) return capLeft[k] ? RECT{ x, top, x + bw, top + S(float(capH)) } : RECT{ w - x - bw, top, w - x, top + S(float(capH)) };
				x += bw + S(float(capGap));
			}
			return {};
		}

		// The navigation pane's background, with its edge on the right.
		virtual void NavPane(Canvas &cv, int x, int y, int w, int h)
		{
			cv.RoundRect(x, y, w, h, 0, nav[0], nav[1], 255);
			for (int j = y; j < y + h; j++) cv.Blend(x + w, j, nav[2], 255);
		}

		// A navigation-pane item: plain, hovered (1) or selected (2).
		virtual void NavItem(Canvas &cv, int x, int y, int w, int h, int state)
		{
			if (state == 0) return;
			auto *c = state == 2 ? navSel : navHot;
			cv.RoundRect(x, y, w, h, S(segmentTabs ? 5.f : 2.f), c[0], c[1], 255);
			cv.RoundFrame(x, y, w, h, S(segmentTabs ? 5.f : 2.f), c[2], 255);
		}

		// A tab on the strip; the selected one's rectangle reaches the page.
		virtual void Tab(Canvas &cv, RECT rc, bool on, bool hot)
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			if (segmentTabs)
			{
				if (on || hot) cv.RoundRect(rc.left, rc.top + S(1), w, h - S(4), S(5), on ? 0xFFFFFF : 0xDDDDDD, on ? 0xFFFFFF : 0xDDDDDD, 255);
				if (on) cv.RoundFrame(rc.left, rc.top + S(1), w, h - S(4), S(5), 0xC4C4C4, 255);
				return;
			}
			cv.RoundRect(rc.left, rc.top, w, h, S(float(radius)), on ? page : (hot ? tabHot[0] : tabOff[0]), on ? page : (hot ? tabHot[1] : tabOff[1]), 255, true);
			cv.RoundFrame(rc.left, rc.top, w, h + S(3), S(float(radius)), hot ? tabHotEdge : edge, 255, true);
		}
	};

	// Windows Vista Aero.
	struct Vista : Theme
	{
		uint32_t glassTop = 0x7FA8D0, glassBottom = 0x4C7BAA; // the default Aero "sky" tint
		int glassAlpha = 205;

		Vista()
		{
			name = L"Windows Vista";
			glow = 5;
		}

		// The glass frame: a translucent tinted body, a glossy upper half on the title bar, dark outer and light
		// inner edges. The client area (below the title bar, inset by the frame) is left for the caller.
		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			int r = S(6);
			cv.RoundRect(0, 0, cv.w, cv.h, r, glassTop, glassBottom, glassAlpha);
			cv.RoundRect(1, 1, cv.w - 2, titleH / 2, r, 0xFFFFFF, 0xFFFFFF, 60, true); // gloss
			Extra(cv, titleH);
			cv.RoundFrame(0, 0, cv.w, cv.h, r, 0x000000, 170);
			cv.RoundFrame(1, 1, cv.w - 2, cv.h - 2, r - 1, 0xFFFFFF, 110);
			text->Draw(cv, S(10), (titleH - text->Measure(title).cy) / 2, title, 0x000000, false, cv.w - S(80), S(5));
			// The client area's sunken edge.
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, 0x000000, 90);
		}
		virtual void Extra(Canvas &, int) {}

		// Vista's glossy red close pill, or the light glass minimise and maximise buttons. hover brightens them.
		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
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
				Glyph(cv, cx, cy, CaptionClose, 0xFFFFFF, S(7), S(2));
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

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
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

		void Popup(Canvas &cv) override
		{
			int r = S(6);
			cv.RoundRect(0, 0, cv.w, cv.h, r, glassTop, glassBottom, glassAlpha);
			cv.RoundRect(1, 1, cv.w - 2, cv.h / 2, r, 0xFFFFFF, 0xFFFFFF, 60, true);
			cv.RoundFrame(0, 0, cv.w, cv.h, r, 0x000000, 170);
			cv.RoundFrame(1, 1, cv.w - 2, cv.h - 2, r - 1, 0xFFFFFF, 110);
		}
	};

	// Windows 7: paler, clearer glass with the diagonal light streaks, and wider caption buttons.
	struct Seven : Vista
	{
		Seven()
		{
			name = L"Windows 7";
			glassTop = 0xA9C8EA, glassBottom = 0x7FA4CF, glassAlpha = 185;
			capW[0] = 26, capW[1] = 26, capW[2] = 45, capH = 19, capTop = 0;
		}

		void Extra(Canvas &cv, int) override
		{
			// three soft bands, only where the frame already is
			for (int band = 0; band < 3; band++)
			{
				int x0 = cv.w / 5 + band * S(70) * (band + 1), bw = S(14) + band * S(8);
				for (int y = 0; y < cv.h; y++)
					for (int x = x0 - y / 2; x < x0 - y / 2 + bw; x++)
						if (x >= 0 && x < cv.w && (cv.px[size_t(y) * cv.w + x] >> 24)) cv.Blend(x, y, 0xFFFFFF, 28);
			}
		}
	};

	// Windows 8: flat, a solid coloured frame, a centred title and flat square caption buttons.
	struct Eight : Theme
	{
		static constexpr uint32_t frame = 0x6A9BD8, frameEdge = 0x4A7CBE;

		Eight()
		{
			name = L"Windows 8";
			radius = 0, boxRadius = 0;
			capW[0] = 26, capW[1] = 27, capW[2] = 45, capH = 20, capTop = 0;
			heading = 0x000000, navInk = 0x000000, edge = 0xACACAC, groupEdge = 0xDCDCDC;
			tabOff[0] = 0xF0F0F0, tabOff[1] = 0xE5E5E5, tabHot[0] = 0xECF4FC, tabHot[1] = 0xDCECFC, tabHotEdge = 0x7EB4EA;
			status[0] = status[1] = 0xF0F0F0, status[2] = 0xD7D7D7;
			nav[0] = nav[1] = 0xFFFFFF, nav[2] = 0xE5E5E5;
			navHot[0] = navHot[1] = 0xE5F3FB, navHot[2] = 0x70C0E7, navSel[0] = navSel[1] = 0xCBE8F6, navSel[2] = 0x26A0DA;
			box[0] = box[1] = 0xFFFFFF, box[2] = 0x707070, boxHot[0] = boxHot[1] = 0xF3F9FF, boxHot[2] = 0x3399FF;
			mark = 0x000000, dot[0] = dot[1] = 0x333333;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, 0, frame, frame, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, frameEdge, 255);
			int tw = text->Measure(title).cx;
			text->Draw(cv, std::max(S(10), (cv.w - tw) / 2), (titleH - text->Measure(title).cy) / 2, title, 0x000000, false, cv.w - S(130));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, frameEdge, 255);
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			bool close = kind == CaptionClose;
			if (close) cv.RoundRect(rc.left, rc.top, w, h, 0, hover ? 0xE04343 : 0xC75050, hover ? 0xE04343 : 0xC75050, 255);
			else if (hover) cv.RoundRect(rc.left, rc.top, w, h, 0, 0xFFFFFF, 0xFFFFFF, 70);
			Glyph(cv, rc.left + w / 2, rc.top + h / 2 + (kind == CaptionMin ? S(3) : 0), kind, close ? 0xFFFFFF : 0x000000, S(8), S(1.5f));
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			static const uint32_t top[] = { 0xF0F0F0, 0xECF4FC, 0xDAECFC }, bottom[] = { 0xE5E5E5, 0xDCECFC, 0xC4E0FC }, edge[] = { 0xACACAC, 0x7EB4EA, 0x569DE5 };
			cv.RoundRect(x, y, w, h, 0, top[state], bottom[state], 255);
			cv.RoundFrame(x, y, w, h, 0, edge[state], 255);
		}
	};

	// Windows 10: a white frame with a thin border, full-height caption buttons and flat controls.
	struct Ten : Theme
	{
		Ten()
		{
			name = L"Windows 10";
			radius = 0, boxRadius = 0;
			capW[0] = capW[1] = capW[2] = 46, capH = 28, capTop = 1, capInset = 1;
			heading = 0x000000, navInk = 0x000000, edge = 0xD9D9D9, groupEdge = 0xDCDCDC, strip = 0xFFFFFF;
			tabOff[0] = tabOff[1] = 0xF0F0F0, tabHot[0] = tabHot[1] = 0xD8EAF9, tabHotEdge = 0x7EB4EA;
			status[0] = status[1] = 0xF0F0F0, status[2] = 0xD7D7D7;
			nav[0] = nav[1] = 0xFFFFFF, nav[2] = 0xF0F0F0;
			navHot[0] = navHot[1] = navHot[2] = 0xE5F3FF, navSel[0] = navSel[1] = 0xCCE8FF, navSel[2] = 0x99D1FF;
			box[0] = box[1] = 0xFFFFFF, box[2] = 0x333333, boxHot[0] = boxHot[1] = 0xFFFFFF, boxHot[2] = 0x0078D7;
			mark = 0x000000, dot[0] = dot[1] = 0x000000;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, 0, 0xFFFFFF, 0xFFFFFF, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, 0x6E6E6E, 200);
			text->Draw(cv, S(10), (titleH - text->Measure(title).cy) / 2, title, 0x000000, false, cv.w - S(160));
			(void)border;
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			bool close = kind == CaptionClose;
			if (hover) cv.RoundRect(rc.left, rc.top, w, h, 0, close ? 0xE81123 : 0xE5E5E5, close ? 0xE81123 : 0xE5E5E5, 255);
			Glyph(cv, rc.left + w / 2, rc.top + h / 2, kind, close && hover ? 0xFFFFFF : 0x000000, S(10), 1);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			static const uint32_t fill[] = { 0xE1E1E1, 0xE5F1FB, 0xCCE4F7 }, edge[] = { 0xADADAD, 0x0078D7, 0x005499 };
			cv.RoundRect(x, y, w, h, 0, fill[state], fill[state], 255);
			cv.RoundFrame(x, y, w, h, 0, edge[state], 255);
		}
	};

	// Classic Mac OS (Platinum): grey windows, a ridged title bar with the name centred on a plain patch, a close
	// box on the left, zoom and collapse boxes on the right, black-edged rounded buttons.
	struct ClassicMac : Theme
	{
		ClassicMac()
		{
			name = L"Mac OS 8-9";
			page = 0xDDDDDD, pressedInk = 0xFFFFFF, heading = 0x000000, navInk = 0x000000, edge = 0x666666, groupEdge = 0x888888, strip = 0xDDDDDD;
			tabOff[0] = 0xCCCCCC, tabOff[1] = 0xBBBBBB, tabHot[0] = 0xE6E6E6, tabHot[1] = 0xD0D0D0, tabHotEdge = 0x000000;
			status[0] = status[1] = 0xDDDDDD, status[2] = 0x888888;
			nav[0] = nav[1] = 0xEEEEEE, nav[2] = 0x888888;
			navHot[0] = navHot[1] = 0xE0E0F0, navHot[2] = 0xB0B0D0, navSel[0] = navSel[1] = 0xCCCCFF, navSel[2] = 0x666699;
			box[0] = box[1] = 0xFFFFFF, box[2] = 0x000000, boxHot[0] = boxHot[1] = 0xEEEEEE, boxHot[2] = 0x000000;
			mark = 0x000000, dot[0] = dot[1] = 0x000000;
			radius = 4, boxRadius = 0;
			capW[0] = capW[1] = capW[2] = 13, capH = 13, capTop = 8, capGap = 5, capInset = 8;
			capLeft[CaptionClose] = true;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, 0, page, page, 255);
			cv.RoundFrame(1, 1, cv.w - 2, cv.h - 2, 0, 0xFFFFFF, 220);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, 0x000000, 255);
			// the ridges
			for (int y = S(6); y < titleH - S(6); y += 2)
				for (int x = S(5); x < cv.w - S(5); x++)
				{
					cv.Blend(x, y, 0xFFFFFF, 255);
					cv.Blend(x, y + 1, 0x999999, 255);
				}
			auto size = text->Measure(title, true);
			int tx = std::max(S(30), (cv.w - int(size.cx)) / 2);
			cv.RoundRect(tx - S(8), S(4), std::min(int(size.cx), cv.w - S(110)) + S(16), titleH - S(8), 0, page, page, 255);
			text->Draw(cv, tx, (titleH - size.cy) / 2, title, 0x000000, true, cv.w - S(110));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, 0x888888, 255);
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			cv.RoundRect(rc.left - S(3), rc.top - S(2), w + S(6), h + S(4), 0, page, page, 255); // clear of the ridges
			cv.RoundRect(rc.left, rc.top, w, h, 0, hover ? 0x999999 : 0xEEEEEE, hover ? 0x777777 : 0xBBBBBB, 255);
			cv.RoundFrame(rc.left, rc.top, w, h, 0, 0x000000, 255);
			if (kind == CaptionMax) cv.RoundFrame(rc.left, rc.top, w / 2 + 1, h / 2 + 1, 0, 0x000000, 255);
			if (kind == CaptionMin)
				for (int i = 0; i < 2; i++) cv.RoundRect(rc.left, rc.top + h / 2 - S(1) + i * S(2), w, 1, 0, 0x000000, 0x000000, 255);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			static const uint32_t top[] = { 0xEEEEEE, 0xF6F6F6, 0x666666 }, bottom[] = { 0xCCCCCC, 0xD8D8D8, 0x444444 };
			int r = S(4);
			cv.RoundRect(x, y, w, h, r, top[state], bottom[state], 255);
			cv.RoundFrame(x, y, w, h, r, 0x000000, 255);
			if (state != 2) cv.RoundFrame(x + 1, y + 1, w - 2, h - 2, r - 1, 0xFFFFFF, 150);
		}

		void Popup(Canvas &cv) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, 0, page, page, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, 0x000000, 255);
			cv.RoundFrame(1, 1, cv.w - 2, cv.h - 2, 0, 0xFFFFFF, 220);
		}
	};

	// Modern macOS: a light grey window with big rounded corners, the three coloured lights on the left, a centred
	// title, white rounded buttons that turn the accent blue when picked, a segmented control for tabs.
	struct ModernMac : Theme
	{
		ModernMac()
		{
			name = L"macOS 11-15";
			page = 0xFFFFFF, pressedInk = 0xFFFFFF, heading = 0x6E6E73, navInk = 0x000000, edge = 0xD0D0D0, groupEdge = 0xDCDCDC, strip = 0xECECEC;
			segmentTabs = true;
			status[0] = status[1] = 0xECECEC, status[2] = 0xD8D8D8;
			nav[0] = nav[1] = 0xE8E8EC, nav[2] = 0xD8D8D8;
			navHot[0] = navHot[1] = navHot[2] = 0xDCDCE0, navSel[0] = navSel[1] = navSel[2] = 0xCFCFD5;
			box[0] = box[1] = 0xFFFFFF, box[2] = 0xBFBFBF, boxHot[0] = boxHot[1] = 0xFFFFFF, boxHot[2] = 0x9A9A9A;
			boxOn = 0x007AFF, mark = 0xFFFFFF, dot[0] = dot[1] = 0xFFFFFF;
			radius = 10, boxRadius = 3;
			capW[0] = capW[1] = capW[2] = 12, capH = 12, capTop = 8, capGap = 8, capInset = 12;
			capLeft[0] = capLeft[1] = capLeft[2] = true;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			int r = S(10);
			cv.RoundRect(0, 0, cv.w, cv.h, r, 0xEDEDED, 0xE6E6E6, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, r, 0x000000, 60);
			cv.RoundFrame(1, 1, cv.w - 2, cv.h - 2, r - 1, 0xFFFFFF, 110);
			auto size = text->Measure(title, true);
			text->Draw(cv, std::max(S(80), (cv.w - int(size.cx)) / 2), (titleH - size.cy) / 2, title, 0x4D4D4D, true, cv.w - S(160));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, 0xD0D0D0, 255);
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			static const uint32_t fill[] = { 0xFEBC2E, 0x28C840, 0xFF5F57 }, rim[] = { 0xDEA123, 0x1AAB29, 0xE0443E }, ink[] = { 0x985712, 0x0B650D, 0x4D0000 };
			int d = rc.right - rc.left;
			cv.RoundRect(rc.left, rc.top, d, d, d / 2, fill[kind], fill[kind], 255);
			cv.RoundFrame(rc.left, rc.top, d, d, d / 2, rim[kind], 255);
			if (hover) Glyph(cv, rc.left + d / 2, rc.top + d / 2, kind, ink[kind], S(6), std::max(1, S(1.2f)), true);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			static const uint32_t top[] = { 0xFFFFFF, 0xF5F5F5, 0x3B95FF }, bottom[] = { 0xFFFFFF, 0xF0F0F0, 0x0A6CF0 }, edge[] = { 0xC4C4C4, 0xB4B4B4, 0x0A62D8 };
			int r = S(5);
			cv.RoundRect(x, y, w, h, r, top[state], bottom[state], 255);
			cv.RoundFrame(x, y, w, h, r, edge[state], 255);
		}

		void Popup(Canvas &cv) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, S(10), 0xF2F2F2, 0xF2F2F2, 245);
			cv.RoundFrame(0, 0, cv.w, cv.h, S(10), 0x000000, 45);
		}
	};

	void Line(Canvas &cv, int x, int y, int w, int h, uint32_t c)
	{
		cv.RoundRect(x, y, w, h, 0, c, c, 255);
	}

	// The Windows 9x 3D edge: raised (buttons, windows) or sunken (fields, pressed buttons).
	void Bevel(Canvas &cv, int x, int y, int w, int h, bool sunken)
	{
		uint32_t o1 = sunken ? 0x808080 : 0xFFFFFF, o2 = sunken ? 0xFFFFFF : 0x000000, i1 = sunken ? 0x000000 : 0xDFDFDF, i2 = sunken ? 0xDFDFDF : 0x808080;
		Line(cv, x, y, w, 1, o1), Line(cv, x, y, 1, h, o1), Line(cv, x, y + h - 1, w, 1, o2), Line(cv, x + w - 1, y, 1, h, o2);
		Line(cv, x + 1, y + 1, w - 2, 1, i1), Line(cv, x + 1, y + 1, 1, h - 2, i1), Line(cv, x + 1, y + h - 2, w - 2, 1, i2), Line(cv, x + w - 2, y + 1, 1, h - 2, i2);
	}

	// Windows 98: grey 3D chrome, a navy-to-blue title bar, small bevelled caption buttons.
	struct NinetyEight : Theme
	{
		uint32_t titleRight = 0x1084D0; // the title bar fades from navy to this

		NinetyEight()
		{
			name = L"Windows 98";
			page = 0xC0C0C0, heading = 0x000000, navInk = 0x000000, navSelInk = 0xFFFFFF, edge = 0x808080, groupEdge = 0x808080, strip = 0xC0C0C0;
			tabOff[0] = tabOff[1] = tabHot[0] = tabHot[1] = 0xC0C0C0, tabHotEdge = 0x808080;
			status[0] = status[1] = 0xC0C0C0, status[2] = 0x808080;
			nav[0] = nav[1] = 0xFFFFFF, nav[2] = 0x808080;
			navHot[0] = navHot[1] = navHot[2] = 0xFFFFFF, navSel[0] = navSel[1] = navSel[2] = 0x000080;
			box[0] = box[1] = 0xFFFFFF, box[2] = 0x808080, boxHot[0] = boxHot[1] = 0xFFFFFF, boxHot[2] = 0x000000;
			mark = 0x000000, dot[0] = dot[1] = 0x000000;
			radius = 0, boxRadius = 0;
			capW[0] = capW[1] = capW[2] = 16, capH = 14, capTop = 8, capGap = 2, capInset = 8;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, 0, page, page, 255);
			Bevel(cv, 0, 0, cv.w, cv.h, false);
			int bx = S(4), by = S(4), bw = cv.w - 2 * bx, bh = titleH - S(6);
			for (int i = 0; i < bw; i++)
			{
				float t = bw > 1 ? float(i) / (bw - 1) : 0;
				auto ch = [t](int a, int b) { return uint32_t(a + (b - a) * t); };
				Line(cv, bx + i, by, 1, bh, (ch(0x00, int(titleRight >> 16)) << 16) | (ch(0x00, int(titleRight >> 8 & 0xFF)) << 8) | ch(0x80, int(titleRight & 0xFF)));
			}
			text->Draw(cv, S(8), by + (bh - text->Measure(title, true).cy) / 2, title, 0xFFFFFF, true, cv.w - S(90));
			Bevel(cv, border - 2, titleH - 2, cv.w - 2 * border + 4, cv.h - titleH - border + 4, true);
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			Line(cv, rc.left, rc.top, w, h, page);
			Bevel(cv, rc.left, rc.top, w, h, false);
			(void)hover; // 98 had no hover
			Glyph(cv, rc.left + w / 2, rc.top + h / 2 + (kind == CaptionMin ? S(2) : 0), kind, 0x000000, S(7), 2);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			Line(cv, x, y, w, h, page);
			Bevel(cv, x, y, w, h, state == 2);
		}

		void Popup(Canvas &cv) override
		{
			Line(cv, 0, 0, cv.w, cv.h, page);
			Bevel(cv, 0, 0, cv.w, cv.h, false);
		}
	};

	// Windows XP Luna: a rounded blue title bar with bold white text, blue and red rounded caption buttons, white
	// buttons with an orange glow on hover, green ticks.
	struct XP : Theme
	{
		XP()
		{
			name = L"Windows XP";
			page = 0xFCFCFE, heading = 0x0046D5, navInk = 0x215DC6, navSelInk = 0xFFFFFF, edge = 0x919B9C, groupEdge = 0xD0D0BF, strip = 0xECE9D8;
			tabOff[0] = 0xFEFEFE, tabOff[1] = 0xECEBE6, tabHot[0] = 0xFFFFFF, tabHot[1] = 0xF4F3EE, tabHotEdge = 0xE68B2C;
			status[0] = status[1] = 0xECE9D8, status[2] = 0xACA899;
			nav[0] = 0xD6DFF7, nav[1] = 0xC6D3F7, nav[2] = 0x7BA2E7;
			navHot[0] = navHot[1] = 0xE8EEFB, navHot[2] = 0x7BA2E7, navSel[0] = navSel[1] = navSel[2] = 0x316AC5;
			box[0] = 0xDCDCD7, box[1] = 0xFFFFFF, box[2] = 0x1C5180, boxHot[0] = 0xFEDF9C, boxHot[1] = 0xFFFFFF, boxHot[2] = 0x1C5180;
			mark = 0x21A121, dot[0] = 0x55D551, dot[1] = 0x0E8E0A;
			radius = 3, boxRadius = 0;
			capW[0] = capW[1] = capW[2] = 21, capH = 21, capTop = 4, capGap = 2, capInset = 5;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			int r = S(8);
			cv.RoundRect(0, 0, cv.w, cv.h, r, 0x0058EE, 0x0046D5, 255, true);
			cv.RoundRect(1, 1, cv.w - 2, S(6), r, 0x8CBAFF, 0x2D7BF6, 255, true); // the light top edge
			cv.RoundRect(0, titleH - S(4), cv.w, S(4), 0, 0x0A50D8, 0x0040C0, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, r, 0x001EA0, 255, true);
			auto size = text->Measure(title, true);
			int ty = (titleH - size.cy) / 2;
			text->Draw(cv, S(9) + 1, ty + 1, title, 0x0A1883, true, cv.w - S(100));
			text->Draw(cv, S(9), ty, title, 0xFFFFFF, true, cv.w - S(100));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, 0x001EA0, 255);
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			bool close = kind == CaptionClose;
			uint32_t top = close ? (hover ? 0xF5A189 : 0xE5866A) : (hover ? 0x8DBBFF : 0x6A9EFC);
			uint32_t bottom = close ? (hover ? 0xD0482A : 0xC2391C) : (hover ? 0x2A6CF0 : 0x1B57D9);
			cv.RoundRect(rc.left, rc.top, w, h, S(3), top, bottom, 255);
			cv.RoundFrame(rc.left, rc.top, w, h, S(3), 0xFFFFFF, 230);
			int cx = rc.left + w / 2, cy = rc.top + h / 2;
			if (kind == CaptionMax)
			{
				int k = S(4.5f);
				cv.RoundFrame(cx - k, cy - k, 2 * k + 1, 2 * k + 1, 0, 0xFFFFFF, 255);
				Line(cv, cx - k, cy - k, 2 * k + 1, S(2.5f), 0xFFFFFF);
			}
			else Glyph(cv, cx, cy + (kind == CaptionMin ? S(4) : 0), kind, 0xFFFFFF, S(close ? 8.f : 7.f), S(2.5f));
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			int r = S(3);
			cv.RoundRect(x, y, w, h, r, state == 2 ? 0xE6E6E0 : 0xFFFFFF, state == 2 ? 0xE2E2DA : 0xECEBE6, 255);
			cv.RoundFrame(x, y, w, h, r, 0x003C74, 255);
			if (state == 1)
			{
				cv.RoundFrame(x + 1, y + 1, w - 2, h - 2, r - 1, 0xF8B330, 255);
				cv.RoundFrame(x + 2, y + 2, w - 4, h - 4, std::max(0, r - 2), 0xFDD889, 255);
			}
		}

		void Popup(Canvas &cv) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, 0, 0xFFFFE1, 0xFFFFE1, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, 0x000000, 255);
		}
	};

	// TempleOS: white windows with a double blue border, blue text, [X]-style caption buttons in text.
	struct Temple : Theme
	{
		static constexpr uint32_t blue = 0x0000AA, cyan = 0x55FFFF;

		Temple()
		{
			name = L"TempleOS";
			ink = blue, pressedInk = 0xFFFFFF, heading = blue, navInk = blue, navSelInk = 0xFFFFFF, edge = blue, groupEdge = blue, strip = 0xFFFFFF;
			tabOff[0] = tabOff[1] = 0xFFFFFF, tabHot[0] = tabHot[1] = cyan, tabHotEdge = blue;
			status[0] = status[1] = 0xFFFFFF, status[2] = blue;
			nav[0] = nav[1] = 0xFFFFFF, nav[2] = blue;
			navHot[0] = navHot[1] = cyan, navHot[2] = blue, navSel[0] = navSel[1] = navSel[2] = blue;
			box[0] = box[1] = 0xFFFFFF, box[2] = blue, boxHot[0] = boxHot[1] = cyan, boxHot[2] = blue;
			mark = blue, dot[0] = dot[1] = blue;
			radius = 0, boxRadius = 0;
			capW[0] = capW[1] = capW[2] = 24, capH = 20, capTop = 4, capGap = 2, capInset = 10;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, 0, 0xFFFFFF, 0xFFFFFF, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, blue, 255);
			cv.RoundFrame(S(3), S(3), cv.w - 2 * S(3), cv.h - 2 * S(3), 0, blue, 255);
			auto size = text->Measure(title);
			int tx = std::max(S(12), (cv.w - int(size.cx)) / 2);
			Line(cv, tx - S(6), 0, std::min(int(size.cx), cv.w - S(120)) + S(12), S(4), 0xFFFFFF); // the title sits in the border
			text->Draw(cv, tx, (titleH - size.cy) / 2, title, blue, false, cv.w - S(120));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, blue, 255);
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			static const wchar_t *labels[] = { L"[_]", L"[\u25A1]", L"[X]" };
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			if (hover) Line(cv, rc.left, rc.top, w, h, blue);
			auto size = text->Measure(labels[kind]);
			text->Draw(cv, rc.left + (w - size.cx) / 2, rc.top + (h - size.cy) / 2, labels[kind], hover ? 0xFFFFFF : blue);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			uint32_t fill = state == 2 ? blue : state == 1 ? cyan : 0xFFFFFF;
			Line(cv, x, y, w, h, fill);
			cv.RoundFrame(x, y, w, h, 0, blue, 255);
		}

		void Popup(Canvas &cv) override
		{
			Line(cv, 0, 0, cv.w, cv.h, 0xFFFFFF);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, blue, 255);
			cv.RoundFrame(S(3), S(3), cv.w - 2 * S(3), cv.h - 2 * S(3), 0, blue, 255);
		}
	};

	// Windows 95: 98 without the title gradient.
	struct NinetyFive : NinetyEight
	{
		NinetyFive()
		{
			name = L"Windows 95";
			titleRight = 0x000080;
		}
	};

	// Windows 3.1: a grey sizing border, a solid navy title bar with the name centred, the control-menu box on the
	// left, arrow buttons on the right, white dialogs.
	struct Three : Theme
	{
		Three()
		{
			name = L"Windows 3.1";
			heading = 0x000000, navInk = 0x000000, navSelInk = 0xFFFFFF, edge = 0x000000, groupEdge = 0x000000, strip = 0xFFFFFF;
			tabOff[0] = tabOff[1] = 0xC0C0C0, tabHot[0] = tabHot[1] = 0xC0C0C0, tabHotEdge = 0x000000;
			status[0] = status[1] = 0xFFFFFF, status[2] = 0x000000;
			nav[0] = nav[1] = 0xFFFFFF, nav[2] = 0x000000;
			navHot[0] = navHot[1] = navHot[2] = 0xFFFFFF, navSel[0] = navSel[1] = navSel[2] = 0x000080;
			box[0] = box[1] = 0xFFFFFF, box[2] = 0x000000, boxHot[0] = boxHot[1] = 0xFFFFFF, boxHot[2] = 0x000000;
			mark = 0x000000, dot[0] = dot[1] = 0x000000;
			radius = 0, boxRadius = 0;
			capW[0] = capW[1] = capW[2] = 20, capH = 20, capTop = 4, capGap = 0, capInset = 4;
			capLeft[CaptionClose] = true;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			Line(cv, 0, 0, cv.w, cv.h, 0xC0C0C0);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, 0x000000, 255);
			int b = S(4);
			Line(cv, b, b, cv.w - 2 * b, titleH - b, 0x000080);
			cv.RoundFrame(b - 1, b - 1, cv.w - 2 * b + 2, titleH - b + 2, 0, 0x000000, 255);
			auto size = text->Measure(title, true);
			text->Draw(cv, std::max(S(30), (cv.w - int(size.cx)) / 2), b + (titleH - b - size.cy) / 2, title, 0xFFFFFF, true, cv.w - S(110));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, 0x000000, 255);
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top, cx = rc.left + w / 2, cy = rc.top + h / 2;
			(void)hover;
			Line(cv, rc.left, rc.top, w, h, 0xC0C0C0);
			cv.RoundFrame(rc.left, rc.top, w, h, 0, 0x000000, 255);
			if (kind == CaptionClose)
			{
				// the control-menu box: a white bar with a dark shadow
				Line(cv, cx - S(6) + 1, cy - 1 + 1, S(12), S(3), 0x808080);
				Line(cv, cx - S(6), cy - 1, S(12), S(3), 0xFFFFFF);
				cv.RoundFrame(cx - S(6), cy - 1, S(12), S(3), 0, 0x000000, 255);
				return;
			}
			Bevel(cv, rc.left + 1, rc.top + 1, w - 2, h - 2, false);
			int k = S(4);
			for (int i = 0; i <= k; i++) // an arrow: down to minimise, up to maximise
				Line(cv, cx - i, kind == CaptionMin ? cy + k / 2 - i : cy - k / 2 + i, 2 * i + 1, 1, 0x000000);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			Line(cv, x, y, w, h, 0xC0C0C0);
			cv.RoundFrame(x, y, w, h, S(2), 0x000000, 255);
			uint32_t lt = state == 2 ? 0x808080 : 0xFFFFFF, rb = state == 2 ? 0xC0C0C0 : 0x808080;
			Line(cv, x + 1, y + 1, w - 2, S(2), lt), Line(cv, x + 1, y + 1, S(2), h - 2, lt);
			Line(cv, x + 1, y + h - 1 - S(2), w - 2, S(2), rb), Line(cv, x + w - 1 - S(2), y + 1, S(2), h - 2, rb);
		}

		void Popup(Canvas &cv) override
		{
			Line(cv, 0, 0, cv.w, cv.h, 0xFFFFFF);
			cv.RoundFrame(0, 0, cv.w, cv.h, 0, 0x000000, 255);
			cv.RoundFrame(1, 1, cv.w - 2, cv.h - 2, 0, 0x000000, 255);
		}
	};

	// Windows 11: Windows 10's layout with rounded corners, a light Mica grey, rounded controls and the accent blue on
	// picked buttons and ticked boxes.
	struct Eleven : Ten
	{
		Eleven()
		{
			name = L"Windows 11";
			radius = 8, boxRadius = 4, pressedInk = 0xFFFFFF, strip = 0xF3F3F3, edge = 0xE5E5E5;
			tabOff[0] = tabOff[1] = 0xF3F3F3, tabHot[0] = tabHot[1] = 0xEAEAEA, tabHotEdge = 0xE5E5E5;
			status[0] = status[1] = 0xF3F3F3, status[2] = 0xE5E5E5;
			nav[0] = nav[1] = 0xF3F3F3, nav[2] = 0xE5E5E5;
			navHot[0] = navHot[1] = navHot[2] = 0xEAEAEA, navSel[0] = navSel[1] = navSel[2] = 0xE0E0E0;
			box[0] = box[1] = 0xFDFDFD, box[2] = 0x8A8A8A, boxHot[0] = boxHot[1] = 0xF3F3F3, boxHot[2] = 0x8A8A8A;
			boxOn = 0x005FB8, mark = 0xFFFFFF, dot[0] = dot[1] = 0xFFFFFF;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			int r = S(8);
			cv.RoundRect(0, 0, cv.w, cv.h, r, 0xF3F3F3, 0xF3F3F3, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, r, 0x000000, 60);
			text->Draw(cv, S(12), (titleH - text->Measure(title).cy) / 2, title, 0x000000, false, cv.w - S(160));
			(void)border;
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			bool close = kind == CaptionClose;
			if (hover)
			{
				uint32_t c = close ? 0xC42B1C : 0xE9E9E9;
				cv.RoundRect(rc.left, rc.top, w, h, close ? S(8) : 0, c, c, 255, true); // the close button follows the corner
				if (close) Line(cv, rc.left, rc.top, w - S(8), h, c);
			}
			Glyph(cv, rc.left + w / 2, rc.top + h / 2, kind, close && hover ? 0xFFFFFF : 0x000000, S(10), 1);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			int r = S(4);
			uint32_t fill = state == 2 ? 0x005FB8 : state == 1 ? 0xF6F6F6 : 0xFBFBFB;
			cv.RoundRect(x, y, w, h, r, fill, fill, 255);
			cv.RoundFrame(x, y, w, h, r, state == 2 ? 0x005FB8 : 0xE5E5E5, 255);
			if (state != 2) Line(cv, x + r, y + h - 1, w - 2 * r, 1, 0xCCCCCC); // the darker bottom edge
		}

		void Popup(Canvas &cv) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, S(8), 0xF9F9F9, 0xF9F9F9, 250);
			cv.RoundFrame(0, 0, cv.w, cv.h, S(8), 0x000000, 50);
		}
	};

	// System 1-6: black and white, a striped title bar, the close box on the left and the zoom box on the right.
	struct System6 : ClassicMac
	{
		uint32_t stripe = 0x000000, stripeGap = 0xFFFFFF, boxFill = 0xFFFFFF;

		System6()
		{
			name = L"System 1-6";
			page = 0xFFFFFF, pressedInk = 0xFFFFFF, heading = 0x000000, navInk = 0x000000, navSelInk = 0xFFFFFF, edge = 0x000000, groupEdge = 0x000000, strip = 0xFFFFFF;
			tabOff[0] = tabOff[1] = tabHot[0] = tabHot[1] = 0xFFFFFF, tabHotEdge = 0x000000;
			status[0] = status[1] = 0xFFFFFF, status[2] = 0x000000;
			nav[0] = nav[1] = 0xFFFFFF, nav[2] = 0x000000;
			navHot[0] = navHot[1] = 0xFFFFFF, navHot[2] = 0x000000, navSel[0] = navSel[1] = navSel[2] = 0x000000;
			box[0] = box[1] = boxHot[0] = boxHot[1] = 0xFFFFFF, box[2] = boxHot[2] = 0x000000;
			radius = 6, boxRadius = 0;
			capW[0] = capW[1] = capW[2] = 11, capH = 11, capTop = 7, capGap = 4, capInset = 8;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			Line(cv, 0, 0, cv.w, cv.h, page);
			cv.RoundFrame(0, 0, cv.w - 1, cv.h - 1, 0, 0x000000, 255);
			Line(cv, cv.w - 1, 1, 1, cv.h - 1, 0x000000), Line(cv, 1, cv.h - 1, cv.w - 1, 1, 0x000000); // the drop shadow
			Line(cv, 1, 1, cv.w - 3, titleH - 2, stripeGap);
			for (int y = S(5); y < titleH - S(5); y += 2) Line(cv, S(3), y, cv.w - S(3) * 2 - 1, 1, stripe);
			auto size = text->Measure(title, true);
			int tx = std::max(S(30), (cv.w - int(size.cx)) / 2);
			Line(cv, tx - S(8), S(3), std::min(int(size.cx), cv.w - S(110)) + S(16), titleH - S(6), stripeGap);
			text->Draw(cv, tx, (titleH - size.cy) / 2, title, 0x000000, true, cv.w - S(110));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 1, cv.h - titleH - border + 1, 0, 0x000000, 255);
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top;
			Line(cv, rc.left - S(2), rc.top - S(2), w + S(4), h + S(4), stripeGap); // clear of the stripes
			Line(cv, rc.left, rc.top, w, h, hover ? 0x000000 : boxFill);
			cv.RoundFrame(rc.left, rc.top, w, h, 0, 0x000000, 255);
			uint32_t ink = hover ? 0xFFFFFF : 0x000000;
			if (kind == CaptionMax) cv.RoundFrame(rc.left, rc.top, w / 2 + 1, h / 2 + 1, 0, ink, 255);
			if (kind == CaptionMin) Line(cv, rc.left + S(2), rc.top + h / 2, w - S(4), 1, ink);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			int r = S(6);
			cv.RoundRect(x, y, w, h, r, state == 2 ? 0x000000 : boxFill, state == 2 ? 0x000000 : boxFill, 255);
			cv.RoundFrame(x, y, w, h, r, 0x000000, 255);
			if (state == 1) cv.RoundFrame(x + 1, y + 1, w - 2, h - 2, r - 1, 0x000000, 255);
		}

		void Popup(Canvas &cv) override
		{
			Line(cv, 0, 0, cv.w, cv.h, 0xFFFFFF);
			cv.RoundFrame(0, 0, cv.w - 1, cv.h - 1, 0, 0x000000, 255);
			Line(cv, cv.w - 1, 1, 1, cv.h - 1, 0x000000), Line(cv, 1, cv.h - 1, cv.w - 1, 1, 0x000000);
		}
	};

	// System 7: the same shapes, with grey stripes, light grey dialogs and a lavender highlight.
	struct System7 : System6
	{
		System7()
		{
			name = L"System 7";
			stripe = 0x999999, stripeGap = 0xDDDDDD, boxFill = 0xEEEEEE;
			page = 0xEEEEEE, strip = 0xDDDDDD, navSelInk = 0x000000;
			tabOff[0] = tabOff[1] = 0xDDDDDD, tabHot[0] = tabHot[1] = 0xEEEEEE;
			status[0] = status[1] = 0xDDDDDD, nav[0] = nav[1] = 0xFFFFFF;
			navHot[0] = navHot[1] = 0xEEEEFF, navSel[0] = navSel[1] = 0xCCCCFF, navSel[2] = 0x6666AA;
		}
	};

	// A glossy round light (Aqua gel), lighter at the top with a highlight.
	void Gel(Canvas &cv, int x, int y, int w, int h, int r, uint32_t c, uint32_t rim, int shine)
	{
		cv.RoundRect(x, y, w, h, r, Canvas::Mix(c, 0xFFFFFF, 0.45f), c, 255);
		cv.RoundFrame(x, y, w, h, r, rim, 255);
		if (shine) cv.RoundRect(x + w / 5, y + 1, w - 2 * (w / 5), h * 2 / 5, std::min(r, h / 5), 0xFFFFFF, 0xFFFFFF, shine);
	}

	// Mac OS X 10.0-10.6 (Aqua): pinstripes, glossy gel lights and pill buttons, blue gel for the picked one.
	struct Aqua : ModernMac
	{
		Aqua()
		{
			name = L"Mac OS X 10.0-10.6";
			page = 0xF2F2F2, strip = 0xE8E8E8, heading = 0x000000;
			status[0] = status[1] = 0xE8E8E8;
			nav[0] = 0xE8EDF5, nav[1] = 0xD6DDE5, navSel[0] = 0x6AA2E8, navSel[1] = navSel[2] = 0x3B7DD8, navSelInk = 0xFFFFFF;
			boxOn = 0x3D8EF0, radius = 6, boxRadius = 3;
			capW[0] = capW[1] = capW[2] = 14, capH = 14, capTop = 6, capGap = 7, capInset = 10;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			int r = S(6);
			cv.RoundRect(0, 0, cv.w, cv.h, r, 0xECECEC, 0xECECEC, 255, true);
			for (int y = r; y < cv.h - 1; y += 2) Line(cv, 1, y, cv.w - 2, 1, 0xF8F8F8); // the pinstripes
			cv.RoundFrame(0, 0, cv.w, cv.h, r, 0x000000, 110, true);
			auto size = text->Measure(title);
			text->Draw(cv, std::max(S(80), (cv.w - int(size.cx)) / 2), (titleH - size.cy) / 2, title, 0x000000, false, cv.w - S(160));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, 0xA0A0A0, 255);
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			static const uint32_t fill[] = { 0xE8A92B, 0x5DB33B, 0xD9332A }, rim[] = { 0x9A6A10, 0x2F7A1C, 0x8E1A14 };
			int d = rc.right - rc.left;
			Gel(cv, rc.left, rc.top, d, d, d / 2, fill[kind], rim[kind], 200);
			if (hover) Glyph(cv, rc.left + d / 2, rc.top + d / 2 + 1, kind, 0x4A1A00, S(6), std::max(1, S(1.2f)), true);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			bool on = state == 2;
			Gel(cv, x, y, w, h, h / 2, on ? 0x3D8EF0 : state == 1 ? 0xE6E6E6 : 0xDADADA, on ? 0x1A4D9A : 0x8A8A8A, 150);
		}

		void Popup(Canvas &cv) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, S(4), 0xF8F8F8, 0xF8F8F8, 240);
			cv.RoundFrame(0, 0, cv.w, cv.h, S(4), 0x000000, 80);
		}
	};

	// OS X 10.7-10.9: a grey gradient title bar, softer lights, white gradient buttons, blue gradient when picked.
	struct Lion : ModernMac
	{
		Lion()
		{
			name = L"OS X 10.7-10.9";
			page = 0xEDEDED, strip = 0xE3E3E3, heading = 0x333333;
			nav[0] = 0xE6EBF0, nav[1] = 0xDCE2E8, navSel[0] = 0x6CB3FA, navSel[1] = navSel[2] = 0x2A7BE0, navSelInk = 0xFFFFFF;
			box[0] = 0xFFFFFF, box[1] = 0xE8E8E8, box[2] = 0x8E8E8E, boxOn = 0, mark = 0x000000, dot[0] = 0x6CB3FA, dot[1] = 0x0D7BF2;
			radius = 4, boxRadius = 3;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			int r = S(5);
			cv.RoundRect(0, 0, cv.w, cv.h, r, 0xEDEDED, 0xEDEDED, 255, true);
			cv.RoundRect(0, 0, cv.w, titleH, r, 0xEBEBEB, 0xD5D5D5, 255, true);
			cv.RoundFrame(0, 0, cv.w, cv.h, r, 0x000000, 100, true);
			auto size = text->Measure(title);
			text->Draw(cv, std::max(S(80), (cv.w - int(size.cx)) / 2), (titleH - size.cy) / 2, title, 0x4D4D4D, false, cv.w - S(160));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, 0xAAAAAA, 255);
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			static const uint32_t fill[] = { 0xFDBC40, 0x34C84A, 0xFC605C }, rim[] = { 0xDE9F34, 0x27AA35, 0xDE4A47 };
			int d = rc.right - rc.left;
			Gel(cv, rc.left, rc.top, d, d, d / 2, fill[kind], rim[kind], 70);
			if (hover) Glyph(cv, rc.left + d / 2, rc.top + d / 2, kind, 0x5A2A00, S(6), std::max(1, S(1.2f)), true);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			static const uint32_t top[] = { 0xFFFFFF, 0xFFFFFF, 0x6CB3FA }, bottom[] = { 0xF1F1F1, 0xE6E6E6, 0x0D7BF2 }, edge[] = { 0xA6A6A6, 0x8E8E8E, 0x2A68C8 };
			int r = S(4);
			cv.RoundRect(x, y, w, h, r, top[state], bottom[state], 255);
			cv.RoundFrame(x, y, w, h, r, edge[state], 255);
		}
	};

	// OS X 10.10-10.15: flat, a plain light title bar with a regular-weight title, flat lights, flat blue when picked.
	struct Yosemite : ModernMac
	{
		Yosemite()
		{
			name = L"OS X 10.10-10.15";
			radius = 5, boxRadius = 3;
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			int r = S(5);
			cv.RoundRect(0, 0, cv.w, cv.h, r, 0xECECEC, 0xECECEC, 255);
			cv.RoundFrame(0, 0, cv.w, cv.h, r, 0x000000, 70);
			auto size = text->Measure(title);
			text->Draw(cv, std::max(S(80), (cv.w - int(size.cx)) / 2), (titleH - size.cy) / 2, title, 0x4D4D4D, false, cv.w - S(160));
			cv.RoundFrame(border - 1, titleH - 1, cv.w - 2 * border + 2, cv.h - titleH - border + 2, 0, 0xD0D0D0, 255);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			uint32_t fill = state == 2 ? 0x1E8BFF : state == 1 ? 0xF7F7F7 : 0xFFFFFF;
			int r = S(4);
			cv.RoundRect(x, y, w, h, r, fill, fill, 255);
			cv.RoundFrame(x, y, w, h, r, state == 2 ? 0x0A70F0 : 0xC8C8C8, 255);
		}

		void Popup(Canvas &cv) override
		{
			cv.RoundRect(0, 0, cv.w, cv.h, S(5), 0xF2F2F2, 0xF2F2F2, 245);
			cv.RoundFrame(0, 0, cv.w, cv.h, S(5), 0x000000, 45);
		}
	};

	// macOS 26 (Liquid Glass): real glass. The window is kept out of screen captures; every display refresh the
	// screen behind it is grabbed and lightly blurred, and the glass shapes show it bent like a lens towards their
	// rounded edges with a little colour split, a specular rim lit from the top left (and its bounce bottom right).
	// The frame and the floating sidebar are glass; capsule tabs and buttons catch the light. Where each glass pixel
	// samples from is worked out once per paint (Bake), so a refresh is only the grab and a gather.
	struct Glass : ModernMac
	{

		Glass()
		{
			name = L"macOS 26";
			page = 0xFFFFFF, heading = 0x86868B, navInk = 0x1D1D1F, edge = 0xE3E3E8, groupEdge = 0xE8E8ED, stripAlpha = 0;
			navSel[0] = navSel[1] = 0xFFFFFF, navHot[0] = navHot[1] = 0xFFFFFF;
			box[2] = 0xC7C7CC, boxOn = 0x0088FF, radius = 18, boxRadius = 5;
			capW[0] = capW[1] = capW[2] = 14, capH = 14, capTop = 9, capGap = 9, capInset = 18;
		}

		// Apple's "regular" Liquid Glass (HIG, Materials): the backdrop is blurred and its luminosity pulled into a
		// band labels read over, and the whole look turns dark (white labels) over dark content.
		std::atomic<bool> dark{ false };
		bool Live() const override { return true; }
		uint32_t TabInk(bool on) const override { (void)on; return dark ? 0xF5F5F7 : 0x1D1D1F; }
		bool Prepare(Backdrop &bd, HWND hwnd, int w, int h) override
		{
			RECT wr;
			GetWindowRect(hwnd, &wr);
			int m = S(24); // room for the lens to reach past the edge
			int bw = w + 2 * m, bh = h + 2 * m;
			int hw = bw / 2, hh = bh / 2;
			auto &half = bd.small;
			half.resize(size_t(hw) * hh);
			if (!bd.grab.Grab(wr.left - m, wr.top - m, bw, bh, half.data()))
			{
				auto &backdrop = bd.px;
				backdrop.resize(size_t(bw) * bh);
				// no desktop duplication (remote desktop, old driver): GDI, much slower
				HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
				BITMAPINFO bi{};
				bi.bmiHeader = { sizeof(bi.bmiHeader), bw, -bh, 1, 32, BI_RGB };
				void *bits;
				auto dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
				auto old = SelectObject(dc, dib);
				BitBlt(dc, 0, 0, bw, bh, screen, wr.left - m, wr.top - m, SRCCOPY);
				memcpy(backdrop.data(), bits, backdrop.size() * 4);
				SelectObject(dc, old);
				DeleteObject(dib);
				DeleteDC(dc);
				ReleaseDC(nullptr, screen);
				for (int j = 0; j < hh; j++)
					for (int i = 0; i < hw; i++)
					{
						int sum[3] = {};
						for (int y = 0; y < 2; y++)
							for (int x = 0; x < 2; x++)
							{
								uint32_t p = backdrop[size_t(2 * j + y) * bw + 2 * i + x];
								sum[0] += p & 0xFF, sum[1] += p >> 8 & 0xFF, sum[2] += p >> 16 & 0xFF;
							}
						half[size_t(j) * hw + i] = uint32_t(sum[0] / 4) | uint32_t(sum[1] / 4) << 8 | uint32_t(sum[2] / 4) << 16;
					}
			}
			// two box blurs each way (about a gaussian): shapes show through, fine detail like text goes soft
			for (int pass = 0; pass < 2; pass++) Blur(half, bd.scratch, hw, hh, std::max(1, S(2)));
			// light or dark: the mean luma of what's seen through the glass, with some hysteresis
			// ponytail: one look per window; macOS decides per bar
			int64_t sum = 0, n = 0;
			for (uint32_t i : bd.probe)
				if (i < half.size()) sum += Luma(half[i]), n++;
			if (!n) return false;
			int mean = int(sum / n);
			bool was = dark;
			if (mean < 100) dark = true;
			else if (mean > 125) dark = false;
			return dark != was;
		}
		static int Luma(uint32_t c) { return int((c >> 16 & 0xFF) * 54 + (c >> 8 & 0xFF) * 183 + (c & 0xFF) * 19) >> 8; }

		// A box blur of radius r, w by h, both ways; tmp is scratch. Running sums, rows only (cache friendly).
		static void Blur(std::vector<uint32_t> &px, std::vector<uint32_t> &tmp, int w, int h, int r)
		{
			tmp.resize(px.size());
			uint32_t inv = 65536 / (2 * r + 1);
			auto pack = [inv](const int *s) { return (uint32_t(s[0]) * inv >> 16) | (uint32_t(s[1]) * inv >> 16) << 8 | (uint32_t(s[2]) * inv >> 16) << 16; };
			auto add = [](int *s, uint32_t c, int k) { s[0] += k * int(c & 0xFF), s[1] += k * int(c >> 8 & 0xFF), s[2] += k * int(c >> 16 & 0xFF); };
			for (int y = 0; y < h; y++) // across: px to tmp
			{
				const uint32_t *row = &px[size_t(y) * w];
				uint32_t *out = &tmp[size_t(y) * w];
				int s[3] = {};
				for (int i = -r; i <= r; i++) add(s, row[std::clamp(i, 0, w - 1)], 1);
				for (int x = 0; x < w; x++)
				{
					out[x] = pack(s);
					add(s, row[std::min(x + r + 1, w - 1)], 1), add(s, row[std::max(x - r, 0)], -1);
				}
			}
			std::vector<int> s(size_t(w) * 3); // down: tmp to px, a running sum per column
			for (int i = -r; i <= r; i++)
				for (int x = 0; x < w; x++) add(&s[3 * x], tmp[size_t(std::clamp(i, 0, h - 1)) * w + x], 1);
			for (int y = 0; y < h; y++)
			{
				const uint32_t *in = &tmp[size_t(std::min(y + r + 1, h - 1)) * w], *outgoing = &tmp[size_t(std::max(y - r, 0)) * w];
				uint32_t *out = &px[size_t(y) * w];
				for (int x = 0; x < w; x++)
				{
					out[x] = pack(&s[3 * x]);
					add(&s[3 * x], in[x], 1), add(&s[3 * x], outgoing[x], -1);
				}
			}
		}

		// Calls f(x, y, cover, nx, ny, depth) for each canvas pixel inside the shape: coverage, the outward normal
		// and the distance in from the rounded edge.
		template<class F> static void Walk(const Shape &s, int cw, int ch, F f)
		{
			float hw = s.w / 2.f, hh = s.h / 2.f;
			for (int j = std::max(0, -s.y); j < s.h && s.y + j < ch; j++)
				for (int i = std::max(0, -s.x); i < s.w && s.x + i < cw; i++)
				{
					float px = i + 0.5f - hw, py = j + 0.5f - hh;
					float qx = std::abs(px) - (hw - s.r), qy = std::abs(py) - (hh - s.r);
					float ox = std::max(qx, 0.f), oy = std::max(qy, 0.f), ol = std::hypot(ox, oy);
					float dist = ol + std::min(std::max(qx, qy), 0.f) - s.r;
					float cover = std::clamp(0.5f - dist, 0.f, 1.f);
					if (cover <= 0) continue;
					float nx, ny;
					if (ol > 0) nx = ox / ol, ny = oy / ol;
					else if (qx > qy) nx = 1, ny = 0;
					else nx = 0, ny = 1;
					f(s.x + i, s.y + j, cover, px < 0 ? -nx : nx, py < 0 ? -ny : ny, -dist);
				}
		}

		// A glass rounded rectangle: frost (0-1) mixes in the tint, lens is how far the edge bends the view. While
		// painting it only cuts a hole in the UI; Underlay fills it every refresh.
		void GlassRect(Canvas &cv, int x, int y, int w, int h, int r, float frost, uint32_t tint, float lens)
		{
			shapes.push_back({ x, y, w, h, r, frost, tint, lens });
			Walk(shapes.back(), cv.w, cv.h, [&](int px, int py, float cover, float, float, float) {
				auto &p = cv.px[size_t(py) * cv.w + px];
				uint32_t keep = uint32_t(256 * (1 - cover));
				p = ((p & 0xFF00FF) * keep >> 8 & 0xFF00FF) | ((p >> 8 & 0xFF00FF) * keep & 0xFF00FF00);
			});
		}

		void Bake(Backdrop &bd, const Canvas &ui, std::vector<Baked> &out) override
		{
			int m = S(24), hw = (ui.w + 2 * m) / 2, hh = (ui.h + 2 * m) / 2;
			float band = float(S(26));
			auto at = [&](float x, float y) { // a pixel of the half-size backdrop
				return uint32_t(std::clamp(int(y + m) / 2, 0, hh - 1) * hw + std::clamp(int(x + m) / 2, 0, hw - 1));
			};
			for (auto &s : shapes)
				Walk(s, ui.w, ui.h, [&](int x, int y, float cover, float nx, float ny, float depth) {
					uint32_t idx = uint32_t(y) * ui.w + x;
					if (ui.px[idx] >> 24 == 255) return; // under solid UI: nothing to see
					float k = depth < band ? 1 - depth / band : 0;
					float bend = s.lens * (1 - std::sqrt(std::max(0.f, 1 - k * k))); // a dome: flat inside, steep at the rim
					// specular: a thin bright rim where the light (top left) or its bounce (bottom right) hits, a soft
					// glow just inside it, and a faint darker band where the lens turns
					float rim = std::clamp(1 - depth / S(1.8f), 0.f, 1.f), light = std::abs(-0.6f * nx - 0.8f * ny);
					float glow = std::clamp(1 - depth / S(7), 0.f, 1.f);
					float dark = 0.10f * k * (1 - k) * 4 * (1 - light), white = std::min(1.f, rim * (0.35f + 0.65f * light) + 0.3f * glow * glow * light);
					out.push_back({ idx,
						{ at(x - nx * bend * 1.18f, y - ny * bend * 1.18f), at(x - nx * bend, y - ny * bend), at(x - nx * bend * 0.82f, y - ny * bend * 0.82f) }, // red bends most, blue least
						s.tint, uint8_t(255 * cover), uint8_t(255 * s.frost), uint8_t(255 * dark), uint8_t(255 * white) });
				});
			bd.probe.clear();
			for (size_t i = 0; i < out.size(); i += 7) bd.probe.push_back(out[i].src[1]);
		}

		void Underlay(Backdrop &bd, Canvas &cv, const Canvas &ui, const std::vector<Baked> &baked) override
		{
			auto &half = bd.small, &scratch = bd.scratch;
			cv.w = ui.w, cv.h = ui.h, cv.px = ui.px;
			scratch.resize(cv.px.size());
			auto mix = [](uint32_t a, uint32_t b, uint32_t t) { // t of 255 toward b
				uint32_t o = 0;
				for (int sh = 0; sh < 24; sh += 8) o |= ((a >> sh & 0xFF) * (255 - t) + (b >> sh & 0xFF) * t) / 255 << sh;
				return o;
			};
			bool isDark = dark;
			for (auto &b : baked) scratch[b.idx] = 0;
			for (auto &b : baked) // the glass, shapes in paint order (premultiplied)
			{
				uint32_t c = (half[b.src[0]] & 0xFF0000) | (half[b.src[1]] & 0xFF00) | (half[b.src[2]] & 0xFF);
				// luminosity into a band: 43-100% light, 8-73% dark; hue and saturation kept
				int y = Luma(c), d = (isDark ? 20 : 110) + y * 165 / 255 - y;
				uint32_t lum = 0;
				for (int sh = 0; sh < 24; sh += 8) lum |= uint32_t(std::clamp(int(c >> sh & 0xFF) + d, 0, 255)) << sh;
				c = mix(mix(mix(lum, isDark ? 0x2C2C2E : b.tint, b.frost), 0, b.dark), 0xFFFFFF, b.white);
				uint32_t &g = scratch[b.idx], keep = 255 - b.cover, o = 0;
				for (int sh = 0; sh < 24; sh += 8) o |= ((c >> sh & 0xFF) * b.cover + (g >> sh & 0xFF) * keep) / 255 << sh;
				g = o | (b.cover + (g >> 24) * keep / 255) << 24;
			}
			for (auto &b : baked) // the UI over it
			{
				uint32_t u = ui.px[b.idx], g = scratch[b.idx], keep = 255 - (u >> 24), o = 0;
				for (int sh = 0; sh < 32; sh += 8) o |= ((u >> sh & 0xFF) + (g >> sh & 0xFF) * keep / 255) << sh;
				cv.px[b.idx] = o;
			}
		}

		// A capsule or panel over the page: a milky fill, a white rim brightest at the top, a faint dark outline.
		static void Pane(Canvas &cv, int x, int y, int w, int h, int r, uint32_t top, uint32_t bottom, int a)
		{
			cv.RoundRect(x, y, w, h, r, top, bottom, a);
			cv.RoundFrame(x, y, w, h, r, 0x000000, 28);
			cv.RoundFrame(x + 1, y + 1, w - 2, h - 2, std::max(0, r - 1), 0xFFFFFF, 200);
			cv.RoundRect(x + r, y + 1, w - 2 * r, 1, 0, 0xFFFFFF, 0xFFFFFF, 255); // the top glint
		}

		void Frame(Canvas &cv, int titleH, int border, const std::wstring &title) override
		{
			navInk = TabInk(false);
			GlassRect(cv, 0, 0, cv.w, cv.h, S(18), 0.04f, 0xFAFAFC, float(S(24)));
			cv.RoundFrame(0, 0, cv.w, cv.h, S(18), 0x000000, 40);
			auto size = text->Measure(title, true);
			text->Draw(cv, std::max(S(100), (cv.w - int(size.cx)) / 2), (titleH - size.cy) / 2, title, TabInk(false), true, cv.w - S(200));
			(void)border;
		}

		void CaptionButton(Canvas &cv, RECT rc, bool hover, CaptionKind kind) override
		{
			static const uint32_t fill[] = { 0xFFBD2E, 0x28C941, 0xFF5F57 }, ink[] = { 0x985712, 0x0B650D, 0x4D0000 };
			int d = rc.right - rc.left;
			cv.RoundRect(rc.left, rc.top, d, d, d / 2, Canvas::Mix(fill[kind], 0xFFFFFF, 0.15f), fill[kind], 255);
			cv.RoundFrame(rc.left, rc.top, d, d, d / 2, 0x000000, 35);
			if (hover) Glyph(cv, rc.left + d / 2, rc.top + d / 2, kind, ink[kind], S(6), std::max(1, S(1.3f)), true);
		}

		void Button(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			if (state == 2) Pane(cv, x, y, w, h, h / 2, 0x3DA2FF, 0x0077F0, 255);
			else Pane(cv, x, y, w, h, h / 2, state == 1 ? 0xFFFFFF : 0xFBFBFD, state == 1 ? 0xF0F0F5 : 0xF2F2F6, 235);
		}

		void Tab(Canvas &cv, RECT rc, bool on, bool hot) override
		{
			int w = rc.right - rc.left, h = rc.bottom - rc.top - S(4);
			if (on && dark) Pane(cv, rc.left, rc.top + S(1), w, h, h / 2, 0x6C6C70, 0x5A5A5E, 230);
			else if (on) Pane(cv, rc.left, rc.top + S(1), w, h, h / 2, 0xFFFFFF, 0xF4F4F8, 230);
			else if (hot) cv.RoundRect(rc.left, rc.top + S(1), w, h, h / 2, 0xFFFFFF, 0xFFFFFF, dark ? 40 : 110);
		}

		void NavPane(Canvas &cv, int x, int y, int w, int h) override
		{
			cv.RoundRect(x, y, w + 1, h, 0, page, page, 255);
			GlassRect(cv, x + S(6), y + S(6), w - S(10), h - S(12), S(14), 0.3f, 0xF4F4F7, float(S(20)));
		}

		void NavItem(Canvas &cv, int x, int y, int w, int h, int state) override
		{
			if (state == 0) return;
			cv.RoundRect(x + S(4), y, w - S(8), h, S(8), 0xFFFFFF, 0xFFFFFF, dark ? (state == 2 ? 70 : 35) : (state == 2 ? 200 : 110));
		}

		void Popup(Canvas &cv) override
		{
			GlassRect(cv, 0, 0, cv.w, cv.h, S(16), 0.3f, 0xFAFAFC, float(S(20)));
			cv.RoundFrame(0, 0, cv.w, cv.h, S(16), 0x000000, 40);
		}
	};

	Three threeTheme;
	NinetyFive ninetyFiveTheme;
	NinetyEight ninetyEightTheme;
	XP xpTheme;
	Vista vistaTheme;
	Seven sevenTheme;
	Eight eightTheme;
	Ten tenTheme;
	Eleven elevenTheme;
	ClassicMac classicMacTheme;
	ModernMac modernMacTheme;
	Temple templeTheme;
	System6 system6Theme;
	System7 system7Theme;
	Aqua aquaTheme;
	Lion lionTheme;
	Yosemite yosemiteTheme;
	Glass glassTheme;
	Theme *const themes[] = { &threeTheme, &ninetyFiveTheme, &ninetyEightTheme, &xpTheme, &vistaTheme, &sevenTheme, &eightTheme, &tenTheme, &elevenTheme, &classicMacTheme, &modernMacTheme, &templeTheme,
		&system6Theme, &system7Theme, &aquaTheme, &lionTheme, &yosemiteTheme, &glassTheme }; // new ones at the end: the pref is the index
	const std::vector<int> windowsThemes = { 0, 1, 2, 3, 4, 5, 6, 7, 8 }, macThemes = { 12, 13, 9, 14, 15, 16, 10, 17 };
	constexpr int templeIndex = 11;
	constexpr int defaultTheme = 4; // Vista
	Theme *theme = &vistaTheme;

	// A top-level layered window that repaints its Canvas whenever Invalidate() is called.
	struct Panel
	{
		HWND hwnd = nullptr;
		Canvas cv;
		ByteString name;
		std::wstring title;
		std::atomic<bool> dirty{ true }; // the vsync thread sets it too
		bool excluded = false; // a live (glass) theme: kept out of screen captures, recomposed every display refresh
		// Live: the last paint (with holes where the glass goes) and its baked glass, what's behind, and the composed
		// frame. A thread recomposes and shows it every display refresh (DwmFlush), whatever TPT's own loop is doing;
		// liveLock keeps a repaint from swapping ui and baked under it.
		Canvas ui, out;
		std::vector<Theme::Baked> baked;
		Theme::Backdrop backdrop;
		std::mutex liveLock;
		std::thread vsync;
		std::atomic<bool> vsyncRun{ false };

		void StartVsync()
		{
			vsyncRun = true;
			vsync = std::thread([this] {
				// the vertical blank of the screen the window is on; DwmFlush if there's none
				// ponytail: picked once, so a window dragged to a screen with another refresh rate keeps the first's
				IDXGIOutput *screen = nullptr;
				RECT wr;
				GetWindowRect(hwnd, &wr);
				POINT c{ (wr.left + wr.right) / 2, (wr.top + wr.bottom) / 2 };
				IDXGIFactory1 *factory;
				if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&factory)))
				{
					IDXGIAdapter1 *adapter;
					for (UINT i = 0; !screen && factory->EnumAdapters1(i, &adapter) == S_OK; i++)
					{
						IDXGIOutput *output;
						for (UINT j = 0; !screen && adapter->EnumOutputs(j, &output) == S_OK; j++)
						{
							DXGI_OUTPUT_DESC d;
							output->GetDesc(&d);
							if (PtInRect(&d.DesktopCoordinates, c)) screen = output;
							else output->Release();
						}
						adapter->Release();
					}
					factory->Release();
				}
				while (vsyncRun)
				{
					auto t0 = std::chrono::steady_clock::now();
					if (!screen || FAILED(screen->WaitForVBlank())) DwmFlush();
					if (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(1)) Sleep(4); // no vblank to wait for
					Compose();
				}
				if (screen) screen->Release();
			});
		}

		void Compose()
		{
			std::lock_guard lock(liveLock);
			if (!theme->Live() || ui.px.empty()) return; // switching away: the next Present stops this
			if (theme->Prepare(backdrop, hwnd, ui.w, ui.h)) dirty = true;
			theme->Underlay(backdrop, out, ui, baked);
			Show(out);
		}

		void StopVsync()
		{
			vsyncRun = false;
			if (vsync.joinable()) vsync.join();
		}
		int opacity = 255;
		// An app window: taskbar button, normal z-order, resizable, minimise/maximise. Otherwise a small on-top popup
		// (notifications).
		bool appWindow = false;
		int grip = 0; // resize border width
		Vec2<int> minSize{ 0, 0 };
		int drag = 0; // the frame part (HTCAPTION, HTLEFT...) being dragged, 0 = none
		POINT dragFrom{};
		RECT dragRect{};
		HWND owner = nullptr; // an owned window: no taskbar button, normal z-order, minimises with its owner

		virtual ~Panel()
		{
			StopVsync();
			if (hwnd)
			{
				SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
				DestroyWindow(hwnd);
			}
		}
		virtual void Paint() = 0;
		virtual LRESULT HitTest(int, int) { return HTCLIENT; }
		virtual void Mouse(UINT, int, int, WPARAM) {}
		// WM_KEYDOWN, WM_CHAR, WM_KILLFOCUS; true = handled. Only while a panel has taken the keyboard (see
		// SideWindow::search).
		virtual bool Key(UINT, WPARAM) { return false; }
		virtual void Drop(const std::wstring &path) { (void)path; } // a file dropped on an app window

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
			if (appWindow) DragAcceptFiles(hwnd, TRUE);
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
			if (theme->Live() != excluded)
			{
				excluded = theme->Live();
				// WDA_EXCLUDEFROMCAPTURE (Windows 10 2004+) or WDA_NONE; looked up because the SDK target predates it
				static auto setAffinity = reinterpret_cast<BOOL(WINAPI *)(HWND, DWORD)>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowDisplayAffinity"));
				if (setAffinity) setAffinity(hwnd, excluded ? 0x11 : 0);
				if (!excluded) StopVsync(); // started below, once there's a paint to show
				dirty = true;
			}
			if (dirty)
			{
				theme->shapes.clear();
				std::fill(cv.px.begin(), cv.px.end(), 0);
				Paint();
				dirty = false;
				if (excluded)
				{
					std::lock_guard lock(liveLock);
					ui = cv;
					baked.clear();
					theme->Bake(backdrop, ui, baked);
				}
			}
			if (excluded)
			{
				if (!vsync.joinable()) StartVsync();
				return; // the vsync thread shows it
			}
			Show(cv);
		}

		void Show(const Canvas &cv)
		{
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
				case WM_NCLBUTTONDOWN:
					// Moved and sized by hand: Windows' own move/size loop runs on TPT's thread and holds the sim still
					// until the button comes up. ponytail: no drag-to-top maximise or Snap layouts that way
					if ((wp == HTCAPTION || (wp >= HTLEFT && wp <= HTBOTTOMRIGHT)) && !IsZoomed(hwnd))
					{
						self->drag = int(wp);
						GetCursorPos(&self->dragFrom);
						GetWindowRect(hwnd, &self->dragRect);
						SetCapture(hwnd);
						return 0;
					}
					break;
				case WM_CAPTURECHANGED:
					if (self->drag)
					{
						self->drag = 0;
						self->SavePosition();
					}
					break;
				case WM_MOVING:
					if (self->appWindow)
					{
						// Dock: within a few pixels of a screen edge, the window sticks to it.
						auto *r = (RECT *)lp;
						MONITORINFO mi{ sizeof(mi) };
						GetMonitorInfoW(MonitorFromRect(r, MONITOR_DEFAULTTONEAREST), &mi);
						auto wk = mi.rcWork;
						int snap = S(16), dx = 0, dy = 0;
						if (std::abs(r->left - wk.left) < snap) dx = wk.left - r->left;
						else if (std::abs(r->right - wk.right) < snap) dx = wk.right - r->right;
						if (std::abs(r->top - wk.top) < snap) dy = wk.top - r->top;
						else if (std::abs(r->bottom - wk.bottom) < snap) dy = wk.bottom - r->bottom;
						OffsetRect(r, dx, dy);
						return TRUE;
					}
					break;
				case WM_MOUSEMOVE:
				{
					if (int d = self->drag)
					{
						POINT p;
						GetCursorPos(&p);
						int dx = p.x - self->dragFrom.x, dy = p.y - self->dragFrom.y;
						RECT r = self->dragRect;
						auto min = self->minSize;
						if (d == HTCAPTION) OffsetRect(&r, dx, dy);
						if (d == HTLEFT || d == HTTOPLEFT || d == HTBOTTOMLEFT) r.left = std::min(r.left + dx, r.right - min.X);
						if (d == HTRIGHT || d == HTTOPRIGHT || d == HTBOTTOMRIGHT) r.right = std::max(r.right + dx, r.left + min.X);
						if (d == HTTOP || d == HTTOPLEFT || d == HTTOPRIGHT) r.top = std::min(r.top + dy, r.bottom - min.Y);
						if (d == HTBOTTOM || d == HTBOTTOMLEFT || d == HTBOTTOMRIGHT) r.bottom = std::max(r.bottom + dy, r.top + min.Y);
						if (d == HTCAPTION) SendMessageW(hwnd, WM_MOVING, 0, LPARAM(&r)); // the screen-edge dock
						SetWindowPos(hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
						return 0;
					}
					TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
					TrackMouseEvent(&tme);
					self->Mouse(msg, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), wp);
					return 0;
				}
				case WM_KEYDOWN:
				case WM_CHAR:
				case WM_KILLFOCUS:
					if (self->Key(msg, wp)) return 0;
					break;
				case WM_DROPFILES:
				{
					auto drop = HDROP(wp);
					wchar_t path[MAX_PATH];
					if (DragQueryFileW(drop, 0, path, MAX_PATH)) self->Drop(path); // ponytail: the first file only
					DragFinish(drop);
					return 0;
				}
				case WM_LBUTTONUP:
					if (self->drag)
					{
						ReleaseCapture(); // ends the drag (WM_CAPTURECHANGED)
						return 0;
					}
					[[fallthrough]];
				case WM_MOUSELEAVE:
				case WM_LBUTTONDOWN:
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
	void OpenTorn(int tab);
	void ThemeChanged(); // repaint every window
	void SetTheme(int i);
	void Restart(int scale, bool windowed, bool force = false);
	void LoadSceneTiles();

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
		int fixedTab = -1;    // a tab torn off into its own window: shows only that one
		bool closing = false; // closed: Tick drops it (not from inside its own click)
		int titleH, border, tabH, navW, statusH, tileW, tileH, gap;
		uint64_t shownState = 0;
		// Saves tab: the saves and stamps on this computer, thumbnails rendered one per tick.
		struct Local
		{
			ByteString path, stamp; // stamp: its ID, for a stamp
			std::wstring name;
			std::unique_ptr<VideoBuffer> thumb;
			bool tried = false;
		};
		std::vector<Local> locals;
		// powdertoy.co.uk's saves, a page at a time (the Saves tab's Online saves)
		struct Online
		{
			int id, date, up, down;
			std::wstring name, author;
			std::unique_ptr<http::ThumbnailRequest> req;
			std::unique_ptr<VideoBuffer> thumb;
			bool tried = false;
		};
		std::vector<Online> online;
		std::unique_ptr<http::SearchSavesRequest> searching;
		std::wstring onlineQuery, onlineStatus = L"Loading\u2026";
		int onlineSort = 0, onlinePeriod = 0, onlinePage = 0, onlinePer = 12, onlineTotal = 0; // sort: popular, new, favourites, mine
		bool onlineSearch = true; // search again at the next tick
		DWORD localsAt = 0; // when the folders were last listed
		static constexpr int savesTab = 4;
		std::wstring query; // Elements tab search: tools from every section whose name or description has it
		bool typing = false; // a search box has the keyboard
		std::wstring *typed = &query; // which one: the elements' or the online saves'
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
			if (!query.empty())
			{
				auto lower = [](std::wstring s) {
					for (auto &c : s) c = wchar_t(std::towlower(c));
					return s;
				};
				auto q = lower(query);
				std::vector<Tool *> rest; // names starting with the query first, then any other match
				for (auto *menu : sections)
					for (auto *tool : menu->GetToolList())
					{
						auto name = lower(Label(tool));
						if (!tool->MenuVisible) continue;
						if (name.rfind(q, 0) == 0) out.push_back(tool);
						else if (name.find(q) != name.npos || lower(Wide(tool->Description)).find(q) != std::wstring::npos) rest.push_back(tool);
					}
				out.insert(out.end(), rest.begin(), rest.end());
				return out;
			}
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

		RECT CloseRect() { return theme->CaptionRect(cv.w, border, CaptionClose); }
		RECT MaxRect() { return theme->CaptionRect(cv.w, border, CaptionMax); }
		RECT MinRect() { return theme->CaptionRect(cv.w, border, CaptionMin); }
		// Below the tabs, above the status bar.
		RECT Content()
		{
			return { border, titleH + tabH, cv.w - border, cv.h - border - statusH };
		}
		RECT SearchRect()
		{
			auto c = Content();
			return { c.left + navW + gap, c.top + gap, c.right - gap, c.top + gap + S(23) };
		}
		RECT ToolArea()
		{
			auto c = Content();
			return { c.left + navW + gap, SearchRect().bottom + gap, c.right - gap, c.bottom };
		}

		// The search box takes the keyboard from TPT until Enter, Esc or a click elsewhere.
		void StartTyping(std::wstring *field = nullptr)
		{
			typed = field ? field : &query;
			typing = true;
			SetForegroundWindow(hwnd);
			SetFocus(hwnd);
		}
		void StopTyping()
		{
			typing = false;
			dirty = true;
			if (mainWindow && GetForegroundWindow() == hwnd) SetForegroundWindow(mainWindow);
		}
		bool Key(UINT msg, WPARAM wp) override
		{
			if (!typing) return false;
			if (msg == WM_KILLFOCUS) typing = false;
			else if (msg == WM_KEYDOWN && wp == VK_ESCAPE) typed->clear(), StopTyping();
			else if (msg == WM_KEYDOWN && wp == VK_RETURN && typed == &onlineQuery)
			{
				onlinePage = 0;
				onlineSearch = true;
				StopTyping();
			}
			else if (msg == WM_KEYDOWN && wp == VK_RETURN)
			{
				auto tools = Tools();
				if (!tools.empty()) GameController::Ref().SetActiveTool(0, tools[0]);
				StopTyping();
			}
			else if (msg == WM_CHAR && wp == VK_BACK && !typed->empty()) typed->pop_back();
			else if (msg == WM_CHAR && wp >= 32) *typed += wchar_t(wp);
			else return msg == WM_CHAR;
			if (typed == &query) scroll = 0;
			dirty = true;
			return true;
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

		// --- controls in the theme's look; each draws itself and registers its hit rectangle.

		void PushButton(int x, int y, int w, const std::wstring &label, std::function<void()> action, const std::wstring &tip = L"", bool pressed = false)
		{
			RECT rc{ x, y, x + w, y + S(23) };
			theme->Button(cv, x, y, w, S(23), pressed ? 2 : (Hovered(rc) ? 1 : 0));
			auto size = text->Measure(label);
			text->Draw(cv, x + std::max(S(3), int(w - size.cx) / 2), y + (S(23) - size.cy) / 2, label, pressed ? theme->pressedInk : theme->ink, false, w - S(6));
			Add(rc, std::move(action), tip);
		}

		// A check box (round = radio button) with its label; returns the width used.
		int Toggle(int x, int y, const std::wstring &label, bool on, bool round, std::function<void()> action, const std::wstring &tip = L"")
		{
			int b = S(13), w = b + S(6) + text->Measure(label).cx;
			RECT rc{ x, y, x + w, y + S(20) };
			bool hot = Hovered(rc);
			int by = y + (S(20) - b) / 2, r = round ? b / 2 : S(float(theme->boxRadius));
			auto *c = hot ? theme->boxHot : theme->box;
			bool filled = on && theme->boxOn;
			cv.RoundRect(x, by, b, b, r, filled ? theme->boxOn : c[0], filled ? theme->boxOn : c[1], 255);
			cv.RoundFrame(x, by, b, b, r, filled ? theme->boxOn : c[2], 255);
			if (on && round)
			{
				int d = b / 2;
				cv.RoundRect(x + (b - d) / 2, by + (b - d) / 2, d, d, d / 2, theme->dot[0], theme->dot[1], 255);
			}
			if (on && !round)
			{
				int cx = x + b / 2, cy = by + b / 2, k = S(2.5f);
				for (int t = 0; t < S(1.6f); t++)
				{
					for (int i = 0; i <= k; i++) cv.Blend(cx - k - 1 + i, cy + i - 1 + t, theme->mark, 255);
					for (int i = 0; i <= 2 * k; i++) cv.Blend(cx - 1 + i, cy + k - 1 - i + t, theme->mark, 255);
				}
			}
			text->Draw(cv, x + b + S(6), y + (S(20) - text->Measure(label).cy) / 2, label, theme->ink);
			Add(rc, std::move(action), tip);
			return w;
		}

		// A group box: an outline with a heading; returns the top of its inside.
		int Group(int x, int y, int w, int h, const std::wstring &title)
		{
			cv.RoundFrame(x, y + S(8), w, h - S(8), S(float(std::min(theme->radius, 6))), theme->groupEdge, 255);
			auto size = text->Measure(title);
			cv.RoundRect(x + S(8), y, size.cx + S(8), size.cy, 0, theme->page, theme->page, 255);
			text->Draw(cv, x + S(12), y, title, theme->heading);
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
			theme->NavPane(cv, c.left, c.top, navW, c.bottom - c.top);
			auto sections = Sections();
			for (int i = 0; i < int(sections.size()); i++)
			{
				auto rc = NavRect(i);
				bool on = i == section && query.empty(); // a search spans every section
				theme->NavItem(cv, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, on ? 2 : (Hovered(rc) ? 1 : 0));
				text->Draw(cv, rc.left + S(6), rc.top + S(3), Wide(sections[i]->GetDescription()), on && theme->navSelInk != 0xFFFFFFFF ? theme->navSelInk : theme->navInk, on, rc.right - rc.left - S(10));
				Add(rc, [this, i] { section = i, scroll = 0, query.clear(); });
			}
			auto sr = SearchRect();
			int sw = sr.right - sr.left, sh = sr.bottom - sr.top;
			cv.RoundRect(sr.left, sr.top, sw, sh, S(3), 0xFFFFFF, 0xFFFFFF, 255);
			bool here = typing && typed == &query;
			cv.RoundFrame(sr.left, sr.top, sw, sh, S(3), here ? 0x3C7FB1 : (Hovered(sr) ? 0x7EB4EA : 0xABADB3), 255);
			auto shown = query.empty() && !here ? std::wstring(L"Search elements") : query;
			auto qs = text->Measure(shown.empty() ? std::wstring(L" ") : shown);
			int ty = sr.top + (sh - qs.cy) / 2;
			text->Draw(cv, sr.left + S(6), ty, shown, query.empty() ? 0x6D6D6D : 0x000000, false, sw - S(12));
			if (here) Line(cv, sr.left + S(6) + (query.empty() ? 0 : std::min(int(qs.cx), sw - S(12))), ty, std::max(1, S(1)), qs.cy, 0x000000); // the caret
			Add(sr, [this] { StartTyping(); }, L"Type to find an element by name or description; Enter picks the first, Esc clears");
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
				theme->Button(tile, 0, 0, tileW, tileH, selected ? 2 : (Hovered(rc) ? 1 : 0));
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
				text->Draw(tile, S(9) + sw, (tileH - text->Measure(Label(tool)).cy) / 2, Label(tool), selected ? theme->pressedInk : theme->ink, false, tileW - sw - S(12));
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
			int x = c.left + S(10), w = c.right - c.left - S(20), y = c.top + S(8) - scroll;

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

			top = Group(x, y, w, S(100), L"Edges");
			Toggle(x + S(12), top + S(24), L"Open windows are walls (sand piles on title bars)", windowWalls, false, [] {
				windowWalls = !windowWalls;
				GlobalPrefs::Ref().Set("Overlay.WindowWalls", windowWalls);
			}, L"Only while the sim is on top of every window");
			Toggle(x + S(12), top + S(48), L"Desktop icons are walls (sand piles on them, in their shapes)", iconWalls, false, [] {
				iconWalls = !iconWalls;
				GlobalPrefs::Ref().Set("Overlay.IconWalls", iconWalls);
			}, L"Sand piles on your icons, shaped like each icon (not when the sim is behind them)");
			const wchar_t *edges[] = { L"Void", L"Solid", L"Loop" };
			const wchar_t *edgeTips[] = { L"Particles and air leave at the edges", L"The edges are walls", L"Particles wrap around (whole screen only for now)" };
			int ex = x + S(12);
			for (int i = 0; i < 3; i++)
			{
				ex += Toggle(ex, top, edges[i], gc.GetEdgeMode() == i, true, [i] { GameController::Ref().SetEdgeMode(i); }, edgeTips[i]) + S(18);
			}
			y += S(108);

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
			y += S(108);

			auto &opts = Options();
			auto scale = opts.GetTemperatureScale();
			top = Group(x, y, w, S(36) + S(24) + 5 * S(32), L"Heat and air");
			int half = (w - S(24)) / 2, lx = x + S(12), bx = x + S(220), nw = std::min(S(200), x + w - S(12) - bx);
			Toggle(lx, top, L"Heat simulation", opts.GetHeatSimulation(), false, [&opts] { opts.SetHeatSimulation(!opts.GetHeatSimulation()); },
			       L"Off = the old heatless sim some old saves need");
			Toggle(lx + half, top, L"Water equalisation", opts.GetWaterEqualisation(), false, [&opts] { opts.SetWaterEqualisation(!opts.GetWaterEqualisation()); },
			       L"Liquids level out like connected water (slow)");
			int ry = top + S(30);
			auto row = [&](const wchar_t *label) {
				Caption(lx, ry + S(3), label);
				int at = ry;
				ry += S(32);
				return at;
			};
			StringBuilder temp;
			temp << Format::Precision(2);
			format::RenderTemperature(temp, opts.GetAmbientAirTemperature(), scale);
			NumberButton(bx, row(L"Air temperature"), nw, "Ambient air temperature", temp.Build(),
			             FloatSetting(MIN_TEMP, MAX_TEMP, [scale](const String &s) { return format::StringToTemperature(s, scale); }, [&opts](float v) { opts.SetAmbientAirTemperature(v); }),
			             L"The air's temperature at the edges (with ambient heat); a unit like 20C or 300K works");
			NumberButton(bx, row(L"Air pressure"), nw, "Ambient air pressure", String::Build(Format::Precision(2), opts.GetEdgePressure()),
			             FloatSetting(MIN_PRESSURE, MAX_PRESSURE, [](const String &s) { return s.ToNumber<float>(); }, [&opts](float v) { opts.SetEdgePressure(v); }),
			             L"The pressure at the edges");
			NumberButton(bx, row(L"Vorticity"), nw, "Vorticity confinement", String::Build(Format::Precision(2), opts.GetVorticityCoeff()),
			             FloatSetting(0, 1, [](const String &s) { return s.ToNumber<float>(); }, [&opts](float v) { opts.SetVorticityCoeff(v); }),
			             L"How much the air keeps its swirls, 0 to 1");
			ListButton(bx, row(L"Heat convection"), nw, { L"None", L"Legacy", L"Boussinesq" }, opts.GetConvectionMode(),
			           [&opts](int i) { opts.SetConvectionMode(i); }, L"How hot air rises");
			ListButton(bx, row(L"Temperatures in"), nw, { L"Kelvin", L"Celsius", L"Fahrenheit" }, int(scale),
			           [&opts](int i) { opts.SetTemperatureScale(TempScale(i)); }, L"For the HUD and typed temperatures");
		}

		void PaintDisplay()
		{
			auto c = Content();
			auto &gc = GameController::Ref();
			auto *model = gc.GetModel();
			int x = c.left + S(10), w = c.right - c.left - S(20), y = c.top + S(8) - scroll;
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
			int x = c.left + S(10), w = c.right - c.left - S(20), y = c.top + S(8) - scroll;
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
			RECT swatch{ x + S(12), top + S(28), x + S(52), top + S(50) };
			cv.RoundRect(swatch.left, swatch.top, S(40), S(22), S(3), rgb, rgb, 255);
			cv.RoundFrame(swatch.left, swatch.top, S(40), S(22), S(3), Hovered(swatch) ? 0x3C7FB1 : 0x707070, 255);
			Add(swatch, [] { GameController::Ref().OpenColourPicker(); }, L"TPT's colour picker, with transparency");
			PushButton(x + S(60), top + S(26), S(140), L"Pick colour\u2026", [this] { later = [this] { PickDecoColour(); }; },
			           L"The colour the deco tools paint with (click the swatch for transparency)");
		}

		void PaintSaves()
		{
			auto c = Content();
			int x = c.left + S(10), w = c.right - c.left - S(20), y = c.top + S(8) - scroll;
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
				name = file->GetDisplayName().size() ? Wide(file->GetDisplayName()) : Platform::WinWiden(file->GetName());
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
			PaintOnline(x, y, w);
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
			y += S(60);
			// What's on this computer, with thumbnails: click opens (a stamp: place it), right-click for more.
			int tw = S(124), th = S(78), cellH = th + S(22), pad = S(8);
			int cols = std::max(1, (w - S(24) + pad) / (tw + pad));
			for (int kind = 0; kind < 2; kind++)
			{
				std::vector<Local *> list;
				for (auto &l : locals)
					if (l.stamp.empty() == (kind == 0)) list.push_back(&l);
				int rows = std::max(1, (int(list.size()) + cols - 1) / cols);
				top = Group(x, y, w, S(66) + rows * (cellH + pad), kind ? L"Stamps" : L"Saves on this computer");
				PushButton(x + S(12), top, bw, L"Show folder", [kind] {
					auto dir = ByteString(kind ? STAMPS_DIR : LOCAL_SAVE_DIR);
					Platform::MakeDirectory(dir);
					ShellExecuteW(nullptr, L"open", FullPath(dir).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
				}, kind ? L"The stamps folder" : L"The saves folder; drop a .cps or .stm file on this window to open it");
				top += S(31);
				if (list.empty()) Caption(x + S(12), top, kind ? L"No stamps yet" : L"No saves yet", 0x6D6D6D);
				for (int i = 0; i < int(list.size()); i++)
				{
					auto &l = *list[i];
					int cx = x + S(12) + (i % cols) * (tw + pad), cy = top + (i / cols) * (cellH + pad);
					RECT rc{ cx, cy, cx + tw, cy + cellH };
					cv.RoundRect(cx, cy, tw, th, 0, 0x000000, 0x000000, 255); // TPT's black, behind a thumbnail that doesn't fill
					if (l.thumb)
					{
						auto size = l.thumb->Size();
						int ox = cx + (tw - size.X) / 2, oy = cy + (th - size.Y) / 2;
						for (int py = 0; py < size.Y; py++)
							for (int px = 0; px < size.X; px++) cv.Blend(ox + px, oy + py, l.thumb->Data()[py * size.X + px] & 0xFFFFFF, 255);
					}
					cv.RoundFrame(cx - 1, cy - 1, tw + 2, th + 2, 0, Hovered(rc) ? 0x3C7FB1 : 0xABADB3, 255);
					auto ns = text->Measure(l.name);
					text->Draw(cv, cx + std::max(0, (tw - int(ns.cx)) / 2), cy + th + S(3), l.name, theme->ink, false, tw);
					Add(rc, [path = l.path, stamp = l.stamp] { OpenLocal(path, stamp); },
					    l.name + (kind ? L": click to place it, right-click for more" : L": click to open, right-click for more"),
					    [this, path = l.path, stamp = l.stamp, name = l.name] { later = [this, path, stamp, name] { LocalMenu(path, stamp, name); }; });
				}
				y += S(74) + rows * (cellH + pad);
			}
		}

		// powdertoy.co.uk's saves: a search box, sort, a page of thumbnails (click: TPT's preview, to open, vote,
		// comment) and page buttons. y moves past it.
		void PaintOnline(int x, int &y, int w)
		{
			int tw = S(124), th = S(78), cellH = th + S(38), pad = S(8);
			int cols = std::max(1, (w - S(24) + pad) / (tw + pad));
			if (onlinePer != cols * 3)
			{
				onlinePer = cols * 3; // a wider window: more a page
				onlineSearch = true;
			}
			int rows = online.empty() ? 1 : (int(online.size()) + cols - 1) / cols;
			int h = S(31) + S(36) + rows * (cellH + pad) + S(31) + S(8);
			int top = Group(x, y, w, h, L"Online saves");
			RECT sr{ x + S(12), top, x + w - S(12), top + S(23) };
			int sw = sr.right - sr.left, sh = sr.bottom - sr.top;
			bool here = typing && typed == &onlineQuery;
			cv.RoundRect(sr.left, sr.top, sw, sh, S(3), 0xFFFFFF, 0xFFFFFF, 255);
			cv.RoundFrame(sr.left, sr.top, sw, sh, S(3), here ? 0x3C7FB1 : (Hovered(sr) ? 0x7EB4EA : 0xABADB3), 255);
			auto shown = onlineQuery.empty() && !here ? std::wstring(L"Search saves") : onlineQuery;
			auto qs = text->Measure(shown.empty() ? std::wstring(L" ") : shown);
			int ty = sr.top + (sh - qs.cy) / 2;
			text->Draw(cv, sr.left + S(6), ty, shown, onlineQuery.empty() ? 0x6D6D6D : 0x000000, false, sw - S(12));
			if (here) Line(cv, sr.left + S(6) + (onlineQuery.empty() ? 0 : std::min(int(qs.cx), sw - S(12))), ty, std::max(1, S(1)), qs.cy, 0x000000);
			Add(sr, [this] { StartTyping(&onlineQuery); }, L"Words, user:name or a save ID; Enter searches, Esc clears");
			int ry = top + S(31), sx = x + S(12);
			const wchar_t *sorts[] = { L"Popular", L"New", L"Favourites", L"Mine" };
			bool signedIn = Client::Ref().GetAuthUser().has_value();
			for (int i = 0; i < (signedIn ? 4 : 2); i++)
			{
				sx += Toggle(sx, ry, sorts[i], onlineSort == i, true, [this, i] { onlineSort = i, onlinePage = 0, onlineSearch = true; }) + S(14);
			}
			if (onlineSort == 0)
			{
				ListButton(x + w - S(12) - S(140), ry, S(140), { L"All time", L"Today", L"This week", L"This month", L"This year" }, onlinePeriod,
				           [this](int i) { onlinePeriod = i, onlinePage = 0, onlineSearch = true; }, L"Popular saves from this time");
			}
			int gy = ry + S(36);
			if (online.empty()) Caption(x + S(12), gy, onlineStatus, 0x6D6D6D);
			for (int i = 0; i < int(online.size()); i++)
			{
				auto &o = online[i];
				int cx = x + S(12) + (i % cols) * (tw + pad), cy = gy + (i / cols) * (cellH + pad);
				RECT rc{ cx, cy, cx + tw, cy + cellH };
				cv.RoundRect(cx, cy, tw, th, 0, 0x000000, 0x000000, 255);
				if (o.thumb)
				{
					auto size = o.thumb->Size();
					int ox = cx + (tw - size.X) / 2, oy = cy + (th - size.Y) / 2;
					for (int py = 0; py < size.Y; py++)
						for (int px = 0; px < size.X; px++) cv.Blend(ox + px, oy + py, o.thumb->Data()[py * size.X + px] & 0xFFFFFF, 255);
				}
				cv.RoundFrame(cx - 1, cy - 1, tw + 2, th + 2, 0, Hovered(rc) ? 0x3C7FB1 : 0xABADB3, 255);
				auto ns = text->Measure(o.name);
				text->Draw(cv, cx + std::max(0, (tw - int(ns.cx)) / 2), cy + th + S(3), o.name, theme->ink, false, tw);
				auto info = L"\u25B2" + std::to_wstring(o.up) + L" \u25BC" + std::to_wstring(o.down) + L"  " + o.author;
				auto is = text->Measure(info, Text::Style{ 10, FW_NORMAL, false, false, 0x6D6D6D });
				text->Draw(cv, cx + std::max(0, (tw - int(is.cx)) / 2), cy + th + S(19), info, Text::Style{ 10, FW_NORMAL, false, false, 0x6D6D6D });
				Add(rc, [id = o.id, date = o.date] { GameController::Ref().OpenSavePreview(id, date, savePreviewNormal); },
				    o.name + L" by " + o.author + L": click to see it, then open");
			}
			int py = gy + rows * (cellH + pad), pages = std::max(1, (onlineTotal + onlinePer - 1) / onlinePer);
			if (onlinePage > 0) PushButton(x + S(12), py, S(110), L"\u25C0 Previous", [this] { onlinePage--, onlineSearch = true; });
			if (onlinePage + 1 < pages) PushButton(x + w - S(12) - S(110), py, S(110), L"Next \u25B6", [this] { onlinePage++, onlineSearch = true; });
			auto page = L"Page " + std::to_wstring(onlinePage + 1) + L" of " + std::to_wstring(pages);
			Caption(x + (w - int(text->Measure(page).cx)) / 2, py + S(3), page, 0x6D6D6D);
			y += h + S(8);
		}

		// The Online saves search and its thumbnails, polled while the Saves tab shows
		void TickOnline()
		{
			if (onlineSearch && !searching)
			{
				onlineSearch = false;
				auto sort = onlineSort == 1 ? http::sortByDate : http::sortByVotes;
				auto category = onlineSort == 2 ? http::categoryFavourites : onlineSort == 3 ? http::categoryMyOwn : http::categoryNone;
				auto period = onlineSort == 0 ? http::Period(onlinePeriod) : http::allSaves;
				searching = std::make_unique<http::SearchSavesRequest>(onlinePage * onlinePer, onlinePer, Platform::WinNarrow(onlineQuery), period, sort, category);
				searching->Start();
				online.clear();
				onlineStatus = L"Loading\u2026";
				dirty = true;
			}
			if (searching && searching->CheckDone())
			{
				try
				{
					auto [total, saves] = searching->Finish();
					onlineTotal = total;
					for (auto &s : saves)
					{
						Online o{ s->GetID(), s->GetVersion(), s->GetVotesUp(), s->GetVotesDown(), Wide(s->GetName()), Platform::WinWiden(s->GetUserName()) };
						online.push_back(std::move(o));
					}
					onlineStatus = online.empty() ? L"No saves found" : L"";
				}
				catch (const http::RequestError &ex)
				{
					onlineStatus = L"Couldn't reach powdertoy.co.uk: " + Platform::WinWiden(ex.what());
				}
				searching.reset();
				dirty = true;
			}
			for (auto &o : online)
			{
				if (!o.tried)
				{
					o.tried = true;
					o.req = std::make_unique<http::ThumbnailRequest>(o.id, o.date, Vec2<int>{ S(124), S(78) });
					o.req->Start();
				}
				if (o.req && o.req->CheckDone())
				{
					try
					{
						o.thumb = o.req->Finish();
					}
					catch (const http::RequestError &)
					{
					}
					o.req.reset();
					dirty = true;
				}
			}
		}

		// The start page, in place of TPT's text over the game at startup: the same help, laid out like a Vista
		// welcome page. Drawn on its own canvas, then the visible part is copied in (it scrolls).
		int welcomeH = 0;
		int formH = 0; // the Tools, Simulation, Display, Saves and Overlay pages' height (they scroll when the window is short)
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
			text->Draw(cv, x0, y, L"Desktop " DESKTOP_VERSION L"  \u00B7  The Powder Toy " + std::to_wstring(DISPLAY_VERSION[0]) + L"." +
			                          std::to_wstring(DISPLAY_VERSION[1]), subtitle);
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
			int x = c.left + S(10), w = c.right - c.left - S(20), y = c.top + S(8) - scroll;
			auto &prefs = GlobalPrefs::Ref();
			auto key = [&](const char *name, const char *fallback) {
				return Platform::WinWiden(prefs.Get(ByteString("Overlay.Keys.") + name, ByteString(fallback)));
			};
			int top = Group(x, y, w, S(100), L"Clicks go to");
			const wchar_t *modes[] = { L"Auto: TPT while it's in front, the desktop once another app is", L"Pass-through: the sim takes no clicks (side windows still do)", L"Locked: everything to TPT" };
			for (int i = 0; i < int(OverlayInput::Count); i++)
			{
				Toggle(x + S(12), top + i * S(24), modes[i], int(OverlayGetInput()) == i, true, [i] { OverlaySetInput(OverlayInput(i)); });
			}
			y += S(108);
			top = Group(x, y, w, S(84), L"Sim layer");
			const wchar_t *layers[] = { L"On top of every window", L"Under windows, over the desktop icons",
			                            L"Behind the desktop icons, like a wallpaper (clicks go to the desktop; Locked comes up to draw)" };
			for (int i = 0; i < int(OverlayLayer::Count); i++)
			{
				Toggle(x + S(12), top + i * S(24), layers[i], int(OverlayGetLayer()) == i, true, [i] { OverlaySetLayer(OverlayLayer(i)); });
			}
			y += S(92);
			top = Group(x, y, w, S(92), L"Window");
			Toggle(x + S(12), top, L"Show TPT's own menus too", showTPTMenus, false, [] {
				showTPTMenus = !showTPTMenus;
				GlobalPrefs::Ref().Set("Overlay.ShowTPTMenus", showTPTMenus);
				if (!overlayWindowed) Restart(ui::Engine::Ref().windowFrameOps.scale, false, true); // they need room on screen
			}, L"Restarts TPT: the sim leaves room for the menus");
			Toggle(x + S(12), top + S(24), L"Pass clicks through while a full-screen app or game is in front", overlayAutoPass, false, [] {
				overlayAutoPass = !overlayAutoPass;
				GlobalPrefs::Ref().Set("Overlay.AutoPassThrough", overlayAutoPass);
			});
			// Start with Windows: a value under the user's Run key, pointing at this exe
			static const wchar_t *runKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", *runName = L"The Desktop Powder Toy";
			bool startup = RegGetValueW(HKEY_CURRENT_USER, runKey, runName, RRF_RT_REG_SZ, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
			Toggle(x + S(12), top + S(48), L"Start with Windows", startup, false, [startup] {
				HKEY k;
				if (RegOpenKeyExW(HKEY_CURRENT_USER, runKey, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
				if (startup) RegDeleteValueW(k, runName);
				else
				{
					wchar_t exe[MAX_PATH];
					GetModuleFileNameW(nullptr, exe, MAX_PATH);
					std::wstring line = L"\"" + std::wstring(exe) + L"\"";
					RegSetValueExW(k, runName, 0, REG_SZ, (const BYTE *)line.c_str(), DWORD((line.size() + 1) * sizeof(wchar_t)));
				}
				RegCloseKey(k);
			}, L"Opens on the desktop when you sign in");
			y += S(100);
			// Apps that always get the clicks while they're in front (Overlay.PassApps: exe names, ';'-separated)
			auto apps = PassApps();
			int ah = S(50) + int(apps.size()) * S(26);
			top = Group(x, y, w, ah, L"Pass clicks through while these apps are in front");
			for (int i = 0; i < int(apps.size()); i++)
			{
				int ry = top + i * S(26);
				Caption(x + S(12), ry, Platform::WinWiden(apps[i]));
				PushButton(x + w - S(100), ry, S(88), L"Remove", [apps, i]() mutable {
					apps.erase(apps.begin() + i);
					SetPassApps(apps);
				}, L"Clicks go back to the overlay's input mode while it's in front");
			}
			{
				int by = top + int(apps.size()) * S(26), bx = x + S(12), bw = S(220);
				PushButton(bx, by, bw, L"Add a running app\u2026", [this, bx, by, bw] {
					later = [this, rc = RECT{ bx, by, bx + bw, by + S(23) }] {
						auto running = RunningApps();
						HMENU m = CreatePopupMenu();
						for (int i = 0; i < int(running.size()); i++) AppendMenuW(m, MF_STRING, UINT(i + 1), Platform::WinWiden(running[i]).c_str());
						if (running.empty()) AppendMenuW(m, MF_STRING | MF_GRAYED, 0, L"No other apps have windows open");
						POINT p{ rc.left, rc.bottom };
						ClientToScreen(hwnd, &p);
						int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY, p.x, p.y, 0, hwnd, nullptr);
						DestroyMenu(m);
						if (cmd > 0)
						{
							auto list = PassApps();
							if (std::find(list.begin(), list.end(), running[cmd - 1]) == list.end()) list.push_back(running[cmd - 1]);
							SetPassApps(list);
						}
						dirty = true;
					};
				}, L"An app with a window open now");
				for (int i = 0, k = S(4), ax = bx + bw - S(20), ay = by + S(10); i <= k; i++) Line(cv, ax + i, ay + i, 2 * (k - i) + 1, 1, theme->ink); // the arrow
			}
			y += ah + S(8);
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
			top = Group(x, y, w, S(108), L"Size");
			Caption(x + S(12), top + S(56), L"Runs as");
			int mx = x + S(200);
			mx += Toggle(mx, top + S(56), L"Desktop overlay", !overlayWindowed, true, [] { Restart(ui::Engine::Ref().windowFrameOps.scale, false); },
			             L"Over the desktop (restarts TPT; the scene comes along)") + S(18);
			Toggle(mx, top + S(56), L"Normal window", overlayWindowed, true, [] { Restart(ui::Engine::Ref().windowFrameOps.scale, true); },
			       L"TPT's own window with these side windows (restarts TPT; the scene comes along, clipped to the smaller sim)");
			// The sim's pixel size: a different sim size, so TPT restarts with the scene carried over.
			Caption(x + S(12), top + S(28), L"Sim pixel size");
			int sx = x + S(200);
			for (int sc = 1; sc <= 6; sc++)
			{
				auto [cells, real] = OverlaySimSize(sc);
				if (real != sc) continue; // too big a sim at this size; the next one up is the same
				wchar_t label[32];
				swprintf(label, 32, L"%d\u00D7", sc);
				wchar_t tip[96];
				swprintf(tip, 96, L"%dx%d sim pixels (restarts TPT; the scene comes along, clipped if it doesn't fit)", cells.X * CELL, cells.Y * CELL);
				sx += Toggle(sx, top + S(28), label, ui::Engine::Ref().windowFrameOps.scale == sc, true, [sc] { Restart(sc, overlayWindowed); }, tip) + S(18);
			}
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
			y += S(116);
			top = Group(x, y, w, S(108), L"Look");
			int current = int(std::find(std::begin(themes), std::end(themes), theme) - std::begin(themes));
			auto family = [&](int row, const wchar_t *label, const std::vector<int> &ids) {
				int by = top + row * S(32), bx = x + S(200), bw = S(220);
				bool in = std::count(ids.begin(), ids.end(), current) > 0;
				Caption(x + S(12), by, label);
				PushButton(bx, by, bw, std::wstring(in ? themes[current]->name : L"\u2014"), [this, &ids, bx, by, bw] {
					later = [this, &ids, rc = RECT{ bx, by, bx + bw, by + S(23) }] { PickTheme(ids, rc); };
				}, L"Windows and menus look like this system's");
				for (int i = 0, k = S(4), ax = bx + bw - S(20), ay = by + S(10); i <= k; i++) Line(cv, ax + i, ay + i, 2 * (k - i) + 1, 1, theme->ink); // the arrow
			};
			family(0, L"Windows", windowsThemes);
			family(1, L"Mac", macThemes);
			Toggle(x + S(12), top + 2 * S(32), L"TempleOS", current == templeIndex, true, [] { SetTheme(templeIndex); }, L"Windows and menus look like TempleOS");
			y += S(116);

			auto &opts = Options();
			top = Group(x, y, w, S(36) + 3 * S(24) + 2 * S(32), L"TPT settings");
			int half = (w - S(24)) / 2, lx = x + S(12);
			Toggle(lx, top, L"Perfect circle brush", opts.GetPerfectCircle(), false, [&opts] { opts.SetPerfectCircle(!opts.GetPerfectCircle()); }, L"Rounder small circle brushes");
			Toggle(lx + half, top, L"Pressure in saves", opts.GetIncludePressure(), false, [&opts] { opts.SetIncludePressure(!opts.GetIncludePressure()); },
			       L"Saves and stamps keep the air pressure");
			Toggle(lx, top + S(24), L"Smooth scrolling", opts.GetMomentumScroll(), false, [&opts] { opts.SetMomentumScroll(!opts.GetMomentumScroll()); }, L"Momentum scrolling in TPT's lists");
			Toggle(lx + half, top + S(24), L"Quit without asking", opts.GetFastQuit(), false, [&opts] { opts.SetFastQuit(!opts.GetFastQuit()); }, L"No confirmation when closing TPT");
			Toggle(lx, top + 2 * S(24), L"Show avatars", opts.GetShowAvatars(), false, [&opts] { opts.SetShowAvatars(!opts.GetShowAvatars()); }, L"Profile pictures in the online browser");
			int ry = top + 3 * S(24) + S(4);
			Caption(lx, ry + S(3), L"Deco colour space");
			ListButton(x + S(220), ry, std::min(S(200), x + w - S(232)), { L"sRGB", L"Linear", L"Gamma 2.2", L"Gamma 1.8" }, opts.GetDecoSpace(),
			           [&opts](int i) { opts.SetDecoSpace(i); }, L"How deco colours blend");
			PushButton(lx, ry + S(32), S(200), L"All of TPT's options\u2026", [] { GameController::Ref().OpenOptions(); }, L"TPT's own settings window: everything else");
		}

		// TPT's settings, through its own options model (which saves the prefs).
		static OptionsModel &Options()
		{
			static OptionsModel options(GameController::Ref().GetModel());
			return options;
		}

		// A setting picked from a list: the choice on a button, the list as a native menu under it.
		void ListButton(int bx, int by, int bw, std::vector<std::wstring> names, int current, std::function<void(int)> set, const std::wstring &tip)
		{
			PushButton(bx, by, bw, current >= 0 && current < int(names.size()) ? names[current] : L"\u2014", [this, names, current, set, bx, by, bw] {
				later = [this, names, current, set, rc = RECT{ bx, by, bx + bw, by + S(23) }] {
					HMENU m = CreatePopupMenu();
					for (int i = 0; i < int(names.size()); i++) AppendMenuW(m, MF_STRING | (i == current ? MF_CHECKED : 0), UINT(i + 1), names[i].c_str());
					POINT p{ rc.left, rc.bottom };
					ClientToScreen(hwnd, &p);
					int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY, p.x, p.y, 0, hwnd, nullptr);
					DestroyMenu(m);
					if (cmd > 0) set(cmd - 1);
					dirty = true;
				};
			}, tip);
			for (int i = 0, k = S(4), ax = bx + bw - S(20), ay = by + S(10); i <= k; i++) Line(cv, ax + i, ay + i, 2 * (k - i) + 1, 1, theme->ink); // the arrow
		}

		// A number setting: the value on a button; a click asks for a new one. use(text, apply): is it valid, and
		// when apply, set it.
		void NumberButton(int bx, int by, int bw, const String &title, const String &value, std::function<bool(const String &, bool)> use, const std::wstring &tip)
		{
			PushButton(bx, by, bw, Wide(value), [this, title, value, use] {
				later = [this, title, value, use] {
					int none = 0;
					String text = value;
					if (OverlayWindows::Form(title, title + ":", {}, none, text, {}, [use](int, const String &v) { return use(v, false); }) == 1) use(text, true);
					dirty = true;
				};
			}, tip);
		}

		// A float typed in, within [lo, hi].
		static std::function<bool(const String &, bool)> FloatSetting(float lo, float hi, std::function<float(const String &)> parse, std::function<void(float)> set)
		{
			return [=](const String &s, bool apply) {
				try
				{
					float v = parse(s);
					if (!(v >= lo && v <= hi)) return false;
					if (apply) set(v);
					return true;
				}
				catch (const std::exception &)
				{
					return false;
				}
			};
		}

		// A theme family's list as a menu under its button.
		void ScanLocals()
		{
			std::vector<Local> found;
			auto dir = ByteString::Build(LOCAL_SAVE_DIR, PATH_SEP_CHAR);
			auto files = Platform::DirectorySearch(dir, "", { ".cps" });
			std::sort(files.begin(), files.end(), [](const ByteString &a, const ByteString &b) { return a.ToLower() < b.ToLower(); });
			for (auto &file : files)
			{
				auto name = file.SplitFromEndBy(PATH_SEP_CHAR).After().SplitFromEndBy('.').Before();
				found.push_back({ dir + file, "", Wide(name.FromUtf8()) });
			}
			Client::Ref().RescanStamps();
			for (auto &id : Client::Ref().GetStamps()) // newest first, as TPT lists them
				found.push_back({ ByteString::Build(STAMPS_DIR, PATH_SEP_CHAR, id, ".stm"), id, Platform::WinWiden(id) });
			for (auto &l : found)
				for (auto &old : locals)
					if (old.path == l.path) l.thumb = std::move(old.thumb), l.tried = old.tried;
			if (found.size() != locals.size()) dirty = true;
			locals = std::move(found);
		}

		void TickLocals()
		{
			if (tab != savesTab || !IsWindowVisible(hwnd)) return;
			TickOnline();
			if (GetTickCount() - localsAt > 2000)
			{
				ScanLocals();
				localsAt = GetTickCount();
			}
			for (auto &l : locals)
			{
				if (l.tried) continue;
				// ponytail: rendered on TPT's thread, one per tick; a big save is a short hitch. TPT's own
				// ThumbnailRendererTask threads it if that shows
				l.tried = true;
				SaveFile file(l.path, true);
				if (auto *save = file.LazyGetGameSave())
				{
					RendererSettings settings;
					l.thumb = SaveRenderer::Ref().Render(save, false, settings);
					if (l.thumb) l.thumb->ResizeToFit({ S(124), S(78) }, true);
				}
				dirty = true;
				break;
			}
		}

		static void OpenLocal(const ByteString &path, const ByteString &stamp)
		{
			auto &gc = GameController::Ref();
			if (stamp.size())
			{
				auto file = Client::Ref().GetStamp(stamp);
				if (!file || !file->GetGameSave()) new ErrorMessage("Error loading stamp", file ? file->GetError() : String("Not found"));
				else
				{
					Client::Ref().MoveStampToFront(stamp);
					gc.LoadStamp(file->TakeGameSave());
				}
				return;
			}
			auto file = std::make_unique<SaveFile>(path, true);
			if (!file->LazyGetGameSave())
			{
				new ErrorMessage("Error loading save", file->GetError());
				return;
			}
			file->SetDisplayName(path.SplitFromEndBy(PATH_SEP_CHAR).After().SplitFromEndBy('.').Before().FromUtf8());
			gc.HistorySnapshot();
			gc.LoadSaveFile(std::move(file));
		}

		static std::wstring FullPath(const ByteString &path)
		{
			wchar_t full[MAX_PATH];
			return GetFullPathNameW(Platform::WinWiden(path).c_str(), MAX_PATH, full, nullptr) ? full : Platform::WinWiden(path);
		}

		void LocalMenu(const ByteString &path, const ByteString &stamp, const std::wstring &name)
		{
			HMENU m = CreatePopupMenu();
			AppendMenuW(m, MF_STRING | MF_DEFAULT, 1, stamp.size() ? L"Place" : L"Open");
			AppendMenuW(m, MF_STRING, 2, L"Show in folder");
			AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
			AppendMenuW(m, MF_STRING, 3, L"Delete\u2026");
			POINT p;
			GetCursorPos(&p);
			int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, nullptr);
			DestroyMenu(m);
			if (cmd == 1) OpenLocal(path, stamp);
			if (cmd == 2) ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"/select,\"" + FullPath(path) + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
			if (cmd == 3 && MessageBoxW(hwnd, (L"Delete " + name + L"? This can't be undone.").c_str(), L"The Powder Toy", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES)
			{
				if (stamp.size()) Client::Ref().DeleteStamp(stamp);
				else Platform::RemoveFile(path);
				localsAt = 0; // list again now
			}
		}

		// A .cps dropped on the window opens; a .stm is placed as a stamp.
		void Drop(const std::wstring &path) override
		{
			auto p = Platform::WinNarrow(path);
			auto ext = p.SplitFromEndBy('.').After().ToLower();
			if (ext == "cps") OpenLocal(p, "");
			else if (ext == "stm")
			{
				std::vector<char> data;
				if (Platform::ReadFile(data, p))
				{
					try
					{
						GameController::Ref().LoadStamp(std::make_unique<GameSave>(std::move(data)));
					}
					catch (std::exception &e)
					{
						new ErrorMessage("Error loading stamp", ByteString(e.what()).FromUtf8());
					}
				}
			}
		}

		// Windows' own colour dialog; the alpha stays as it was.
		void PickDecoColour()
		{
			static COLORREF custom[16] = {}; // the dialog's custom colours, for this run
			auto &gc = GameController::Ref();
			auto col = gc.GetModel()->GetColourSelectorColour();
			CHOOSECOLORW cc{ sizeof(cc) };
			cc.hwndOwner = hwnd, cc.rgbResult = RGB(col.Red, col.Green, col.Blue), cc.lpCustColors = custom;
			cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
			if (ChooseColorW(&cc)) gc.SetColour(ui::Colour(GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult), col.Alpha));
			dirty = true;
		}

		void PickTheme(const std::vector<int> &ids, RECT rc)
		{
			HMENU m = CreatePopupMenu();
			for (int i : ids) AppendMenuW(m, MF_STRING | (theme == themes[i] ? MF_CHECKED : 0), UINT(i + 1), themes[i]->name);
			POINT p{ rc.left, rc.bottom };
			ClientToScreen(hwnd, &p);
			// ponytail: modal here, so the sim waits while the menu is open; NativeMenu's thread if that matters
			int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY, p.x, p.y, 0, hwnd, nullptr);
			DestroyMenu(m);
			if (cmd > 0) SetTheme(cmd - 1);
		}

		void Paint() override
		{
			hits.clear();
			theme->Frame(cv, titleH, border, fixedTab >= 0 ? tabNames[fixedTab] : L"The Powder Toy");
			theme->CaptionButton(cv, MinRect(), Hovered(MinRect()), CaptionMin);
			theme->CaptionButton(cv, MaxRect(), Hovered(MaxRect()), CaptionMax);
			theme->CaptionButton(cv, CloseRect(), Hovered(CloseRect()), CaptionClose);
			Add(MinRect(), [this] { ShowWindow(hwnd, SW_MINIMIZE); }, L"Minimise to the taskbar");
			Add(MaxRect(), [this] { ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE); }, IsZoomed(hwnd) ? L"Restore" : L"Maximise");
			if (fixedTab >= 0) Add(CloseRect(), [this] { closing = true; }, L"Close (the tab is still in the main window)");
			else Add(CloseRect(), [] { OverlayWindows::TogglePalette(); }, L"Close (the tray icon or Ctrl+Alt+E brings it back)");

			// The tab strip: the selected tab joined to the page.
			int tx = border + S(4);
			cv.RoundRect(border, titleH, cv.w - 2 * border, tabH, 0, theme->strip, theme->strip, theme->stripAlpha);
			for (int i = 0; i < int(std::size(tabNames)); i++)
			{
				if (fixedTab >= 0 && i != fixedTab) continue;
				int tw = text->Measure(tabNames[i]).cx + S(20);
				bool on = i == tab;
				RECT rc{ tx, titleH + (on ? S(2) : S(4)), tx + tw, titleH + tabH + (on ? 1 : 0) };
				bool hot = !on && Hovered(rc);
				theme->Tab(cv, rc, on, hot);
				text->Draw(cv, tx + S(10), rc.top + (rc.bottom - rc.top - text->Measure(tabNames[i]).cy) / 2 - (theme->segmentTabs ? S(1) : 0), tabNames[i], theme->TabInk(i == tab));
				if (fixedTab < 0) Add(rc, [this, i] { tab = i, scroll = 0; }, L"Right-click: open this tab in its own window", [i] { OpenTorn(i); });
				tx += tw + S(1);
			}
			// The page.
			auto c = Content();
			cv.RoundRect(c.left, c.top, c.right - c.left, c.bottom - c.top, 0, theme->page, theme->page, 255);
			for (int i = c.left; i < c.right; i++) cv.Blend(i, c.top, theme->edge, 255);
			// The selected tab's bottom edge opens into the page.
			int sx = border + S(4);
			for (int i = 0; i < tab && fixedTab < 0; i++) sx += text->Measure(tabNames[i]).cx + S(21);
			if (!theme->segmentTabs)
				for (int i = sx + 1; i < sx + text->Measure(tabNames[tab]).cx + S(19); i++) cv.Blend(i, c.top, theme->page, 255);

			// Form pages scroll: rows outside the page are put back after, and controls there dropped.
			bool form = tab >= 1 && tab <= 5;
			size_t firstPageHit = hits.size();
			size_t topEnd = size_t(c.top + 1) * cv.w, bottomStart = size_t(c.bottom) * cv.w;
			std::vector<uint32_t> above(cv.px.begin(), cv.px.begin() + topEnd), below(cv.px.begin() + bottomStart, cv.px.end());
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
			if (form)
			{
				std::copy(above.begin(), above.end(), cv.px.begin());
				std::copy(below.begin(), below.end(), cv.px.begin() + bottomStart);
				int bottom = c.top;
				for (size_t i = firstPageHit; i < hits.size(); i++) bottom = std::max(bottom, int(hits[i].rc.bottom));
				formH = bottom + scroll - c.top + S(20);
				hits.erase(std::remove_if(hits.begin() + firstPageHit, hits.end(), [&](const Hit &h) {
					return h.rc.top < c.top + 1 || h.rc.bottom > c.bottom;
				}), hits.end());
			}

			// The status bar: what the hovered control does.
			int sy = cv.h - border - statusH;
			cv.RoundRect(border, sy, cv.w - 2 * border, statusH, 0, theme->status[0], theme->status[1], 255);
			for (int i = border; i < cv.w - border; i++) cv.Blend(i, sy, theme->status[2], 255);
			if (hover >= 0 && hover < int(hits.size()))
			{
				text->Draw(cv, border + S(8), sy + S(3), hits[hover].tip, theme->heading, false, cv.w - 2 * border - S(16));
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
			if (msg == WM_MOUSEWHEEL && tab >= 1 && tab <= 5)
			{
				auto c = Content();
				scroll = std::clamp(scroll - GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * S(40), 0, std::max(0, formH - int(c.bottom - c.top)));
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
			TickLocals();
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
			auto deco = model->GetColourSelectorColour(); // TPT's picker or Lua changed the deco colour
			mix(uint64_t(deco.Red) << 24 | uint64_t(deco.Green) << 16 | uint64_t(deco.Blue) << 8 | deco.Alpha);
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
			theme->Popup(cv);
			auto size = text->Measure(message, true);
			text->Draw(cv, (cv.w - size.cx) / 2, (cv.h - size.cy) / 2, message, 0x000000, true, cv.w, theme->glow ? S(float(theme->glow)) : 0);
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

	std::unique_ptr<SideWindow> palette;
	std::vector<std::unique_ptr<SideWindow>> torn; // tabs in their own windows (Overlay.Windows.Torn, a bit per tab)

	void DirtyAll()
	{
		if (palette) palette->dirty = true;
		for (auto &t : torn) t->dirty = true;
	}

	void SaveTorn()
	{
		int mask = 0;
		for (auto &t : torn) mask |= 1 << t->fixedTab;
		GlobalPrefs::Ref().Set("Overlay.Windows.Torn", mask);
	}

	// A window's saved spot and size (Overlay.Windows.<name>.*), else the default, kept on its monitor's work area.
	RECT PlaceWindow(const ByteString &name, int x, int y, int w, int h, int minW, int minH)
	{
		auto &prefs = GlobalPrefs::Ref();
		POINT saved{ prefs.Get("Overlay.Windows." + name + ".X", x), prefs.Get("Overlay.Windows." + name + ".Y", y) };
		POINT grip{ saved.x + w / 2, saved.y + S(10) }; // the title bar must be on a monitor
		if (MonitorFromPoint(grip, MONITOR_DEFAULTTONULL))
		{
			x = saved.x;
			y = saved.y;
		}
		w = std::max(prefs.Get("Overlay.Windows." + name + ".W", w), minW);
		h = std::max(prefs.Get("Overlay.Windows." + name + ".H", h), minH);
		// The whole window on its monitor's work area (a saved spot can hang off the edge).
		MONITORINFO mi{ sizeof(mi) };
		GetMonitorInfoW(MonitorFromPoint({ x + w / 2, y + S(10) }, MONITOR_DEFAULTTONEAREST), &mi);
		auto work = mi.rcWork;
		w = std::min(w, int(work.right - work.left));
		h = std::min(h, int(work.bottom - work.top));
		x = std::clamp(x, int(work.left), int(work.right) - w);
		y = std::clamp(y, int(work.top), int(work.bottom) - h);
		return { x, y, x + w, y + h };
	}

	void ApplyPassThrough(Panel &panel);

	// The scene carried across a restart: saves of at most 255x255 cells (the save format's limit), named by their
	// top-left cell, in the temp folder.
	std::wstring SceneTile(int x, int y)
	{
		wchar_t tmp[MAX_PATH];
		GetTempPathW(MAX_PATH, tmp);
		return std::wstring(tmp) + L"tpt-overlay-scene-" + std::to_wstring(x) + L"-" + std::to_wstring(y) + L".cps";
	}

	void LoadSceneTiles()
	{
		auto *model = GameController::Ref().GetModel();
		auto *sim = model->GetSimulation();
		GameController::Ref().GetView()->PauseRendererThread();
		for (int y = 0; y < YCELLS; y += 255)
			for (int x = 0; x < XCELLS; x += 255)
			{
				auto file = Platform::WinNarrow(SceneTile(x, y));
				std::vector<char> data;
				if (!Platform::FileExists(file) || !Platform::ReadFile(data, file)) continue;
				try
				{
					GameSave save(data);
					sim->Load(&save, true, Vec2(x, y));
				}
				catch (std::exception &) {} // a bad tile: that part of the scene is lost
				DeleteFileW(SceneTile(x, y).c_str());
			}
	}

	// Saves the scene, sets the new pixel size and starts a fresh TPT, which loads the scene; this one quits.
	// The other pixel size or mode: the scene goes along in tiles, the new TPT loads it, this one quits.
	void Restart(int scale, bool windowed, bool force)
	{
		if (!force && scale == ui::Engine::Ref().windowFrameOps.scale && windowed == overlayWindowed) return;
		auto *model = GameController::Ref().GetModel();
		auto *sim = model->GetSimulation();
		for (int y = 0; y < YCELLS; y += 255)
			for (int x = 0; x < XCELLS; x += 255)
			{
				auto tile = RectSized(Vec2(x, y), Vec2(std::min(255, XCELLS - x), std::min(255, YCELLS - y)));
				auto save = sim->Save(true, RectSized(tile.pos * CELL, tile.size * CELL));
				if (!save) continue;
				save->paused = model->GetPaused();
				auto data = save->Serialise().second; // .first is not success (TPT's own save path ignores it too)
				if (!data.empty()) Platform::WriteFile(data, Platform::WinNarrow(SceneTile(x, y)));
			}
		GlobalPrefs::Ref().Set("Overlay.PendingScene", true);
		GlobalPrefs::Ref().Set("Scale", scale);
		// The same command line, with overlay:on or overlay:window.
		wchar_t exe[MAX_PATH];
		GetModuleFileNameW(nullptr, exe, MAX_PATH);
		std::wstring line = GetCommandLineW();
		for (auto *arg : { L" overlay:window", L" overlay:on" })
			if (auto at = line.find(arg); at != std::wstring::npos) line.erase(at, wcslen(arg));
		line += windowed ? L" overlay:window" : L" overlay:on";
		STARTUPINFOW si{ sizeof(si) };
		PROCESS_INFORMATION pi{};
		if (CreateProcessW(exe, line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
		{
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
			SDL_Event quit{};
			quit.type = SDL_QUIT;
			SDL_PushEvent(&quit);
		}
	}

	void OpenTorn(int tab)
	{
		for (auto &t : torn)
			if (t->fixedTab == tab)
			{
				SetWindowPos(t->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
				return;
			}
		auto w = std::make_unique<SideWindow>();
		w->fixedTab = w->tab = tab;
		w->name = ByteString("Tab") + ByteString(std::to_string(tab));
		w->title = SideWindow::tabNames[tab];
		POINT cur;
		GetCursorPos(&cur);
		auto r = PlaceWindow(w->name, cur.x - S(60), cur.y - S(12), S(560), S(560), S(360), S(300));
		w->grip = w->border;
		w->minSize = { S(360), S(300) };
		w->Create(r.left, r.top, r.right - r.left, r.bottom - r.top);
		w->Present();
		ShowWindow(w->hwnd, SW_SHOWNOACTIVATE);
		SetWindowPos(w->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		ApplyPassThrough(*w);
		torn.push_back(std::move(w));
		SaveTorn();
	}
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
		RECT CloseRect() { return theme->CaptionRect(cv.w, border, CaptionClose); }
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
				theme->Frame(cv, titleH, border, title);
				theme->CaptionButton(cv, CloseRect(), hoverClose, CaptionClose);
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

	void SetTheme(int i)
	{
		theme = themes[i];
		GlobalPrefs::Ref().Set("Overlay.Theme", i);
		ThemeChanged();
	}

	void ThemeChanged()
	{
		DirtyAll();
		for (auto &h : hosts)
			if (h) h->dirty = true;
	}
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
		if (lp == NIN_BALLOONUSERCLICK)
		{
			ShellExecuteW(nullptr, L"open", L"https://github.com/PVCPC/The-Desktop-Powder-Toy/releases/latest", nullptr, nullptr, SW_SHOWNORMAL);
		}
		if (lp == WM_RBUTTONUP)
		{
			// The plain Windows menu: settings and the way into the Vista windows.
			enum { elements = 1, pause, clear, inputBase = 10, showMenus = 20, exitItem = 99 };
			auto &prefs = GlobalPrefs::Ref();
			auto label = [&](const wchar_t *text, const char *keyName, const char *fallback) {
				return std::wstring(text) + L"\t" + Platform::WinWiden(prefs.Get(ByteString("Overlay.Keys.") + keyName, ByteString(fallback)));
			};
			HMENU menu = CreatePopupMenu();
			bool open = palette && IsWindowVisible(palette->hwnd);
			AppendMenuW(menu, MF_STRING | (open ? MF_CHECKED : 0), elements, label(L"Elements window", "Palette", "Ctrl+Alt+E").c_str());
			AppendMenuW(menu, MF_STRING, pause, GameController::Ref().GetPaused() ? L"Resume" : L"Pause");
			AppendMenuW(menu, MF_STRING, clear, L"Clear the sim");
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
			if (cmd == pause) GameController::Ref().SetPaused(!GameController::Ref().GetPaused());
			if (cmd == clear) GameController::Ref().ClearSim();
			if (cmd >= inputBase && cmd < inputBase + int(OverlayInput::Count))
			{
				OverlaySetInput(OverlayInput(cmd - inputBase));
			}
			if (cmd == showMenus)
			{
				showTPTMenus = !showTPTMenus;
				prefs.Set("Overlay.ShowTPTMenus", showTPTMenus);
				if (!overlayWindowed) Restart(ui::Engine::Ref().windowFrameOps.scale, false, true);
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
	iconWalls = prefs.Get("Overlay.IconWalls", true);
	// Default UI scale: the larger of Windows' display scaling (TPT is DPI aware) and screen height / 1080, in
	// quarter steps. Overridable.
	HDC screen = GetDC(nullptr);
	float dpiScale = GetDeviceCaps(screen, LOGPIXELSY) / 96.f;
	ReleaseDC(nullptr, screen);
	float heightScale = GetSystemMetrics(SM_CYSCREEN) / 1080.f;
	uiScale = prefs.Get("Overlay.UIScale", std::max(1.f, std::round(std::max(dpiScale, heightScale) * 4) / 4));
	showTPTMenus = prefs.Get("Overlay.ShowTPTMenus", false);
	theme = themes[std::clamp(prefs.Get("Overlay.Theme", defaultTheme), 0, int(std::size(themes)) - 1)];
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
	static std::unique_ptr<http::Request> release;
	static bool checked = false;
	if (!checked)
	{
		checked = true;
		release = std::make_unique<http::Request>("https://api.github.com/repos/PVCPC/The-Desktop-Powder-Toy/releases/latest");
		release->Start();
	}
	if (release && release->CheckDone())
	{
		ByteString tag;
		try
		{
			auto [status, data] = release->Finish();
			auto at = data.find("\"tag_name\"");
			if (status == 200 && at != data.npos)
			{
				auto q = data.find('"', data.find(':', at) + 1) + 1;
				tag = data.substr(q, data.find('"', q) - q);
			}
		}
		catch (const http::RequestError &) {}
		release.reset();
		auto version = [](const std::wstring &v) { // "v1.2.3" or "1.2.3" -> {1, 2, 3}
			std::array<int, 3> n{};
			swscanf_s(v.c_str() + (v.size() && v[0] == L'v'), L"%d.%d.%d", &n[0], &n[1], &n[2]);
			return n;
		};
		auto &prefs = GlobalPrefs::Ref();
		if (tag.size() && version(Platform::WinWiden(tag)) > version(DESKTOP_VERSION) && tag != prefs.Get("Overlay.ToldVersion", ByteString()))
		{
			prefs.Set("Overlay.ToldVersion", tag);
			tray.uFlags = NIF_INFO;
			wcscpy_s(tray.szInfoTitle, (L"The Desktop Powder Toy " + Platform::WinWiden(tag) + L" is out").c_str());
			wcscpy_s(tray.szInfo, L"Click to download it.");
			tray.dwInfoFlags = NIIF_INFO;
			Shell_NotifyIconW(NIM_MODIFY, &tray);
			tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
		}
	}
	static std::vector<Notification *> seen;
	auto notes = GameController::Ref().GetModel()->GetNotifications();
	if (notes != seen)
	{
		for (auto *n : notes)
			if (std::find(seen.begin(), seen.end(), n) == seen.end()) Notify(n->Message.ToUtf8().c_str());
		seen = notes;
		DirtyAll();
	}
	static bool areaRestored = false;
	if (!areaRestored)
	{
		auto &prefs = GlobalPrefs::Ref();
		int w = prefs.Get("Overlay.PlayArea.W", 0), h = prefs.Get("Overlay.PlayArea.H", 0); // 0: the whole sim, whatever its size
		SetPlayArea(prefs.Get("Overlay.PlayArea.X", 0), prefs.Get("Overlay.PlayArea.Y", 0), w > 0 ? w : CELLS.X, h > 0 ? h : CELLS.Y);
		areaRestored = true;
		if (GlobalPrefs::Ref().Get("Overlay.PendingScene", false))
		{
			GlobalPrefs::Ref().Set("Overlay.PendingScene", false);
			LoadSceneTiles(); // from a restart at another pixel size
		}
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
	static bool tornRestored = false;
	if (!tornRestored)
	{
		tornRestored = true;
		int mask = GlobalPrefs::Ref().Get("Overlay.Windows.Torn", 0);
		for (int i = 0; i < int(std::size(SideWindow::tabNames)); i++)
			if (mask & (1 << i)) OpenTorn(i);
	}
	for (auto &t : torn)
	{
		t->Tick();
		if (t->dirty) t->Present();
	}
	if (std::erase_if(torn, [](auto &t) { return t->closing; })) SaveTorn();
	if (toast)
	{
		toast->Tick();
	}
	SyncHosts();
	TickMenu();
	TickRecording();
	if (later)
	{
		auto run = std::move(later);
		later = nullptr;
		run();
	}
	if (rebuildPalette)
	{
		rebuildPalette = false;
		reopenTab = palette ? palette->tab : -1;
		palette.reset();
		torn.clear(); // reopened at the new scale from the saved mask
		tornRestored = false;
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
		DirtyAll();
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
	DirtyAll();
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
		auto r = PlaceWindow(palette->name, x, y, w, h, S(360), S(300));
		palette->grip = palette->border;
		palette->minSize = { S(360), S(300) };
		palette->Create(r.left, r.top, r.right - r.left, r.bottom - r.top);
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
	for (auto &t : torn) ApplyPassThrough(*t);
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

void OverlayWindows::ApplySimMasks(uint32_t *argb, int w, const uint8_t *occupied)
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
	OverlayBands(YRES, [&](int y0, int y1) {
	for (int y = y0; y < y1; y++)
		for (int x = 0; x < XRES; x++)
		{
			auto &p = argb[y * w + x];
			if (zoomOn && zoom.Contains({ x, y }))
			{
				p |= 0xFF000000;
			}
			else if (occupied[y * XRES + x] & 2) // the walls as of the frame: the live ones may have moved since
			{
				p = 0; // a window's wall: the window shows through
			}
			else if (!area.Contains({ x, y }))
			{
				p = 0;
			}
			else if (!(p & 0xFFFFFF) && (occupied[y * XRES + x] & 1)) // a black particle: solid
			{
				p = 0xFF000000;
			}
		}
	});
	if (!full)
	{
		// A faint frame just outside the area, so you can see where it ends.
		auto br = area.BottomRight();
		// Around it, a band you can't see but can grab to drag the edges (alpha 0 lets clicks through).
		auto mark = [&](int x, int y, uint32_t c) {
			if (x >= 0 && y >= 0 && x < XRES && y < YRES) argb[y * w + x] = c;
		};
		for (int d = 4; d >= 1; d--)
		{
			uint32_t c = d == 1 ? 0x785080C0 : 0x01000000;
			for (int x = area.pos.X - d; x <= br.X + d; x++) mark(x, area.pos.Y - d, c), mark(x, br.Y + d, c);
			for (int y = area.pos.Y - d + 1; y < br.Y + d; y++) mark(area.pos.X - d, y, c), mark(br.X + d, y, c);
		}
	}
}

// The desktop icons: where each sits on screen (grid or free placement) and its shape (the icon's own picture from the
// shell, opaque pixels solid), through the desktop's IFolderView. Positions are re-read every few frames, so dragged
// icons push sand live; shapes are cached per file and size. Empty when Explorer isn't there or icons are hidden. All
// of it runs on its own thread (fetching icon pictures takes seconds at first), so frames never wait on Explorer.
struct DeskIcon
{
	RECT r;
	std::shared_ptr<const std::vector<uint8_t>> shape; // (r.right - r.left) squared, 1 = solid; null = the whole square
};
// Null (a plain square for now) when not cached yet and this pass's budget of new pictures is spent: a desktop can
// hold hundreds of icons, and fetching them all at once takes seconds.
static std::shared_ptr<const std::vector<uint8_t>> IconShape(IShellFolder *folder, PCUITEMID_CHILD child, int size, int &budget)
{
	static std::map<std::wstring, std::shared_ptr<const std::vector<uint8_t>>> cache;
	IShellItem *item = nullptr;
	if (FAILED(SHCreateItemWithParent(nullptr, folder, child, IID_PPV_ARGS(&item)))) return nullptr;
	std::wstring key = std::to_wstring(size) + L"|";
	wchar_t *name = nullptr;
	if (SUCCEEDED(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &name))) key += name, CoTaskMemFree(name);
	if (auto it = cache.find(key); it != cache.end()) return item->Release(), it->second;
	if (budget-- <= 0) return item->Release(), nullptr;
	auto shape = std::make_shared<std::vector<uint8_t>>(size_t(size) * size, 1); // a square if the picture fails
	// The shell's own cached icon list, drawn at the desktop's size: ~1 ms an icon, where IShellItemImageFactory took ~30
	// (a full desktop sat as squares for 15 s). The desktop is the namespace root, so the child ID is an absolute one.
	SHFILEINFOW fi{};
	IImageList *images = nullptr;
	HICON icon = nullptr;
	if (SHGetFileInfoW(LPCWSTR(child), 0, &fi, sizeof(fi), SHGFI_PIDL | SHGFI_SYSICONINDEX) &&
	    SUCCEEDED(SHGetImageList(size > 48 ? SHIL_JUMBO : size > 32 ? SHIL_EXTRALARGE : SHIL_LARGE, IID_PPV_ARGS(&images))) &&
	    SUCCEEDED(images->GetIcon(fi.iIcon, ILD_TRANSPARENT, &icon)))
	{
		BITMAPINFO bi{};
		bi.bmiHeader = { sizeof(bi.bmiHeader), size, -size, 1, 32, BI_RGB };
		uint32_t *px = nullptr;
		HDC dc = CreateCompatibleDC(nullptr);
		if (HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&px, nullptr, 0))
		{
			auto old = SelectObject(dc, bmp);
			std::fill(px, px + size * size, 0u);
			DrawIconEx(dc, 0, 0, icon, size, size, 0, nullptr, DI_NORMAL);
			GdiFlush();
			bool alpha = std::any_of(px, px + size * size, [](uint32_t c) { return c >> 24; });
			for (int i = 0; i < size * size; i++) // old icons without alpha: whatever the mask let through
				(*shape)[i] = alpha ? px[i] >> 24 >= 128 : (px[i] & 0xFFFFFF) != 0;
			SelectObject(dc, old);
			DeleteObject(bmp);
		}
		DeleteDC(dc);
		DestroyIcon(icon);
	}
	if (images) images->Release();
	item->Release();
	return cache[key] = shape;
}
static std::vector<DeskIcon> ReadDesktopIcons(IFolderView2 *&view, HWND &list)
{
	std::vector<DeskIcon> icons;
	if (!view)
	{
		IShellWindows *windows = nullptr;
		if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&windows)))) return icons;
		VARIANT loc{}, root{};
		loc.vt = VT_I4, loc.lVal = CSIDL_DESKTOP;
		long found = 0;
		IDispatch *disp = nullptr;
		IServiceProvider *services = nullptr;
		IShellBrowser *browser = nullptr;
		IShellView *shellView = nullptr;
		if (windows->FindWindowSW(&loc, &root, SWC_DESKTOP, &found, SWFO_NEEDDISPATCH, &disp) == S_OK &&
		    SUCCEEDED(disp->QueryInterface(IID_PPV_ARGS(&services))) &&
		    SUCCEEDED(services->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser))) &&
		    SUCCEEDED(browser->QueryActiveShellView(&shellView)) && SUCCEEDED(shellView->QueryInterface(IID_PPV_ARGS(&view))))
		{
			HWND defView = nullptr;
			shellView->GetWindow(&defView);
			list = FindWindowExW(defView, nullptr, L"SysListView32", nullptr);
		}
		for (IUnknown *p : std::initializer_list<IUnknown *>{ shellView, browser, services, disp, windows })
			if (p) p->Release();
		if (!view) return icons;
	}
	FOLDERVIEWMODE mode;
	int size = 0;
	POINT spacing{};
	IEnumIDList *items = nullptr;
	IShellFolder *folder = nullptr;
	if (FAILED(view->GetViewModeAndIconSize(&mode, &size)) || FAILED(view->GetSpacing(&spacing)) ||
	    FAILED(view->GetFolder(IID_PPV_ARGS(&folder))) || FAILED(view->Items(SVGIO_ALLVIEW, IID_PPV_ARGS(&items))))
	{
		if (folder) folder->Release();
		view->Release(), view = nullptr; // Explorer restarted: find it again
		return icons;
	}
	// ponytail: the icon rect is a guess from its position (no per-item rect from IFolderView); ~15 ms a new shape, so a
	// full desktop (443 items here) takes ~7 s to get them all
	if (IsWindowVisible(list))
	{
		PITEMID_CHILD item;
		int budget = 64;
		while (items->Next(1, &item, nullptr) == S_OK)
		{
			POINT p;
			if (SUCCEEDED(view->GetItemPosition(item, &p)))
			{
				ClientToScreen(list, &p);
				int x = p.x, y = p.y + 6; // matched by eye against sand resting on icons
				icons.push_back({ { x, y, x + size, y + size }, IconShape(folder, item, size, budget) });
			}
			CoTaskMemFree(item);
		}
	}
	items->Release();
	folder->Release();
	return icons;
}
static std::vector<DeskIcon> DesktopIcons()
{
	static std::mutex lock;
	static std::vector<DeskIcon> latest;
	static std::atomic<int> wanted{ 0 }; // frames since the last ask; the thread idles when nobody asks
	wanted = 0;
	static bool started = [] {
		std::thread([] {
			CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
			IFolderView2 *view = nullptr;
			HWND list = nullptr;
			while (true)
			{
				if (wanted++ > 10)
				{
					Sleep(200);
					continue;
				}
				bool had = view;
				auto icons = ReadDesktopIcons(view, list);
				{
					std::lock_guard g(lock);
					latest = std::move(icons);
				}
				Sleep(view ? 8 : had ? 0 : 1000); // looking for Explorer: once a second
			}
		}).detach();
		return true;
	}();
	(void)started;
	std::lock_guard g(lock);
	return latest;
}

// Every frame: every visible window's rectangle, in sim pixels, blocks particles (Simulation::pixelWalls), so they
// pile on title bars and flow around windows. A window moving onto particles shoves each one along its move to the
// first free pixel past it, at the window's speed; one that appeared or grew lifts them out over its top. One with
// no room left stays put, hidden under the window (never deleted).
static std::map<HWND, RECT> wallRects, frameRects; // the wall windows when the sim last read them / at the frame drawn

void OverlayWindows::FrameWalls(std::vector<uint8_t> &mask, uint8_t bit)
{
	frameRects = wallRects;
	if (!&GameController::Ref()) return;
	auto &walls = GameController::Ref().GetModel()->GetSimulation()->pixelWalls;
	for (size_t i = 0; i < walls.size() && i < mask.size(); i++)
		if (walls[i]) mask[i] |= bit;
}

std::vector<OverlayWindows::WindowShift> OverlayWindows::WindowShifts()
{
	std::vector<WindowShift> out;
	RECT sr;
	GetWindowRect(mainWindow, &sr);
	int sw = sr.right - sr.left, sh = sr.bottom - sr.top;
	if (sw <= 0 || sh <= 0) return out;
	auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); }; // as TickWindowWalls
	for (auto &[hwnd, at] : frameRects)
	{
		RECT r;
		if (DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)) != S_OK) GetWindowRect(hwnd, &r);
		if (r.left == at.left && r.top == at.top) continue;
		out.push_back({ px(at.left - sr.left, WINDOWW, sw), px(at.top - sr.top, WINDOWH, sh), px(at.right - sr.left, WINDOWW, sw),
		                px(at.bottom - sr.top, WINDOWH, sh), float(double(r.left - at.left) * WINDOWW / sw),
		                float(double(r.top - at.top) * WINDOWH / sh) });
	}
	return out;
}

// The exe name (lower case) of the app owning a window
static ByteString ExeName(HWND hwnd)
{
	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!proc) return {};
	wchar_t path[MAX_PATH];
	DWORD n = MAX_PATH;
	bool ok = QueryFullProcessImageNameW(proc, 0, path, &n);
	CloseHandle(proc);
	if (!ok) return {};
	std::wstring name(path, n);
	name = name.substr(name.find_last_of(L"\\/") + 1);
	CharLowerW(name.data());
	return Platform::WinNarrow(name);
}

static std::vector<ByteString> PassApps()
{
	std::vector<ByteString> out;
	for (auto &s : GlobalPrefs::Ref().Get("Overlay.PassApps", ByteString("")).PartitionBy(';'))
		if (s.size()) out.push_back(s);
	return out;
}

static void SetPassApps(const std::vector<ByteString> &apps)
{
	ByteString s;
	for (auto &a : apps) s += (s.size() ? ";" : "") + a;
	GlobalPrefs::Ref().Set("Overlay.PassApps", s);
}

// Apps with a normal window open now, but not us
static std::vector<ByteString> RunningApps()
{
	std::vector<ByteString> out;
	EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
		BOOL cloaked = FALSE;
		DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
		DWORD pid = 0;
		GetWindowThreadProcessId(hwnd, &pid);
		if (!IsWindowVisible(hwnd) || cloaked || GetWindow(hwnd, GW_OWNER) || (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) ||
		    !GetWindowTextLengthW(hwnd) || pid == GetCurrentProcessId())
		{
			return TRUE;
		}
		auto &out = *(std::vector<ByteString> *)lp;
		auto name = ExeName(hwnd);
		if (name.size() && std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
		return TRUE;
	}, LPARAM(&out));
	std::sort(out.begin(), out.end());
	return out;
}

ByteString OverlayWindows::PassApp(void *hwnd)
{
	if (!hwnd) return {};
	auto apps = PassApps();
	if (apps.empty()) return {};
	auto name = ExeName(HWND(hwnd));
	return std::find(apps.begin(), apps.end(), name) != apps.end() ? name : ByteString();
}

static void TickWindowWalls()
{
	auto *sim = GameController::Ref().GetModel()->GetSimulation();
	// once per sim tick: frames drawn between ticks (the screen's rate is higher) have no particles to move. Every
	// other frame while paused, so windows still shove.
	static int lastTick = -1, skipped = 0;
	if (sim->currentTick == lastTick && ++skipped < 2) return;
	lastTick = sim->currentTick, skipped = 0;
	// wallRects: where each window was on the last tick
	std::vector<OverlayWall> wins;
	RECT sr;
	GetWindowRect(mainWindow, &sr);
	int sw = sr.right - sr.left, sh = sr.bottom - sr.top;
	// windows are walls only when the sim is over them; icons whenever it's over the icons
	bool doWindows = windowWalls && OverlayGetLayer() == OverlayLayer::OnTop;
	bool doIcons = iconWalls && OverlayGetLayer() != OverlayLayer::BehindIcons;
	if (overlayWindowed || (!doWindows && !doIcons) || sw <= 0 || sh <= 0)
	{
		sim->pixelWalls.clear();
		wallRects.clear();
		return;
	}
	std::map<HWND, RECT> now;
	struct Ctx { std::vector<OverlayWall> *wins; std::map<HWND, RECT> *now; RECT sr; int sw, sh; } ctx{ &wins, &now, sr, sw, sh };
	if (doWindows) EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
		auto &c = *(Ctx *)lp;
		auto ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
		BOOL cloaked = FALSE;
		DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
		wchar_t cls[32];
		GetClassNameW(hwnd, cls, 32);
		if (hwnd == mainWindow || !IsWindowVisible(hwnd) || IsIconic(hwnd) || cloaked || (ex & WS_EX_TRANSPARENT) ||
		    ((ex & WS_EX_TOOLWINDOW) && GetWindowThreadProcessId(hwnd, nullptr) != GetCurrentThreadId() &&
		     wcscmp(cls, L"Shell_TrayWnd") && wcscmp(cls, L"Shell_SecondaryTrayWnd")) || // the taskbar is a floor
		    !wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW"))
		{
			return TRUE;
		}
		RECT r; // the visible frame, without the invisible resize borders
		if (DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)) != S_OK) GetWindowRect(hwnd, &r);
		auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); }; // screen -> sim pixels
		OverlayWall w{ std::max(0, px(r.left - c.sr.left, WINDOWW, c.sw)), std::max(0, px(r.top - c.sr.top, WINDOWH, c.sh)),
		               std::min(XRES, px(r.right - c.sr.left, WINDOWW, c.sw)), std::min(YRES, px(r.bottom - c.sr.top, WINDOWH, c.sh)), 0, 0, {} };
		(*c.now)[hwnd] = r;
		if (w.x1 <= w.x0 || w.y1 <= w.y0) return TRUE;
		if (auto it = wallRects.find(hwnd); it != wallRects.end())
		{
			w.mx = px(r.left - it->second.left, WINDOWW, c.sw);
			w.my = px(r.top - it->second.top, WINDOWH, c.sh);
		}
		c.wins->push_back(w);
		return TRUE;
	}, LPARAM(&ctx));
	wallRects = std::move(now);
	auto icons = doIcons ? DesktopIcons() : std::vector<DeskIcon>{};
	if (doIcons)
		for (auto &icon : icons)
		{
			auto &r = icon.r;
			auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); };
			OverlayWall w{ std::max(0, px(r.left - sr.left, WINDOWW, sw)), std::max(0, px(r.top - sr.top, WINDOWH, sh)),
			               std::min(XRES, px(r.right - sr.left, WINDOWW, sw)), std::min(YRES, px(r.bottom - sr.top, WINDOWH, sh)), 0, 0, {} };
			if (icon.shape)
				// each sim pixel takes the icon pixel under its centre
				w.solid = [&icon, sr, sw, sh](int x, int y) {
					int n = icon.r.right - icon.r.left;
					int ix = int(sr.left + (x + 0.5) * sw / WINDOWW) - icon.r.left, iy = int(sr.top + (y + 0.5) * sh / WINDOWH) - icon.r.top;
					return ix >= 0 && iy >= 0 && ix < n && iy < n && (*icon.shape)[size_t(iy) * n + ix];
				};
			if (w.x1 > w.x0 && w.y1 > w.y0) wins.push_back(w);
		}

	OverlayApplyWalls(sim, wins);
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
	for (auto &t : torn) clear(t->hwnd);
	for (auto &host : hosts)
	{
		clear(host->hwnd);
	}
}

// Which play-area edges are under the mouse: 1 left, 2 right, 4 top, 8 bottom (4 px outside to 1 inside).
static int AreaEdges(Simulation *sim, int x, int y)
{
	auto a = sim->PlayRect();
	auto br = a.BottomRight();
	if (x < a.pos.X - 4 || x > br.X + 4 || y < a.pos.Y - 4 || y > br.Y + 4) return 0;
	return (x <= a.pos.X + 1 ? 1 : x >= br.X - 1 ? 2 : 0) | (y <= a.pos.Y + 1 ? 4 : y >= br.Y - 1 ? 8 : 0);
}

bool OverlayWindows::AreaMouse(int x, int y, int what)
{
	static int drag = 0, shown = 0;
	static Vec2<int> from{ 0, 0 };
	static Rect<int> start = CELLS.OriginRect();
	if (!&GameController::Ref() || !overlayMode) return false;
	auto *sim = GameController::Ref().GetModel()->GetSimulation();
	if (what == 1)
	{
		drag = AreaEdges(sim, x, y);
		from = { x, y };
		start = sim->playArea;
	}
	else if (what == 2)
	{
		bool was = drag;
		drag = 0;
		return was;
	}
	else if (drag)
	{
		int dx = int(std::lround(double(x - from.X) / CELL)), dy = int(std::lround(double(y - from.Y) / CELL));
		int l = start.pos.X, t = start.pos.Y, r = l + start.size.X, b = t + start.size.Y;
		if (drag & 1) l = std::clamp(l + dx, 0, r - 8);
		if (drag & 2) r = std::clamp(r + dx, l + 8, XCELLS);
		if (drag & 4) t = std::clamp(t + dy, 0, b - 8);
		if (drag & 8) b = std::clamp(b + dy, t + 8, YCELLS);
		if (RectSized(Vec2(l, t), Vec2(r - l, b - t)) != sim->playArea) SetPlayArea(l, t, r - l, b - t);
	}
	int e = drag ? drag : AreaEdges(sim, x, y);
	if (e != shown)
	{
		static SDL_Cursor *we = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE), *ns = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS),
		                  *nwse = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENWSE), *nesw = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENESW);
		SDL_SetCursor(!e ? SDL_GetDefaultCursor() : e == 5 || e == 10 ? nwse : e == 6 || e == 9 ? nesw : e & 3 ? we : ns);
		shown = e;
	}
	return drag;
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
	bool full = area == CELLS.OriginRect();
	prefs.Set("Overlay.PlayArea.W", full ? 0 : area.size.X);
	prefs.Set("Overlay.PlayArea.H", full ? 0 : area.size.Y);
}


int OverlayWindows::Form(const String &title, const String &prompt, const std::vector<String> &options, int &choice, String &text,
                         const std::vector<String> &extras, std::function<bool(int, const String &)> valid, int limit)
{
	if (!mainWindow) return -1;
	enum { idCombo = 100, idEdit, idExtra };
	struct State
	{
		std::function<bool(int, const String &)> valid;
		HWND combo = nullptr, edit, ok;
		int result = -1;
		void Check()
		{
			wchar_t buf[512];
			GetWindowTextW(edit, buf, 512);
			EnableWindow(ok, valid(int(SendMessageW(combo, CB_GETCURSEL, 0, 0)), Platform::WinNarrow(buf).FromUtf8()));
		}
	} st{ valid };
	static bool registered = false;
	if (!registered)
	{
		WNDCLASSW wc{};
		wc.lpfnWndProc = [](HWND w, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
			auto *s = (State *)GetWindowLongPtrW(w, GWLP_USERDATA);
			if (s && msg == WM_COMMAND)
			{
				int id = LOWORD(wp), code = HIWORD(wp);
				if ((id == idCombo && code == CBN_SELCHANGE) || (id == idEdit && code == EN_CHANGE)) s->Check();
				else if (id == IDOK && IsWindowEnabled(s->ok)) s->result = 1;
				else if (id == IDCANCEL) s->result = 0;
				else if (id >= idExtra) s->result = 2 + id - idExtra;
				return 0;
			}
			if (s && msg == WM_CLOSE) return s->result = 0, 0;
			return DefWindowProcW(w, msg, wp, lp);
		};
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
		wc.lpszClassName = L"TPTOverlayForm";
		RegisterClassW(&wc);
		registered = true;
	}
	HWND owner = palette && IsWindowVisible(palette->hwnd) ? palette->hwnd : mainWindow;
	HDC screen = GetDC(nullptr);
	int dpi = GetDeviceCaps(screen, LOGPIXELSY); // the system DPI, which SystemParametersInfo's font is sized for
	ReleaseDC(nullptr, screen);
	auto D = [dpi](int v) { return MulDiv(v, dpi, 96); };
	NONCLIENTMETRICSW ncm{ sizeof(ncm) };
	SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
	HFONT font = CreateFontIndirectW(&ncm.lfMessageFont);

	int dy = options.empty() ? 32 : 0; // no drop-down: everything moves up
	int cw = D(340), ch = D(146 - dy);
	RECT wr{ 0, 0, cw, ch };
	DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU, ex = WS_EX_DLGMODALFRAME | WS_EX_TOPMOST;
	AdjustWindowRectEx(&wr, style, FALSE, ex);
	RECT or_;
	GetWindowRect(owner, &or_);
	int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
	HWND dlg = CreateWindowExW(ex, L"TPTOverlayForm", Wide(title).c_str(), style, (or_.left + or_.right - ww) / 2,
	                           (or_.top + or_.bottom - wh) / 2, ww, wh, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
	auto child = [&](const wchar_t *cls, const wchar_t *txt, DWORD st, int x, int y, int w, int hh, int id) {
		HWND c = CreateWindowExW(cls == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, txt, WS_CHILD | WS_VISIBLE | st,
		                         D(x), D(y), D(w), D(hh), dlg, HMENU(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
		SendMessageW(c, WM_SETFONT, WPARAM(font), TRUE);
		return c;
	};
	child(L"STATIC", Wide(prompt).c_str(), 0, 12, 12, 316, 18, -1);
	if (!options.empty())
	{
		st.combo = child(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 12, 36, 160, 300, idCombo);
		for (auto &o : options) SendMessageW(st.combo, CB_ADDSTRING, 0, LPARAM(Wide(o).c_str()));
		SendMessageW(st.combo, CB_SETCURSEL, WPARAM(choice), 0);
	}
	st.edit = child(L"EDIT", Wide(text).c_str(), ES_AUTOHSCROLL | WS_TABSTOP, 12, 68 - dy, 316, 24, idEdit);
	if (limit) SendMessageW(st.edit, EM_SETLIMITTEXT, WPARAM(limit), 0);
	int bx = 12;
	for (int i = 0; i < int(extras.size()); i++, bx += 81) child(L"BUTTON", Wide(extras[i]).c_str(), BS_PUSHBUTTON | WS_TABSTOP, bx, 108 - dy, 75, 26, idExtra + i);
	st.ok = child(L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, 172, 108 - dy, 75, 26, IDOK);
	child(L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, 253, 108 - dy, 75, 26, IDCANCEL);
	SetWindowLongPtrW(dlg, GWLP_USERDATA, LONG_PTR(&st));
	st.Check();
	st.result = -1;

	// ponytail: a modal loop on TPT's thread, so the sim waits while it's open (as with the menus)
	EnableWindow(owner, FALSE);
	ShowWindow(dlg, SW_SHOW);
	SetForegroundWindow(dlg);
	SetFocus(st.edit);
	SendMessageW(st.edit, EM_SETSEL, 0, -1);
	MSG msg;
	while (st.result < 0 && GetMessageW(&msg, nullptr, 0, 0))
	{
		if (IsDialogMessageW(dlg, &msg)) continue;
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	if (msg.message == WM_QUIT) PostQuitMessage(int(msg.wParam));
	wchar_t buf[512];
	GetWindowTextW(st.edit, buf, 512);
	text = Platform::WinNarrow(buf).FromUtf8();
	if (st.combo) choice = int(SendMessageW(st.combo, CB_GETCURSEL, 0, 0));
	EnableWindow(owner, TRUE);
	DestroyWindow(dlg);
	DeleteObject(font);
	SetForegroundWindow(owner);
	return std::max(0, st.result);
}

// The Lua console: a read-only log over a command line. Enter runs (TPT's ! commands too; an unfinished block
// shows "..." and waits for the rest), Up/Down walk the history, Esc hides it.
namespace
{
	struct Console
	{
		HWND wnd = nullptr, out = nullptr, in = nullptr;
		HFONT font = nullptr;
		WNDPROC editProc = nullptr;
		std::vector<std::wstring> history;
		size_t at = 0;

		void Append(const std::wstring &s)
		{
			std::wstring t;
			for (auto c : s) // the edit control wants \r\n
			{
				if (c == L'\n') t += L'\r';
				t += c;
			}
			int n = GetWindowTextLengthW(out);
			SendMessageW(out, EM_SETSEL, n, n);
			SendMessageW(out, EM_REPLACESEL, FALSE, LPARAM((t + L"\r\n").c_str()));
		}

		void Run()
		{
			int n = GetWindowTextLengthW(in);
			std::wstring cmd(size_t(n), L'\0');
			GetWindowTextW(in, cmd.data(), n + 1);
			SetWindowTextW(in, L"");
			if (cmd.empty()) return;
			if (history.empty() || history.back() != cmd) history.push_back(cmd);
			at = history.size();
			Append(L"> " + cmd);
			auto *ci = GameController::Ref().GetCommandInterface();
			int code = ci->Command(Platform::WinNarrow(cmd).FromUtf8());
			auto result = Wide(ci->GetLastError());
			if (result.size()) Append(code && result != L"..." ? L"Error: " + result : result); // "...": waiting for the rest of a block
		}

		static LRESULT CALLBACK InputProc(HWND w, UINT msg, WPARAM wp, LPARAM lp);
		static LRESULT CALLBACK Proc(HWND w, UINT msg, WPARAM wp, LPARAM lp);

		void Layout()
		{
			RECT rc;
			GetClientRect(wnd, &rc);
			TEXTMETRICW tm;
			HDC dc = GetDC(in);
			auto old = SelectObject(dc, font);
			GetTextMetricsW(dc, &tm);
			SelectObject(dc, old);
			ReleaseDC(in, dc);
			int ih = tm.tmHeight + 10;
			MoveWindow(out, 0, 0, rc.right, rc.bottom - ih, TRUE);
			MoveWindow(in, 0, rc.bottom - ih, rc.right, ih, TRUE);
		}

		void Show()
		{
			if (!wnd)
			{
				WNDCLASSW wc{};
				wc.lpfnWndProc = Proc;
				wc.hInstance = GetModuleHandleW(nullptr);
				wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
				wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
				wc.lpszClassName = L"TPTOverlayConsole";
				wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
				RegisterClassW(&wc);
				HDC screen = GetDC(nullptr);
				int dpi = GetDeviceCaps(screen, LOGPIXELSY);
				ReleaseDC(nullptr, screen);
				font = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
				                   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
				RECT work;
				SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
				int w = MulDiv(760, dpi, 96), h = MulDiv(420, dpi, 96);
				wnd = CreateWindowExW(WS_EX_APPWINDOW, L"TPTOverlayConsole", L"Lua console - The Powder Toy", WS_OVERLAPPEDWINDOW,
				                      (work.left + work.right - w) / 2, work.bottom - h - MulDiv(40, dpi, 96), w, h,
				                      nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
				out = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
				                      0, 0, 0, 0, wnd, nullptr, GetModuleHandleW(nullptr), nullptr);
				in = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, wnd, nullptr,
				                     GetModuleHandleW(nullptr), nullptr);
				SendMessageW(out, WM_SETFONT, WPARAM(font), FALSE);
				SendMessageW(in, WM_SETFONT, WPARAM(font), FALSE);
				SendMessageW(out, EM_SETLIMITTEXT, 0, 0); // no 32k cap
				SendMessageW(in, EM_SETCUEBANNER, TRUE, LPARAM(L"Lua, or !command; Enter runs, Up/Down for history, Esc hides"));
				editProc = WNDPROC(SetWindowLongPtrW(in, GWLP_WNDPROC, LONG_PTR(InputProc)));
				Append(L"The Powder Toy's Lua console. Try: tpt.version.major, sim.partCount(), or !set type dust sand");
				Layout();
			}
			ShowWindow(wnd, IsIconic(wnd) ? SW_RESTORE : SW_SHOW);
			SetForegroundWindow(wnd);
			SetFocus(in);
		}
	} console;

	LRESULT CALLBACK Console::InputProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
	{
		if (msg == WM_KEYDOWN && wp == VK_RETURN) return console.Run(), 0;
		if (msg == WM_KEYDOWN && wp == VK_ESCAPE) return ShowWindow(console.wnd, SW_HIDE), 0;
		if (msg == WM_KEYDOWN && (wp == VK_UP || wp == VK_DOWN) && !console.history.empty())
		{
			auto &c = console;
			c.at = wp == VK_UP ? (c.at ? c.at - 1 : 0) : std::min(c.at + 1, c.history.size());
			SetWindowTextW(w, c.at < c.history.size() ? c.history[c.at].c_str() : L"");
			SendMessageW(w, EM_SETSEL, GetWindowTextLengthW(w), GetWindowTextLengthW(w));
			return 0;
		}
		if (msg == WM_CHAR && (wp == L'\r' || wp == 27)) return 0; // no beep
		return CallWindowProcW(console.editProc, w, msg, wp, lp);
	}

	LRESULT CALLBACK Console::Proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
	{
		switch (msg)
		{
		case WM_SIZE: if (console.out) console.Layout(); return 0;
		case WM_SETFOCUS: if (console.in) SetFocus(console.in); return 0;
		case WM_CLOSE: ShowWindow(w, SW_HIDE); return 0;
		case WM_CTLCOLORSTATIC: // the read-only log on a white background, like a normal field
			SetBkColor(HDC(wp), GetSysColor(COLOR_WINDOW));
			return LRESULT(GetSysColorBrush(COLOR_WINDOW));
		}
		return DefWindowProcW(w, msg, wp, lp);
	}
}

bool OverlayWindows::ShowConsole()
{
	if (!mainWindow) return false;
	console.Show();
	return true;
}

void OverlayWindows::HideConsole()
{
	if (console.wnd) ShowWindow(console.wnd, SW_HIDE);
}
#else
void OverlayWindows::Init(void *) {}
#ifdef __APPLE__
#include "Overlay.h"
#include "OverlayMac.h"
#include "gui/game/GameController.h"
#include "gui/game/GameModel.h"
#include "simulation/Simulation.h"
#include "SimulationConfig.h"
#include "prefs/GlobalPrefs.h"
#include <SDL.h>
#include <cmath>
#include <map>

// Other apps' windows are walls, as on Windows (TickWindowWalls there)
void OverlayWindows::Tick()
{
	if (!&GameController::Ref()) return;
	auto *sim = GameController::Ref().GetModel()->GetSimulation();
	static int lastTick = -1, skipped = 0;
	if (sim->currentTick == lastTick && ++skipped < 2) return;
	lastTick = sim->currentTick, skipped = 0;
	static std::map<uint32_t, OverlayMac::Rect> last; // where each window was on the last tick
	std::map<uint32_t, OverlayMac::Rect> now;
	std::vector<OverlayWall> walls;
	auto me = OverlayMac::Window();
	double sw = me.r - me.l, sh = me.b - me.t;
	if (sw > 0 && sh > 0 && GlobalPrefs::Ref().Get("Overlay.WindowWalls", true))
		for (auto &r : OverlayMac::OtherWindows())
		{
			auto px = [](double v, int n, double nw) { return int(std::lround(v * n / nw)); }; // screen -> sim pixels
			OverlayWall w{ std::max(0, px(r.l - me.l, WINDOWW, sw)), std::max(0, px(r.t - me.t, WINDOWH, sh)),
			               std::min(XRES, px(r.r - me.l, WINDOWW, sw)), std::min(YRES, px(r.b - me.t, WINDOWH, sh)), 0, 0, {} };
			now[r.id] = r;
			if (w.x1 <= w.x0 || w.y1 <= w.y0) continue;
			if (auto it = last.find(r.id); it != last.end())
			{
				w.mx = px(r.l - it->second.l, WINDOWW, sw);
				w.my = px(r.t - it->second.t, WINDOWH, sh);
			}
			walls.push_back(w);
		}
	last = std::move(now);
	OverlayApplyWalls(sim, walls);
}

void OverlayMacMenu(int item)
{
	auto &gc = GameController::Ref();
	if (item < int(OverlayInput::Count)) OverlaySetInput(OverlayInput(item));
	if (item == 10) gc.SetPaused(!gc.GetPaused());
	if (item == 11) gc.ClearSim();
	if (item == 99)
	{
		SDL_Event quit{};
		quit.type = SDL_QUIT;
		SDL_PushEvent(&quit);
	}
}

bool OverlayMacChecked(int item)
{
	return item < int(OverlayInput::Count) ? int(OverlayGetInput()) == item : item == 10 && GameController::Ref().GetPaused();
}
#else
void OverlayWindows::Tick() {}
#endif
void OverlayWindows::TogglePalette() {}
void OverlayWindows::SetPassThrough(bool) {}
void OverlayWindows::Notify(const char *) {}
bool OverlayWindows::HideTPTMenus() { return false; }
void OverlayWindows::ApplySimMasks(uint32_t *, int, const uint8_t *) {}
void OverlayWindows::FrameWalls(std::vector<uint8_t> &, uint8_t) {}
std::vector<OverlayWindows::WindowShift> OverlayWindows::WindowShifts() { return {}; }
void OverlayWindows::ClearUnderWindows(uint32_t *, int, int, int, int) {}
void OverlayWindows::SetPlayArea(int, int, int, int) {}
bool OverlayWindows::AreaMouse(int, int, int) { return false; }
int OverlayWindows::Form(const String &, const String &, const std::vector<String> &, int &, String &, const std::vector<String> &,
                         std::function<bool(int, const String &)>, int) { return -1; }
bool OverlayWindows::ShowConsole() { return false; }
void OverlayWindows::HideConsole() {}
#endif
