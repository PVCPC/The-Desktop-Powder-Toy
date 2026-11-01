#include "PowderToySDL.h"
#include "SimulationConfig.h"
#include "WindowIcon.h"
#include "Overlay.h"
#include "Config.h"
#include "gui/interface/Engine.h"
#include "graphics/Graphics.h"
#include "common/platform/Platform.h"
#include "common/clipboard/Clipboard.h"
#include "FrameSchedule.h"
#include "OverlayWindows.h"
#include "prefs/GlobalPrefs.h"
#include <iostream>
#include <algorithm>
#if defined(_WIN32) || defined(__APPLE__)
constexpr bool overlayResizable = true; // overlay:window's resizable sim view (blit)
#else
constexpr bool overlayResizable = false;
#endif

int desktopWidth = 1280;
int desktopHeight = 1024;
SDL_Window *sdl_window = nullptr;
SDL_Renderer *sdl_renderer = nullptr;
SDL_Texture *sdl_texture = nullptr;
bool vsyncHint = false;
WindowFrameOps currentFrameOps;
bool momentumScroll = true;
bool showAvatars = true;
bool showLargeScreenDialog = false;
int mousex = 0;
int mousey = 0;
int mouseButton = 0;
bool mouseDown = false;
bool calculatedInitialMouse = false;
bool hasMouseMoved = false;
double correctedFrameTimeAvg = 0;
static bool prevContributesToFps = false;

static FrameSchedule tickSchedule;
static FrameSchedule drawSchedule;
static FrameSchedule clientTickSchedule;
static FrameSchedule fpsUpdateSchedule;

void StartTextInput()
{
	SDL_StartTextInput();
}

void StopTextInput()
{
	SDL_StopTextInput();
}

void SetTextInputRect(int x, int y, int w, int h)
{
	// Why does SDL_SetTextInputRect not take logical coordinates???
	SDL_Rect rect;
#if SDL_VERSION_ATLEAST(2, 0, 18)
	int wx, wy, wwx, why;
	SDL_RenderLogicalToWindow(sdl_renderer, float(x), float(y), &wx, &wy);
	SDL_RenderLogicalToWindow(sdl_renderer, float(x + w), float(y + h), &wwx, &why);
	rect.x = wx;
	rect.y = wy;
	rect.w = wwx - wx;
	rect.h = why - wy;
#else
	// TODO: use SDL_RenderLogicalToWindow when ubuntu deigns to update to sdl 2.0.18
	auto scale = ui::Engine::Ref().windowFrameOps.scale;
	rect.x = x * scale;
	rect.y = y * scale;
	rect.w = w * scale;
	rect.h = h * scale;
#endif
	SDL_SetTextInputRect(&rect);
}

void ClipboardPush(ByteString text)
{
	SDL_SetClipboardText(text.c_str());
}

ByteString ClipboardPull()
{
	return ByteString(SDL_GetClipboardText());
}

int GetModifiers()
{
	return SDL_GetModState();
}

unsigned int GetTicks()
{
	return SDL_GetTicks();
}

uint64_t GetNowNs()
{
	return uint64_t(SDL_GetTicks()) * UINT64_C(1'000'000);
}

static void CalculateMousePosition(int *x, int *y)
{
	int globalMx, globalMy;
	SDL_GetGlobalMouseState(&globalMx, &globalMy);
	int windowX, windowY;
	SDL_GetWindowPosition(sdl_window, &windowX, &windowY);

	if (x)
		*x = (globalMx - windowX) / currentFrameOps.scale;
	if (y)
		*y = (globalMy - windowY) / currentFrameOps.scale;
}

