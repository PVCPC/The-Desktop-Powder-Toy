// Part of OverlayWindows.cpp (included by OverlayWindows.cpp): Windows dialogs: Form and the console
#ifdef _WIN32
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
#endif
