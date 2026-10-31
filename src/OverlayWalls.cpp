#include "OverlayWalls.h"
#include "simulation/Simulation.h"
#include "SimulationConfig.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>

OverlayFeel overlayFeel;

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
			if (!free || (overlayFeel.grip < 1 && sim->rng.uniform01() >= overlayFeel.grip)) tx = p.x, ty = p.y; // blocked (another window, a wall) or slipped: scraped off
			auto &part = sim->parts[ID(p.r)];
			part.x = float(tx);
			part.y = float(ty);
			if ((fx || fy) && overlayFeel.fling > 0)
			{
				// livelier than the bare speed, and a little up so it leaves the surface instead of scraping along it
				float k = 1.5f * overlayFeel.fling;
				part.vx = std::clamp(fx * k, -25.f, 25.f);
				part.vy = std::clamp(fy * k - std::abs(fx) * k / 3, -25.f, 25.f);
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
		float push = 10 * overlayFeel.push; // the window's speed, per frame
		float vx = std::clamp(mx * overlayFeel.push, -push, push), vy = std::clamp(my * overlayFeel.push, -push, push);
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
	// Anything hot (fire, lava, plasma...) touching a window scorches it: marks that move with it until the sim is cleared.
	// ponytail: marks are kept from the visible top-left, so a window half off screen shifts them; keep the full rect if it matters.
	if (overlayFeel.scorch > 0)
		for (auto &w : walls)
		{
			if (w.solid || !w.id) continue;
			int ww = w.x1 - w.x0, wh = w.y1 - w.y0;
			auto it = sim->windowSoot.find(w.id);
			if (it != sim->windowSoot.end()) it->second.x = w.x0, it->second.y = w.y0;
			auto hot = [&](int x, int y) {
				if (x < 0 || y < 0 || x >= XRES || y >= YRES) return false;
				int r = sim->pmap[y][x];
				return r && sim->parts[ID(r)].temp >= 573.15f; // 300 C
			};
			auto scorch = [&](int x, int y) { // a window pixel next to the hot one
				auto &s = sim->windowSoot[w.id];
				if (s.w != ww || s.h != wh)
				{
					std::vector<uint8_t> a(size_t(ww) * wh, 0); // resized: keep what still fits, from the top-left
					for (int j = 0; j < std::min(wh, s.h); j++)
						for (int i = 0; i < std::min(ww, s.w); i++) a[size_t(j) * ww + i] = s.a[size_t(j) * s.w + i];
					s = { w.x0, w.y0, ww, wh, std::move(a) };
				}
				for (int j = -2; j <= 2; j++)
					for (int i = -2; i <= 2; i++)
					{
						int sx = x - w.x0 + i, sy = y - w.y0 + j, d = i * i + j * j;
						if (sx < 0 || sy < 0 || sx >= ww || sy >= wh || d > 4) continue;
						auto &v = s.a[size_t(sy) * ww + sx];
						v = uint8_t(std::min(255.f, v + (d ? 3 : 10) * overlayFeel.scorch));
					}
			};
			for (int x = w.x0; x < w.x1; x++)
			{
				if (hot(x, w.y0 - 1)) scorch(x, w.y0);
				if (hot(x, w.y1)) scorch(x, w.y1 - 1);
			}
			for (int y = w.y0; y < w.y1; y++)
			{
				if (hot(w.x0 - 1, y)) scorch(w.x0, y);
				if (hot(w.x1, y)) scorch(w.x1 - 1, y);
			}
		}
}

void OverlayDrawSoot(const Simulation *sim, uint32_t *argb, int stride, const uint8_t *occupied, uint8_t wallBit)
{
	for (auto &[id, s] : sim->windowSoot)
		for (int j = 0; j < s.h; j++)
			for (int i = 0; i < s.w; i++)
			{
				int x = s.x + i, y = s.y + j, a = s.a[size_t(j) * s.w + i];
				if (!a || x < 0 || y < 0 || x >= XRES || y >= YRES || !(occupied[size_t(y) * XRES + x] & wallBit)) continue;
				a = a * 7 / 8; // charcoal, premultiplied: never quite black, so the window still shows through
				argb[size_t(y) * stride + x] = uint32_t(a) << 24 | uint32_t(0x22 * a / 255) << 16 | uint32_t(0x1A * a / 255) << 8 | uint32_t(0x12 * a / 255);
			}
}

void OverlayCursorWind(Simulation *sim, int x, int y, int dx, int dy)
{
	if ((!dx && !dy) || std::abs(dx) > 200 || std::abs(dy) > 200 || overlayFeel.wind <= 0) return; // still, or a jump
	// like dragging TPT's WIND tool, softer: a breeze 3 cells around the cursor, fading out from it
	constexpr int r = 3;
	for (int j = -r; j <= r; j++)
		for (int i = -r; i <= r; i++)
		{
			int cx = x / CELL + i, cy = y / CELL + j;
			if (cx < 0 || cy < 0 || cx >= XCELLS || cy >= YCELLS || i * i + j * j > r * r) continue;
			float k = 0.08f * overlayFeel.wind * (1 - std::sqrt(float(i * i + j * j)) / (r + 1));
			sim->vx[cy][cx] = std::clamp(sim->vx[cy][cx] + dx * k, -15.f, 15.f);
			sim->vy[cy][cx] = std::clamp(sim->vy[cy][cx] + dy * k, -15.f, 15.f);
		}
}
