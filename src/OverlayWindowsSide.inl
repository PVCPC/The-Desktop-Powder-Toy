// Part of OverlayWindows.cpp (included inside OverlayWindows.cpp's anonymous namespace): the side window (SideWindow): every tab
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
			bool drag = false; // left also runs while the button is held and the mouse moves (sliders)
		};
		std::vector<Hit> hits;
		int hover = -1;
		int tab = 0, section = 0, scroll = 0;
		// Smooth wheel scrolling: where scroll is gliding to (-1: still), and where the glide last put it (anything else
		// moving scroll, a tab change say, stops the glide)
		int scrollGoal = -1, scrollWas = 0;
		void Wheel(int step, int maxScroll)
		{
			scrollGoal = std::clamp((scrollGoal >= 0 ? scrollGoal : scroll) - step, 0, std::max(0, maxScroll));
			scrollWas = scroll;
		}
		void Glide()
		{
			if (scrollGoal < 0) return;
			if (scroll != scrollWas) { scrollGoal = -1; return; }
			int d = scrollGoal - scroll;
			scroll += d / 3 ? d / 3 : d; // a third of the way each frame
			scrollWas = scroll;
			if (scroll == scrollGoal) scrollGoal = -1;
			dirty = true;
			overlayMenuGliding = true;
		}
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
		static constexpr const wchar_t *tabNames[] = { L"Elements", L"Tools", L"Simulation", L"Display", L"Saves", L"Overlay", L"Fun", L"Welcome", L"Buddy", L"TPT" };
		static constexpr int toolsTab = 1, welcomeTab = 7, buddyTab = 8, tptTab = 9; // Buddy: only once his file is chosen
		static constexpr int tabOrder[] = { 0, 1, 2, 3, 4, tptTab, 5, 6, buddyTab, welcomeTab }; // as shown (Buddy next to Fun, TPT's own settings by Saves)

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
			if (!theme->CheckBox(cv, x, by, b, on, round, hot))
			{
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
			}
			text->Draw(cv, x + b + S(6), y + (S(20) - text->Measure(label).cy) / 2, label, theme->ink);
			Add(rc, std::move(action), tip);
			return w;
		}

		// A group box: an outline with a heading; returns the top of its inside.
		int Group(int x, int y, int w, int h, const std::wstring &title)
		{
			if (!theme->GroupFrame(cv, x, y + S(8), w, h - S(8))) cv.RoundFrame(x, y + S(8), w, h - S(8), S(float(std::min(theme->radius, 6))), theme->groupEdge, 255);
			auto size = text->Measure(title);
			cv.RoundRect(x + S(8), y, size.cx + S(8), size.cy, 0, theme->page, theme->page, 255);
			text->Draw(cv, x + S(12), y, title, theme->heading);
			return y + size.cy + S(4);
		}

		// A group box whose heading folds it: a small triangle before the title, pointing right when folded
		void FoldGroup(int x, int y, int w, int h, const std::wstring &title, bool folded)
		{
			Group(x, y, w, h, L"    " + title);
			int k = S(4), cx = x + S(16), cy = y + text->Measure(title).cy / 2;
			for (int i = 0; i <= k; i++)
				for (int j = -(k - i); j <= k - i; j++)
					cv.Blend(folded ? cx - k / 2 + i : cx + j, folded ? cy + j : cy - k / 2 + i, theme->heading, 255);
		}

		void Caption(int x, int y, const std::wstring &s, uint32_t rgb = 0x000000)
		{
			text->Draw(cv, x, y + (S(20) - text->Measure(s).cy) / 2, s, rgb);
		}

		// A track with a knob for v in [lo, hi]; a click on the track puts it there.
		void Slider(int x, int y, int w, float v, float lo, float hi, std::function<void(float)> set, const std::wstring &tip = L"")
		{
			RECT rc{ x, y, x + w, y + S(20) };
			bool hot = Hovered(rc);
			int kw = S(9), mid = y + S(10), tx = x + int((w - kw) * std::clamp((v - lo) / (hi - lo), 0.f, 1.f));
			cv.RoundRect(x, mid - S(2), w, S(4), S(2), 0xE7EAEA, 0xE7EAEA, 255);
			cv.RoundFrame(x, mid - S(2), w, S(4), S(2), 0xB0B0B0, 255);
			cv.RoundRect(x, mid - S(2), tx - x + kw / 2, S(4), S(2), theme->boxOn ? theme->boxOn : 0x3C7FB1, theme->boxOn ? theme->boxOn : 0x3C7FB1, 255);
			cv.RoundRect(tx, y + S(2), kw, S(16), S(2), hot ? 0xEAF6FD : 0xF2F2F2, hot ? 0xA7D9F5 : 0xD5D5D5, 255);
			cv.RoundFrame(tx, y + S(2), kw, S(16), S(2), hot ? 0x3C7FB1 : 0x707070, 255);
			Add(rc, [this, x, w, kw, lo, hi, set] {
				POINT p;
				GetCursorPos(&p);
				ScreenToClient(hwnd, &p);
				set(lo + (hi - lo) * std::clamp(float(p.x - x - kw / 2) / (w - kw), 0.f, 1.f));
			}, tip);
			hits.back().drag = true;
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
			Toggle(x + w - S(130), top, L"Show its edge", areaFrame, false, [] {
				areaFrame = !areaFrame;
				GlobalPrefs::Ref().Set("Overlay.AreaFrame", areaFrame);
			}, L"A faint blue line around the play area when it's smaller than the screen (its edge can be dragged either way)");
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

			top = Group(x, y, w, S(88), L"Running");
			bool paused = gc.GetPaused();
			int bw4 = (w - S(24) - 3 * S(6)) / 4;
			PushButton(x + S(12), top, bw4, paused ? L"Resume" : L"Pause", [] { auto &g = GameController::Ref(); g.SetPaused(!g.GetPaused()); }, L"Space", paused);
			PushButton(x + S(12) + bw4 + S(6), top, bw4, L"Step", [] { GameController::Ref().FrameStep(); }, L"One frame (F)");
			PushButton(x + S(12) + 2 * (bw4 + S(6)), top, bw4, L"Reload", [] { GameController::Ref().ReloadSim(); }, L"Back to the last loaded save (F5)");
			PushButton(x + S(12) + 3 * (bw4 + S(6)), top, bw4, L"Clear", [] { GameController::Ref().ClearSim(); }, L"Empty the sim");
			Toggle(x + S(12), top + S(36), L"Multicore physics", model->GetSimulation()->parallelUpdate, false, [] {
				auto *sim = GameController::Ref().GetModel()->GetSimulation();
				sim->parallelUpdate = !sim->parallelUpdate;
				GlobalPrefs::Ref().Set("Simulation.Multicore", sim->parallelUpdate);
			}, L"Most elements update on all CPU cores: several times faster in big scenes. Off: one core, TPT's exact order");
			Toggle(x + w / 2, top + S(36), L"Multicore drawing", overlayFastDraw, false, [] {
				overlayFastDraw = !overlayFastDraw;
				GlobalPrefs::Ref().Set("Overlay.FastDraw", overlayFastDraw);
			}, L"Makes each frame on all CPU cores and skips the empty parts of the screen: the window and icon walls, and TPT's fire glow. "
			   L"The same picture and the same sim, just faster, most of all at 1\u00D7 pixel size with little in it. Off: one core, as before");
			y += S(96);

			top = Group(x, y, w, S(52), L"Edges");
			const wchar_t *edges[] = { L"Void", L"Solid", L"Loop" };
			const wchar_t *edgeTips[] = { L"Particles and air leave at the edges", L"The edges are walls", L"Particles wrap around (whole screen only for now)" };
			int ex = x + S(12);
			for (int i = 0; i < 3; i++)
			{
				ex += Toggle(ex, top, edges[i], gc.GetEdgeMode() == i, true, [i] { GameController::Ref().SetEdgeMode(i); }, edgeTips[i]) + S(18);
			}
			y += S(60);

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
		// The Fun tab: what the sim does with the desktop, by topic, each setting with its description under it.
		// (and the Buddy tab, the same way)
		void PaintFun(FunRow *rows, size_t n)
		{
			auto c = Content();
			int x = c.left + S(10), w = c.right - c.left - S(20), y = c.top + S(8) - scroll;
			const Text::Style note{ 11, FW_NORMAL, false, false, 0x6D6D6D };
			for (size_t i = 0; i < n;)
			{
				const wchar_t *title = rows[i++].name;
				// a click on a heading folds its rows away (remembered; all but the tab's first start folded)
				ByteString pref = "Overlay.Folded.";
				for (auto *t = title; *t; t++) pref.push_back(char(*t));
				bool folded = GlobalPrefs::Ref().Get(pref, i > 1);
				int top = y + text->Measure(title).cy + S(4), ry = top;
				Add(RECT{ x, y, x + w, top }, [pref, folded] { GlobalPrefs::Ref().Set(pref, !folded); }, folded ? L"Show this section" : L"Hide this section");
				if (folded)
				{
					while (i < n && rows[i].tip) i++;
					FoldGroup(x, y, w, top - y + S(6), title, true);
					y = top + S(12);
					continue;
				}
				for (; i < n && rows[i].tip; i++)
				{
					auto &f = rows[i];
					int nx = x + S(12);
					if (f.b)
					{
						Toggle(nx, ry, f.name, *f.b, false, [&f] {
							*f.b = !*f.b;
							GlobalPrefs::Ref().Set(f.pref, *f.b);
						}, f.tip);
						if (f.b == &fun.buddy)
						{
							auto file = GlobalPrefs::Ref().Get("Overlay.Fun.BuddySwf", ByteString(""));
							PushButton(x + w - S(132), ry - S(3), S(120), L"Choose SWF...", [] { ChooseBuddySwf(); }, file.empty() ? L"No file chosen yet" : Wide(file.FromUtf8()));
						}
						nx += S(19); // under the label, past the box
					}
					else if (f.v)
					{
						FunSliderRow(f, x, w, ry);
					}
					else if (f.kind == 1)
					{
						Caption(nx, ry, f.name);
						const wchar_t *kinds[] = { L"Walk through", L"Active window", L"Solid" };
						int kx = x + S(160);
						for (int k = 0; k < 3; k++)
							kx += Toggle(kx, ry, kinds[k], fun.buddyWindows == k, true, [k] {
								fun.buddyWindows = k;
								GlobalPrefs::Ref().Set("Overlay.Buddy.Windows", k);
							}) + S(18);
					}
					else if (f.kind == 2)
					{
						Caption(nx, ry, f.name);
						const wchar_t *menus[] = { L"File", L"Skins", L"Items", L"Modes", L"Settings", L"Help" };
						int bw = (w - S(172)) / 6;
						for (int k = 0; k < 6; k++)
							PushButton(x + S(160) + k * bw, ry - S(3), bw - S(4), menus[k], [this, k] {
								if (!OverlayBuddy::GameMenu(k, hwnd)) OverlayWindows::Notify("Turn Interactive Buddy on first");
							}, L"");
					}
					else if (f.kind == 3)
					{
						Caption(nx, ry, f.name);
						PushButton(x + S(160), ry - S(3), S(220), L"Import from the desktop app", [] {
							OverlayWindows::Notify(OverlayBuddy::ImportSave() ? "Buddy has your desktop app's save now" : "No desktop Interactive Buddy save found");
						}, L"");
					}
					else
					{
						Caption(nx, ry, f.name);
						const wchar_t *kinds[] = { L"By season", L"Snow", L"Rain", L"Leaves", L"Hail", L"Fog" };
						int kx = x + S(160);
						for (int k = 0; k < 6; k++)
						{
							if (k == 4) kx = x + S(160), ry += S(26); // second row
							kx += Toggle(kx, ry, kinds[k], fun.weatherKind == k, true, [k] {
								fun.weatherKind = k;
								GlobalPrefs::Ref().Set("Overlay.Fun.WeatherKind", k);
							}) + S(18);
						}
					}
					ry = Para(nx, ry + S(21), x + w - S(12), f.tip, note) + S(8);
				}
				FoldGroup(x, y, w, ry - y, title, false);
				y = ry + S(10);
			}
			Add(RECT{ x, y, x + 1, y + 1 }, nullptr); // lets the page scroll to the last description
		}

		// A Fun slider: name, track and value ("off" at the far left where that is off)
		void FunSliderRow(FunRow &f, int x, int w, int ry)
		{
			Caption(x + S(12), ry, f.name);
			wchar_t val[16];
			if (f.unit == 1) swprintf(val, 16, *f.v >= 1 ? L"%d" : L"off", int(*f.v));
			else if (f.unit == 2) swprintf(val, 16, *f.v >= 1 ? L"%d min" : L"off", int(*f.v));
			else if (f.unit == 3) swprintf(val, 16, L"%d s", int(*f.v));
			else if (f.unit == 4) swprintf(val, 16, *f.v >= 1 ? L"%d:00" : L"off", int(*f.v) % 24);
			else if (f.unit == 5) swprintf(val, 16, *f.v >= 1 ? L"%d px" : L"off", int(*f.v));
			else if (f.unit == 6) swprintf(val, 16, *f.v >= 1 ? L"%d s" : L"off", int(*f.v));
			else if (f.unit == 7) swprintf(val, 16, *f.v >= 1 ? L"%d / h" : L"off", int(*f.v));
			else if (f.unit == 8) swprintf(val, 16, L"%d%%", int(*f.v));
			else if (f.unit == 9) swprintf(val, 16, *f.v >= 1 ? L"%d" : L"no limit", int(*f.v));
			else swprintf(val, 16, *f.v > 0 ? L"%d%%" : L"off", int(std::lround(*f.v * 100)));
			Caption(x + w - S(60), ry, val);
			Slider(x + S(160), ry, w - S(232), *f.v, f.lo, f.hi, [&f](float v) {
				*f.v = f.unit == 3 ? std::round(v / 10) * 10 : f.unit ? std::round(v) : std::round(v * 20) / 20;
				GlobalPrefs::Ref().Set(f.pref, *f.v);
			}, f.tip);
		}

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
			PushButton(x + S(12), top, bw, L"Options\u2026", [this] { if (fixedTab < 0) tab = tptTab, scroll = 0; else GameController::Ref().OpenOptions(); }, L"TPT's own settings (the TPT tab)");
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
		// Words wrapped to [x, right); returns the y after the last line.
		int Para(int x, int y, int right, const std::wstring &s, const Text::Style &st)
		{
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
		}

		void PaintWelcome()
		{
			using Style = Text::Style;
			auto c = Content();
			int pw = c.right - c.left, ph = S(1800); // (room for the Buddy paragraph)
			Canvas page;
			page.Resize(pw, ph);
			page.RoundRect(0, 0, pw, ph, 0, theme->page, theme->page, 255); // in the look's page, heading and ink colours
			std::swap(cv, page);
			struct Link { RECT rc; std::function<void()> go; std::wstring tip; };
			std::vector<Link> links;

			int x0 = S(18), x1 = pw - S(18), y = S(14);
			const Style title{ 22, FW_LIGHT, false, false, theme->heading }, subtitle{ 12, FW_NORMAL, true, false, 0x6D6D6D },
			            link{ 12, FW_NORMAL, false, true, 0x0066CC }, body{ 12, FW_NORMAL, false, false, theme->ink },
			            note{ 11, FW_NORMAL, true, false, 0x6D6D6D }, footer{ 10, FW_NORMAL, false, false, 0x8A8A8A };

			// The top of the page as each family greeted you: 95's Welcome dialog, XP and Vista's Welcome Center, 7 to 11's
			// Getting Started, the old Macs' small plain window, the modern Macs' icon, heading and Continue.
			int gen = Generation(int(std::find(std::begin(themes), std::end(themes), theme) - std::begin(themes)));
			enum { Classic, Center, Started, OldMac, NewMac } family = gen <= 2 ? Classic : gen <= 4 ? Center
			                                                         : gen == 9 || gen == 12 || gen == 13 ? OldMac
			                                                         : gen == 10 || gen >= 14 ? NewMac : Started;
			const std::wstring version = L"Desktop " DESKTOP_VERSION L"  ·  The Powder Toy " + std::to_wstring(DISPLAY_VERSION[0]) + L"." +
			                             std::to_wstring(DISPLAY_VERSION[1]);
			std::vector<std::pair<const wchar_t *, std::function<void()>>> sites;
			for (auto [label, url] : { std::pair{ L"powdertoy.co.uk", L"https://powdertoy.co.uk" },
			                           std::pair{ L"Discord", L"https://tpt.io/discord" },
			                           std::pair{ L"Create an account", L"https://powdertoy.co.uk/Register.html" } })
				sites.push_back({ label, [url = std::wstring(url)] { ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL); } });
			auto centred = [&](const std::wstring &s, const Style &st) {
				auto size = text->Measure(s, st);
				text->Draw(cv, (pw - int(size.cx)) / 2, y, s, st);
				y += size.cy;
			};
			// the sites as links in a row, from x (or centred: x < 0)
			auto siteLinks = [&](int x) {
				if (x < 0)
				{
					x = -S(18);
					for (auto &site : sites) x += int(text->Measure(site.first, link).cx) + S(18);
					x = (pw - x) / 2;
				}
				for (auto &[label, go] : sites)
				{
					auto size = text->Measure(label, link);
					text->Draw(cv, x, y, label, link);
					links.push_back({ RECT{ x, y, x + int(size.cx), y + int(size.cy) }, go, label });
					x += size.cx + S(18);
				}
				y += S(30);
			};
			// a row of the look's push buttons, from x (or centred: x < 0)
			auto buttons = [&](int x, int bw, int bh, const std::vector<std::pair<const wchar_t *, std::function<void()>>> &list) {
				int gap = S(10);
				bw = std::min(bw, (x1 - x0 - gap * (int(list.size()) - 1)) / int(list.size())); // fit the page
				int total = int(list.size()) * (bw + gap) - gap;
				if (x < 0) x = (pw - total) / 2;
				const Style cap{ 12, FW_NORMAL, false, false, theme->ink };
				for (auto &[label, go] : list)
				{
					theme->Button(cv, x, y, bw, bh, 0);
					auto size = text->Measure(label, cap);
					text->Draw(cv, x + (bw - int(size.cx)) / 2, y + (bh - int(size.cy)) / 2, label, cap);
					links.push_back({ RECT{ x, y, x + bw, y + bh }, go, label });
					x += bw + gap;
				}
				y += bh;
			};
			const wchar_t *tipLabel = L"Tip";
			if (family == Classic)
			{
				// "Welcome to" then the name in bold, an etched rule, and its buttons
				const Style light{ 22, FW_NORMAL, false, false, theme->ink }, bold{ 22, FW_BOLD, false, false, theme->ink };
				text->Draw(cv, x0, y, L"Welcome to ", light);
				text->Draw(cv, x0 + text->Measure(L"Welcome to ", light).cx, y, L"The Powder Toy", bold);
				y += text->Measure(L"Ag", light).cy + S(6);
				for (int i = x0; i < x1; i++) cv.Blend(i, y, 0x808080, 255), cv.Blend(i, y + 1, 0xFFFFFF, 255);
				y += S(8);
				text->Draw(cv, x0, y, version, body);
				y += text->Measure(L"Ag", body).cy + S(10);
				buttons(x0, S(140), S(24), sites);
				y += S(18);
				tipLabel = L"Did you know...";
			}
			else if (family == Center)
			{
				// the Welcome Center's banner, in the look's heading colour fading out
				int bh = S(74);
				auto mix = [](uint32_t a, uint32_t b, int t) {
					auto ch = [&](int sh) { return uint32_t((int((a >> sh) & 0xFF) * (255 - t) + int((b >> sh) & 0xFF) * t) / 255) << sh; };
					return ch(16) | ch(8) | ch(0);
				};
				for (int i = 0; i < pw; i++)
				{
					uint32_t c = mix(theme->heading, theme->page, i * 200 / std::max(1, pw));
					for (int j = 0; j < bh; j++) cv.Blend(i, j, c, 255);
				}
				y = S(12);
				text->Draw(cv, x0, y, L"Welcome to The Powder Toy", Style{ 22, FW_LIGHT, false, false, 0xFFFFFF });
				y += text->Measure(L"Ag", title).cy + S(2);
				text->Draw(cv, x0, y, version, Style{ 12, FW_NORMAL, false, false, 0xFFFFFF });
				y = bh + S(14);
				buttons(x0, S(170), S(40), sites);
				y += S(20);
			}
			else if (family == OldMac || family == NewMac)
			{
				// centred: the app's icon (modern Macs), the name, the version, the sites, and Continue
#ifdef _WIN32
				static HICON icon = HICON(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101), IMAGE_ICON, 256, 256, 0));
				if (family == NewMac && icon)
				{
					int is = S(64);
					CaptureGdi(cv, (pw - is) / 2, y, is, is, [&](HDC dc, RECT &rc) {
						DrawIconEx(dc, 0, 0, icon, rc.right, rc.bottom, 0, nullptr, DI_NORMAL);
					});
					y += is + S(10);
				}
