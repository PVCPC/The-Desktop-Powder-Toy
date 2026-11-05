// Part of OverlayWindows.cpp (included by OverlayWindows.cpp): windows, icons and the taskbar as walls, once per sim tick
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
	static RECT grabRect;
	grabRect = sr;
#ifdef __APPLE__
	overlayGrabScreen = [](int x0, int y0, int w, int h, uint32_t *out) {
		double px = MacScale(), sw = (grabRect.right - grabRect.left) / px, sh = (grabRect.bottom - grabRect.top) / px;
		return sw > 0 && sh > 0 && OverlayMac::Grab(grabRect.left / px + x0 * sw / WINDOWW, grabRect.top / px + y0 * sh / WINDOWH, w * sw / WINDOWW, h * sh / WINDOWH, w, h, out);
	};
#else
	// shards take the colour under them: GDI's copy of the screen leaves out layered windows, so not the overlay
	overlayGrabScreen = [](int x0, int y0, int w, int h, uint32_t *out) {
		int sw = grabRect.right - grabRect.left, sh = grabRect.bottom - grabRect.top;
		if (sw <= 0 || sh <= 0) return false;
		HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
		BITMAPINFO bi{};
		bi.bmiHeader = { sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB };
		void *bits = nullptr;
		HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
		bool ok = false;
		if (bmp)
		{
			auto old = SelectObject(dc, bmp);
			SetStretchBltMode(dc, COLORONCOLOR);
			ok = StretchBlt(dc, 0, 0, w, h, screen, grabRect.left + x0 * sw / WINDOWW, grabRect.top + y0 * sh / WINDOWH,
			                w * sw / WINDOWW, h * sh / WINDOWH, SRCCOPY);
			if (ok)
				for (int i = 0; i < w * h; i++) out[i] = ((uint32_t *)bits)[i] & 0xFFFFFF;
			SelectObject(dc, old);
			DeleteObject(bmp);
		}
		DeleteDC(dc);
		ReleaseDC(nullptr, screen);
		return ok;
	};
#endif
	// windows are walls only when the sim is over them; icons whenever it's over the icons
	// the mouse over the desktop (where the overlay lets clicks through) stirs the air
	static POINT lastCursor{ -1, -1 };
	POINT cursor;
	if (!overlayWindowed && sw > 0 && sh > 0 && GetCursorPos(&cursor))
	{
		auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); };
		int x = px(cursor.x - sr.left, WINDOWW, sw), y = px(cursor.y - sr.top, WINDOWH, sh);
		sim->petPointer = { x, y, lastCursor.x >= 0 ? float(std::hypot(px(cursor.x - lastCursor.x, WINDOWW, sw), px(cursor.y - lastCursor.y, WINDOWH, sh))) : 0.f };
		if (lastCursor.x >= 0 && x >= 0 && y >= 0 && x < XRES && y < YRES && GetAncestor(WindowFromPoint(cursor), GA_ROOT) != mainWindow)
		{
			OverlayCursorWind(sim, x, y, px(cursor.x - lastCursor.x, WINDOWW, sw), px(cursor.y - lastCursor.y, WINDOWH, sh));
		}
		lastCursor = cursor;
	}
	TickFun(sim);
	bool doWindows = windowWalls && OverlayGetLayer() == OverlayLayer::OnTop;
	overlayWallpaperSeen = doWindows || OverlayGetLayer() != OverlayLayer::OnTop;
	bool doIcons = iconWalls && OverlayGetLayer() != OverlayLayer::BehindIcons;
	// Mouse is a broom: a small solid block at the pointer where clicks go to the desktop, moving like a window
	POINT at;
	bool broom = fun.broom && !overlayWindowed && GetCursorPos(&at) && GetAncestor(WindowFromPoint(at), GA_ROOT) != mainWindow;
#ifdef __APPLE__
	// It covers one screen, so no gaps. The Dock is a solid shape, added with the windows below; only when it can't be read
	// (no Accessibility) is there a floor where it starts, or everything would drain out of the void edge.
	static std::vector<RECT> gaps;
	gaps.clear();
	double dockL, dockT, dockR, dockB;
	bool dockKnown = OverlayMac::DockFrame(dockL, dockT, dockR, dockB);
	if (windowWalls && !dockKnown)
	{
		RECT floor;
		GetWindowRect(macDock, &floor);
		gaps.push_back({ sr.left, floor.top, sr.right, sr.bottom });
	}
