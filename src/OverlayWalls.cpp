#include "OverlayWalls.h"
#include "simulation/Simulation.h"
#include "SimulationConfig.h"
#include "simulation/ElementClasses.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>

OverlayFeel overlayFeel;

// The marks of a window (or icon) where it is now and at its size now: resized, what still fits is kept, from the top-left.
static Simulation::WindowMarks &Marks(Simulation *sim, const OverlayWall &w)
{
	auto &m = sim->windowMarks[w.id];
	int ww = w.x1 - w.x0, wh = w.y1 - w.y0;
	if (m.w != ww || m.h != wh)
	{
		for (auto &a : m.a)
		{
			std::vector<uint8_t> b(size_t(ww) * wh, 0);
			for (int j = 0; j < std::min(wh, m.h) && !a.empty(); j++)
				for (int i = 0; i < std::min(ww, m.w); i++) b[size_t(j) * ww + i] = a[size_t(j) * m.w + i];
			a = std::move(b);
		}
		m.w = ww, m.h = wh;
	}
	m.x = w.x0, m.y = w.y0;
	return m;
}

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
	// Destructible (off unless switched on): acid and blasts eat holes in windows and icons. Only the sim's walls and
	// the overlay's paint change, and clearing the sim mends them: the real windows and icons are never touched.
	using M = Simulation::WindowMark;
	for (auto &w : walls)
		if (auto it = sim->windowMarks.find(w.id); overlayFeel.destructible && w.id && it != sim->windowMarks.end() && it->second.any[M::MarkHole])
		{
			auto &m = Marks(sim, w);
			for (int y = w.y0; y < w.y1; y++)
				for (int x = w.x0; x < w.x1; x++)
					if (m.a[M::MarkHole][size_t(y - w.y0) * m.w + (x - w.x0)]) mask[size_t(y) * XRES + x] = 0;
		}
	enum { Blasted = 100, Eaten = 200 }; // what made the hole: its rim shows it
	auto eat = [&](int x, int y, uint8_t how) { // a wall pixel: the window or icon it belongs to loses it
		for (auto &w : walls)
			if (w.id && x >= w.x0 && x < w.x1 && y >= w.y0 && y < w.y1 && (!w.solid || w.solid(x, y)))
			{
				auto &m = Marks(sim, w);
				m.a[M::MarkHole][size_t(y - w.y0) * m.w + (x - w.x0)] = how;
				m.any[M::MarkHole] = true;
				mask[size_t(y) * XRES + x] = 0;
				return;
			}
	};
	if (overlayFeel.destructible && overlayFeel.acidEat > 0)
		for (int i = 0; i <= sim->parts.active; i++)
		{
			auto &p = sim->parts[i];
			if (p.type != PT_ACID) continue;
			int x = int(p.x + 0.5f), y = int(p.y + 0.5f);
			for (auto [dx, dy] : { std::pair{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } })
			{
				int nx = x + dx, ny = y + dy;
				if (nx < 0 || ny < 0 || nx >= XRES || ny >= YRES || !mask[size_t(ny) * XRES + nx] || sim->rng.uniform01() >= 0.1f * overlayFeel.acidEat) continue;
				eat(nx, ny, Eaten);
				if (sim->rng.uniform01() < 0.4f) // ragged: now and then a bit more crumbles
				{
					int ex = nx + sim->rng.between(-1, 1), ey = ny + sim->rng.between(-1, 1);
					if (ex >= 0 && ey >= 0 && ex < XRES && ey < YRES && mask[size_t(ey) * XRES + ex]) eat(ex, ey, Eaten);
				}
				if (sim->rng.uniform01() < 0.3f) sim->kill_part(i); // used up, like acid eating anything else
				break;
			}
		}
	if (overlayFeel.destructible && overlayFeel.blast > 0)
		for (int cy = 0; cy < YCELLS; cy++)
			for (int cx = 0; cx < XCELLS; cx++)
			{
				// a blast tears out a ragged round chunk, bigger the harder it hits (a small C-4 charge peaks ~6 in its own cells)
				float r = std::min(24.f, (sim->pv[cy][cx] - 3) * 5 * overlayFeel.blast);
				if (r <= 0) continue;
				int ccx = cx * CELL + CELL / 2, ccy = cy * CELL + CELL / 2, n = int(r) + 2;
				for (int y = std::max(0, ccy - n); y < std::min(YRES, ccy + n); y++)
					for (int x = std::max(0, ccx - n); x < std::min(XRES, ccx + n); x++)
					{
						float d = std::hypot(float(x - ccx), float(y - ccy)), edge = r + 1.5f * std::sin(x * 1.7f + y * 2.3f) * std::cos(x * 0.9f - y * 1.3f);
						if (d < edge && mask[size_t(y) * XRES + x]) eat(x, y, Blasted);
					}
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
		if (w.id && !w.solid && std::abs(mx) <= 40 && std::abs(my) <= 40) // icons jump when dropped: nothing to fling
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
		// a dropped icon takes its pile with it however far it went; a window jumping (snapping, maximising) leaves it
		if (!w.id || (!mx && !my && !fx && !fy) || (!w.solid && (std::abs(mx) > 40 || std::abs(my) > 40))) continue;
		struct Rider { int r, x, y; };
		std::vector<Rider> riders;
		for (int x = std::max(0, w.x0 - mx); x < std::min(XRES, w.x1 - mx); x++)
		{
			int top = w.y0 - my - 1; // the row just above the old top edge (an icon: of its shape in this column)
			if (w.solid)
			{
				while (top + 1 < w.y1 - my && !w.solid(x + mx, top + 1 + my)) top++;
				if (top + 1 >= w.y1 - my) continue; // nothing of it in this column
			}
			if (top >= YRES) continue;
			for (int y = top; y >= 0 && sim->pmap[y][x]; y--) riders.push_back({ sim->pmap[y][x], x, y });
		}
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
	// What touches a window marks it, marks that move with it: fire and lava scorch, ice and cryo frost, acid etches
	// pits, water streaks. Frost melts and water dries; scorch and pits stay until the sim is cleared.
	// ponytail: marks are kept from the visible top-left, so a window half off screen shifts them; keep the full rect if it matters.
	const float rate[M::MarkKinds] = { overlayFeel.water, overlayFeel.frost, overlayFeel.acid, overlayFeel.scorch };
	for (auto &w : walls)
	{
		if (w.solid || !w.id) continue;
		int ww = w.x1 - w.x0, wh = w.y1 - w.y0;
		if (auto it = sim->windowMarks.find(w.id); it != sim->windowMarks.end()) it->second.x = w.x0, it->second.y = w.y0;
		auto kindAt = [&](int x, int y) {
			if (x < 0 || y < 0 || x >= XRES || y >= YRES) return -1;
			int r = sim->pmap[y][x];
			if (!r) return -1;
			int t = TYP(r);
			float temp = sim->parts[ID(r)].temp;
			int k = t == PT_ACID                                                     ? M::MarkAcid
			        : t == PT_WATR || t == PT_DSTW || t == PT_SLTW || t == PT_CBNW ? M::MarkWater
			        : temp >= 573.15f                                                ? M::MarkSoot  // 300 C
			        : temp <= 253.15f                                                ? M::MarkFrost // -20 C
			                                                                         : -1;
			return k >= 0 && rate[k] > 0 ? k : -1;
		};
		auto mark = [&](int kind, int x, int y) { // a window pixel next to what touched it
			auto &m = Marks(sim, w);
			auto &a = m.a[kind];
			m.any[kind] = true;
			auto add = [&](int i, int j, float v, int cap) {
				int sx = x - w.x0 + i, sy = y - w.y0 + j;
				if (sx < 0 || sy < 0 || sx >= ww || sy >= wh) return;
				auto &p = a[size_t(sy) * ww + sx];
				p = uint8_t(std::min(float(cap), p + v * rate[kind]));
			};
			switch (kind)
			{
			case M::MarkSoot: // a smudge
				for (int j = -2; j <= 2; j++)
					for (int i = -2; i <= 2; i++)
						if (i * i + j * j <= 4) add(i, j, i || j ? 3 : 10, 255);
				break;
			case M::MarkFrost: // speckled crystals, wider
				for (int j = -3; j <= 3; j++)
					for (int i = -3; i <= 3; i++)
						if (i * i + j * j <= 9) add(i, j, float(sim->rng.between(0, 4)), 200);
				break;
			case M::MarkAcid: // now and then a pit
				if (sim->rng.uniform01() < 0.2f)
				{
					add(0, 0, 50, 255);
					add(-1, 0, 15, 255), add(1, 0, 15, 255), add(0, -1, 15, 255), add(0, 1, 15, 255);
				}
				break;
			case M::MarkWater: // a streak running down
				for (int j = 0; j < 10; j++) add(0, j, 4 - j * 0.3f, 140);
				break;
			}
		};
		for (int x = w.x0; x < w.x1; x++)
		{
			if (int k = kindAt(x, w.y0 - 1); k >= 0) mark(k, x, w.y0);
			if (int k = kindAt(x, w.y1); k >= 0) mark(k, x, w.y1 - 1);
		}
		for (int y = w.y0; y < w.y1; y++)
		{
			if (int k = kindAt(w.x0 - 1, y); k >= 0) mark(k, w.x0, y);
			if (int k = kindAt(w.x1, y); k >= 0) mark(k, w.x1 - 1, y);
		}
	}
	// frost melts and water dries (about 20 s and 10 s from full at 60 fps)
	for (auto &[id, m] : sim->windowMarks)
		for (int kind : { int(M::MarkFrost), int(M::MarkWater) })
		{
			if (!m.any[kind] || sim->currentTick % 6 != 0) continue;
			bool any = false;
			for (auto &p : m.a[kind])
				if (p) p -= std::min<uint8_t>(p, kind == M::MarkFrost ? 1 : 2), any |= p != 0;
			m.any[kind] = any;
		}
}

