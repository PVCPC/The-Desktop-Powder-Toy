// Part of OverlayWindows.cpp (included inside OverlayWindows.cpp's anonymous namespace): drawing: the canvas, text, caption glyphs, the desktop capture
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
		const wchar_t *face = L"Segoe UI"; // the look's (Theme::font, at Theme::fontScale)
		float faceScale = 1;
		bool darkPage = false; // the look's page is dark: black and grey text is drawn light instead

		void SetScale()
		{
			if (font) DeleteObject(font);
			if (bold) DeleteObject(bold);
			for (auto &[key, f] : styled) DeleteObject(f);
			styled.clear();
			font = CreateFontW(-S(12 * faceScale), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, face);
			bold = CreateFontW(-S(12 * faceScale), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, face);
		}

		// A look's own font (TempleOS's) lacks some of the UI's symbols: plain stand-ins for those.
		std::wstring Plain(const std::wstring &in) const
		{
			if (!wcscmp(face, L"Segoe UI")) return in;
			std::wstring out;
			for (auto c : in)
				out += c == L'…' ? L"..." : c == L'·' ? L"-" : c == L'□' ? L"#" : c == L'—' ? L"--" : std::wstring(1, c);
			return out;
		}

		SIZE Measure(const std::wstring &in, bool isBold = false)
		{
			auto s = Plain(in);
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
				f = CreateFontW(st.px ? -st.px : -S(st.size * faceScale), 0, 0, 0, st.weight, st.italic, st.underline, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, face);
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
		void Draw(Canvas &cv, int x, int y, const std::wstring &in, uint32_t rgb, bool isBold = false, int maxW = 1 << 20, int glow = 0)
		{
			auto s = Plain(in);
			if (int r = rgb >> 16 & 0xFF, g = rgb >> 8 & 0xFF, b = rgb & 0xFF; darkPage && std::max({ r, g, b }) < 0x90 && std::max({ r, g, b }) - std::min({ r, g, b }) < 24)
				rgb = 0xFFFFFF - rgb; // ponytail: assumes it sits on the page; text on its own light box would need its own colour
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
#ifdef _WIN32
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

#else
	// The Mac's: what's on screen under one window, at half size, at most 30 times a second (a capture is a read from the
	// window server, no GPU copy to chain). Needs Screen Recording; without it the glass has nothing behind it to bend.
	struct DesktopGrab
	{
		HWND window = nullptr;
		double last = 0;
		bool have = false;

		bool Grab(int x, int y, int bw, int bh, uint32_t *dst)
		{
			double now = OverlayMac::Now(), s = MacScale();
			if (have && now - last < 1 / 30.0) return true;
			last = now;
			have = OverlayMac::GrabBelow(MacWindowNumber(window), x / s, y / s, bw / s, bh / s, bw / 2, bh / 2, dst);
			return have;
		}
	};

#endif
	// An OS look (pref Overlay.Theme). The theme draws the frame, caption buttons, push buttons and popups; tabs,
	// list items, check boxes, group boxes and the status bar are drawn here in its colours. Sizes are unscaled.
	struct Theme
	{
		const wchar_t *name = L"";
		const wchar_t *colour = nullptr; // one of its generation's colour schemes (themeColours), or none
		const wchar_t *font = L"Segoe UI";
		float fontScale = 1;
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
		// A check box (round: radio button) b square at x, y; false: drawn here in the theme's colours.
		virtual bool CheckBox(Canvas &cv, int x, int y, int b, bool on, bool round, bool hot) { (void)cv, (void)x, (void)y, (void)b, (void)on, (void)round, (void)hot; return false; }
		// A group box's outline; false: drawn here in groupEdge.
		virtual bool GroupFrame(Canvas &cv, int x, int y, int w, int h) { (void)cv, (void)x, (void)y, (void)w, (void)h; return false; }
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

