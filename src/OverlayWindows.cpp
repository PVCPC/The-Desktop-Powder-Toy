// Overlay mode's own desktop windows (OVERLAY.md): each menu is a real top-level layered window, drawn here in
// software with per-pixel alpha. The look is a Theme (Windows Vista, 7, 8, 10, Classic Mac, macOS).
#include "OverlayWindows.h"
#include "OverlayWalls.h"
#include "common/Parallel.h"
#include "OverlayBuddy.h"
#ifdef _WIN32
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0602 // IFolderView2 for the desktop icons, IDesktopWallpaper for holes (the build targets XP)
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
#include "simulation/ElementClasses.h"
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
#include "luna_png.h"
#include "xp2_png.h"
#include "skin_vista_png.h"
#include "skin_seven_png.h"
#include "skin_eight_png.h"
#include "skin_ten_png.h"
#include "skin_system6_png.h"
#include "skin_system7_png.h"
#include "templeos_font_ttf.h"
#include "skin_mac9_png.h"
#include "skin_aqua_png.h"
#include "skin_lion_png.h"
#include "skin_yosemite_png.h"
#include "skin_macos_png.h"
#include "skin_tendark_png.h"
#include "skin_macosdark_png.h"
#include "OverlaySkins.h"
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
#include <span>
#include <cstring>
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
#include <uxtheme.h>
#include <vssym32.h>
#pragma comment(lib, "uxtheme.lib")
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")
#include <shlobj.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <exdisp.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <uiautomation.h>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#include <d3d11.h>
#include <dxgi1_2.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <ctime>
#include <deque>
// the Overlay tab's pass-through apps (Overlay.PassApps)
static std::vector<ByteString> PassApps();
static void SetPassApps(const std::vector<ByteString> &apps);
static std::vector<ByteString> RunningApps();
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#undef small       // rpcndr.h, via dwmapi.h
#undef GetUserName // SaveInfo::GetUserName
#elif defined(__APPLE__)
#include "Overlay.h"
#include "gui/game/GameController.h"
#include "gui/game/GameModel.h"
#include "gui/game/GameView.h"
#include "gui/game/Notification.h"
#include "gui/interface/Engine.h"
#include "client/GameSave.h"
#include "client/http/Request.h"
#include "simulation/Simulation.h"
#include "simulation/ElementClasses.h"
#include "simulation/SimulationData.h"
#include "SimulationConfig.h"
#include "prefs/GlobalPrefs.h"
#include "common/platform/Platform.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <codecvt>
#include <cstring>
#include <deque>
#include <locale>
#include <map>
#include <memory>
#include <mutex>
#include <SDL.h>
#include <tuple>
#include <vector>
#include "OverlayMacWin32.h"
#include "gui/game/Menu.h"
#include "gui/game/Brush.h"
#include "gui/game/tool/Tool.h"
#include "gui/game/RenderPreset.h"
#include "gui/game/IntroText.h"
#include "gui/SavePreviewType.h"
#include "gui/options/OptionsModel.h"
#include "gui/dialogues/ErrorMessage.h"
#include "client/Client.h"
#include "client/SaveFile.h"
#include "client/SaveInfo.h"
#include "client/http/SearchSavesRequest.h"
#include "client/http/ThumbnailRequest.h"
#include "graphics/VideoBuffer.h"
#include "graphics/Graphics.h"
#include "graphics/Renderer.h"
#include "simulation/Air.h"
#include "simulation/ElementGraphics.h"
#include "simulation/SaveRenderer.h"
#include "lua/CommandInterface.h"
#include "Format.h"
#include "luna_png.h"
#include "xp2_png.h"
#include "skin_vista_png.h"
#include "skin_seven_png.h"
#include "skin_eight_png.h"
#include "skin_ten_png.h"
#include "skin_system6_png.h"
#include "skin_system7_png.h"
#include "templeos_font_ttf.h"
#include "skin_mac9_png.h"
#include "skin_aqua_png.h"
#include "skin_lion_png.h"
#include "skin_yosemite_png.h"
#include "skin_macos_png.h"
#include "skin_tendark_png.h"
#include "skin_macosdark_png.h"
#include "OverlaySkins.h"
#include "Config.h"
#include "gui/interface/Window.h"
#include "gui/interface/Button.h"
#include "gui/interface/Checkbox.h"
#include "gui/interface/DropDown.h"
#include "gui/interface/Label.h"
#include "gui/interface/Panel.h"
#include "gui/interface/ProgressBar.h"
#include "gui/interface/Slider.h"
#include "gui/interface/Textbox.h"
#include <atomic>
#include <chrono>
#include <cwctype>
#include <functional>
#include <thread>
static std::vector<ByteString> PassApps();
static void SetPassApps(const std::vector<ByteString> &apps);
static std::vector<ByteString> RunningApps();
#endif

#if defined(_WIN32) || defined(__APPLE__)
namespace
	#include "OverlayWindowsSettings.inl"
	#include "OverlayWindowsCanvas.inl"
	#include "OverlayWindowsThemes.inl"
	#include "OverlayWindowsPanel.inl"
	#include "OverlayWindowsSide.inl"
	#include "OverlayWindowsToast.inl"
	#include "OverlayWindowsHosts.inl"
	#include "OverlayWindowsTray.inl"
}

#ifdef __APPLE__
// SDL can miss a button's release when our window goes back to ignoring the mouse mid-press, or another app takes it; TPT
// would keep drawing. The system knows what's really held.
static void ReleaseStuckButtons()
{
	static uint32_t sent = 0;
	int x, y;
	uint32_t held = SDL_GetMouseState(&x, &y);
	sent &= held;
	for (auto [button, index] : { std::pair{ SDL_BUTTON_LEFT, 0 }, { SDL_BUTTON_RIGHT, 1 } })
	{
		uint32_t mask = SDL_BUTTON(button);
		if (!(held & mask) || (sent & mask) || OverlayMac::ButtonDown(index)) continue;
		sent |= mask;
		SDL_Event up{};
		up.type = SDL_MOUSEBUTTONUP;
		up.button.button = uint8_t(button), up.button.state = SDL_RELEASED, up.button.clicks = 1, up.button.x = x, up.button.y = y;
		SDL_PushEvent(&up);
	}
}

// The Dock stands in for the taskbar: its buttons for the throw, a badge that went up is the Mac's "flashed its button",
// an app that started pops a block. Twice a second is as often as the Dock is read.
static void TickMacDock(Simulation *sim)
{
	static std::map<std::string, std::pair<int, bool>> dock; // badge, running
	bool wanted = fun.notify || fun.taskPop || fun.taskThrow;
	if (!wanted)
	{
		dock.clear();
		return;
	}
	if (sim->currentTick % 30) return;
	auto windows = OverlayMac::OtherWindows();
	std::map<std::string, std::pair<int, bool>> now;
	std::vector<TaskButton> buttons;
	double s = MacScale();
	RECT floor;
	GetWindowRect(macDock, &floor);
	for (auto &item : OverlayMac::DockItems())
	{
		RECT r{ long(item.l * s), long(item.t * s), long(item.r * s), long(item.b * s) };
		buttons.push_back({ Platform::WinWiden(item.name), r });
		now[item.name] = { item.badge, item.running };
		auto was = dock.find(item.name);
		if (was == dock.end()) continue; // the first look: nothing has changed yet
		if (fun.notify && item.badge > was->second.first)
			for (auto &w : windows) // front to back: its top window
				if (w.owner == item.name)
				{
					fun.flashed.push_back(HWND(uintptr_t(w.id)));
					break;
				}
		if (fun.taskPop && item.running && !was->second.second)
		{
			std::lock_guard lock(taskMutex);
			taskPopped.push_back({ r.left, std::min(r.top, floor.top), r.right, r.bottom }); // from the sim's floor, the Dock is below it
		}
	}
	dock = std::move(now);
	std::lock_guard lock(taskMutex);
	taskButtons = std::move(buttons);
}

