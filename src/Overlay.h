#pragma once
#include <SDL.h>
#include <cstdint>
#include <utility>
#include "common/Vec2.h"

// overlay:on: a borderless, always-on-top window over the desktop. Empty (black) pixels are see-through; what
// clicks do depends on the mode (OVERLAY.md). Windows only so far.
extern bool overlayMode;
// overlay:window: TPT's normal window (its own background and menus), still with the overlay's side windows and tray.
extern bool overlayWindowed;
// The side windows, tray and hotkeys, once per frame (the windowed mode's part of OverlayPresent).
void OverlayTickUI();

enum class OverlayInput
{
	Auto,        // particles and menus take clicks, empty space passes them to the desktop
	PassThrough, // TPT takes no input; the sim just runs over the desktop
	Locked,      // TPT takes every click, empty space included
	Count,
};

enum class OverlayLayer
{
	OnTop,       // over every window
	UnderWindows, // over the desktop and its icons, under every app window
	BehindIcons, // part of the wallpaper: under the desktop icons, takes no clicks
	Count,
};

void ApplyOverlay(SDL_Window *window);
void OverlaySetLayer(OverlayLayer layer);
OverlayLayer OverlayGetLayer();
void OverlaySetInput(OverlayInput input);
OverlayInput OverlayGetInput();
void OverlayLoadKeys(); // (re)reads the hotkeys from Overlay.Keys.* in powder.pref
extern bool overlayAutoPass; // clicks pass through while a full-screen app or game is in front (Overlay.AutoPassThrough)
// Presents the frame (0xRRGGBB pixels, w x h) scaled to the window, with per-pixel alpha. Also polls the hotkeys.
void OverlayPresent(SDL_Window *window, const uint32_t *vid, int w, int h);
// Each new sim frame, before it's presented: the particles it was rendered from, so frames drawn between sim ticks
// can slide the moving ones from where they were to where they are (sim ticks 60 a second, the screen up to 120+).
struct RenderableSimulation;
void OverlayFrameParts(const RenderableSimulation &sim, bool zoom);
// The sim size (in cells) and pixel scale that fill the primary screen, starting from the given scale and raising it
// until the sim fits pmap. Stock size off Windows.
std::pair<Vec2<int>, int> OverlaySimSize(int scale);
