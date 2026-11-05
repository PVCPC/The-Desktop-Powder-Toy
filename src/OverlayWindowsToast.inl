// Part of OverlayWindows.cpp (included inside OverlayWindows.cpp's anonymous namespace): toasts, window placement, the scene across restarts, torn-off tabs
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

	// The scene carried across a restart or to the next run: saves of at most 255x255 cells (the save format's limit),
	// named by their top-left cell, next to powder.pref.
	std::wstring SceneTile(int x, int y)
	{
		return L"overlay-scene-" + std::to_wstring(x) + L"-" + std::to_wstring(y) + L".cps";
	}

	bool sceneSaved = false; // a restart saved it already, and the new TPT is loading it

	// In cells. A save bigger than the sim is refused outright, and the sim shrinks when the pixel size goes up.
	constexpr int sceneTile = 24;

	void SaveSceneTiles()
	{
		if (sceneSaved) return;
		sceneSaved = true;
		auto *model = GameController::Ref().GetModel();
		auto *sim = model->GetSimulation();
		std::vector<bool> used(size_t(XCELLS) * YCELLS);
		for (int i = 0; i < sim->parts.active; i++)
			if (sim->parts[i].type)
			{
				int x = int(sim->parts[i].x + 0.5f) / CELL, y = int(sim->parts[i].y + 0.5f) / CELL;
				if (x >= 0 && y >= 0 && x < XCELLS && y < YCELLS) used[size_t(y) * XCELLS + x] = true;
			}
		for (int y = 0; y < YCELLS; y += sceneTile)
			for (int x = 0; x < XCELLS; x += sceneTile)
			{
				auto tile = RectSized(Vec2(x, y), Vec2(std::min(sceneTile, XCELLS - x), std::min(sceneTile, YCELLS - y)));
				bool any = false; // most of a desktop is empty
				for (int cy = y; cy < y + tile.size.Y; cy++)
					for (int cx = x; cx < x + tile.size.X; cx++) any = any || used[size_t(cy) * XCELLS + cx] || sim->bmap[cy][cx];
				if (!any) continue;
				auto save = sim->Save(true, RectSized(tile.pos * CELL, tile.size * CELL));
				if (!save) continue;
				save->paused = model->GetPaused();
				auto data = save->Serialise().second; // .first is not success (TPT's own save path ignores it too)
				if (!data.empty()) Platform::WriteFile(data, Platform::WinNarrow(SceneTile(x, y)));
			}
		GlobalPrefs::Ref().Set("Overlay.PendingScene", true);
	}

	void LoadSceneTiles()
	{
		auto *model = GameController::Ref().GetModel();
		auto *sim = model->GetSimulation();
		GameController::Ref().GetView()->PauseRendererThread();
		for (int y = 0; y < YCELLS; y += sceneTile)
			for (int x = 0; x < XCELLS; x += sceneTile)
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
		// tiles past the edge of a smaller sim: gone, or a later run at a bigger size would bring old sand back
		for (auto &file : Platform::DirectorySearch(".", "overlay-scene-", { ".cps" }))
			Platform::RemoveFile(file);
	}

	// Saves the scene, sets the new pixel size and starts a fresh TPT, which loads the scene; this one quits.
	// The other pixel size or mode: the scene goes along in tiles, the new TPT loads it, this one quits.
	void Restart(int scale, bool windowed, bool force)
	{
		if (!force && scale == ui::Engine::Ref().windowFrameOps.scale && windowed == overlayWindowed) return;
		SaveSceneTiles();
		GlobalPrefs::Ref().Set("Scale", scale);
#ifdef _WIN32
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
#else
		GlobalPrefs::Ref().Set("Overlay.Windowed", windowed);
		if (OverlayMac::Relaunch(nullptr))
		{
			SDL_Event quit{};
			quit.type = SDL_QUIT;
			SDL_PushEvent(&quit);
		}
#endif
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