static void MacInit()
{
	overlayFeel.holeBorder = false; // that's for the dark line DWM draws round a window
	OverlayMac::SetShiftSource([] {
		std::vector<OverlayMac::Shift> out;
		for (auto &s : OverlayWindows::WindowShifts())
			out.push_back({ s.x0, s.y0, s.x1, s.y1, s.dx, s.dy, uint32_t(uintptr_t(s.id)) });
		return out;
	});
	// icons as walls are on by default and need this; macOS asks once, and the Welcome tab has the status
	if (iconWalls && !OverlayMac::AccessibilityGranted()) OverlayMac::AskAccessibility();
}
#endif

void OverlayWindows::Init(void *mainHwnd)
{
	mainWindow = HWND(mainHwnd);
	auto &prefs = GlobalPrefs::Ref();
	windowWalls = prefs.Get("Overlay.WindowWalls", true);
	iconWalls = prefs.Get("Overlay.IconWalls", true);
	areaFrame = prefs.Get("Overlay.AreaFrame", false);
	overlayFeel.destructible = prefs.Get("Overlay.Feel.Destructible", false);
	overlayFeel.shardTint = prefs.Get("Overlay.Feel.ShardTint", true);
	overlayFeel.shardParticles = prefs.Get("Overlay.Feel.ShardParticles", false);
	overlayFeel.holeBorder = prefs.Get("Overlay.Feel.HoleBorder", true);
	overlayFeel.wallpaper = prefs.Get("Overlay.Feel.Wallpaper", false);
	overlayFeel.gradient = prefs.Get("Overlay.Gradient", false);
	overlayFeel.gradientTop = uint32_t(prefs.Get("Overlay.GradientTop", int(overlayFeel.gradientTop)));
	overlayFeel.gradientBottom = uint32_t(prefs.Get("Overlay.GradientBottom", int(overlayFeel.gradientBottom)));
	for (auto [v, name] : { std::pair{ &overlayFeel.grip, "Grip" }, { &overlayFeel.fling, "Fling" }, { &overlayFeel.push, "Push" }, { &overlayFeel.wind, "Wind" }, { &overlayFeel.scorch, "Scorch" }, { &overlayFeel.frost, "Frost" }, { &overlayFeel.acid, "Acid" }, { &overlayFeel.water, "Water" }, { &overlayFeel.acidEat, "AcidEat" }, { &overlayFeel.blast, "Blast" }, { &overlayFeel.holeOpacity, "HoleSolid" }, { &overlayFeel.shards, "Shards" }, { &overlayFeel.shardSize, "ShardSize" }, { &overlayFeel.blackHole, "BlackHole" }, { &overlayFeel.shake, "Shake" } })
		*v = prefs.Get(ByteString("Overlay.Feel.") + name, *v);
	fun.weatherKind = prefs.Get("Overlay.Fun.WeatherKind", 0);
	fun.buddyWindows = prefs.Get("Overlay.Buddy.Windows", 1);
	for (auto rows : { std::span<FunRow>(funRows), std::span<FunRow>(buddyRows) })
	for (auto &f : rows)
	{
		if (f.b) *f.b = prefs.Get(f.pref, *f.b);
		if (f.v) *f.v = prefs.Get(f.pref, *f.v);
	}
	// Default UI scale: the larger of Windows' display scaling (TPT is DPI aware) and screen height / 1080, in
	// quarter steps. Overridable.
	HDC screen = GetDC(nullptr);
	float dpiScale = GetDeviceCaps(screen, LOGPIXELSY) / 96.f;
	ReleaseDC(nullptr, screen);
	float heightScale = GetSystemMetrics(SM_CYSCREEN) / 1080.f;
	uiScale = prefs.Get("Overlay.UIScale", std::max(1.f, std::round(std::max(dpiScale, heightScale) * 4) / 4));
	showTPTMenus = prefs.Get("Overlay.ShowTPTMenus", false);
#ifdef __APPLE__
	mainWindow = macOverlay;
	MacInit();
	// this Mac's own look: Liquid Glass needs to see what's behind the window, 11-15 doesn't
	theme = themes[std::clamp(prefs.Get("Overlay.Theme", OverlayMac::ScreenCaptureGranted() ? 17 : 10), 0, int(std::size(themes)) - 1)];
#else
	theme = themes[std::clamp(prefs.Get("Overlay.Theme", defaultTheme), 0, int(std::size(themes)) - 1)];
#endif
	text = new Text;
	text->SetScale();
	UseThemeFont();
	AddTrayIcon();
}

static void TickRecording();
static void TickWindowWalls();

int OverlayWindows::WorkBottom(int x)
{
#ifdef _WIN32
	RECT sr;
	if (!mainWindow || !GetWindowRect(mainWindow, &sr) || sr.right <= sr.left || sr.bottom <= sr.top) return YRES;
	int sw = sr.right - sr.left, sh = sr.bottom - sr.top;
	MONITORINFO mi{ sizeof(mi) };
	if (!GetMonitorInfoW(MonitorFromPoint({ int(sr.left + x * sw / WINDOWW), int(sr.bottom - 1) }, MONITOR_DEFAULTTONEAREST), &mi)) return YRES;
	return std::clamp(int((mi.rcWork.bottom - sr.top) * WINDOWH / sh), 1, YRES);
#else
	// the Dock's top where it is (or the floor where it starts, without Accessibility); past its ends, the screen's bottom
	RECT sr, dock;
	if (!GetWindowRect(macOverlay, &sr) || sr.right <= sr.left || sr.bottom <= sr.top || !GetWindowRect(macDock, &dock)) return YRES;
	long px = sr.left + long(x) * (sr.right - sr.left) / WINDOWW;
	double l, t, r, b;
	if (OverlayMac::DockFrame(l, t, r, b) && (px < l * MacScale() || px > r * MacScale())) return YRES;
	return std::clamp(int((dock.top - sr.top) * WINDOWH / (sr.bottom - sr.top)), 1, YRES);
#endif
}