#else
	// The overlay spans every monitor; where none is (beside a shorter one) is solid, so sand stops at its edge
	static std::vector<RECT> gaps = [] {
		std::vector<RECT> mons, out;
		EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR, HDC, LPRECT r, LPARAM lp) -> BOOL {
			((std::vector<RECT> *)lp)->push_back(*r);
			return TRUE;
		}, LPARAM(&mons));
		RECT v{ GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN) };
		v.right = v.left + GetSystemMetrics(SM_CXVIRTUALSCREEN), v.bottom = v.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
		std::vector<long> ys{ v.top, v.bottom };
		for (auto &m : mons) ys.push_back(m.top), ys.push_back(m.bottom);
		std::sort(ys.begin(), ys.end());
		for (size_t i = 0; i + 1 < ys.size(); i++)
		{
			if (ys[i] == ys[i + 1]) continue;
			std::vector<std::pair<long, long>> spans; // monitors across this band
			for (auto &m : mons)
				if (m.top <= ys[i] && m.bottom >= ys[i + 1]) spans.push_back({ m.left, m.right });
			std::sort(spans.begin(), spans.end());
			long x = v.left;
			for (auto [l, r] : spans)
			{
				if (l > x) out.push_back({ x, ys[i], l, ys[i + 1] });
				x = std::max(x, r);
			}
			if (x < v.right) out.push_back({ x, ys[i], v.right, ys[i + 1] });
		}
		return out;
	}();
#endif
	if (overlayWindowed || (!doWindows && !doIcons && gaps.empty() && !broom) || sw <= 0 || sh <= 0)
	{
		wallRects.clear();
		std::vector<OverlayWall> him;
		OverlayBuddy::Walls(him); // he is still solid
		if (him.empty()) sim->pixelWalls.clear();
		else OverlayApplyWalls(sim, him);
		return;
	}
	std::map<HWND, RECT> now;
#ifdef __APPLE__
	static std::map<HWND, int> pids; // whose window it is
	if (doWindows)
		for (auto &o : OverlayMac::OtherWindows())
		{
			HWND hwnd = HWND(uintptr_t(o.id));
			RECT r = MacRect(o);
			auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); }; // screen -> sim pixels
			OverlayWall w{ std::max(0, px(r.left - sr.left, WINDOWW, sw)), std::max(0, px(r.top - sr.top, WINDOWH, sh)),
			               std::min(XRES, px(r.right - sr.left, WINDOWW, sw)), std::min(YRES, px(r.bottom - sr.top, WINDOWH, sh)), 0, 0, {} };
			now[hwnd] = r;
			pids[hwnd] = o.pid;
			if (w.x1 <= w.x0 || w.y1 <= w.y0) continue;
			if (auto it = wallRects.find(hwnd); it != wallRects.end())
			{
				w.mx = px(r.left - it->second.left, WINDOWW, sw);
				w.my = px(r.top - it->second.top, WINDOWH, sh);
			}
			w.id = uintptr_t(hwnd);
			wins.push_back(w);
		}
#else
	struct Ctx { std::vector<OverlayWall> *wins; std::map<HWND, RECT> *now; RECT sr; int sw, sh; } ctx{ &wins, &now, sr, sw, sh };
	if (doWindows) EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
		auto &c = *(Ctx *)lp;
		if (hwnd == mainWindow || !IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE; // most windows: cheap checks first
		auto ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
		BOOL cloaked = FALSE;
		DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
		wchar_t cls[32];
		GetClassNameW(hwnd, cls, 32);
		if (cloaked || (ex & WS_EX_TRANSPARENT) ||
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
		w.id = uintptr_t(hwnd);
		c.wins->push_back(w);
		return TRUE;
	}, LPARAM(&ctx));
