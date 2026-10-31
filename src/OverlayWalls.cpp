#include "OverlayWalls.h"
#include "simulation/Simulation.h"
#include "SimulationConfig.h"
#include <algorithm>
#include <cmath>

void OverlayApplyWalls(Simulation *sim, const std::vector<OverlayWall> &walls)
{
	auto old = std::move(sim->pixelWalls);
	auto &mask = sim->pixelWalls;
	mask.assign(size_t(XRES) * YRES, 0);
	for (auto &w : walls)
		for (int y = w.y0; y < w.y1; y++)
		{
			if (!w.solid)
			{
				std::fill(&mask[size_t(y) * XRES + w.x0], &mask[size_t(y) * XRES + w.x1], 1);
				continue;
			}
			for (int x = w.x0; x < w.x1; x++)
				if (w.solid(x, y)) mask[size_t(y) * XRES + x] = 1;
		}
	// What rests on a window rides along when it moves (friction): each column's stack of particles on its old top
	// edge goes the same way. Big jumps (snapping, maximising) leave it behind.
	for (auto &w : walls)
	{
		int mx = w.mx, my = w.my;
		if (w.solid || (!mx && !my) || std::abs(mx) > 40 || std::abs(my) > 40) continue;
		int top = w.y0 - my - 1; // the row just above the old top edge
		if (top < 0 || top >= YRES) continue;
		struct Rider { int r, x, y; };
		std::vector<Rider> riders;
		for (int x = std::max(0, w.x0 - mx); x < std::min(XRES, w.x1 - mx); x++)
			for (int y = top; y >= 0 && sim->pmap[y][x]; y--) riders.push_back({ sim->pmap[y][x], x, y });
		// front ones first: a rider's old spot is then never taken by one moved before it, so a blocked one stays put
		std::sort(riders.begin(), riders.end(), [mx, my](const Rider &a, const Rider &b) { return a.x * mx + a.y * my > b.x * mx + b.y * my; });
		for (auto &p : riders) sim->pmap[p.y][p.x] = 0;
		for (auto &p : riders)
		{
			int tx = p.x + mx, ty = p.y + my;
			bool free = tx >= 0 && ty >= 0 && tx < XRES && ty < YRES && !sim->pmap[ty][tx] && !mask[size_t(ty) * XRES + tx] &&
			            !sim->IsWallBlocking(tx, ty, TYP(p.r));
			if (!free) tx = p.x, ty = p.y; // blocked (another window, a wall): scraped off
			auto &part = sim->parts[ID(p.r)];
			part.x = float(tx);
			part.y = float(ty);
			sim->pmap[ty][tx] = p.r;
		}
	}
	for (auto &w : walls)
	{
		int mx = w.mx, my = w.my;
		if (!mx && !my) my = -1;
		float len = std::hypot(float(mx), float(my)), sx = mx / len, sy = my / len;
		float vx = std::clamp(mx * 1.f, -10.f, 10.f), vy = std::clamp(my * 1.f, -10.f, 10.f); // the window's speed, per frame
		for (int y = w.y0; y < w.y1; y++)
			for (int x = w.x0; x < w.x1; x++)
			{
				if (!mask[size_t(y) * XRES + x] || (!old.empty() && old[size_t(y) * XRES + x])) continue; // only what the window just moved onto
				int r = sim->pmap[y][x];
				if (!r) continue;
				for (int k = 1; k < XRES + YRES; k++)
				{
					int tx = int(std::lround(x + sx * k)), ty = int(std::lround(y + sy * k));
					if (tx < 0 || ty < 0 || tx >= XRES || ty >= YRES) break; // nowhere to go: stays, under the window
					bool own = tx >= w.x0 && tx < w.x1 && ty >= w.y0 && ty < w.y1 && (!w.solid || mask[size_t(ty) * XRES + tx]);
					if (!own && mask[size_t(ty) * XRES + tx]) break; // another window or icon: no tunnelling through it
					if (own || sim->pmap[ty][tx] || sim->IsWallBlocking(tx, ty, TYP(r))) continue;
					auto &part = sim->parts[ID(r)];
					part.x = float(tx);
					part.y = float(ty);
					part.vx = vx;
					part.vy = vy;
					sim->pmap[ty][tx] = r;
					sim->pmap[y][x] = 0;
					break;
				}
			}
	}
}