bool OverlayWindows::ActiveEdges(uintptr_t &id, int &left, int &right, int &top, int &bottom)
{
#ifdef _WIN32
	// as the desktop Interactive Buddy did it: the active window, or the last one that wasn't ours
	static HWND last;
	HWND h = GetForegroundWindow();
	DWORD pid = 0;
	if (h) GetWindowThreadProcessId(h, &pid);
	if (h && pid != GetCurrentProcessId()) last = h;
	h = last;
	wchar_t cls[32] = {};
	RECT sr, outer, client;
	if (!mainWindow || !h || !IsWindow(h) || !IsWindowVisible(h) || IsIconic(h) || !GetWindowRect(mainWindow, &sr) ||
	    sr.right <= sr.left || sr.bottom <= sr.top || !GetWindowRect(h, &outer)) return false;
	GetClassNameW(h, cls, 32);
	if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd")) return false;
	DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &outer, sizeof(outer));
	GetClientRect(h, &client);
	POINT b{ 0, client.bottom };
	ClientToScreen(h, &b);
	MONITORINFO mi{ sizeof(mi) };
	GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
	auto sx = [&](int x) { return int((x - sr.left) * WINDOWW / (sr.right - sr.left)); };
	auto sy = [&](int y) { return int((y - sr.top) * WINDOWH / (sr.bottom - sr.top)); };
	id = uintptr_t(h);
	left = sx(outer.left), right = sx(outer.right);
	top = outer.top - mi.rcWork.top >= 90 ? sy(outer.top) : -1;
	bottom = b.y < mi.rcWork.bottom - 1 && b.y > mi.rcWork.top ? sy(b.y) : -1;
	return right > left;
#else
	// the front app's front window, or the last one that wasn't ours (points; a Mac window has no frame outside its content)
	static uint32_t last;
	static OverlayMac::Rect lastRect;
	int front = OverlayMac::FrontPid();
	auto all = OverlayMac::OtherWindows();
	bool found = false;
	if (front != getpid())
		for (auto &w : all)
			if (w.pid == front)
			{
				last = w.id;
				break;
			}
	for (auto &w : all)
		if (w.id == last) lastRect = w, found = true;
	auto me = OverlayMac::Window();
	if (!found || !last || me.r <= me.l || me.b <= me.t) return false;
	auto &w = lastRect;
	auto sx = [&](double x) { return int((x - me.l) * WINDOWW / (me.r - me.l)); };
	auto sy = [&](double y) { return int((y - me.t) * WINDOWH / (me.b - me.t)); };
	RECT dock;
	double floorPt = GetWindowRect(macDock, &dock) ? dock.top / MacScale() : me.b;
	id = last;
	left = sx(w.l), right = sx(w.r);
	top = w.t - me.t >= 90 / MacScale() + 30 ? sy(w.t) : -1; // (the menu bar's 30 points, then the 90 pixels Windows leaves)
	bottom = w.b < floorPt - 1 && w.b > me.t ? sy(w.b) : -1;
	return right > left;
#endif
}

void OverlayWindows::Tick()
{
	if (!mainWindow || !&GameController::Ref())
	{
		return;
	}
#ifdef __APPLE__
	ReleaseStuckButtons();
	TickMacDock(GameController::Ref().GetModel()->GetSimulation());
	OverlayMac::HoldDockClicks(fun.taskThrow && !overlayWindowed);
	OverlayMac::WantDesktop(!overlayWindowed && iconWalls && OverlayGetLayer() != OverlayLayer::BehindIcons, !overlayWindowed && (windowWalls || fun.bin || fun.notify || fun.taskPop || fun.taskThrow), !overlayWindowed && fun.typing);
#endif
	TickWindowWalls();
	static auto &release = *new std::unique_ptr<http::Request>(); // never destroyed: at exit it would outlive the request manager
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
		// Saved at another pixel size: it covers the same part of the screen at this one. Saved before the size was
		// kept with it: unknown, so the whole sim.
		int was = prefs.Get("Overlay.PlayArea.Scale", 0), now = ui::Engine::Ref().windowFrameOps.scale;
		auto at = [&](const char *k) { return was ? prefs.Get(k, 0) * was / now : 0; };
		if (!was) w = h = 0;
		if (!overlayWindowed) SetPlayArea(at("Overlay.PlayArea.X"), at("Overlay.PlayArea.Y"), w > 0 ? w * was / now : CELLS.X, h > 0 ? h * was / now : CELLS.Y);
		areaRestored = true;
		if (GlobalPrefs::Ref().Get("Overlay.PendingScene", false))
		{
			GlobalPrefs::Ref().Set("Overlay.PendingScene", false);
			LoadSceneTiles(); // from the last run, or a restart at another pixel size
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

void OverlayWindows::SaveScene()
{
#ifdef __APPLE__
	OverlayMac::Shutdown();
#endif
	SaveSceneTiles();
}

void OverlayWindows::ToggleBuddyClicks()
{
	fun.buddyClicks = !fun.buddyClicks;
	GlobalPrefs::Ref().Set("Overlay.Buddy.GameClicks", fun.buddyClicks);
	Notify(fun.buddyClicks ? "Clicks: Buddy's game" : "Clicks: TPT");
}

void OverlayWindows::ClearSim()
{
	GameController::Ref().ClearSim();
	Notify("Cleared");
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

// What a hole eaten in a window shows: the desktop wallpaper behind it, per monitor, in sim pixels (XRES x YRES,
// 0x00RRGGBB). Rebuilt now and then while there are holes (slideshows, moved overlay).
// ponytail: wallpaper drawn as "fill" (cover, centred) whatever the fit setting; read IDesktopWallpaper::GetPosition if it matters.
#ifdef _WIN32
static const std::vector<uint32_t> &HoleBackdrop(RECT sr, int sw, int sh)
{
	static std::vector<uint32_t> backdrop;
	static ULONGLONG built = 0;
	static RECT builtFor{};
	if (!backdrop.empty() && GetTickCount64() - built < 30000 && EqualRect(&sr, &builtFor)) return backdrop;
	built = GetTickCount64(), builtFor = sr;
	backdrop.assign(size_t(XRES) * YRES, 0x1A1A1A);
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); // already on (S_FALSE) or another mode: either is fine
	IDesktopWallpaper *wall = nullptr;
	IWICImagingFactory *wic = nullptr;
	UINT monitors = 0;
	if (FAILED(CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&wall))) ||
	    FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
	    FAILED(wall->GetMonitorDevicePathCount(&monitors)))
		monitors = 0;
	COLORREF solid = 0x1A1A1A;
	if (wall) wall->GetBackgroundColor(&solid);
	for (UINT i = 0; i < monitors; i++)
	{
		LPWSTR id = nullptr, file = nullptr;
		RECT mr;
		if (FAILED(wall->GetMonitorDevicePathAt(i, &id)) || FAILED(wall->GetMonitorRECT(id, &mr)))
		{
			CoTaskMemFree(id);
			continue;
		}
		wall->GetWallpaper(id, &file);
		std::vector<uint8_t> px;
		UINT iw = 0, ih = 0;
		IWICBitmapDecoder *dec = nullptr;
		IWICBitmapFrameDecode *frame = nullptr;
		IWICFormatConverter *conv = nullptr;
		if (file && *file && SUCCEEDED(wic->CreateDecoderFromFilename(file, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) &&
		    SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
		    SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) &&
		    SUCCEEDED(conv->GetSize(&iw, &ih)) && iw && ih)
		{
			px.resize(size_t(iw) * ih * 4);
			if (FAILED(conv->CopyPixels(nullptr, iw * 4, UINT(px.size()), px.data()))) px.clear();
		}
		for (IUnknown *p : std::initializer_list<IUnknown *>{ conv, frame, dec })
			if (p) p->Release();
		CoTaskMemFree(file);
		CoTaskMemFree(id);
		double mw = mr.right - mr.left, mh = mr.bottom - mr.top;
		double scale = px.empty() ? 1 : std::max(mw / iw, mh / ih), ox = (mw - iw * scale) / 2, oy = (mh - ih * scale) / 2;
		for (int y = 0; y < YRES; y++)
			for (int x = 0; x < XRES; x++)
			{
				double sx = sr.left + (x + 0.5) * sw / WINDOWW, sy = sr.top + (y + 0.5) * sh / WINDOWH; // screen point
				if (sx < mr.left || sx >= mr.right || sy < mr.top || sy >= mr.bottom) continue;
				auto &b = backdrop[size_t(y) * XRES + x];
				if (px.empty())
				{
					b = GetRValue(solid) << 16 | GetGValue(solid) << 8 | GetBValue(solid);
					continue;
				}
				int ix = std::clamp(int((sx - mr.left - ox) / scale), 0, int(iw) - 1), iy = std::clamp(int((sy - mr.top - oy) / scale), 0, int(ih) - 1);
				auto *q = &px[(size_t(iy) * iw + ix) * 4];
				b = q[2] << 16 | q[1] << 8 | q[0];
			}
	}
	if (wic) wic->Release();
	if (wall) wall->Release();
	return backdrop;
}