#endif
				centred(L"Welcome to The Powder Toy", Style{ family == NewMac ? 22 : 18, FW_BOLD, false, false, theme->heading });
				y += S(4);
				centred(version, family == NewMac ? subtitle : body);
				y += S(12);
				if (family == OldMac)
					for (int i = x0 + S(40); i < x1 - S(40); i++) cv.Blend(i, y, theme->ink, 255);
				y += S(12);
				siteLinks(-1);
				if (family == NewMac)
				{
					buttons(-1, S(110), S(24), { { L"Continue", [this] { tab = 0, scroll = 0; } } });
					y += S(24);
				}
			}
			else
			{
				text->Draw(cv, x0, y, L"Welcome to The Powder Toy", title);
				y += text->Measure(L"Ag", title).cy + S(2);
				text->Draw(cv, x0, y, version, subtitle);
				y += text->Measure(L"Ag", subtitle).cy + S(8);
				siteLinks(x0);
			}

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
				const Style h2{ 17, FW_SEMIBOLD, false, false, theme->heading };
				text->Draw(cv, x0, y, name, h2);
				y += text->Measure(L"Ag", h2).cy + S(3);
				for (int i = x0; i < x1; i++) cv.Blend(i, y, theme->groupEdge, 255);
				y += S(12);
			};

			header(L"Getting started");
			// The tip: in the look's tooltip
			{
				const Style tipStyle{ 12, FW_BOLD, false, false, 0x7A5B00 };
				int top = y, tx = x0 + S(30), px = tx + std::max(S(32), int(text->Measure(tipLabel, tipStyle).cx) + S(8));
				int bottom = Para(px, top + S(8), x1 - S(10),
				                  L"Pick a material in the Elements tab, then draw anywhere on your desktop. Ctrl+Alt+E shows or hides this window.", body) + S(8);
				Canvas tip;
				tip.Resize(x1 - x0, bottom - top);
				theme->Popup(tip);
				for (int j = 0; j < tip.h; j++)
					for (int i = 0; i < tip.w; i++)
						if (uint32_t c = tip.px[size_t(j) * tip.w + i], a = c >> 24; a) // (premultiplied: it was drawn over nothing)
						{
							auto ch = [&](int sh) { return std::min(255u, ((c >> sh) & 0xFF) * 255 / a) << sh; };
							cv.Blend(x0 + i, top + j, ch(16) | ch(8) | ch(0), int(a));
						}
				cv.RoundRect(x0 + S(10), top + S(9), S(16), S(16), S(8), 0x3C8DDE, 0x1E5FAA, 255);
				text->Draw(cv, x0 + S(16), top + S(9), L"i", Style{ 11, FW_BOLD, true, false, 0xFFFFFF });
				text->Draw(cv, tx, top + S(8), tipLabel, tipStyle);
				Para(px, top + S(8), x1 - S(10),
				     L"Pick a material in the Elements tab, then draw anywhere on your desktop. Ctrl+Alt+E shows or hides this window.", body);
				y = bottom + S(18);
			}
			header(L"Interactive Buddy");
			y = Para(x0, y, x1, L"The buddy from the Flash game Interactive Buddy can live on your desktop in front of the sim: sand piles on him, water floats him, fire burns him, and his own items spray real water, fire and gravity into it. The game isn't included: bring your own copy (Interactive Buddy v1.01 or v1.02, a .swf file) and pick it in the Fun tab with Choose SWF. Your file is only read, never copied.", body) + S(18);

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
							const Style cap{ 11, FW_SEMIBOLD, false, false, theme->ink };
							int cw = text->Measure(key, cap).cx + S(12), ch = S(20);
							theme->Button(cv, kx, ky, cw, ch, 0); // a key: the look's push button
							text->Draw(cv, kx + S(6), ky + (ch - text->Measure(key, cap).cy) / 2, key, cap);
							kx += cw + S(3);
						}
						kx += S(4);
					}
					int dx = std::max(x0 + keyW, kx + S(8));
					int end = Para(dx, y + S(1), x1, row.what, body);
					if (row.aside)
					{
						end = Para(dx, end, x1, row.aside, note);
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
#ifdef __APPLE__
			const wchar_t *autoLabel = L"Auto: empty desktop is TPT's, windows and icons keep their clicks";
#else
			const wchar_t *autoLabel = L"Auto: TPT while it's in front, the desktop once another app is";
#endif
			const wchar_t *modes[] = { autoLabel, L"Pass-through: the sim takes no clicks (side windows still do)", L"Locked: everything to TPT" };
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
#ifdef _WIN32
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
#else
			Toggle(x + S(12), top + S(48), L"Start at login", OverlayMac::StartsAtLogin(), false, [] { OverlayMac::SetStartsAtLogin(!OverlayMac::StartsAtLogin()); },
			       L"Opens on the desktop when you log in");
#endif
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
#ifdef _WIN32
			int keyRows = 6;
#else
			int keyRows = 5; // ponytail: the Mac's hotkeys are its five; Buddy's clicks key is Windows only for now
#endif
			top = Group(x, y, w, S(166) + (keyRows - 5) * S(26), L"Shortcuts (any app)");
			std::wstring rows[][2] = {
				{ L"Next input mode", key("CycleInput", "Ctrl+Alt+O") },
				{ L"Pass-through on/off", key("PassThrough", "Ctrl+Alt+P") },
				{ L"Locked on/off", key("Locked", "Ctrl+Alt+L") },
				{ L"Show/hide this window", key("Palette", "Ctrl+Alt+E") },
				{ L"Clear the sim", key("Clear", "Ctrl+Alt+C") },
				{ L"Clicks: TPT or Buddy", key("BuddyClicks", "Ctrl+Alt+B") },
			};
			const char *names[] = { "CycleInput", "PassThrough", "Locked", "Palette", "Clear", "BuddyClicks" };
			for (int i = 0; i < keyRows; i++)
			{
				int ry = top + i * S(26);
				Caption(x + S(12), ry, rows[i][0]);
				bool rec = recording == names[i];
				Caption(x + S(200), ry, rec ? L"Press the new keys (Esc cancels)" : rows[i][1], rec ? 0xC25100 : theme->heading);
				PushButton(x + w - S(100), ry, S(88), rec ? L"Cancel" : L"Change\u2026", [rec, name = std::string(names[i])] {
					recording = rec ? "" : name;
				}, L"Ctrl, Alt, Shift or Win plus a letter, digit or F key");
			}
			y += S(174) + (keyRows - 5) * S(26);
			top = Group(x, y, w, S(108), L"Size");
			Caption(x + S(12), top + S(56), L"Runs as");
			int mx = x + S(200);
			mx += Toggle(mx, top + S(56), L"Desktop overlay", !overlayWindowed, true, [] { Restart(ui::Engine::Ref().windowFrameOps.scale, false); },
			             L"Over the desktop (restarts TPT; the scene comes along)") + S(18);
			mx += Toggle(mx, top + S(56), L"Normal window", overlayWindowed, true, [] { Restart(ui::Engine::Ref().windowFrameOps.scale, true); },
			             L"TPT's own window with these side windows; resize it for more or less room (restarts TPT; the scene comes along)") + S(18);
			bool onTop = GlobalPrefs::Ref().Get("Overlay.WindowOnTop", true);
			Toggle(mx, top + S(56), L"Stay on top", onTop, false, [onTop] {
				OverlayWindowOnTop(!onTop);
				// the menu windows (the resizable panels: menu and torn-off tabs)
				EnumThreadWindows(GetCurrentThreadId(), [](HWND h, LPARAM top) {
					wchar_t cls[32];
					if (GetClassNameW(h, cls, 32) && !wcscmp(cls, L"TPTOverlayPanel") && GetWindowLongPtrW(h, GWL_STYLE) & WS_THICKFRAME)
						SetWindowPos(h, top ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
					return TRUE;
				}, LPARAM(!onTop));
			}, L"TDPT's menu windows (and its own window, as a Normal window) stay over your other windows, even while you use them. Off: they go behind whatever you click");
			// The sim's pixel size: a different sim size, so TPT restarts with the scene carried over.
			Caption(x + S(12), top + S(28), L"Sim pixel size");
			int sx = x + S(200);
			for (int sc = 1; sc <= 6; sc++)
			{
				auto cells = OverlaySimSize(sc).first;
				wchar_t label[32];
				swprintf(label, 32, L"%d\u00D7", sc);
				wchar_t tip[160];
				swprintf(tip, 160, L"%dx%d sim pixels (restarts TPT; the scene comes along, clipped if it doesn't fit)%s", cells.X * CELL, cells.Y * CELL,
				         cells.X * cells.Y * CELL * CELL > MAX_NPART ? L"; at most 4.2 million particles" : L"");
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
			int current = int(std::find(std::begin(themes), std::end(themes), theme) - std::begin(themes)), gen = Generation(current);
			const std::vector<int> *colours = nullptr;
			for (auto &c : themeColours)
				if (c[0] == gen) colours = &c;
			int colourRows = colours ? int(colours->size() + 2) / 3 : 0; // three to a row
			top = Group(x, y, w, S(140) + colourRows * S(32), L"Look");
			auto family = [&](int row, const wchar_t *label, const std::vector<int> &ids) {
				int by = top + row * S(32), bx = x + S(200), bw = S(220);
				bool in = std::count(ids.begin(), ids.end(), gen) > 0;
				Caption(x + S(12), by, label);
				PushButton(bx, by, bw, std::wstring(in ? themes[gen]->name : L"\u2014"), [this, &ids, bx, by, bw] {
					later = [this, &ids, rc = RECT{ bx, by, bx + bw, by + S(23) }] { PickTheme(ids, rc); };
				}, L"Windows and menus look like this system's");
				for (int i = 0, k = S(4), ax = bx + bw - S(20), ay = by + S(10); i <= k; i++) Line(cv, ax + i, ay + i, 2 * (k - i) + 1, 1, theme->ink); // the arrow
			};
			family(0, L"Windows", windowsThemes);
			family(1, L"Mac", macThemes);
			Toggle(x + S(12), top + 2 * S(32), L"TempleOS", current == templeIndex, true, [] { SetTheme(templeIndex); }, L"Windows and menus look like TempleOS");
			if (colours)
			{
				Caption(x + S(12), top + 3 * S(32), L"Colour");
				int cx = 0;
				for (size_t k = 0; k < colours->size(); k++)
				{
					int i = (*colours)[k], cy = top + int(3 + k / 3) * S(32);
					if (k % 3 == 0) cx = x + S(200);
					cx += Toggle(cx, cy, themes[i]->colour, i == current, true, [i] { SetTheme(i); }, std::wstring(themes[i]->name) + L" in " + themes[i]->colour) + S(18);
				}
			}
			{
				// the gradient wallpaper and its two colours
				int gy = top + (3 + colourRows) * S(32), bx = x + S(200);
				Caption(x + S(12), gy, L"Wallpaper");
				bx += Toggle(bx, gy, L"Gradient", overlayFeel.gradient, false, [this] {
					overlayFeel.gradient = !overlayFeel.gradient;
					GlobalPrefs::Ref().Set("Overlay.Gradient", overlayFeel.gradient);
				}, L"The desktop background as a gradient where it shows (needs windows solid, or the sim under windows). Only drawn by the overlay: your real wallpaper is never changed") + S(18);
				for (auto [c, label, pref] : { std::tuple{ &overlayFeel.gradientTop, L"Top", "Overlay.GradientTop" }, std::tuple{ &overlayFeel.gradientBottom, L"Bottom", "Overlay.GradientBottom" } })
				{
					int bw = S(90);
					PushButton(bx, gy - S(3), bw, label, [this, c, pref] {
						later = [this, c, pref] {
							static COLORREF custom[16] = {};
							CHOOSECOLORW cc{ sizeof(cc) };
							cc.hwndOwner = hwnd, cc.rgbResult = RGB(*c >> 16, (*c >> 8) & 0xFF, *c & 0xFF), cc.lpCustColors = custom;
							cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
							if (ChooseColorW(&cc))
							{
								*c = uint32_t(GetRValue(cc.rgbResult)) << 16 | GetGValue(cc.rgbResult) << 8 | GetBValue(cc.rgbResult);
								GlobalPrefs::Ref().Set(pref, int(*c));
							}
							dirty = true;
						};
					}, std::wstring(L"Pick the gradient's ") + (c == &overlayFeel.gradientTop ? L"top" : L"bottom") + L" colour");
					int sw = S(12);
					cv.RoundRect(bx + S(8), gy - S(3) + (S(23) - sw) / 2, sw, sw, S(2), *c, *c, 255); // the colour
					cv.RoundFrame(bx + S(8), gy - S(3) + (S(23) - sw) / 2, sw, sw, S(2), theme->edge, 255);
					bx += bw + S(10);
				}
			}
			y += S(148) + colourRows * S(32);
		}

		// The rest of TPT's own options window (Simulation has the air, heat and gravity ones), through its options model.
		// ponytail: its window settings (scale, fullscreen, resizable) are left out: the overlay sizes TPT's window itself
		void PaintTPT()
		{
			auto c = Content();
			auto &opts = Options();
			int x = c.left + S(10), w = c.right - c.left - S(20), y = c.top + S(8) - scroll;
			int half = (w - S(24)) / 2, lx = x + S(12), bx = x + S(220), nw = std::min(S(200), x + w - S(12) - bx);
			struct Switch { const wchar_t *name; bool (OptionsModel::*get)(); void (OptionsModel::*set)(bool); const wchar_t *tip; } switches[] = {
				{ L"Perfect circle brush", &OptionsModel::GetPerfectCircle, &OptionsModel::SetPerfectCircle, L"Rounder small circle brushes" },
				{ L"Pressure in saves", &OptionsModel::GetIncludePressure, &OptionsModel::SetIncludePressure, L"Saves, stamps and copies keep the air pressure" },
				{ L"Smooth scrolling", &OptionsModel::GetMomentumScroll, &OptionsModel::SetMomentumScroll, L"Momentum scrolling in TPT's lists" },
				{ L"Sticky categories", &OptionsModel::GetMouseClickRequired, &OptionsModel::SetMouseClickRequired, L"TPT's element menu changes category on a click, not on hover" },
				{ L"Quit without asking", &OptionsModel::GetFastQuit, &OptionsModel::SetFastQuit, L"No confirmation when closing TPT" },
				{ L"Ctrl+Q quits anywhere", &OptionsModel::GetGlobalQuit, &OptionsModel::SetGlobalQuit, L"Ctrl+Q quits from any of TPT's screens" },
				{ L"Key under Esc closes console", &OptionsModel::GetGraveExitsConsole, &OptionsModel::SetGraveExitsConsole, L"Turn off if that key types 0 on your keyboard" },
				{ L"Windows clipboard", &OptionsModel::GetNativeClipoard, &OptionsModel::SetNativeClipoard, L"Copied stamps go through the Windows clipboard: paste them into another TPT" },
				{ L"Show avatars", &OptionsModel::GetShowAvatars, &OptionsModel::SetShowAvatars, L"Profile pictures in the online browser" },
				{ L"Message of the day", &OptionsModel::GetAutoStartupRequest, &OptionsModel::SetAutoStartupRequest, L"Fetch TPT's message of the day and notifications at startup" },
				{ L"Log errors to a file", &OptionsModel::GetRedirectStd, &OptionsModel::SetRedirectStd, L"Errors and other messages go to a file in the data folder (for bug reports)" },
			};
			int n = int(std::size(switches)), rows = (n + 2) / 2; // (and the render thread)
			int top = Group(x, y, w, S(36) + rows * S(24) + S(32), L"TPT settings");
			for (int i = 0; i < n; i++)
			{
				auto &t = switches[i];
				Toggle(lx + (i % 2) * half, top + (i / 2) * S(24), t.name, (opts.*t.get)(), false, [&opts, t] { (opts.*t.set)(!(opts.*t.get)()); }, t.tip);
			}
			Toggle(lx + (n % 2) * half, top + (n / 2) * S(24), L"Separate render thread", opts.GetThreadedRendering(), false, [&opts] { opts.SetThreadedRendering(!opts.GetThreadedRendering()); },
			       L"Draws on its own thread: can be faster with fancy effects on");
			int ry = top + rows * S(24) + S(4);
			Caption(lx, ry + S(3), L"Deco colour space");
			ListButton(bx, ry, nw, { L"sRGB", L"Linear", L"Gamma 2.2", L"Gamma 1.8" }, opts.GetDecoSpace(), [&opts](int i) { opts.SetDecoSpace(i); }, L"How deco colours blend");
			y = ry + S(48);

			auto row = [&](const wchar_t *label) {
				Caption(lx, ry + S(3), label);
				int at = ry;
				ry += S(32);
				return at;
			};
			ry = Group(x, y, w, S(36) + 2 * S(32), L"Frame rate");
			auto fps = opts.GetFpsLimit();
			auto *fe = std::get_if<FpsLimitExplicit>(&fps);
			NumberButton(bx, row(L"Simulation cap"), nw, "Simulation framerate cap (0: uncapped)", fe ? String::Build(Format::Precision(0), fe->value) : String("0"),
			             FloatSetting(0, FpsLimitExplicit::maxSane, [](const String &s) { return s.ToNumber<float>(); }, [&opts](float v) {
				             if (v < FpsLimitExplicit::minSane) opts.SetFpsLimit(FpsLimitNone{});
				             else opts.SetFpsLimit(FpsLimitExplicit{ v });
			             }), L"Simulation frames a second; 0 = as fast as it goes (TPT's default is 60)");
			auto draw = opts.GetDrawLimit();
			auto *de = std::get_if<DrawLimitExplicit>(&draw);
			NumberButton(bx, row(L"Drawing cap"), nw, "Rendering framerate cap (0: the display's)", de ? String::Build(de->value) : String("0"),
			             FloatSetting(0, float(DrawLimitExplicit::maxSane), [](const String &s) { return s.ToNumber<float>(); }, [&opts](float v) {
				             if (v < 1) opts.SetDrawLimit(DrawLimitDisplay{});
				             else opts.SetDrawLimit(DrawLimitExplicit{ int(v) });
			             }), L"Frames drawn a second; 0 = the display's refresh rate");
			y = ry + S(16);

			ry = Group(x, y, w, S(36) + 4 * S(32), L"Air and gravity numbers");
			auto number = [&](const wchar_t *label, const String &title, float v, float lo, float hi, void (OptionsModel::*set)(float), const wchar_t *tip) {
				NumberButton(bx, row(label), nw, title, String::Build(Format::Precision(2), v),
				             FloatSetting(lo, hi, [](const String &s) { return s.ToNumber<float>(); }, [&opts, set](float v) { (opts.*set)(v); }), tip);
			};
			number(L"Edge wind across", "Ambient air velocity X", opts.GetEdgeVelocityX(), -256, 256, &OptionsModel::SetEdgeVelocityX, L"Air blowing in from the edges, left to right (minus: right to left)");
			number(L"Edge wind down", "Ambient air velocity Y", opts.GetEdgeVelocityY(), -256, 256, &OptionsModel::SetEdgeVelocityY, L"Air blowing in from the edges, downward (minus: up)");
			number(L"Custom gravity across", "Custom gravity X", opts.GetCustomGravityX(), -1000, 1000, &OptionsModel::SetCustomGravityX, L"Sideways pull when Gravity is Custom (Simulation tab)");
			number(L"Custom gravity down", "Custom gravity Y", opts.GetCustomGravityY(), -1000, 1000, &OptionsModel::SetCustomGravityY, L"Downward pull when Gravity is Custom (Simulation tab)");
			y = ry + S(16);

			PushButton(lx, y, S(200), L"Open data folder", [] {
				if (auto cwd = Platform::GetCwd(); !cwd.empty()) Platform::OpenURI(cwd);
			}, L"Where TPT keeps its saves, stamps, scripts and settings");
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
			for (int i : ids) AppendMenuW(m, MF_STRING | (Generation(int(std::find(std::begin(themes), std::end(themes), theme) - std::begin(themes))) == i ? MF_CHECKED : 0), UINT(i + 1), themes[i]->name);
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
			Add(MinRect(), [] { OverlayWindows::TogglePalette(); }, L"Hide to the tray");
			Add(MaxRect(), [this] { ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE); }, IsZoomed(hwnd) ? L"Restore" : L"Maximise");
			if (fixedTab >= 0) Add(CloseRect(), [this] { closing = true; }, L"Close (the tab is still in the main window)");
			else Add(CloseRect(), [] { OverlayWindows::TogglePalette(); }, L"Close (the tray icon or Ctrl+Alt+E brings it back)");

			// The tab strip: the selected tab joined to the page.
			int tx = border + S(4);
			cv.RoundRect(border, titleH, cv.w - 2 * border, tabH, 0, theme->strip, theme->strip, theme->stripAlpha);
			// padding around each tab's name, less when they would not all fit
			int pad = S(20);
			if (fixedTab < 0)
			{
				int names = 0;
				for (int i = 0; i < int(std::size(tabNames)); i++)
					if (i != buddyTab || BuddyTabShown()) names += text->Measure(tabNames[i]).cx + S(1);
				pad = std::clamp((cv.w - 2 * tx - names) / int(std::size(tabNames)), S(6), S(20));
			}
			for (int i : tabOrder)
			{
				if (fixedTab >= 0 && i != fixedTab) continue;
				if (i == buddyTab && !BuddyTabShown())
				{
					if (tab == buddyTab) tab = 6; // his file was taken away: back to Fun
					continue;
				}
				int tw = text->Measure(tabNames[i]).cx + pad;
				bool on = i == tab;
				RECT rc{ tx, titleH + (on ? S(2) : S(4)), tx + tw, titleH + tabH + (on ? 1 : 0) };
				bool hot = !on && Hovered(rc);
				theme->Tab(cv, rc, on, hot);
				text->Draw(cv, tx + pad / 2, rc.top + (rc.bottom - rc.top - text->Measure(tabNames[i]).cy) / 2 - (theme->segmentTabs ? S(1) : 0), tabNames[i], theme->TabInk(i == tab));
				if (fixedTab < 0) Add(rc, [this, i] { tab = i, scroll = 0; }, L"Right-click: open this tab in its own window", [i] { OpenTorn(i); });
				tx += tw + S(1);
			}
			// The page.
			auto c = Content();
			cv.RoundRect(c.left, c.top, c.right - c.left, c.bottom - c.top, 0, theme->page, theme->page, 255);
			for (int i = c.left; i < c.right; i++) cv.Blend(i, c.top, theme->edge, 255);
			// The selected tab's bottom edge opens into the page.
			int sx = border + S(4);
			for (int i : tabOrder)
			{
				if (i == tab || fixedTab >= 0) break;
				if (i != buddyTab || BuddyTabShown()) sx += text->Measure(tabNames[i]).cx + pad + S(1);
			}
			if (!theme->segmentTabs)
				for (int i = sx + 1; i < sx + text->Measure(tabNames[tab]).cx + pad - S(1); i++) cv.Blend(i, c.top, theme->page, 255);

			// Form pages scroll: rows outside the page are put back after, and controls there dropped.
			bool form = (tab >= 1 && tab <= 6) || tab == buddyTab || tab == tptTab;
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
			case 6: PaintFun(funRows, std::size(funRows)); break;
			case buddyTab: PaintFun(buddyRows, std::size(buddyRows)); break;
			case welcomeTab: PaintWelcome(); break;
			case toolsTab: PaintTools(); break;
			case tptTab: PaintTPT(); break;
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
			if (at >= 0 && ((msg == WM_LBUTTONUP) || (msg == WM_MOUSEMOVE && (wp & MK_LBUTTON) && hits[at].drag)) && hits[at].left)
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
				Wheel(GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * S(51), welcomeH - int(c.bottom - c.top));
			}
			if (msg == WM_MOUSEWHEEL && ((tab >= 1 && tab <= 6) || tab == buddyTab || tab == tptTab))
			{
				auto c = Content();
				Wheel(GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * S(40), formH - int(c.bottom - c.top));
			}
			if (msg == WM_MOUSEWHEEL && tab == 0)
			{
				auto tools = Tools();
				auto area = ToolArea();
				int rows = (int(tools.size()) + Columns() - 1) / Columns();
				Wheel(GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * (tileH + gap), rows * (tileH + gap) - int(area.bottom - area.top));
			}
		}

		// Repaint when anything shown changes, from here, from TPT's keys, or from Lua.
		void Tick()
		{
			TickLocals();
			Glide();
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

