#include "OverlayWalls.h"
#include "simulation/Simulation.h"
#include "SimulationConfig.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>

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
	// edge goes the same way. Big jumps (snapping, maximising) leave it behind. When the window slows, stops or turns,
	// the riders keep the speed it lost: shake a window and the sand flies off.
	// Window positions don't arrive in step with ticks (a steady drag reads 8, 0, 8, 0 px), so the speed is averaged:
	// the 4 ticks before against the last 2.
	using History = std::array<std::pair<int, int>, 6>; // moves per tick, oldest first
	static std::map<uintptr_t, History> lastMoves;
	std::map<uintptr_t, History> moves;
	for (auto &w : walls)
	{
		int mx = w.mx, my = w.my, fx = 0, fy = 0;
		if (w.id && std::abs(mx) <= 40 && std::abs(my) <= 40)
		{
			auto &h = moves[w.id] = lastMoves[w.id];
			std::rotate(h.begin(), h.begin() + 1, h.end());
			h.back() = { mx, my };
			float ax = 0, ay = 0, bx = 0, by = 0;
			for (int i = 0; i < 4; i++) ax += h[i].first / 4.f, ay += h[i].second / 4.f;
			for (int i = 4; i < 6; i++) bx += h[i].first / 2.f, by += h[i].second / 2.f;
			float was = std::hypot(ax, ay), now = std::hypot(bx, by);
			if (was >= 3 && (now < was * 0.4f || ax * bx + ay * by < 0)) // it stopped short or turned back
			{
				fx = int(std::lround(ax - bx)), fy = int(std::lround(ay - by));
				h.fill({ int(std::lround(bx)), int(std::lround(by)) }); // flung once; the speed it has now carries on
			}
		}
		if (w.solid || (!mx && !my && !fx && !fy) || std::abs(mx) > 40 || std::abs(my) > 40) continue;
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
			if (fx || fy)
			{
				// livelier than the bare speed, and a little up so it leaves the surface instead of scraping along it
				part.vx = std::clamp(fx * 1.5f, -15.f, 15.f);
				part.vy = std::clamp(fy * 1.5f - std::abs(fx) * 0.5f, -15.f, 15.f);
			}
			sim->pmap[ty][tx] = p.r;
		}
	}
	lastMoves = std::move(moves);
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

void OverlayCursorWind(Simulation *sim, int x, int y, int dx, int dy)
{
	if ((!dx && !dy) || std::abs(dx) > 200 || std::abs(dy) > 200) return; // still, or a jump
	// like dragging TPT's WIND tool, softer: a breeze 3 cells around the cursor, fading out from it
	constexpr int r = 3;
	for (int j = -r; j <= r; j++)
		for (int i = -r; i <= r; i++)
		{
			int cx = x / CELL + i, cy = y / CELL + j;
			if (cx < 0 || cy < 0 || cx >= XCELLS || cy >= YCELLS || i * i + j * j > r * r) continue;
			float k = 0.08f * (1 - std::sqrt(float(i * i + j * j)) / (r + 1));
			sim->vx[cy][cx] = std::clamp(sim->vx[cy][cx] + dx * k, -15.f, 15.f);
			sim->vy[cy][cx] = std::clamp(sim->vy[cy][cx] + dy * k, -15.f, 15.f);
		}
}
