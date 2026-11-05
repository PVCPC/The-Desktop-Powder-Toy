// Part of OverlayWindows.cpp (included inside OverlayWindows.cpp's anonymous namespace): TPT's own windows and menus hosted in app windows
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

	// The look's font: the UI text is remade in it (TempleOS's is embedded, registered for this process on first use).
	void UseThemeFont()
	{
		text->darkPage = ((theme->page >> 16 & 0xFF) + (theme->page >> 8 & 0xFF) + (theme->page & 0xFF)) < 3 * 0x60;
#ifdef _WIN32
		static bool registered = false;
		if (!registered && !wcscmp(theme->font, L"TempleOS"))
		{
			DWORD n = 0;
			auto ttf = templeos_font_ttf.AsCharSpan();
			AddFontMemResourceEx(const_cast<char *>(ttf.data()), DWORD(ttf.size()), nullptr, &n);
			registered = true;
		}
#endif // ponytail: Mac doesn't register it, and its Segoe UI stand-in shows
		if (text && (wcscmp(text->face, theme->font) || text->faceScale != theme->fontScale))
			text->face = theme->font, text->faceScale = theme->fontScale, text->SetScale();
	}

	void ThemeChanged()
	{
		UseThemeFont();
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
#ifdef __APPLE__
		// not an NSMenu: those hold the main thread, and with it the sim, until they close
		{
			auto *m = menu.get();
			OverlayMac::PopupList(items, at.x / MacScale(), at.y / MacScale(), [m](int picked) { m->picked = picked; });
			return;
		}
#endif
#ifdef _WIN32
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
#endif
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
#ifdef __APPLE__
				int dialogScale = engine.GetScale() * int(std::lround(MacScale())); // TPT's scale is in points, these windows are in pixels
#else
				int dialogScale = engine.GetScale();
#endif
				host->scale = std::max(1, std::min({ dialogScale, int(work.right - work.left - chromeW) / std::max(1, hosted.size.X),
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