#endif
	if (!wallRects.empty() && (fun.closeToss || fun.openShake))
	{
		auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); };
		auto toss = [&](const RECT &r, bool close) {
			TossAbove(sim, px(r.left - sr.left, WINDOWW, sw), px(r.top - sr.top, WINDOWH, sh), px(r.right - sr.left, WINDOWW, sw), close);
		};
		for (auto &[hwnd, r] : wallRects)
			if (fun.closeToss && !now.count(hwnd)) toss(r, true);
		for (auto &[hwnd, r] : now)
			if (fun.openShake && !wallRects.count(hwnd)) toss(r, false);
	}
	// Window lift: a window dragged up fast throws what sits on it
	if (fun.lift > 0)
	{
		auto &els = SimulationData::CRef().elements;
		auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); };
		for (auto &[hwnd, r] : now)
			if (auto it = wallRects.find(hwnd); it != wallRects.end())
				if (int dy = px(r.top - it->second.top, WINDOWH, sh); dy <= -3)
				{
					int x0 = px(r.left - sr.left, WINDOWW, sw), x1 = px(r.right - sr.left, WINDOWW, sw), y0 = px(r.top - sr.top, WINDOWH, sh);
					for (int y = std::max(0, y0 - 80); y < std::min(YRES, y0); y++)
						for (int x = std::max(0, x0); x < std::min(XRES, x1); x++)
							if (int q = sim->pmap[y][x]; q && (els[TYP(q)].Properties & (TYPE_PART | TYPE_LIQUID)))
							{
								auto &p = sim->parts[ID(q)];
								p.vy = std::min(p.vy, dy * fun.lift * 0.5f);
								p.vx += (sim->rng.uniform01() - 0.5f) * 2 * fun.lift;
							}
				}
	}
	wallRects = std::move(now);
	// Busy apps run hot: what sits on a window's top warms toward 20 C + 100 C per core its app keeps busy (at 100%)
	static std::map<HWND, float> busy; // cores in use, sampled every half second
	if (fun.hot <= 0) busy.clear();
	else
	{
		if (sim->currentTick % 30 == 0)
		{
#ifdef __APPLE__
			static std::map<int, std::pair<double, double>> was; // pid: CPU time, when (seconds)
			std::map<int, std::pair<double, double>> is;
			double when = OverlayMac::Now();
			busy.clear();
			for (auto &[hwnd, r] : wallRects)
			{
				int pid = pids[hwnd];
				if (!is.count(pid))
					if (double cpu = OverlayMac::ProcessCpuSeconds(pid); cpu >= 0) is[pid] = { cpu, when };
				auto a = is.find(pid), b = was.find(pid);
				if (a != is.end() && b != was.end() && a->second.second > b->second.second)
					busy[hwnd] = float((a->second.first - b->second.first) / (a->second.second - b->second.second));
			}
			was = std::move(is);
#else
			static std::map<DWORD, std::pair<ULONGLONG, ULONGLONG>> was; // pid: CPU time, when (100 ns)
			std::map<DWORD, std::pair<ULONGLONG, ULONGLONG>> is;
			auto u64 = [](FILETIME f) { return ULARGE_INTEGER{ { f.dwLowDateTime, f.dwHighDateTime } }.QuadPart; };
			FILETIME ft;
			GetSystemTimeAsFileTime(&ft);
			busy.clear();
			for (auto &[hwnd, r] : wallRects)
			{
				DWORD pid = 0;
				GetWindowThreadProcessId(hwnd, &pid);
				if (!is.count(pid))
					if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))
					{
						FILETIME c, e, k, u;
						if (GetProcessTimes(h, &c, &e, &k, &u)) is[pid] = { u64(k) + u64(u), u64(ft) };
						CloseHandle(h);
					}
				auto a = is.find(pid), b = was.find(pid);
				if (a != is.end() && b != was.end() && a->second.second > b->second.second)
					busy[hwnd] = float(a->second.first - b->second.first) / float(a->second.second - b->second.second);
			}
			was = std::move(is);