void blit(pixel *vid)
{
	if (overlayWindowed)
	{
		OverlayTickUI();
#if defined(_WIN32) || defined(__APPLE__)
		// The resizable window shows the screen-sized sim's top-left corner; resizing it gives more or less room.
		static Vec2<int> view{ 0, 0 };
		static SDL_Renderer *viewFor = nullptr; // a remade renderer needs its logical size again
		int scale = std::max(currentFrameOps.scale, 1), ww, wh;
		SDL_GetWindowSize(sdl_window, &ww, &wh);
		Vec2<int> now(std::clamp(ww / scale, 1, XRES), std::clamp(wh / scale, 1, YRES));
		if (now != view || viewFor != sdl_renderer)
		{
			view = now, viewFor = sdl_renderer;
			OverlayWindows::SetWindowedView(view.X, view.Y);
			SDL_RenderSetLogicalSize(sdl_renderer, view.X, view.Y);
		}
		SDL_UpdateTexture(sdl_texture, nullptr, vid, WINDOWW * sizeof (Uint32));
		SDL_RenderClear(sdl_renderer);
		SDL_Rect src{ 0, 0, view.X, view.Y };
		SDL_RenderCopy(sdl_renderer, sdl_texture, &src, nullptr);
		SDL_RenderPresent(sdl_renderer);
		return;
#endif
	}
	else if (overlayMode)
	{
		OverlayPresent(sdl_window, vid, WINDOWW, WINDOWH);
		return;
	}
	SDL_UpdateTexture(sdl_texture, nullptr, vid, WINDOWW * sizeof (Uint32));
	// need to clear the renderer if there are black edges (fullscreen, or resizable window)
	if (currentFrameOps.fullscreen || currentFrameOps.resizable)
		SDL_RenderClear(sdl_renderer);
	SDL_RenderCopy(sdl_renderer, sdl_texture, nullptr, nullptr);
	SDL_RenderPresent(sdl_renderer);
}

void UpdateRefreshRate()
{
	RefreshRate refreshRate;
	int displayIndex = SDL_GetWindowDisplayIndex(sdl_window);
	if (displayIndex >= 0)
	{
		SDL_DisplayMode displayMode;
		if (!SDL_GetCurrentDisplayMode(displayIndex, &displayMode) && displayMode.refresh_rate)
		{
			refreshRate = RefreshRateQueried{ displayMode.refresh_rate };
		}
	}
	ui::Engine::Ref().SetRefreshRate(refreshRate);
}

void SDLOpen()
{
	if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0)
	{
		fprintf(stderr, "Initializing SDL (video subsystem): %s\n", SDL_GetError());
		Platform::Exit(-1);
	}
	Clipboard::Init();

	SDLSetScreen();

	int displayIndex = SDL_GetWindowDisplayIndex(sdl_window);
	if (displayIndex >= 0)
	{
		SDL_Rect rect;
		if (!SDL_GetDisplayUsableBounds(displayIndex, &rect))
		{
			desktopWidth = rect.w;
			desktopHeight = rect.h;
		}
	}
	UpdateRefreshRate();

	StopTextInput();
}

void SDLClose()
{
	if (SDL_GetWindowFlags(sdl_window) & SDL_WINDOW_OPENGL)
	{
		// * nvidia-460 egl registers callbacks with x11 that end up being called
		//   after egl is unloaded unless we grab it here and release it after
		//   sdl closes the display. this is an nvidia driver weirdness but
		//   technically an sdl bug. glfw has this fixed:
		//   https://github.com/glfw/glfw/commit/9e6c0c747be838d1f3dc38c2924a47a42416c081
		SDL_GL_LoadLibrary(nullptr);
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
		SDL_GL_UnloadLibrary();
	}
	SDL_Quit();
}

