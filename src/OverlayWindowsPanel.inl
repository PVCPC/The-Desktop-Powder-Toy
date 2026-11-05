// Part of OverlayWindows.cpp (included inside OverlayWindows.cpp's anonymous namespace): the layered window base (Panel) and its helpers
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
				SetThreadDescription(GetCurrentThread(), L"overlay side window vsync");
#ifdef _WIN32
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
#else
				while (vsyncRun)
				{
					usleep(16000);
					Compose();
				}
#endif
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
		// An app window: no taskbar button (the tray icon opens it), normal z-order, resizable, maximise. Otherwise a small on-top popup
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
			DWORD ex = WS_EX_LAYERED | WS_EX_NOACTIVATE | (appWindow ? WS_EX_TOOLWINDOW : owner ? 0 : WS_EX_TOOLWINDOW | WS_EX_TOPMOST);
			DWORD style = WS_POPUP | (appWindow ? WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_THICKFRAME : 0);
			hwnd = CreateWindowExW(ex, L"TPTOverlayPanel", title.empty() ? Platform::WinWiden(name).c_str() : title.c_str(), style, x, y, w, h,
			                       owner, nullptr, GetModuleHandleW(nullptr), nullptr);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, LONG_PTR(this));
			if (appWindow) DragAcceptFiles(hwnd, TRUE);
			// TDPT's menus stay over other apps' windows (Size: Stay on top, on by default)
			if (appWindow && GlobalPrefs::Ref().Get("Overlay.WindowOnTop", true)) SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
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
					overlayMenuUsed = true; // Auto clicks: TPT is in use (Overlay.cpp)
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