#endif
		}
		auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); };
		for (auto &[hwnd, cores] : busy)
		{
			auto it = wallRects.find(hwnd);
			if (cores < 0.05f || it == wallRects.end()) continue;
			auto &r = it->second;
			float to = std::min(20 + 100 * cores * fun.hot, 600.f) + 273.15f;
			int x0 = std::max(0, px(r.left - sr.left, WINDOWW, sw)), x1 = std::min(XRES, px(r.right - sr.left, WINDOWW, sw));
			int y0 = px(r.top - sr.top, WINDOWH, sh);
			for (int y = std::max(0, y0 - 3); y < std::min(YRES, y0); y++)
				for (int x = x0; x < x1; x++)
					if (int q = sim->pmap[y][x]; q && sim->parts[ID(q)].temp < to) sim->parts[ID(q)].temp += (to - sim->parts[ID(q)].temp) * 0.05f;
		}
	}
	// The hourglass: glass on the taskbar near its right end; sand drops through the (plugged) neck one grain at a time
	// so the top holds the time left; it refills when it runs out. Rebuilt where anything broke it or the sim was cleared
	if (fun.hourglass >= 1)
	{
		static float minutes = 0;
		static ULONGLONG start = 0;
		if (minutes != fun.hourglass) minutes = fun.hourglass, start = GetTickCount64();
		RECT tr;
		HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
		if (tray && GetWindowRect(tray, &tr))
		{
			auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); };
			int cx = px(tr.right - sr.left, WINDOWW, sw) - 40, by = px(tr.top - sr.top, WINDOWH, sh) - 1, ty = by - 24;
			auto half = [](int r) { return r < 12 ? 7 - r * 6 / 11 : 7 - (24 - r) * 6 / 11; }; // inside half-width, row 0-24
			if (cx > 10 && ty > 10 && by < YRES)
			{
				for (int r = -1; r <= 25; r++) // the glass: both sides of every row, the ends, and the plug in the neck
					for (int x = cx - 9; x <= cx + 9; x++)
					{
						int y = ty + r;
						bool glass = r == -1 || r == 25 || (r == 12 && std::abs(x - cx) <= 1) || (r >= 0 && r <= 24 && std::abs(x - cx) == half(r) + 1);
						if (glass && !sim->pmap[y][x] && !sim->PixelWall(x, y)) sim->create_part(-1, x, y, PT_GLAS);
					}
				// top bulb rows 0-11, bottom 13-24
				int top = 0, bottom = 0, total = 0;
				for (int r = 0; r < 12; r++) total += 2 * half(r) + 1;
				total = total * 3 / 4;
				for (int r = 0; r <= 24; r++)
					for (int x = cx - half(r); x <= cx + half(r); x++)
						if (int q = sim->pmap[ty + r][x]; q && TYP(q) == PT_SAND) (r < 12 ? top : bottom)++;
				double run = (GetTickCount64() - start) / 60000.0 / minutes;
				if (run >= 1) start = GetTickCount64(), run = 0; // ran out: turn it over
				int want = int(std::ceil(total * (1 - run)));
				auto add = [&](int r0, int r1) { // a grain in the lowest free spot of rows r0-r1
					for (int r = r1; r >= r0; r--)
						for (int x = cx - half(r); x <= cx + half(r); x++)
							if (!sim->pmap[ty + r][x]) return sim->create_part(-1, x, ty + r, PT_SAND) >= 0;
					return false;
				};
				auto take = [&](int r0, int r1) { // the lowest grain of rows r0-r1
					for (int r = r1; r >= r0; r--)
						for (int x = cx - half(r); x <= cx + half(r); x++)
							if (int q = sim->pmap[ty + r][x]; q && TYP(q) == PT_SAND) return sim->kill_part(ID(q)), true;
					return false;
				};
				for (int k = 0; k < 3 && top > want; k++, top--, bottom++) // through the neck, falling from just under it
				{
					take(0, 11);
					if (sim->pmap[ty + 13][cx] || sim->create_part(-1, cx, ty + 13, PT_SAND) < 0) add(13, 24);
				}
				for (int k = 0; k < 20 && top < want; k++, top++) // turned over (or cleared): back up top
				{
					if (bottom > 0 && take(13, 24)) bottom--;
					add(0, 11);
				}
				for (int k = 0; k < 20 && top + bottom < total; k++, bottom++) add(13, 24);
			}
		}
	}
	// Notifications: a window that flashed its taskbar button gets a ball dropped on it (one per window every 3 s)
	static std::map<HWND, ULONGLONG> dropped;
	for (HWND hwnd : fun.flashed)
	{
		auto it = wallRects.find(hwnd);
		if (it == wallRects.end() || GetTickCount64() - dropped[hwnd] < 3000) continue;
		dropped[hwnd] = GetTickCount64();
		auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); };
		auto &r = it->second;
		int cx = px((r.left + r.right) / 2 - sr.left, WINDOWW, sw), cy = std::max(12, px(r.top - sr.top, WINDOWH, sh) - 20);
		for (int y = -3; y <= 3; y++)
			for (int x = -3; x <= 3; x++)
				if (x * x + y * y <= 10) sim->create_part(-1, cx + x, cy + y, PT_BALL);
	}
	fun.flashed.clear();
	// Taskbar toys: Ctrl-drag an app's taskbar button off the taskbar and a block in its icon's colour drops from the
	// pointer, thrown the way it moved; an app that opens pops one up out of its button. Blocks are stone in that colour.
	if (fun.taskThrow || fun.taskPop)
	{
		StartTaskButtons();
		auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); };
		auto block = [&](const RECT &b, int cx, int cy, float vx, float vy) {
			uint32_t colour = 0x808080; // the most vivid pixel of the button: its icon
			int x0 = std::max(0, px(b.left - sr.left, WINDOWW, sw)), y0 = std::max(0, px(b.top - sr.top, WINDOWH, sh));
			int x1 = std::min(XRES, px(b.right - sr.left, WINDOWW, sw)), y1 = std::min(YRES, px(b.bottom - sr.top, WINDOWH, sh));
			std::vector<uint32_t> pix(size_t(std::max(0, x1 - x0)) * std::max(0, y1 - y0));
			if (pix.size() && overlayGrabScreen && overlayGrabScreen(x0, y0, x1 - x0, y1 - y0, pix.data()))
				for (int best = 40; auto c : pix)
				{
					int r = c >> 16 & 0xFF, g = c >> 8 & 0xFF, bl = c & 0xFF, s = std::max({ r, g, bl }) - std::min({ r, g, bl });
					if (s > best) best = s, colour = c & 0xFFFFFF;
				}
			for (int y = -5; y <= 5; y++)
				for (int x = -5; x <= 5; x++)
					if (int i = sim->create_part(-1, cx + x, cy + y, PT_STNE); i >= 0)
					{
						auto &part = sim->parts[i];
						part.dcolour = 0xFF000000 | colour;
						part.vx = vx, part.vy = vy;
					}
		};
		std::vector<TaskButton> buttons;
		std::vector<RECT> popped;
		{
			std::lock_guard lock(taskMutex);
			buttons = taskButtons;
			popped.swap(taskPopped);
		}
		for (auto &b : popped)
			if (fun.taskPop) block(b, px((b.left + b.right) / 2 - sr.left, WINDOWW, sw), px(b.top - sr.top, WINDOWH, sh) - 8, 0, -4);
		static bool armed = false, thrown = false;
		static RECT from;
		static POINT last;
		POINT c;
		GetCursorPos(&c);
		if (GetAsyncKeyState(VK_LBUTTON) >= 0) armed = thrown = false;
		else if (fun.taskThrow && !armed && !thrown && GetAsyncKeyState(VK_CONTROL) < 0)
			for (auto &b : buttons)
				if (PtInRect(&b.r, c)) armed = true, from = b.r;
		if (armed && (c.y < from.top - 8 || c.y > from.bottom + 8)) // off the taskbar
		{
			block(from, px(c.x - sr.left, WINDOWW, sw), px(c.y - sr.top, WINDOWH, sh),
			      std::clamp(float(px(c.x - last.x, WINDOWW, sw)), -15.f, 15.f), std::clamp(float(px(c.y - last.y, WINDOWH, sh)), -15.f, 15.f));
			armed = false, thrown = true;
		}
		last = c;
	}
	// Window gravity: gravity leans toward the side the active window is on (half a sideways pull at the screen's edge),
	// swinging over slowly. It takes over plain down gravity only, as custom gravity, and gives it back when switched off.
	// ponytail: a scene saved while leaning keeps custom gravity; reset Gravity by hand if that matters.
	static bool tilting = false;
	if (fun.tilt && (sim->gravityMode == GRAV_VERTICAL || (tilting && sim->gravityMode == GRAV_CUSTOM)))
	{
		float lean = 0;
		if (auto it = wallRects.find(GetForegroundWindow()); it != wallRects.end())
			lean = std::clamp(((it->second.left + it->second.right) / 2 - sr.left) / float(sw) * 2 - 1, -1.f, 1.f) * 0.5f;
		if (!tilting) sim->customGravityX = 0;
		sim->gravityMode = GRAV_CUSTOM, tilting = true;
		sim->customGravityX += (lean - sim->customGravityX) * 0.02f;
		sim->customGravityY = 1;
	}
	else if (tilting)
	{
		if (sim->gravityMode == GRAV_CUSTOM) sim->gravityMode = GRAV_VERTICAL;
		tilting = false;
	}
	// Typing sparks: keys pressed (new input with the pointer still and no button down) now and then puff cold sparks
	// up from the text cursor, or from the top of the window being typed in where the app has no standard one
	if (fun.typing)
	{
#ifdef __APPLE__
		static double lastKey = 0; // when the last key went down
		double keyAt = OverlayMac::Now() - OverlayMac::IdleSeconds(true);
		bool typed = keyAt > lastKey + 0.005;
		lastKey = keyAt;
		POINT at{ -1, -1 };
		if (typed && sim->rng.uniform01() < fun.typingChance / 100)
		{
			double cx, cy;
			// from the window's top: a caret is inside the window, and sparks can't start inside a wall
			if (auto it = wallRects.find(GetForegroundWindow()); it != wallRects.end())
				at = { OverlayMac::Caret(cx, cy) ? std::clamp(long(cx * MacScale()), it->second.left, it->second.right) : it->second.left + long(sim->rng.uniform01() * (it->second.right - it->second.left)), it->second.top };
			else if (OverlayMac::Caret(cx, cy))
				at = { long(cx * MacScale()), long(cy * MacScale()) };
		}
#else
		static DWORD lastInput = 0;
		static POINT lastPoint;
		LASTINPUTINFO li{ sizeof(li) };
		POINT pt;
		GetCursorPos(&pt);
		bool typed = GetLastInputInfo(&li) && li.dwTime != lastInput && pt.x == lastPoint.x && pt.y == lastPoint.y &&
		             GetAsyncKeyState(VK_LBUTTON) >= 0 && GetAsyncKeyState(VK_RBUTTON) >= 0;
		lastInput = li.dwTime, lastPoint = pt;
		GUITHREADINFO gi{ sizeof(gi) };
		POINT at{ -1, -1 };
		if (typed && sim->rng.uniform01() < fun.typingChance / 100)
		{
			if (GetGUIThreadInfo(0, &gi) && gi.hwndCaret)
				at = { gi.rcCaret.left, gi.rcCaret.top }, ClientToScreen(gi.hwndCaret, &at);
			else if (auto it = wallRects.find(GetForegroundWindow()); it != wallRects.end())
				at = { it->second.left + long(sim->rng.uniform01() * (it->second.right - it->second.left)), it->second.top };
		}
#endif
		if (at.x >= 0)
		{
			auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); };
			int x = px(at.x - sr.left, WINDOWW, sw), y = px(at.y - sr.top, WINDOWH, sh) - 3;
			for (int k = 0; k < 4; k++) ColdSpark(sim, x + sim->rng.between(-3, 3), y + sim->rng.between(-2, 0));
		}
	}
	// The volcano: a stone cone on the taskbar left of the hourglass; it smokes while the PC's total CPU use is past the
	// slider, and throws lava while it is 20% past it. Rebuilt where broken, like the hourglass
	static float cpu = 0;