#endif // HoleBackdrop
static std::map<HWND, RECT> wallRects, frameRects; // the wall windows when the sim last read them / at the frame drawn
static std::mutex frameLock; // frameRects: WindowShifts is also asked from the GPU drawing thread

#ifdef _WIN32
// What is really behind holed windows: the windows under each one, captured with PrintWindow a few times a second on
// a worker thread (scaled to sim pixels), over the wallpaper.
namespace
{
	struct Shot { int w = 0, h = 0; std::vector<uint32_t> px; }; // the whole window (GetWindowRect), in sim pixels
	std::mutex shotLock;
	std::map<HWND, Shot> shots;
	std::vector<HWND> shotWanted;
	RECT shotScreen{}; // the overlay's screen rect: sim pixel size
}

static void ShotWorker()
{
	HBITMAP bmp = nullptr;
	void *bits = nullptr;
	int bmpW = 0, bmpH = 0;
	for (;;)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		std::vector<HWND> want;
		RECT sr;
		{
			std::lock_guard lock(shotLock);
			want = shotWanted;
			sr = shotScreen;
		}
		std::sort(want.begin(), want.end());
		want.erase(std::unique(want.begin(), want.end()), want.end()); // under several holed windows: shot once
		if (want.empty()) // nothing holed: let the big bitmap go
		{
			if (bmp) DeleteObject(bmp), bmp = nullptr, bmpW = bmpH = 0;
			continue;
		}
		int sw = sr.right - sr.left, sh = sr.bottom - sr.top;
		std::map<HWND, Shot> got;
		for (auto hwnd : want)
		{
			RECT r;
			if (sw <= 0 || sh <= 0 || !GetWindowRect(hwnd, &r) || r.right <= r.left || r.bottom <= r.top) continue;
			int ww = r.right - r.left, wh = r.bottom - r.top;
			HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
			if (!bmp || bmpW < ww || bmpH < wh) // one bitmap, as big as the biggest window so far (not one per window per shot)
			{
				if (bmp) DeleteObject(bmp);
				bmpW = std::max(bmpW, ww), bmpH = std::max(bmpH, wh);
				BITMAPINFO bi{};
				bi.bmiHeader = { sizeof(BITMAPINFOHEADER), bmpW, -bmpH, 1, 32, BI_RGB };
				bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
				if (!bmp) bmpW = bmpH = 0;
			}
			if (bmp)
			{
				auto old = SelectObject(dc, bmp);
				if (PrintWindow(hwnd, dc, 2 /* PW_RENDERFULLCONTENT: GPU-drawn windows too */))
				{
					Shot s{ std::max(1, ww * WINDOWW / sw), std::max(1, wh * WINDOWH / sh), {} };
					s.px.resize(size_t(s.w) * s.h);
					for (int y = 0; y < s.h; y++)
						for (int x = 0; x < s.w; x++)
						{
							int ix = std::min(ww - 1, int((x + 0.5) * sw / WINDOWW)), iy = std::min(wh - 1, int((y + 0.5) * sh / WINDOWH));
							s.px[size_t(y) * s.w + x] = ((uint32_t *)bits)[size_t(iy) * bmpW + ix] & 0xFFFFFF;
						}
					got[hwnd] = std::move(s);
				}
				SelectObject(dc, old);
			}
			DeleteDC(dc);
			ReleaseDC(nullptr, screen);
		}
		std::lock_guard lock(shotLock);
		shots = std::move(got);
	}
}