void SDLSetScreen()
{
	auto newFrameOps = ui::Engine::Ref().windowFrameOps;
	auto newVsyncHint = false; // TODO: DrawLimitVsync
	if (FORCE_WINDOW_FRAME_OPS == forceWindowFrameOpsEmbedded)
	{
		newFrameOps.resizable = false;
		newFrameOps.fullscreen = false;
		newFrameOps.changeResolution = false;
		newFrameOps.forceIntegerScaling = false;
	}
	if (FORCE_WINDOW_FRAME_OPS == forceWindowFrameOpsHandheld)
	{
		newFrameOps.resizable = false;
		newFrameOps.fullscreen = true;
		newFrameOps.changeResolution = false;
		newFrameOps.forceIntegerScaling = false;
	}

	auto currentFrameOpsNorm = currentFrameOps.Normalize();
	auto newFrameOpsNorm = newFrameOps.Normalize();
	auto recreate = !sdl_window ||
	                // Recreate the window when toggling fullscreen, due to occasional issues
	                newFrameOpsNorm.fullscreen       != currentFrameOpsNorm.fullscreen       ||
	                // Also recreate it when enabling resizable windows, to fix bugs on windows,
	                //  see https://github.com/jacob1/The-Powder-Toy/issues/24
	                newFrameOpsNorm.resizable        != currentFrameOpsNorm.resizable        ||
	                newFrameOpsNorm.changeResolution != currentFrameOpsNorm.changeResolution ||
	                newFrameOpsNorm.blurryScaling    != currentFrameOpsNorm.blurryScaling    ||
	                newVsyncHint != vsyncHint;

	if (!(recreate ||
	      newFrameOpsNorm.scale               != currentFrameOpsNorm.scale               ||
	      newFrameOpsNorm.forceIntegerScaling != currentFrameOpsNorm.forceIntegerScaling))
	{
		return;
	}

	auto size = WINDOW * newFrameOpsNorm.scale;
	if (sdl_window && newFrameOpsNorm.resizable)
	{
		SDL_GetWindowSize(sdl_window, &size.X, &size.Y);
	}

	if (recreate)
	{
		if (sdl_texture)
		{
			SDL_DestroyTexture(sdl_texture);
			sdl_texture = nullptr;
		}
		if (sdl_renderer)
		{
			SDL_DestroyRenderer(sdl_renderer);
			sdl_renderer = nullptr;
		}
		if (sdl_window)
		{
			SaveWindowPosition();
			SDL_DestroyWindow(sdl_window);
			sdl_window = nullptr;
		}

		unsigned int flags = 0;
		unsigned int rendererFlags = 0;
		if (newFrameOpsNorm.fullscreen)
		{
			flags = newFrameOpsNorm.changeResolution ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_FULLSCREEN_DESKTOP;
		}
		if (newFrameOpsNorm.resizable || (overlayWindowed && overlayResizable))
		{
			flags |= SDL_WINDOW_RESIZABLE;
		}
		if (vsyncHint)
		{
			rendererFlags |= SDL_RENDERER_PRESENTVSYNC;
		}
		if (overlayMode && !overlayWindowed)
		{
			flags |= SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP;
			// Overlay frames go out through UpdateLayeredWindow (Overlay.cpp); keep SDL off GPU swapchains, which fight it.
			SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
		}
		sdl_window = SDL_CreateWindow(APPNAME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, size.X, size.Y, flags);
		if (!sdl_window)
		{
			fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
			Platform::Exit(-1);
		}
		if constexpr (SET_WINDOW_ICON)
		{
			WindowIcon(sdl_window);
		}
		if (overlayMode)
		{
			ApplyOverlay(sdl_window);
		}
		SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, newFrameOpsNorm.blurryScaling ? "linear" : "nearest");
		sdl_renderer = SDL_CreateRenderer(sdl_window, -1, rendererFlags);
		if (!sdl_renderer)
		{
			fprintf(stderr, "SDL_CreateRenderer failed; available renderers:\n");
			int num = SDL_GetNumRenderDrivers();
			for (int i = 0; i < num; ++i)
			{
				SDL_RendererInfo info;
				SDL_GetRenderDriverInfo(i, &info);
				fprintf(stderr, " - %s\n", info.name);
			}
			Platform::Exit(-1);
		}
		SDL_RenderSetLogicalSize(sdl_renderer, WINDOWW, WINDOWH);
		sdl_texture = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, WINDOWW, WINDOWH);
		if (!sdl_texture)
		{
			fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
			Platform::Exit(-1);
		}
		SDL_RaiseWindow(sdl_window);
		Clipboard::RecreateWindow();
	}
	SDL_RenderSetIntegerScale(sdl_renderer, newFrameOpsNorm.forceIntegerScaling ? SDL_TRUE : SDL_FALSE);
	if (overlayWindowed && overlayResizable)
	{
		// the view's size from last time (stock TPT's sim by default), at most the whole sim
		auto &prefs = GlobalPrefs::Ref();
		int s = newFrameOpsNorm.scale;
		SDL_SetWindowMinimumSize(sdl_window, 64 * s, 64 * s);
		SDL_SetWindowMaximumSize(sdl_window, XRES * s, YRES * s);
#ifdef __APPLE__
		SDL_RestoreWindow(sdl_window); // it opens screen-sized, which macOS counts as maximised
#endif
		if (!(SDL_GetWindowFlags(sdl_window) & SDL_WINDOW_MAXIMIZED))
		{
			SDL_SetWindowSize(sdl_window, std::clamp(prefs.Get("Overlay.WindowedView.W", 612), 64, XRES) * s,
			                  std::clamp(prefs.Get("Overlay.WindowedView.H", 384), 64, YRES) * s);
			LoadWindowPosition();
		}
	}
	else if (!(newFrameOpsNorm.resizable && SDL_GetWindowFlags(sdl_window) & SDL_WINDOW_MAXIMIZED))
	{
		SDL_SetWindowSize(sdl_window, size.X, size.Y);
		LoadWindowPosition();
		if (overlayMode && !overlayWindowed)
		{
			// The overlay starts at its monitor's corner (hidden menus hang off the right and bottom edges); a saved
			// normal-window position would push it off screen. On Windows it spans every monitor (OverlaySimSize).
			SDL_Rect display;
			SDL_GetDisplayBounds(SDL_GetWindowDisplayIndex(sdl_window), &display);
#ifdef _WIN32
			for (int i = 0, n = SDL_GetNumVideoDisplays(); i < n; i++)
			{
				SDL_Rect d;
				if (!SDL_GetDisplayBounds(i, &d)) SDL_UnionRect(&display, &d, &display);
			}
#endif
			SDL_SetWindowPosition(sdl_window, display.x, display.y);
		}
	}
	ApplyFpsLimit();
	if (newFrameOpsNorm.fullscreen)
	{
		SDL_RaiseWindow(sdl_window);
	}
	currentFrameOps = newFrameOps;
	vsyncHint = newVsyncHint;
}