#ifdef __APPLE__
	if (fun.volcano > 0 && sim->currentTick % 30 == 0) cpu = OverlayMac::CpuLoad();
#else
	static ULONGLONG lastIdle = 0, lastTotal = 0;
	if (fun.volcano > 0 && sim->currentTick % 30 == 0)
	{
		FILETIME fi, fk, fu;
		if (GetSystemTimes(&fi, &fk, &fu))
		{
			auto u64 = [](FILETIME t) { return (ULONGLONG(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
			ULONGLONG idleT = u64(fi), total = u64(fk) + u64(fu); // kernel time includes the idle time
			if (lastTotal && total > lastTotal) cpu = 1 - float(idleT - lastIdle) / float(total - lastTotal);
			lastIdle = idleT, lastTotal = total;
		}
	}
#endif
	if (fun.volcano > 0)
	{
		RECT tr;
		HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
		if (tray && GetWindowRect(tray, &tr))
		{
			auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); };
			int cx = px(tr.right - sr.left, WINDOWW, sw) - 100, by = px(tr.top - sr.top, WINDOWH, sh) - 1;
			if (cx > 20 && by > 30 && by < YRES)
			{
				for (int r = 0; r < 14; r++) // rows up from the base; the top two leave a crater
					for (int x = cx - 15 + r; x <= cx + 15 - r; x++)
						if (int y = by - r; !(r >= 12 && std::abs(x - cx) <= 1) && !sim->pmap[y][x] && !sim->PixelWall(x, y))
							if (int i = sim->create_part(-1, x, y, PT_STNE); i >= 0) sim->parts[i].dcolour = 0xFF6B4A35;
				int top = by - 13;
				if (cpu >= fun.volcano && sim->rng.chance(1, 6) && !sim->pmap[top - 2][cx])
					if (int i = sim->create_part(-1, cx, top - 2, PT_SMKE); i >= 0) sim->parts[i].vy = -1;
				// thrown lava (or the stone it cools into) goes after 8 s, so the taskbar doesn't fill up
				static std::deque<std::pair<int, ULONGLONG>> thrown;
				while (!thrown.empty() && GetTickCount64() >= thrown.front().second)
				{
					int i = thrown.front().first, ty = sim->parts[i].type;
					thrown.pop_front();
					if (ty == PT_LAVA || ty == PT_STNE) sim->kill_part(i);
				}
				if (cpu >= std::min(1.f, fun.volcano + 0.2f) && sim->rng.chance(1, 3) && !sim->pmap[top - 3][cx])
					if (int i = sim->create_part(-1, cx, top - 3, PT_LAVA); i >= 0)
					{
						thrown.push_back({ i, GetTickCount64() + 8000 });
						sim->parts[i].vy = -(2 + 2 * sim->rng.uniform01()) * fun.volcanoForce;
						sim->parts[i].vx = (sim->rng.uniform01() - 0.5f) * 2 * fun.volcanoForce;
					}
			}
		}
	}
	static POINT broomAt{ -1, -1 };
	if (broom)
	{
		auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); };
		int cx = px(at.x - sr.left, WINDOWW, sw), cy = px(at.y - sr.top, WINDOWH, sh), b = 5;
		int ox = broomAt.x >= 0 ? broomAt.x : cx, oy = broomAt.x >= 0 ? broomAt.y : cy;
		// the whole path since the last tick is solid (a fast flick would otherwise jump over the sand in between), so
		// what it sweeps over is pushed out ahead of it
		OverlayWall w{ std::max(0, std::min(ox, cx) - b), std::max(0, std::min(oy, cy) - b), std::min(XRES, std::max(ox, cx) + b + 1),
		               std::min(YRES, std::max(oy, cy) + b + 1), cx - ox, cy - oy, [=](int x, int y) {
			               float dx = float(cx - ox), dy = float(cy - oy), l = dx * dx + dy * dy;
			               float t = l > 0 ? std::clamp(((x - ox) * dx + (y - oy) * dy) / l, 0.f, 1.f) : 0;
			               float ex = x - ox - t * dx, ey = y - oy - t * dy;
			               return ex * ex + ey * ey <= float(b * b);
		               } };
		w.id = 2; // not a window handle
		broomAt = { cx, cy };
		if (w.x1 > w.x0 && w.y1 > w.y0) wins.push_back(w);
	}
	else
	{
		broomAt = { -1, -1 };
	}
	for (auto &r : gaps)
	{
		auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); };
		OverlayWall w{ std::max(0, px(r.left - sr.left, WINDOWW, sw)), std::max(0, px(r.top - sr.top, WINDOWH, sh)),
		               std::min(XRES, px(r.right - sr.left, WINDOWW, sw)), std::min(YRES, px(r.bottom - sr.top, WINDOWH, sh)), 0, 0, {} };
		if (w.x1 > w.x0 && w.y1 > w.y0) wins.push_back(w);
	}