// The wallpaper, and in each holed window's holes the windows under it, topmost first.
static const uint32_t *HoleBehind(const Simulation *sim, RECT sr)
{
	int sw = sr.right - sr.left, sh = sr.bottom - sr.top;
	static std::vector<uint32_t> behind;
	behind = HoleBackdrop(sr, sw, sh);
	static bool started = false;
	if (!started) std::thread(ShotWorker).detach(), started = true;
	std::vector<HWND> want;
	std::lock_guard lock(shotLock);
	shotScreen = sr;
	auto px = [](long v, int n, int nw) { return int(std::floor(double(v) * n / nw)); }; // screen -> sim pixels
	for (auto &[id, m] : sim->windowMarks)
	{
		HWND top = HWND(id);
		if (!m.any[Simulation::MarkHole] || !wallRects.count(top)) continue; // icons: just the wallpaper
		auto &hole = m.a[Simulation::MarkHole];
		struct Under { int x0, y0; const Shot *shot; uintptr_t id; bool desk; RECT seen; }; // seen: its visible frame, sim px
		std::vector<Under> under;
		for (HWND h = GetWindow(top, GW_HWNDNEXT); h && under.size() < 12; h = GetWindow(h, GW_HWNDNEXT))
		{
			BOOL cloaked = FALSE;
			DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
			wchar_t cls[32];
			GetClassNameW(h, cls, 32);
			RECT r;
			bool desk = !wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW"); // the desktop: icons, and a live wallpaper app's window
			if (h == mainWindow || !IsWindowVisible(h) || IsIconic(h) || cloaked || !GetWindowRect(h, &r))
				continue;
			int x0 = px(r.left - sr.left, WINDOWW, sw), y0 = px(r.top - sr.top, WINDOWH, sh);
			int x1 = px(r.right - sr.left, WINDOWW, sw), y1 = px(r.bottom - sr.top, WINDOWH, sh);
			if (x1 <= m.x || y1 <= m.y || x0 >= m.x + m.w || y0 >= m.y + m.h) continue;
			want.push_back(h);
			auto it = shots.find(h);
			// the shot takes in the invisible resize borders round the frame, black: not what is seen there
			RECT f = r;
			if (!desk) DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &f, sizeof(f));
			RECT seen{ px(f.left - sr.left, WINDOWW, sw), px(f.top - sr.top, WINDOWH, sh), px(f.right - sr.left, WINDOWW, sw), px(f.bottom - sr.top, WINDOWH, sh) };
			if (it != shots.end()) under.push_back({ x0, y0, &it->second, uintptr_t(h), desk, seen });
		}
		for (int j = 0; j < m.h; j++)
			for (int i = 0; i < m.w; i++)
			{
				int x = m.x + i, y = m.y + j;
				if (!hole[size_t(j) * m.w + i] || x < 0 || y < 0 || x >= XRES || y >= YRES) continue;
				for (auto &u : under)
				{
					int sx = x - u.x0, sy = y - u.y0;
					if (sx < 0 || sy < 0 || sx >= u.shot->w || sy >= u.shot->h) continue;
					if (x < u.seen.left || y < u.seen.top || x >= u.seen.right || y >= u.seen.bottom) continue;
					if (auto o = sim->windowMarks.find(u.id); o != sim->windowMarks.end() && o->second.any[Simulation::MarkHole]) // broken through here too
					{
						auto &om = o->second;
						int ox = x - om.x, oy = y - om.y;
						if (ox >= 0 && oy >= 0 && ox < om.w && oy < om.h && om.a[Simulation::MarkHole][size_t(oy) * om.w + ox]) continue;
					}
					uint32_t c = u.shot->px[size_t(sy) * u.shot->w + sx];
					if (u.desk && !c) continue; // the desktop window drew nothing here: the wallpaper file below
					behind[size_t(y) * XRES + x] = c;
					break;
				}
			}
	}
	shotWanted = std::move(want);
	return behind.data();
}
#else
// What is really behind holed windows: a capture of everything under each one, a few times a second (OverlayMac's worker
// thread), over the wallpaper. Without Screen Recording, just the wallpaper.
static const uint32_t *HoleBehind(const Simulation *sim, RECT sr)
{
	static std::vector<uint32_t> wallpaper, behind;
	static ULONGLONG built = 0;
	int sw = sr.right - sr.left;
	if (wallpaper.empty() || GetTickCount64() - built > 30000)
	{
		built = GetTickCount64();
		wallpaper.resize(size_t(XRES) * YRES);
		// the sim is the window's top-left corner: the wallpaper is read for the window, then cut down to it
		std::vector<uint32_t> whole(size_t(WINDOWW) * WINDOWH);
		OverlayMac::Wallpaper(WINDOWW, WINDOWH, whole.data());
		for (int y = 0; y < YRES; y++) std::copy_n(whole.begin() + size_t(y) * WINDOWW, XRES, wallpaper.begin() + size_t(y) * XRES);
	}
	behind = wallpaper;
	std::vector<uint32_t> want;
	auto px = [](long v, int n, int nw) { return int(std::floor(double(v) * n / nw)); }; // screen -> sim pixels
	for (auto &[id, m] : sim->windowMarks)
	{
		auto it = wallRects.find(HWND(id));
		if (!m.any[Simulation::MarkHole] || it == wallRects.end()) continue; // icons: just the wallpaper
		want.push_back(uint32_t(id));
		auto shot = OverlayMac::TakeShot(uint32_t(id));
		if (!shot) continue;
		int x0 = px(it->second.left - sr.left, WINDOWW, sw), y0 = px(it->second.top - sr.top, WINDOWH, sr.bottom - sr.top);
		auto &hole = m.a[Simulation::MarkHole];
		for (int j = 0; j < m.h; j++)
			for (int i = 0; i < m.w; i++)
			{
				int x = m.x + i, y = m.y + j, sx = x - x0, sy = y - y0;
				if (!hole[size_t(j) * m.w + i] || x < 0 || y < 0 || x >= XRES || y >= YRES || sx < 0 || sy < 0 || sx >= shot->w || sy >= shot->h) continue;
				behind[size_t(y) * XRES + x] = shot->px[size_t(sy) * shot->w + sx];
			}
	}
	OverlayMac::WantShots(want, double(WINDOWW) * MacScale() / sw);
	return behind.data();
}
#endif

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
	// a row at a time (the play area and zoom box are spans of it), so the inner loop is plain enough to vectorise
	auto ab = area.BottomRight(), zb = zoom.BottomRight();
	OverlayBands(YRES, [&](int y0, int y1) {
	std::vector<uint32_t> saved;
	for (int y = y0; y < y1; y++)
	{
		auto *row = argb + size_t(y) * w;
		auto *occ = occupied + size_t(y) * XRES;
		bool inZoom = zoomOn && y >= zoom.pos.Y && y <= zb.Y;
		int zx0 = inZoom ? std::clamp(zoom.pos.X, 0, XRES) : 0, zx1 = inZoom ? std::clamp(zb.X + 1, 0, XRES) : 0;
		int zn = std::max(0, zx1 - zx0);
		saved.assign(row + zx0, row + zx0 + zn); // the magnifier is drawn as it is
		if (y < area.pos.Y || y > ab.Y)
			std::fill(row, row + XRES, 0u);
		else
		{
			int ax0 = std::clamp(area.pos.X, 0, XRES), ax1 = std::clamp(ab.X + 1, 0, XRES);
			std::fill(row, row + ax0, 0u);
			std::fill(row + ax1, row + XRES, 0u);
			for (int x = ax0; x < ax1; x++)
			{
				uint32_t p = row[x];
				uint8_t o = occ[x];
				// a window's wall (as of the frame: the live ones may have moved since) shows the window; a black particle is solid
				row[x] = (o & 2) ? 0u : (!(p & 0xFFFFFF) && (o & 1)) ? 0xFF000000u : p;
			}
		}
		for (int i = 0; i < zn; i++) row[zx0 + i] = saved[i] | 0xFF000000;
	}
	});
	const uint32_t *backdrop = nullptr;
	if (std::any_of(sim->windowMarks.begin(), sim->windowMarks.end(), [](auto &m) { return m.second.any[Simulation::MarkHole]; }))
	{
		RECT sr;
		GetWindowRect(mainWindow, &sr);
		if (sr.right > sr.left && sr.bottom > sr.top) backdrop = HoleBehind(sim, sr);
	}
	OverlayDrawMarks(sim, argb, w, occupied, 2, backdrop);
	if (!full)
	{
		// A faint frame just outside the area, so you can see where it ends (Simulation tab: Show its edge).
		auto br = area.BottomRight();
		// Around it, a band you can't see but can grab to drag the edges (alpha 0 lets clicks through).
		auto mark = [&](int x, int y, uint32_t c) {
			if (x >= 0 && y >= 0 && x < XRES && y < YRES) argb[y * w + x] = c;
		};
		for (int d = 4; d >= 1; d--)
		{
			uint32_t c = d == 1 && areaFrame ? 0x785080C0 : 0x01000000;
			for (int x = area.pos.X - d; x <= br.X + d; x++) mark(x, area.pos.Y - d, c), mark(x, br.Y + d, c);
			for (int y = area.pos.Y - d + 1; y < br.Y + d; y++) mark(area.pos.X - d, y, c), mark(br.X + d, y, c);
		}
	}
}

