#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

class Simulation;

// A window or desktop icon the sim treats as solid: its box in sim pixels, how far it moved since the last tick (sim
// pixels), and for shaped things (icons) which pixels of the box are solid (null: all of it).
struct OverlayWall
{
	int x0, y0, x1, y1, mx, my;
	std::function<bool(int, int)> solid;
	uintptr_t id = 0; // the same window from tick to tick (0: none), so what rides it can be flung off
};

// Rebuilds sim->pixelWalls from the walls, carries what rests on the ones that moved and shoves what they moved onto.
void OverlayApplyWalls(Simulation *sim, const std::vector<OverlayWall> &walls);

// How the desktop pushes the sim around, from the side window's sliders (Overlay.Feel.*): multipliers, 1 = normal, 0 = off;
// grip is the chance (0-1) that a grain rides a moving window each tick.
struct OverlayFeel
{
	float grip = 1, fling = 1, push = 1, wind = 1, scorch = 1, frost = 1, acid = 1, water = 1, acidEat = 1, blast = 1, holeOpacity = 0; // holeOpacity: how much of a broken window still shows in its holes
	float shards = 1, shardSize = 3, blackHole = 1, shake = 0;
	bool destructible = false; // acid and blasts eat holes in windows and icons (overlay only)
	bool shardTint = true;     // shards take the colour of what broke
	bool shardParticles = false; // shards are sim particles (broken glass) instead of drawn chunks
};
extern OverlayFeel overlayFeel;

// Reads what's on screen (without the overlay) under a box of sim pixels, 0x00RRGGBB, w * h; false where it can't.
extern bool (*overlayGrabScreen)(int x0, int y0, int w, int h, uint32_t *out);

// Drawn chunks of broken window still flying or settling (the overlay keeps drawing smoothly while there are).
bool OverlayDebrisFlying();

// How hard the last tick's blasts hit each wall (by OverlayWall::id), for shaking real windows; filled while shake > 0.
extern std::map<uintptr_t, float> overlayKicks;

// Draws the marks on windows (Simulation::windowMarks) on the frame where the window walls are (occupied & wallBit).
// Holes show backdrop (XRES x YRES, 0x00RRGGBB: what is really behind each window there) when given.
void OverlayDrawMarks(const Simulation *sim, uint32_t *argb, int stride, const uint8_t *occupied, uint8_t wallBit, const uint32_t *backdrop = nullptr);

// The mouse moved by (dx, dy) to (x, y), in sim pixels, where the sim takes no clicks: it stirs the air there.
void OverlayCursorWind(Simulation *sim, int x, int y, int dx, int dy);