#ifdef __APPLE__
	if (windowWalls && dockKnown)
	{
		// The Dock itself: solid, rounded at its ends like the real one, and past them nothing, so what slides off falls to
		// the void at the bottom of the screen
		auto px = [&](double v, int n, double from, int nw) { return int(std::lround((v * MacScale() - from) * n / nw)); };
		int x0 = px(dockL, WINDOWW, sr.left, sw), y0 = px(dockT, WINDOWH, sr.top, sh), x1 = px(dockR, WINDOWW, sr.left, sw), y1 = px(dockB, WINDOWH, sr.top, sh);
		float radius = 18.f * float(MacScale()) * WINDOWW / sw;
		OverlayWall w{ std::max(0, x0), std::max(0, y0), std::min(XRES, x1), std::min(YRES, y1), 0, 0, [=](int x, int y) {
			float dx = std::max({ x0 + radius - x - 0.5f, x + 0.5f - (x1 - radius), 0.f }), dy = std::max({ y0 + radius - y - 0.5f, y + 0.5f - (y1 - radius), 0.f });
			return dx * dx + dy * dy <= radius * radius;
		} };
		w.id = 4; // not a window handle
		if (w.x1 > w.x0 && w.y1 > w.y0) wins.push_back(w);
	}
#endif
	auto icons = doIcons ? DesktopIcons() : std::vector<DeskIcon>{};
	static std::map<uintptr_t, RECT> iconRects; // where each icon was on the last tick
	std::map<uintptr_t, RECT> iconsNow;
	overlayFeel.iconBoxes.clear();
	if (doIcons)
		for (auto &icon : icons)
		{
			auto &r = icon.r;
			auto px = [](long v, int n, int nw) { return int(std::lround(double(v) * n / nw)); };
			// (the Mac's: no cell, so the picture's width again either side and half its height below for the name)
			RECT b = icon.box.right > icon.box.left ? icon.box : RECT{ r.left - (r.right - r.left) / 2, r.top, r.right + (r.right - r.left) / 2, r.bottom + (r.bottom - r.top) / 2 };
			overlayFeel.iconBoxes.push_back({ px(b.left - sr.left, WINDOWW, sw), px(b.top - sr.top, WINDOWH, sh), px(b.right - sr.left, WINDOWW, sw), px(b.bottom - sr.top, WINDOWH, sh) });
			OverlayWall w{ std::max(0, px(r.left - sr.left, WINDOWW, sw)), std::max(0, px(r.top - sr.top, WINDOWH, sh)),
			               std::min(XRES, px(r.right - sr.left, WINDOWW, sw)), std::min(YRES, px(r.bottom - sr.top, WINDOWH, sh)), 0, 0, {} };
			// each sim pixel takes the icon pixel under its centre (the whole square while its picture isn't in yet)
			w.solid = [&icon, sr, sw, sh](int x, int y) {
				int n = icon.r.right - icon.r.left;
				int ix = int(sr.left + (x + 0.5) * sw / WINDOWW) - icon.r.left, iy = int(sr.top + (y + 0.5) * sh / WINDOWH) - icon.r.top;
				return ix >= 0 && iy >= 0 && ix < n && iy < n && (!icon.shape || (*icon.shape)[size_t(iy) * n + ix]);
			};
			if (auto it = iconRects.find(icon.id); it != iconRects.end())
			{
				w.mx = px(r.left - it->second.left, WINDOWW, sw);
				w.my = px(r.top - it->second.top, WINDOWH, sh);
			}
			w.id = icon.id;
			iconsNow[icon.id] = r;
			if (fun.bin && icon.bin) // the Recycle Bin swallows whatever loose stuff lands on it
			{
				auto &elements = SimulationData::CRef().elements;
				for (int y = std::max(0, w.y0 - 3); y < std::min(YRES, w.y1); y++)
					for (int x = w.x0; x < w.x1; x++)
						if (int r = sim->pmap[y][x]; r && !(elements[TYP(r)].Properties & TYPE_SOLID)) sim->kill_part(ID(r));
			}
			if (w.x1 > w.x0 && w.y1 > w.y0) wins.push_back(w);
		}
	iconRects = std::move(iconsNow);
	OverlayBuddy::Walls(wins); // his body, in front

	OverlayApplyWalls(sim, wins);