#ifdef _WIN32
// The desktop icons: where each sits on screen (grid or free placement) and its shape (the icon's own picture from the
// shell, opaque pixels solid), through the desktop's IFolderView. Positions are re-read every few frames, so dragged
// icons push sand live; shapes are cached per file and size. Empty when Explorer isn't there or icons are hidden. All
// of it runs on its own thread (fetching icon pictures takes seconds at first), so frames never wait on Explorer.
struct DeskIcon
{
	RECT r;
	std::shared_ptr<const std::vector<uint8_t>> shape; // (r.right - r.left) squared, 1 = solid; null = the whole square
	uintptr_t id; // the same item from pass to pass (a hash of its item ID)
	bool bin = false; // the Recycle Bin
	RECT box{};       // the whole item, its name under the picture included (empty: r)
};
// Null (a plain square for now) when not cached yet and this pass's budget of new pictures is spent: a desktop can
// hold hundreds of icons, and fetching them all at once takes seconds.
static std::shared_ptr<const std::vector<uint8_t>> IconShape(PCUITEMID_CHILD child, int size, int &budget)
{
	// keyed by the item's ID bytes (a name lookup per icon per read cost more than the rest of the read)
	static std::map<std::string, std::shared_ptr<const std::vector<uint8_t>>> cache;
	std::string key = std::to_string(size) + "|" + std::string((const char *)child, ILGetSize(child));
	if (auto it = cache.find(key); it != cache.end()) return it->second;
	if (budget-- <= 0) return nullptr;
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
	if (FAILED(view->GetViewModeAndIconSize(&mode, &size)) || FAILED(view->GetSpacing(&spacing)) ||
	    FAILED(view->Items(SVGIO_ALLVIEW, IID_PPV_ARGS(&items))))
	{
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
				auto id = std::hash<std::string_view>{}({ (const char *)item, ILGetSize(item) });
				static PIDLIST_ABSOLUTE binItem = [] {
					PIDLIST_ABSOLUTE p = nullptr;
					SHGetKnownFolderIDList(FOLDERID_RecycleBinFolder, 0, nullptr, &p);
					return p;
				}();
				bool bin = binItem && ILIsEqual(binItem, PCIDLIST_ABSOLUTE(item)); // a desktop item's ID is also its full one
				icons.push_back({ { x, y, x + size, y + size }, IconShape(item, size, budget), id, bin });
				icons.back().box = { x + size / 2 - spacing.x / 2, y - 8, x + size / 2 + spacing.x / 2, y - 8 + spacing.y }; // its grid cell
			}
			CoTaskMemFree(item);
		}
	}
	items->Release();
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
			SetThreadDescription(GetCurrentThread(), L"overlay desktop icons");
			CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
			IFolderView2 *view = nullptr;
			HWND list = nullptr;
			int still = 0; // reads since the icons last changed
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
					bool same = std::equal(icons.begin(), icons.end(), latest.begin(), latest.end(), [](auto &a, auto &b) {
						return EqualRect(&a.r, &b.r) && a.shape == b.shape;
					});
					still = same ? still + 1 : 0;
					latest = std::move(icons);
				}
				// A read is ~20 ms of round trips to Explorer: fast only while icons may be moving (they just changed, or
				// a button is held on the desktop), otherwise twice a second.
				auto dragging = [] {
					wchar_t cls[16]{};
					GetClassNameW(GetForegroundWindow(), cls, 16);
					return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) && (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW"));
				};
				if (!view) Sleep(had ? 0 : 1000); // looking for Explorer: once a second
				else if (still < 25 || dragging()) Sleep(8);
				else for (int i = 0; i < 20 && !dragging(); i++) Sleep(25);
			}
		}).detach();
		return true;
	}();
	(void)started;
	std::lock_guard g(lock);
	return latest;
}

#else
// The desktop icons and the Trash, as OverlayMac's Accessibility thread last read them (none without the permission)
struct DeskIcon
{
	RECT r;
	std::shared_ptr<const std::vector<uint8_t>> shape; // (r.right - r.left) squared, 1 = solid; null = the whole box
	uintptr_t id;
	bool bin = false; // the Trash
	RECT box{};       // the whole item, its name under the picture included (empty: r)
};

static std::vector<DeskIcon> DesktopIcons()
{
	std::vector<DeskIcon> out;
	double s = MacScale();
	for (auto &i : OverlayMac::Icons())
		out.push_back({ { long(i.l * s), long(i.t * s), long(i.r * s), long(i.b * s) }, i.shape, uintptr_t(i.id), false });
	if (fun.bin)
	{
		// The Dock is under the sim: the Trash is a low block on the floor above it, as wide as its icon
		RECT floor;
		GetWindowRect(macDock, &floor);
		for (auto &d : OverlayMac::DockItems())
			if (d.trash) out.push_back({ { long(d.l * s), floor.top - long(6 * s), long(d.r * s), floor.top }, nullptr, 3, true });
	}
	return out;
}
#endif

// Every frame: every visible window's rectangle, in sim pixels, blocks particles (Simulation::pixelWalls), so they
// pile on title bars and flow around windows. A window moving onto particles shoves each one along its move to the
// first free pixel past it, at the window's speed; one that appeared or grew lifts them out over its top. One with
// no room left stays put, hidden under the window (never deleted).

void OverlayWindows::FrameWalls(std::vector<uint8_t> &mask, uint8_t bit)
{
	{
		std::lock_guard lock(frameLock);
		frameRects = wallRects;
	}
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
	std::lock_guard lock(frameLock);
#ifdef __APPLE__
	if (frameRects.empty()) return out;
	auto live = OverlayMac::OtherWindows(); // one read for them all, at every screen refresh
#endif
	for (auto &[hwnd, at] : frameRects)
	{
		RECT r;
#ifdef _WIN32
		if (DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)) != S_OK) GetWindowRect(hwnd, &r);
#else
		auto it = std::find_if(live.begin(), live.end(), [hwnd = hwnd](auto &w) { return w.id == uint32_t(uintptr_t(hwnd)); });
		if (it == live.end()) continue;
		r = MacRect(*it);
#endif
		if (r.left == at.left && r.top == at.top) continue;
		out.push_back({ px(at.left - sr.left, WINDOWW, sw), px(at.top - sr.top, WINDOWH, sh), px(at.right - sr.left, WINDOWW, sw),
		                px(at.bottom - sr.top, WINDOWH, sh), float(double(r.left - at.left) * WINDOWW / sw),
		                float(double(r.top - at.top) * WINDOWH / sh), hwnd });
	}
	return out;
}

#ifdef _WIN32
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

// Throws what sits in the band above a sim rectangle into the air: up hard when the window under it closes, a
// lighter bump when one opens below it.
#else
static std::vector<ByteString> PassApps()
{
	std::vector<ByteString> out;
	for (auto &s : GlobalPrefs::Ref().Get("Overlay.PassApps", ByteString("")).PartitionBy(';'))
		if (s.size()) out.push_back(s);
	return out;
}

static void SetPassApps(const std::vector<ByteString> &apps)
{
	ByteString all;
	for (auto &a : apps) all += (all.size() ? ";" : "") + a;
	GlobalPrefs::Ref().Set("Overlay.PassApps", all);
}

static std::vector<ByteString> RunningApps()
{
	std::vector<ByteString> out;
	for (auto &w : OverlayMac::OtherWindows())
		if (w.owner.size() && std::find(out.begin(), out.end(), ByteString(w.owner).ToLower()) == out.end()) out.push_back(ByteString(w.owner).ToLower());
	return out;
}

ByteString OverlayWindows::PassApp(void *)
{
	auto front = OverlayMac::FrontName();
	for (auto &a : PassApps())
		if (std::string(a.ToLower()) == front) return a;
	return {};
}
#endif
static void TossAbove(Simulation *sim, int x0, int y0, int x1, bool close)
{
	auto &elements = SimulationData::CRef().elements;
	for (int y = std::max(0, y0 - (close ? 40 : 80)); y < std::min(YRES, y0); y++)
		for (int x = std::max(0, x0); x < std::min(XRES, x1); x++)
		{
			int r = sim->pmap[y][x];
			if (!r || !(elements[TYP(r)].Properties & (TYPE_PART | TYPE_LIQUID))) continue;
			auto &p = sim->parts[ID(r)];
			p.vy -= close ? 1.5f + 3 * sim->rng.uniform01() : 1 + 2 * sim->rng.uniform01();
			p.vx += (sim->rng.uniform01() - 0.5f) * (close ? 5 : 2);
		}
}
#include "OverlayWindowsFun.inl"

