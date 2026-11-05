// Part of OverlayWindows.cpp (included inside OverlayWindows.cpp's anonymous namespace): the tray icon and its menu
	// The notification-area (tray) icon: left click shows or hides the Elements window, right click opens a menu.
	NOTIFYICONDATAW tray{};
	// The taskbar's app buttons as UI Automation sees them (screen pixels), polled on a worker thread every half second
	// while a taskbar toy is on. A button whose name changed just after a window was created (Windows 11 names them
	// "App - 2 running windows") is the app that opened.
	struct TaskButton { std::wstring name; RECT r; };
	std::atomic<ULONGLONG> windowCreated = 0;
	std::mutex taskMutex;
	std::vector<TaskButton> taskButtons;
	std::vector<RECT> taskPopped;
#ifdef _WIN32
	void StartTaskButtons()
	{
		static bool started = false;
		if (started) return;
		started = true;
		std::thread([] {
			CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			IUIAutomation *uia = nullptr;
			IUIAutomationCondition *buttons = nullptr;
			VARIANT v{};
			v.vt = VT_I4, v.lVal = UIA_ButtonControlTypeId;
			if (FAILED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia))) ||
			    FAILED(uia->CreatePropertyCondition(UIA_ControlTypePropertyId, v, &buttons)))
				return;
			std::vector<std::wstring> before;
			bool first = true;
			for (;; Sleep(500))
			{
				if (!fun.taskThrow && !fun.taskPop)
				{
					first = true;
					continue;
				}
				std::vector<TaskButton> now;
				auto scan = [&](HWND hwnd, bool any) { // any: every button (Windows 10's task list); else Windows 11's task buttons
					IUIAutomationElement *root = nullptr;
					IUIAutomationElementArray *found = nullptr;
					if (hwnd && SUCCEEDED(uia->ElementFromHandle(hwnd, &root)) && SUCCEEDED(root->FindAll(TreeScope_Descendants, buttons, &found)) && found)
					{
						int n = 0;
						found->get_Length(&n);
						for (int i = 0; i < n; i++)
						{
							IUIAutomationElement *e = nullptr;
							if (FAILED(found->GetElement(i, &e)) || !e) continue;
							BSTR cls = nullptr, name = nullptr;
							RECT r{};
							e->get_CurrentClassName(&cls);
							if ((any || (cls && wcsstr(cls, L"TaskListButton"))) && SUCCEEDED(e->get_CurrentName(&name)) && SUCCEEDED(e->get_CurrentBoundingRectangle(&r)) && r.right > r.left)
								now.push_back({ name ? name : L"", r });
							SysFreeString(cls);
							SysFreeString(name);
							e->Release();
						}
						found->Release();
					}
					if (root) root->Release();
				};
				for (const wchar_t *cls : { L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd" })
					for (HWND tray = FindWindowExW(nullptr, nullptr, cls, nullptr); tray; tray = FindWindowExW(nullptr, tray, cls, nullptr))
					{
						scan(tray, false);
						HWND list = nullptr;
						EnumChildWindows(tray, [](HWND h, LPARAM lp) {
							wchar_t c[32];
							if (GetClassNameW(h, c, 32) && !wcscmp(c, L"MSTaskListWClass")) return *reinterpret_cast<HWND *>(lp) = h, FALSE;
							return TRUE;
						}, LPARAM(&list));
						scan(list, true);
					}
				std::lock_guard lock(taskMutex);
				if (!first && fun.taskPop && GetTickCount64() - windowCreated < 4000)
					for (auto &b : now)
						if (std::find(before.begin(), before.end(), b.name) == before.end()) taskPopped.push_back(b.r);
				before.clear();
				for (auto &b : now) before.push_back(b.name);
				taskButtons = std::move(now);
				first = false;
			}
		}).detach();
	}
#else
	void StartTaskButtons() {} // TickMacDock reads the Dock
#endif

	constexpr UINT trayMessage = WM_APP + 1;

	LRESULT CALLBACK TrayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
	{
		static const UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
		if (msg == taskbarCreated)
		{
			Shell_NotifyIconW(NIM_ADD, &tray); // Explorer restarted and lost the icon
			return 0;
		}
		static const UINT shellHook = RegisterWindowMessageW(L"SHELLHOOK");
		if (msg == shellHook)
		{
			if (wp == HSHELL_FLASH && fun.notify) fun.flashed.push_back(HWND(lp));
			if (wp == HSHELL_WINDOWCREATED) windowCreated = GetTickCount64();
			return 0;
		}
		if (msg == WM_ENDSESSION && wp)
		{
			SaveSceneTiles(); // shutting down or signing out: TPT gets no quit of its own
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
			AppendMenuW(menu, MF_STRING, clear, label(L"Clear the sim", "Clear", "Ctrl+Alt+C").c_str());
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
#ifdef _WIN32
		RegisterShellHookWindow(tray.hWnd); // taskbar buttons flashing (Notifications drop a ball)
#endif
		std::atexit([] { Shell_NotifyIconW(NIM_DELETE, &tray); });
#ifdef _WIN32
		// On a crash too. A forced kill (Task Manager's End task) can't be caught: Windows clears that icon on hover.
		static LPTOP_LEVEL_EXCEPTION_FILTER previous = SetUnhandledExceptionFilter([](EXCEPTION_POINTERS *e) -> LONG {
			Shell_NotifyIconW(NIM_DELETE, &tray);
			return previous ? previous(e) : EXCEPTION_CONTINUE_SEARCH;
		});
#else
#endif
	}

	void ApplyPassThrough(Panel &panel)
	{
		auto ex = GetWindowLongPtrW(panel.hwnd, GWL_EXSTYLE);
		SetWindowLongPtrW(panel.hwnd, GWL_EXSTYLE, passThrough ? ex | WS_EX_TRANSPARENT : ex & ~LONG_PTR(WS_EX_TRANSPARENT));
		panel.opacity = passThrough ? 110 : 255;
		panel.Present();
	}