#ifdef __APPLE__
	// A Mac window's corners are round: past the curve is open, unless something else stands there. Done on the finished
	// mask, so the walls' own passes keep their plain boxes.
	if (sim->pixelWalls.size() == size_t(XRES) * YRES)
	{
		static const float cornerPoints = [] { return OverlayMac::SystemMajor() >= 26 ? 16.f : 10.f; }();
		int r = int(std::lround(cornerPoints * MacScale() * WINDOWW / sw));
		auto covered = [&](const OverlayWall &self, int x, int y) {
			for (auto &o : wins)
				if (&o != &self && x >= o.x0 && x < o.x1 && y >= o.y0 && y < o.y1 && (!o.solid || o.solid(x, y))) return true;
			return false;
		};
		for (auto &w : wins)
		{
			if (!wallRects.count(HWND(w.id)) || w.x1 - w.x0 < 2 * r || w.y1 - w.y0 < 2 * r) continue;
			for (int j = 0; j < r; j++)
				for (int i = 0; i < r; i++)
				{
					float dx = r - i - 0.5f, dy = r - j - 0.5f;
					if (dx * dx + dy * dy <= float(r * r)) continue;
					for (auto [x, y] : { std::pair{ w.x0 + i, w.y0 + j }, { w.x1 - 1 - i, w.y0 + j }, { w.x0 + i, w.y1 - 1 - j }, { w.x1 - 1 - i, w.y1 - 1 - j } })
						if (!covered(w, x, y)) sim->pixelWalls[size_t(y) * XRES + x] = 0;
				}
		}
	}