static void EventProcess(const SDL_Event &event)
{
	auto &engine = ui::Engine::Ref();
	switch (event.type)
	{
	case SDL_QUIT:
		if (ALLOW_QUIT && (engine.GetFastQuit() || engine.CloseWindow()))
		{
			engine.Exit();
		}
		break;
	case SDL_KEYDOWN:
		if (SDL_GetModState() & KMOD_GUI)
		{
			break;
		}
		if (engine.GetGlobalQuit() && ALLOW_QUIT && !event.key.repeat && event.key.keysym.sym == 'q' && (event.key.keysym.mod&KMOD_CTRL) && !(event.key.keysym.mod&KMOD_ALT))
			engine.ConfirmExit();
		else
			engine.onKeyPress(event.key.keysym.sym, event.key.keysym.scancode, event.key.repeat, event.key.keysym.mod&KMOD_SHIFT, event.key.keysym.mod&KMOD_CTRL, event.key.keysym.mod&KMOD_ALT);
		break;
	case SDL_KEYUP:
		if (SDL_GetModState() & KMOD_GUI)
		{
			break;
		}
		engine.onKeyRelease(event.key.keysym.sym, event.key.keysym.scancode, event.key.repeat, event.key.keysym.mod&KMOD_SHIFT, event.key.keysym.mod&KMOD_CTRL, event.key.keysym.mod&KMOD_ALT);
		break;
	case SDL_TEXTINPUT:
		if (SDL_GetModState() & KMOD_GUI)
		{
			break;
		}
		engine.onTextInput(ByteString(event.text.text).FromUtf8());
		break;
	case SDL_TEXTEDITING:
		if (SDL_GetModState() & KMOD_GUI)
		{
			break;
		}
		engine.onTextEditing(ByteString(event.edit.text).FromUtf8(), event.edit.start);
		break;
	case SDL_MOUSEWHEEL:
	{
		if (!engine.Hosted.empty())
		{
			break; // the overlay's dialog windows have the mouse
		}
		// int x = event.wheel.x;
		int y = event.wheel.y;
		if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
		{
			// x *= -1;
			y *= -1;
		}

		engine.onMouseWheel(mousex, mousey, y); // TODO: pass x?
		break;
	}
	case SDL_MOUSEMOTION:
		mousex = event.motion.x;
		mousey = event.motion.y;
		if (engine.Hosted.empty())
		{
			engine.onMouseMove(mousex, mousey);
		}

		hasMouseMoved = true;
		break;
	case SDL_DROPFILE:
		engine.onFileDrop(event.drop.file);
		SDL_free(event.drop.file);
		break;
	case SDL_MOUSEBUTTONDOWN:
		// if mouse hasn't moved yet, sdl will send 0,0. We don't want that
		if (hasMouseMoved)
		{
			mousex = event.button.x;
			mousey = event.button.y;
		}
		mouseButton = event.button.button;
		if (!engine.Hosted.empty())
		{
			engine.onMouseDown(-(1 << 20), -(1 << 20), mouseButton); // outside every window
			break;
		}
		engine.onMouseDown(mousex, mousey, mouseButton);

		mouseDown = true;
		if constexpr (!DEBUG)
		{
			SDL_CaptureMouse(SDL_TRUE);
		}
		break;
	case SDL_MOUSEBUTTONUP:
		// if mouse hasn't moved yet, sdl will send 0,0. We don't want that
		if (hasMouseMoved)
		{
			mousex = event.button.x;
			mousey = event.button.y;
		}
		mouseButton = event.button.button;
		if (!engine.Hosted.empty() && !mouseDown)
		{
			break;
		}
		engine.onMouseUp(mousex, mousey, mouseButton);

		mouseDown = false;
		if constexpr (!DEBUG)
		{
			SDL_CaptureMouse(SDL_FALSE);
		}
		break;
	case SDL_WINDOWEVENT:
	{
		switch (event.window.event)
		{
		case SDL_WINDOWEVENT_SHOWN:
			if (!calculatedInitialMouse)
			{
				//initial mouse coords, sdl won't tell us this if mouse hasn't moved
				CalculateMousePosition(&mousex, &mousey);
				engine.initialMouse(mousex, mousey);
				engine.onMouseMove(mousex, mousey);
				calculatedInitialMouse = true;
			}
			break;

		case SDL_WINDOWEVENT_DISPLAY_CHANGED:
			UpdateRefreshRate();
			break;
		}
		break;
	}
	}
}