#include "OverlayWindowsWalls.inl"

void OverlayWindows::DrawExtras(uint32_t *argb, int w, int h)
{
	auto *sim = GameController::Ref().GetModel()->GetSimulation();
	auto put = [&](int x, int y, uint32_t rgb, float a) { // premultiplied "over"
		if (x < 0 || y < 0 || x >= std::min(w, XRES) || y >= std::min(h, YRES) || a <= 0) return;
		auto &p = argb[y * w + x];
		auto mix = [&](int s) { return uint32_t(((rgb >> s) & 0xFF) * a + ((p >> s) & 0xFF) * (1 - a)) << s; };
		p = mix(0) | mix(8) | mix(16) | (uint32_t(255 * a + (p >> 24) * (1 - a)) << 24);
	};
	// the rainbow: a faint, see-through arc whose colours blend into each other, soft at both edges and fading toward
	// its feet; only where nothing else is drawn (behind it all), not over windows; fades in and out
	if (auto now = GetTickCount64(); now < rainbowUntil)
	{
		float left = (rainbowUntil - now) / 1000.f;
		float a = fun.rainbowGlow * 0.45f * std::min({ 1.f, (fun.rainbow - left) / 3, left / 3 });
		auto play = sim->PlayRect();
		int ph = play.size.Y, rows = std::max(7, ph / 14), cx = play.pos.X + play.size.X / 2, cy = play.pos.Y + ph + ph / 5;
		auto arc = [&](int outer, int rows, bool flip, float a) { // red outside (flip: inside)
			static const uint32_t stops[] = { 0xFF2020, 0xFF8C00, 0xFFE000, 0x30C030, 0x2080FF, 0x4B30C0, 0x9040E0 };
			for (int k = 0; k < rows; k++)
			{
				float f = 6.f * (flip ? rows - 1 - k : k) / (rows - 1), mix = f - int(f); // between colour stop int(f) and the next
				uint32_t c0 = stops[int(f)], c1 = stops[std::min(6, int(f) + 1)], rgb = 0;
				for (int s = 0; s < 24; s += 8) rgb |= uint32_t(((c0 >> s) & 0xFF) * (1 - mix) + ((c1 >> s) & 0xFF) * mix) << s;
				float edge = std::sin(float(M_PI) * (k + 0.5f) / rows); // soft inner and outer edges
				int r = outer - k, n = int(3.2f * r);
				for (int i = 0; i <= n; i++)
				{
					float t = float(M_PI) * (1 + float(i) / n), up = -std::sin(t); // 1 at the top, 0 at the feet
					int x = cx + int(r * std::cos(t)), y = cy + int(r * std::sin(t));
					if (x >= 0 && y >= 0 && x < std::min(w, XRES) && y < std::min(h, YRES) && !(argb[y * w + x] >> 24) && !sim->PixelWall(x, y))
						put(x, y, rgb, a * edge * std::min(1.f, up * 1.5f));
				}
			}
		};
		int outer = cy - play.pos.Y - ph / 4;
		arc(outer, rows, false, a);
		if (rainbowTwice) arc(outer + rows * 2, rows * 13 / 10, true, a * 0.4f); // the double: wider, fainter, colours reversed
	}
	// shooting stars: at night, now and then a bright head with a fading tail streaks down across the open desktop
	{
		// big: a meteor (slower, glowing orange, a smoke tail; it always comes down and crashes)
		struct Star { float x, y, dx, dy; ULONGLONG born; bool land, big; float hit = -1; ULONGLONG hitAt = 0; }; // land: comes down until it hits something
		static std::vector<Star> stars;
		static ULONGLONG nextStar = 0, nextMeteor = 0;
		auto now = GetTickCount64();
		bool night = funHour >= 20 || funHour < 5;
		// a meteor shower night: the peaks (month, day) of the yearly showers, and the day either side
		bool shower = false;
		if (fun.showers && night)
		{
			time_t tt = time(nullptr) - (funHour < 5 ? 86400 : 0); // after midnight it is still the night before
			tm local;
			localtime_s(&local, &tt);
			for (auto [mo, d] : { std::pair{ 1, 3 }, { 4, 22 }, { 5, 6 }, { 8, 12 }, { 10, 21 }, { 11, 17 }, { 12, 14 } })
				shower |= local.tm_mon + 1 == mo && std::abs(local.tm_mday - d) <= 1;
		}
		float perHour = shower ? std::max(fun.stars, 6.f) * 10 : fun.stars, meteorsPerHour = fun.meteors * (shower ? 3 : 1);
		auto spawn = [&](bool land, bool big) {
			auto play = sim->PlayRect();
			float dir = sim->rng.chance(1, 2) ? 1.f : -1.f, len = play.size.X * (0.15f + 0.15f * sim->rng.uniform01());
			stars.push_back({ play.pos.X + play.size.X * (0.15f + 0.7f * sim->rng.uniform01()), play.pos.Y + play.size.Y * 0.4f * sim->rng.uniform01(), dir * len, len * (big ? 0.9f : land ? 0.8f : 0.35f), now, land, big });
		};
		if (perHour < 1 || !night) nextStar = 0;
		else if (!nextStar) nextStar = now + ULONGLONG(3600000.0 / perHour * (0.3 + 1.4 * sim->rng.uniform01()));
		else if (now >= nextStar) nextStar = 0, spawn(fun.starsLand && sim->rng.chance(1, 3), false);
		if (meteorsPerHour < 1 || !night) nextMeteor = 0;
		else if (!nextMeteor) nextMeteor = now + ULONGLONG(3600000.0 / meteorsPerHour * (0.3 + 1.4 * sim->rng.uniform01()));
		else if (now >= nextMeteor) nextMeteor = 0, spawn(true, true);
		for (auto &st : stars)
		{
			float span = st.big ? 1600.f : 900.f, t = (now - st.born) / span, fade = st.land ? 1 : t > 0.7f ? (1 - t) / 0.3f : 1;
			float len = std::hypot(st.dx, st.dy), ux = st.dx / len, uy = st.dy / len;
			if (st.land && st.hit < 0)
				for (float d = 0; d <= len * t; d += 1) // the first window, particle or screen edge on its way down
				{
					int x = int(st.x + ux * d), y = int(st.y + uy * d);
					bool out = x < 0 || y < 0 || x >= XRES || y >= YRES;
					if (out || (d > 30 && (sim->PixelWall(x, y) || sim->pmap[y][x]))) // (not what it started over)
					{
						st.hit = d, st.hitAt = now;
						if (!out) overlayImpacts.push_back({ x, y, st.big ? 3.f : 1.f });
						break;
					}
				}
			if (st.hit >= 0) t = st.hit / len, fade = std::max(0.f, 1 - (now - st.hitAt) / 400.f); // it hit: the tail fades
			float maxTail = st.big ? 160.f : 90.f, hx = st.x + st.dx * t, hy = st.y + st.dy * t, tail = std::min(len * t, maxTail);
			for (int k = 0; k < int(tail); k++) // head first, the tail falls behind and fades
			{
				uint32_t c = !st.big ? (k < 3 ? 0xFFFFFF : 0xCFE0FF) : k < 4 ? 0xFFF4D0 : k < 30 ? 0xFFB040 : k < 70 ? 0xE05A20 : 0x6A6460; // a meteor: white-hot, fire, then smoke
				for (int o = 0; o < (st.big && k < 90 ? 3 : 1); o++) // a meteor is 3 px thick
				{
					int x = int(hx - ux * k - uy * (o - (st.big ? 1 : 0))), y = int(hy - uy * k + ux * (o - (st.big ? 1 : 0)));
					if (x >= 0 && y >= 0 && x < std::min(w, XRES) && y < std::min(h, YRES) && !(argb[y * w + x] >> 24) && !sim->PixelWall(x, y))
						put(x, y, c, fade * (1 - k / maxTail) * (o == 1 || !st.big ? 1 : 0.5f));
				}
			}
		}
		std::erase_if(stars, [now](auto &st) { return now - st.born >= (st.land ? 9000u : 900u) || (st.hitAt && now - st.hitAt >= 400); });
	}
	// paper planes: a white dart with a grey fold, nose first; landed ones fade over their last 2 s
	for (auto &pl : paperPlanes)
	{
		int d = pl.vx >= 0 ? 1 : -1, x = int(pl.x), y = int(pl.y);
		float a = pl.landed > 480 ? (600 - pl.landed) / 120.f : 1;
		for (int i = 0; i < 7; i++) put(x - d * i, y, 0xB8B8C4, a);
		for (int i = 2; i < 7; i++) put(x - d * i, y - 1, 0xF4F4F4, a);
		for (int i = 4; i < 7; i++) put(x - d * i, y - 2, 0xF4F4F4, a);
	}
	// napping pets: three small z's drifting up from each one's head and fading
	if (sim->petNap)
	{
		auto step = int(GetTickCount64() / 50);
		for (int f = 0; f < MAX_FIGHTERS; f++)
		{
			auto &pet = sim->fighters[f];
			if (!pet.spwn) continue;
			int hx = int(pet.legs[4] + pet.legs[12]) / 2, hy = int(std::min(pet.legs[5], pet.legs[13])) - 16;
			for (int k = 0; k < 3; k++)
			{
				int age = (step + k * 20 + f * 7) % 60; // 0-59
				int zx = hx + 3 + age / 8, zy = hy - age / 3;
				float a = 0.9f * (1 - age / 60.f);
				for (int i = 0; i < 3; i++) // z: top row, bottom row, and the middle of the diagonal
					put(zx + i, zy, 0xE0F0FF, a), put(zx + i, zy + 2, 0xE0F0FF, a);
				put(zx + 1, zy + 1, 0xE0F0FF, a);
			}
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
	if (!&GameController::Ref() || !overlayMode || overlayWindowed) return false; // windowed: the window's edges are the area's
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

void OverlayWindows::SetWindowedView(int w, int h)
{
	if (!&GameController::Ref()) return;
	auto *sim = GameController::Ref().GetModel()->GetSimulation();
	// whole cells covering the view (a partial one would show a strip of wall)
	sim->playArea = RectSized(Vec2(0, 0), Vec2(std::clamp((w + CELL - 1) / CELL, 1, XCELLS), std::clamp((h + CELL - 1) / CELL, 1, YCELLS)));
	sim->ApplyPlayArea();
	GlobalPrefs::Ref().Set("Overlay.WindowedView.W", w);
	GlobalPrefs::Ref().Set("Overlay.WindowedView.H", h);
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
	prefs.Set("Overlay.PlayArea.Scale", ui::Engine::Ref().windowFrameOps.scale);
}


#include "OverlayWindowsDialogs.inl"
#ifdef __APPLE__
// What the Windows side gets from Explorer, the registry and its own controls.
ByteString Platform::WinNarrow(const std::wstring &source)
{
	return std::wstring_convert<std::codecvt_utf8<wchar_t>>().to_bytes(source);
}

std::wstring Platform::WinWiden(const ByteString &source)
{
	return std::wstring_convert<std::codecvt_utf8<wchar_t>>().from_bytes(source);
}

int OverlayWindows::Form(const String &title, const String &prompt, const std::vector<String> &options, int &choice, String &text,
                         const std::vector<String> &extras, std::function<bool(int, const String &)> valid, int)
{
	if (!mainWindow) return -1;
	std::vector<std::wstring> opts, more;
	for (auto &o : options) opts.push_back(Wide(o));
	for (auto &e : extras) more.push_back(Wide(e));
	auto value = Wide(text);
	int result = OverlayMac::Form(Wide(title), Wide(prompt), opts, choice, value, more, [&valid](int c, const std::wstring &v) {
		return !valid || valid(c, Platform::WinNarrow(v).FromUtf8());
	});
	if (result >= 1) text = Platform::WinNarrow(value).FromUtf8();
	return result;
}

namespace
{
	std::vector<std::wstring> consoleHistory;
}

bool OverlayWindows::ShowConsole()
{
	if (!mainWindow) return false;
	OverlayMac::ConsoleOpen({
		[](const std::wstring &cmd) {
			if (cmd.empty()) return;
			if (consoleHistory.empty() || consoleHistory.back() != cmd) consoleHistory.push_back(cmd);
			OverlayMac::ConsoleAppend(L"> " + cmd, false);
			auto *ci = GameController::Ref().GetCommandInterface();
			int code = ci->Command(Platform::WinNarrow(cmd).FromUtf8());
			auto result = Wide(ci->GetLastError());
			if (result.size()) OverlayMac::ConsoleAppend(code && result != L"..." ? L"Error: " + result : result, code && result != L"...");
		},
		[](int back) { return back > 0 && size_t(back) <= consoleHistory.size() ? consoleHistory[consoleHistory.size() - back] : std::wstring(); },
		[] { OverlayMac::FocusOverlay(); },
	});
	return true;
}

void OverlayWindows::HideConsole()
{
	OverlayMac::ConsoleClose();
}
#endif
#else
void OverlayWindows::Init(void *) {}
void OverlayWindows::Tick() {}
int OverlayWindows::WorkBottom(int) { return YRES; }
bool OverlayWindows::ActiveEdges(uintptr_t &, int &, int &, int &, int &) { return false; }
void OverlayWindows::TogglePalette() {}
void OverlayWindows::SetPassThrough(bool) {}
void OverlayWindows::Notify(const char *) {}
bool OverlayWindows::HideTPTMenus() { return false; }
void OverlayWindows::ApplySimMasks(uint32_t *, int, const uint8_t *) {}
void OverlayWindows::FrameWalls(std::vector<uint8_t> &, uint8_t) {}
std::vector<OverlayWindows::WindowShift> OverlayWindows::WindowShifts() { return {}; }
void OverlayWindows::ClearUnderWindows(uint32_t *, int, int, int, int) {}
void OverlayWindows::DrawExtras(uint32_t *, int, int) {}
void OverlayWindows::SetPlayArea(int, int, int, int) {}
void OverlayWindows::SetWindowedView(int, int) {}
bool OverlayWindows::AreaMouse(int, int, int) { return false; }
int OverlayWindows::Form(const String &, const String &, const std::vector<String> &, int &, String &, const std::vector<String> &,
                         std::function<bool(int, const String &)>, int) { return -1; }
bool OverlayWindows::ShowConsole() { return false; }
void OverlayWindows::HideConsole() {}
#endif