#endif

	// Explosions shake the real windows they hit (off unless the Shake slider is up): a jolt that dies away, then the
	// window goes back exactly where it was. Not maximised or full-screen ones, and a window you grab stops shaking.
	struct Shaking { POINT home, set; float amp; };
	static std::map<HWND, Shaking> shaking;
	for (auto [id, kick] : overlayKicks)
	{
		HWND hwnd = HWND(id);
		RECT r;
		if (!wallRects.count(hwnd) || IsZoomed(hwnd) || !GetWindowRect(hwnd, &r)) continue; // icons aren't windows
		auto it = shaking.find(hwnd);
		float amp = std::min(40.f, kick * 4 * overlayFeel.shake) * sw / WINDOWW;
		if (it == shaking.end()) shaking[hwnd] = { { r.left, r.top }, { r.left, r.top }, amp };
		else it->second.amp = std::max(it->second.amp, amp);
	}
	for (auto it = shaking.begin(); it != shaking.end();)
	{
		auto &s = it->second;
		RECT r;
#ifdef __APPLE__
		// the window server reports a move a beat late, so where it is says nothing: a held button says it's you
		if (!IsWindow(it->first) || !GetWindowRect(it->first, &r) || GetAsyncKeyState(VK_LBUTTON) < 0)
#else
		if (!IsWindow(it->first) || !GetWindowRect(it->first, &r) || r.left != s.set.x || r.top != s.set.y)
#endif
		{
			it = shaking.erase(it); // closed, or moved by someone else (you): leave it be
			continue;
		}
		s.amp *= 0.8f;
		bool done = s.amp < 1;
		s.set = done ? s.home : POINT{ s.home.x + LONG(std::lround((sim->rng.uniform01() * 2 - 1) * s.amp)), s.home.y + LONG(std::lround((sim->rng.uniform01() * 2 - 1) * s.amp)) };
		SetWindowPos(it->first, nullptr, s.set.x, s.set.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
		it = done ? shaking.erase(it) : std::next(it);
	}
}