void OverlayDrawMarks(const Simulation *sim, uint32_t *argb, int stride, const uint8_t *occupied, uint8_t wallBit, const uint32_t *backdrop)
{
	// per kind: colour (premultiplied below) and how opaque at full strength, of 256
	constexpr uint32_t colour[] = { 0x8AA8C8, 0xE4F0FF, 0x7A9A2A, 0x221A12 };
	constexpr int opacity[] = { 110, 170, 190, 224 };
	for (auto &[id, m] : sim->windowMarks)
	{
		if (std::none_of(m.any.begin(), m.any.end(), [](bool b) { return b; })) continue;
		for (int j = 0; j < m.h; j++)
			for (int i = 0; i < m.w; i++)
			{
				int x = m.x + i, y = m.y + j;
				if (x < 0 || y < 0 || x >= XRES || y >= YRES) continue;
				if (m.any[Simulation::MarkHole] && m.a[Simulation::MarkHole][size_t(j) * m.w + i]) continue; // below
				if (!(occupied[size_t(y) * XRES + x] & wallBit)) continue;
				uint32_t r = 0, g = 0, b = 0, a = 0; // premultiplied, painted over each other in order
				for (int kind = 0; kind < Simulation::MarkHole; kind++)
				{
					if (!m.any[kind]) continue;
					uint32_t v = m.a[kind][size_t(j) * m.w + i];
					if (!v) continue;
					uint32_t sa = v * opacity[kind] >> 8, c = colour[kind];
					r = (c >> 16) * sa / 255 + r * (255 - sa) / 255;
					g = (c >> 8 & 0xFF) * sa / 255 + g * (255 - sa) / 255;
					b = (c & 0xFF) * sa / 255 + b * (255 - sa) / 255;
					a = sa + a * (255 - sa) / 255;
				}
				if (a) argb[size_t(y) * stride + x] = a << 24 | r << 16 | g << 8 | b;
			}
		if (!m.any[Simulation::MarkHole] || !overlayFeel.destructible) continue;
		// Holes: what is behind the window shows through (what falls through them is drawn over it), ringed by what
		// made them: acid a corroded yellow-green, a blast charred black with a glowing edge.
		auto &hole = m.a[Simulation::MarkHole];
		for (int j = 0; j < m.h; j++)
			for (int i = 0; i < m.w; i++)
			{
				uint8_t how = hole[size_t(j) * m.w + i];
				int x = m.x + i, y = m.y + j;
				if (!how || x < 0 || y < 0 || x >= XRES || y >= YRES) continue;
				auto &p = argb[size_t(y) * stride + x];
				if (!(p >> 24)) p = 0xFF000000 | (backdrop ? backdrop[size_t(y) * XRES + x] : 0x1A1A1A);
				for (int dy = -2; dy <= 2; dy++)
					for (int dx = -2; dx <= 2; dx++)
					{
						int u = i + dx, v = j + dy, d = std::max(std::abs(dx), std::abs(dy));
						if (!d || u < 0 || v < 0 || u >= m.w || v >= m.h || hole[size_t(v) * m.w + u] || x + dx < 0 || y + dy < 0 || x + dx >= XRES || y + dy >= YRES) continue;
						if (!(occupied[size_t(y + dy) * XRES + x + dx] & wallBit)) continue;
						uint32_t rim = how >= 200 ? (d == 1 ? 0xF0C8E040 : 0xA0708A20)  // acid
						                          : (d == 1 ? 0xF0E0661A : 0xE0140A06); // blast: ember, then char
						auto &q = argb[size_t(y + dy) * stride + x + dx];
						if (d == 1 || (q >> 24) < (rim >> 24)) q = rim; // the nearer ring wins
					}
			}
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