std::optional<uint64_t> EngineProcess()
{
	auto &engine = ui::Engine::Ref();

	{
		auto nowNs = GetNowNs();
		if (clientTickSchedule.HasElapsed(nowNs))
		{
			TickClient();
			clientTickSchedule.SetNow(nowNs);
		}
		clientTickSchedule.Arm(10);
		if (fpsUpdateSchedule.HasElapsed(nowNs))
		{
			engine.SetFps(1e9f / correctedFrameTimeAvg);
			fpsUpdateSchedule.SetNow(nowNs);
		}
		fpsUpdateSchedule.Arm(5);
	}

	if (showLargeScreenDialog)
	{
		showLargeScreenDialog = false;
		LargeScreenDialog();
	}

	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		EventProcess(event);
	}

	std::optional<uint64_t> delay;
	auto nowNs = GetNowNs();
	auto effectiveDrawLimit = engine.GetEffectiveDrawCap();
	auto doDraw = !effectiveDrawLimit || drawSchedule.HasElapsed(nowNs);
	auto fpsLimit = ui::Engine::Ref().GetFpsLimit();
	if (auto *explicitLimit = std::get_if<FpsLimitExplicit>(&fpsLimit); explicitLimit && overlayIdle)
	{
		// a settled pile still costs a full update per tick; slow invisible changes (heat, timers) can wait
		explicitLimit->value = std::min(explicitLimit->value, 20.f);
	}
	auto doSimTick = true;
	if (std::holds_alternative<FpsLimitExplicit>(fpsLimit))
	{
		doSimTick = tickSchedule.HasElapsed(nowNs);
	}
	else if (std::holds_alternative<FpsLimitFollowDraw>(fpsLimit))
	{
		doSimTick = doDraw;
	}
	if (doDraw)
	{
		engine.Tick();
	}
	if (doSimTick)
	{
		auto thisContributesToFps = engine.GetContributesToFps();
		if (prevContributesToFps && thisContributesToFps)
		{
			auto correctedFrameTime = tickSchedule.GetFrameTime();
			correctedFrameTimeAvg = correctedFrameTimeAvg + (correctedFrameTime - correctedFrameTimeAvg) * 0.05;
		}
		prevContributesToFps = thisContributesToFps;
		engine.SimTick();
		tickSchedule.SetNow(nowNs);
	}
	if (doDraw)
	{
		engine.Draw();
		drawSchedule.SetNow(nowNs);
		SDLSetScreen();
		blit(engine.g->Data());
	}
	// Until the next frame is due, from now: Arm's own return is from the previous due time, 0 on every pass that
	// didn't draw or tick, and the loop then woke every millisecond to find nothing to do.
	auto untilMs = [doneNs = GetNowNs()](uint64_t dueNs) {
		return dueNs > doneNs ? (dueNs - doneNs + UINT64_C(999'999)) / UINT64_C(1'000'000) : UINT64_C(0);
	};
	if (effectiveDrawLimit)
	{
		drawSchedule.Arm(float(*effectiveDrawLimit));
		delay = untilMs(drawSchedule.GetNow());
	}
	if (auto *fpsLimitExplicit = std::get_if<FpsLimitExplicit>(&fpsLimit))
	{
		tickSchedule.Arm(fpsLimitExplicit->value);
		auto simDelay = untilMs(tickSchedule.GetNow());
		if (delay.has_value() && simDelay < *delay)
		{
			delay = simDelay;
		}
	}
	else if (std::holds_alternative<FpsLimitNone>(fpsLimit))
	{
		delay.reset();
	}
	return delay;
}
