#include "OverlayWalls.h"
#include "simulation/Simulation.h"
#include "SimulationConfig.h"
#include "simulation/ElementClasses.h"
#include "simulation/SimulationData.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include "common/Parallel.h"

OverlayFeel overlayFeel;
std::vector<OverlayImpact> overlayImpacts;

// Chunks of broken window, drawn over the sim: jagged pieces that spin, arc, bounce off windows and fade where they land.
struct Debris
{
	float x, y, vx, vy, angle, spin;
	int life;
	uint32_t colour;
	std::array<float, 6> radius; // a jagged hexagon
};
static std::vector<Debris> debris;
bool OverlayDebrisFlying() { return !debris.empty(); }
bool (*overlayGrabScreen)(int, int, int, int, uint32_t *) = nullptr;
std::map<uintptr_t, float> overlayKicks;

// The marks of a window (or icon) where it is now and at its size now: resized, what still fits is kept, from the top-left.
static Simulation::WindowMarks &Marks(Simulation *sim, const OverlayWall &w)
{
	auto &m = sim->windowMarks[w.id];
	int ww = w.x1 - w.x0, wh = w.y1 - w.y0;
	if (m.w != ww || m.h != wh)
	{
		for (auto &a : m.a)
		{
			if (a.empty()) continue; // a kind this window never had (made by Kind when first needed)
			std::vector<uint8_t> b(size_t(ww) * wh, 0);
			for (int j = 0; j < std::min(wh, m.h); j++)
				for (int i = 0; i < std::min(ww, m.w); i++) b[size_t(j) * ww + i] = a[size_t(j) * m.w + i];
			a = std::move(b);
		}
		m.w = ww, m.h = wh;
	}
	m.x = w.x0, m.y = w.y0;
	return m;
}

// One kind of a window's marks, made when first needed (most windows never get most kinds: each is a byte a pixel)
static std::vector<uint8_t> &Kind(Simulation::WindowMarks &m, int kind)
{
	auto &a = m.a[kind];
	if (a.empty()) a.assign(size_t(m.w) * m.h, 0);
	return a;
}

// Who shows at each sim pixel as of the last tick: 1 + the index of the frontmost window or icon there that isn't
// broken through there (0: none), and the walls' ids by that index (front first)
static std::vector<uint16_t> owner;
static std::vector<uintptr_t> ownerIds;
static uintptr_t OwnerAt(int x, int y)
{
	auto o = owner.size() ? owner[size_t(y) * XRES + x] : 0;
	return o && o <= ownerIds.size() ? ownerIds[o - 1] : 0;
}
static int ZOf(uintptr_t id) // front first; -1: not a wall now
{
	auto it = std::find(ownerIds.begin(), ownerIds.end(), id);
	return it == ownerIds.end() ? -1 : int(it - ownerIds.begin());
}

void OverlayApplyWalls(Simulation *sim, const std::vector<OverlayWall> &walls)
{
	auto old = std::move(sim->pixelWalls);
	auto &mask = sim->pixelWalls;
	// Healing: holes grow back in from their edges (out of the window counts as whole), one ragged ring at a time,
	// 20 rings in the minutes set; a spot with a particle in it waits
	static int healTick = 0;
	if (overlayFeel.heal > 0 && ++healTick >= std::max(1, int(overlayFeel.heal * 3600 / 20)))
	{
		healTick = 0;
		for (auto &[id, m] : sim->windowMarks)
		{
			if (m.any[Simulation::MarkCrack]) // cracks fade
			{
				bool any = false;
				for (auto &p : Kind(m, Simulation::MarkCrack)) p -= std::min<uint8_t>(p, 64), any |= p != 0;
				m.any[Simulation::MarkCrack] = any;
			}
			auto &h = m.a[Simulation::MarkHole];
			if (!m.any[Simulation::MarkHole] || h.empty()) continue;
			auto was = h;
			bool left = false;
			for (int j = 0; j < m.h; j++)
				for (int i = 0; i < m.w; i++)
				{
					if (!was[size_t(j) * m.w + i]) continue;
					auto whole = [&](int a, int b) { return a < 0 || b < 0 || a >= m.w || b >= m.h || !was[size_t(b) * m.w + a]; };
					int x = m.x + i, y = m.y + j;
					bool busy = x >= 0 && y >= 0 && x < XRES && y < YRES && sim->pmap[y][x];
					if (!busy && (whole(i - 1, j) || whole(i + 1, j) || whole(i, j - 1) || whole(i, j + 1)) && sim->rng.uniform01() < 0.6f)
						h[size_t(j) * m.w + i] = 0;
					else left = true;
				}
			m.any[Simulation::MarkHole] = left;
		}
	}
	// Destructible (off unless switched on): acid and blasts eat holes in windows and icons. Only the sim's walls and
	// the overlay's paint change, and clearing the sim mends them: the real windows and icons are never touched.
	// A hole in a front window shows the window behind it, and that one still stops sand: only a pixel no window covers
	// whole is open. Walls come front first; drawn back to front, the front one wins except where it is holed.
	using M = Simulation::WindowMark;
	std::vector<const std::vector<uint8_t> *> holes(walls.size(), nullptr);
	if (overlayFeel.destructible)
		for (size_t k = 0; k < walls.size(); k++)
			if (auto it = sim->windowMarks.find(walls[k].id); walls[k].id && it != sim->windowMarks.end() && it->second.any[M::MarkHole])
				holes[k] = &Kind(Marks(sim, walls[k]), M::MarkHole);
	auto solidAt = [&](size_t k, int x, int y) {
		auto &w = walls[k];
		return x >= w.x0 && x < w.x1 && y >= w.y0 && y < w.y1 && (!w.solid || w.solid(x, y)) &&
		       !(holes[k] && (*holes[k])[size_t(y - w.y0) * (w.x1 - w.x0) + (x - w.x0)]);
	};
	owner.assign(size_t(XRES) * YRES, 0);
	ownerIds.clear();
	for (auto &w : walls) ownerIds.push_back(w.id);
	for (size_t k = walls.size(); k-- > 0;)
	{
		auto &w = walls[k];
		for (int y = w.y0; y < w.y1; y++)
		{
			auto *row = &owner[size_t(y) * XRES];
			if (!w.solid && !holes[k])
				std::fill(row + w.x0, row + w.x1, uint16_t(k + 1));
			else
				for (int x = w.x0; x < w.x1; x++)
					if (solidAt(k, x, y)) row[x] = uint16_t(k + 1);
		}
	}
	mask.resize(owner.size());
	for (size_t i = 0; i < owner.size(); i++) mask[i] = owner[i] != 0;
	enum { Blasted = 100, Eaten = 200 }; // what made the hole: its rim shows it
	auto eat = [&](int x, int y, uint8_t how) { // a wall pixel: the window or icon showing there loses it, the one behind shows
		size_t p = size_t(y) * XRES + x, k = owner[p] - 1;
		if (!owner[p] || !walls[k].id) return;
		auto &w = walls[k];
		auto &m = Marks(sim, w);
		auto &h = Kind(m, M::MarkHole);
		h[size_t(y - w.y0) * m.w + (x - w.x0)] = how;
		m.any[M::MarkHole] = true;
		holes[k] = &h;
		uint16_t next = 0;
		for (size_t j = k + 1; j < walls.size() && !next; j++)
			if (solidAt(j, x, y)) next = uint16_t(j + 1);
		owner[p] = next, mask[p] = next != 0;
	};
	// what touches a window: acid eats it, DEST and singularities destroy it, a bomb blows a chunk out, and anything
	// 2000 C or hotter (plasma, burning thermite, lightning) burns through it
	auto crackCell = [&](int x, int y, bool write) -> uint8_t * { // the crack mark of the window showing at a wall pixel
		size_t p = size_t(y) * XRES + x;
		if (!owner[p] || !walls[owner[p] - 1].id) return nullptr;
		auto &w = walls[owner[p] - 1];
		auto it = sim->windowMarks.find(w.id);
		if (!write && (it == sim->windowMarks.end() || !it->second.any[M::MarkCrack])) return nullptr;
		auto &m = Marks(sim, w);
		m.any[M::MarkCrack] |= write;
		return &Kind(m, M::MarkCrack)[size_t(y - w.y0) * m.w + (x - w.x0)];
	};
	std::vector<uint32_t> grab;
	auto chunk = [&](int cx, int cy, float r, uint8_t how, const float *toward = nullptr) {
		if (overlayFeel.crack && !toward)
		{
			// cracks first: a hit on whole glass only crazes it (jagged lines out from the hit); one on the cracks breaks it
			int R = int(r) + 3, seen = 0, cracked = 0;
			for (int y = std::max(0, cy - R); y < std::min(YRES, cy + R + 1); y++)
				for (int x = std::max(0, cx - R); x < std::min(XRES, cx + R + 1); x++)
					if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= R * R && mask[size_t(y) * XRES + x])
					{
						seen++;
						if (auto *c = crackCell(x, y, false); c && *c) cracked++;
					}
			// a blast's shock wave hits for several ticks: near where it just cracked the glass, it is still that one hit
			static std::vector<std::tuple<int, int, int>> fresh;
			std::erase_if(fresh, [&](auto &f) { return sim->currentTick - std::get<2>(f) > 45; });
			bool same = std::any_of(fresh.begin(), fresh.end(), [&](auto &f) { return std::hypot(float(std::get<0>(f) - cx), float(std::get<1>(f) - cy)) <= r + 20; });
			if (seen && same) return;
			if (seen && cracked < 3)
			{
				fresh.push_back({ cx, cy, sim->currentTick });
				int rays = sim->rng.between(5, 8);
				for (int k = 0; k < rays; k++)
				{
					float a = k * 6.283f / rays + sim->rng.uniform01() * 0.6f, len = std::min(40.f, r * (3 + 3 * sim->rng.uniform01())), px = float(cx), py = float(cy);
					for (int s = 0; s < int(len); s++)
					{
						a += (sim->rng.uniform01() - 0.5f) * 0.5f;
						px += std::cos(a), py += std::sin(a);
						int x = int(px), y = int(py);
						if (x < 0 || y < 0 || x >= XRES || y >= YRES || !mask[size_t(y) * XRES + x]) break;
						if (auto *c = crackCell(x, y, true)) *c = 255;
					}
				}
				return;
			}
		}
		int n = int(r) + 2, bx0 = std::max(0, cx - n), by0 = std::max(0, cy - n), bx1 = std::min(XRES, cx + n), by1 = std::min(YRES, cy + n);
		std::vector<std::pair<int, int>> chips;
		for (int y = by0; y < by1; y++)
			for (int x = bx0; x < bx1; x++)
			{
				float d = std::hypot(float(x - cx), float(y - cy)), edge = r + 1.5f * std::sin(x * 1.7f + y * 2.3f) * std::cos(x * 0.9f - y * 1.3f);
				if (d < edge && mask[size_t(y) * XRES + x])
				{
					eat(x, y, how);
					if (sim->rng.uniform01() < 0.05f * overlayFeel.shards) chips.push_back({ x, y });
				}
			}
		if (chips.empty() || bx1 <= bx0 || by1 <= by0) return;
		// some of it chips off: clumps of shards, the colour of the window they came from, kicked up and out of the hole
		// (or pulled toward a black hole), then they fall
		bool tint = overlayFeel.shardTint && overlayGrabScreen;
		if (tint) grab.resize(size_t(bx1 - bx0) * (by1 - by0)), tint = overlayGrabScreen(bx0, by0, bx1 - bx0, by1 - by0, grab.data());
		int size = std::max(1, int(std::lround(overlayFeel.shardSize)));
		for (auto [x, y] : chips)
		{
			if (!overlayFeel.shardParticles && debris.size() >= 600) break;
			float vx, vy;
			if (toward)
			{
				float dx = toward[0] - x, dy = toward[1] - y, d = std::max(1.f, std::hypot(dx, dy));
				vx = dx / d * 3, vy = dy / d * 3;
			}
			else
			{
				float d = std::max(1.f, std::hypot(float(x - cx), float(y - cy)));
				vx = (x - cx) / d * (2 + 4 * sim->rng.uniform01());
				vy = -(4 + 6 * sim->rng.uniform01());
			}
			// the window's colour, a shade darker so they show against it (a broken edge)
			uint32_t c = tint ? grab[size_t(y - by0) * (bx1 - bx0) + (x - bx0)] : 0x5A5F66;
			uint32_t colour = ((c >> 16 & 0xFF) * 3 / 5) << 16 | ((c >> 8 & 0xFF) * 3 / 5) << 8 | (c & 0xFF) * 3 / 5;
			if (!overlayFeel.shardParticles)
			{
				Debris d{ float(x), float(y), vx, vy, sim->rng.uniform01() * 6.28f, (sim->rng.uniform01() - 0.5f) * 0.6f, 180, c, {} };
				for (auto &r : d.radius) r = overlayFeel.shardSize * (0.5f + 0.7f * sim->rng.uniform01());
				debris.push_back(d);
				continue;
			}
			for (int j = 0; j < size; j++)
				for (int i = 0; i < size; i++)
				{
					int sx = x + i, sy = y + j;
					if (sx >= XRES || sy >= YRES || mask[size_t(sy) * XRES + sx]) continue;
					if (int s = sim->create_part(-1, sx, sy, PT_BGLA); s >= 0)
					{
						auto &q = sim->parts[s];
						q.vx = vx, q.vy = vy;
						q.temp = R_TEMP + 273.15f; // not melted by the blast's own heat straight away
						q.dcolour = 0xFF000000 | colour;
					}
				}
		}
	};
	if (overlayFeel.destructible)
		for (int i = 0; i <= sim->parts.active; i++)
		{
			auto &p = sim->parts[i];
			int t = p.type;
			if (t == PT_NBHL && overlayFeel.blackHole > 0)
			{
				// a black hole tears at windows near it: the nearer, the likelier, and the bits fly into it
				float R = 30 * overlayFeel.blackHole, hole[2] = { p.x, p.y };
				for (int k = 0; k < 3; k++)
				{
					int sx = int(p.x + (sim->rng.uniform01() * 2 - 1) * R), sy = int(p.y + (sim->rng.uniform01() * 2 - 1) * R);
					float d = std::hypot(sx - p.x, sy - p.y);
					if (sx < 0 || sy < 0 || sx >= XRES || sy >= YRES || d > R || !mask[size_t(sy) * XRES + sx]) continue;
					if (sim->rng.uniform01() < 0.15f * (1 - d / R)) chunk(sx, sy, 2, Blasted, hole);
				}
				continue;
			}
			if (t == PT_SNOW && p.dcolour == overlayHailColour) // hail: now and then a stone that hits a window chips it
			{
				int x = int(p.x + 0.5f), y = int(p.y + 0.5f);
				if (p.vy > 1.5f && overlayFeel.blast > 0 && sim->rng.uniform01() < 0.03f * overlayFeel.blast)
					for (int dy = 1; dy <= 3; dy++)
						if (y + dy < YRES && x >= 0 && x < XRES && mask[size_t(y + dy) * XRES + x])
						{
							chunk(x, y + dy, 1, Blasted);
							break;
						}
				continue;
			}
			bool acid = t == PT_ACID && overlayFeel.acidEat > 0, hot = p.temp >= 2273.15f && overlayFeel.blast > 0;
			bool destroys = (t == PT_DEST || t == PT_SING || t == PT_BOMB) && overlayFeel.blast > 0;
			if (!acid && !hot && !destroys) continue;
			int x = int(p.x + 0.5f), y = int(p.y + 0.5f);
			for (auto [dx, dy] : { std::pair{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } })
			{
				int nx = x + dx, ny = y + dy;
				if (nx < 0 || ny < 0 || nx >= XRES || ny >= YRES || !mask[size_t(ny) * XRES + nx]) continue;
				if (acid)
				{
					if (sim->rng.uniform01() >= 0.1f * overlayFeel.acidEat) continue;
					eat(nx, ny, Eaten);
					if (sim->rng.uniform01() < 0.4f) // ragged: now and then a bit more crumbles
					{
						int ex = nx + sim->rng.between(-1, 1), ey = ny + sim->rng.between(-1, 1);
						if (ex >= 0 && ey >= 0 && ex < XRES && ey < YRES && mask[size_t(ey) * XRES + ex]) eat(ex, ey, Eaten);
					}
					if (sim->rng.uniform01() < 0.3f) sim->kill_part(i); // used up, like acid eating anything else
				}
				else if (t == PT_BOMB)
				{
					chunk(nx, ny, 6 * overlayFeel.blast, Blasted);
					sim->kill_part(i); // it went off
				}
				else if (sim->rng.uniform01() < (t == PT_DEST || t == PT_SING ? 0.3f : 0.03f) * overlayFeel.blast)
					chunk(nx, ny, t == PT_SING ? 2.5f : 1.5f, Blasted); // DEST, a singularity, or (slowly) burning through
				break;
			}
		}
	// shooting stars that came down: a burst of sparks and a puff, and a chunk out of a window they hit
	for (auto [ix, iy, size] : overlayImpacts)
	{
		if (overlayFeel.destructible && overlayFeel.blast > 0) chunk(ix, iy, 4 * size * overlayFeel.blast, Blasted);
		sim->pv[iy / CELL][ix / CELL] += 6 * size;
		if (size > 1) // a big meteor: a crater, the rim thrown up and out
		{
			auto &elements = SimulationData::CRef().elements;
			int R = int(5 * size);
			for (int y = std::max(0, iy - R); y < std::min(YRES, iy + R); y++)
				for (int x = std::max(0, ix - R); x < std::min(XRES, ix + R); x++)
					if (int r = sim->pmap[y][x])
					{
						float dx = float(x - ix), dy = float(y - iy), d = std::hypot(dx, dy);
						if (d > R) continue;
						if (d < R / 2.f) sim->kill_part(ID(r));
						else if (!(elements[TYP(r)].Properties & TYPE_SOLID))
							sim->parts[ID(r)].vx = dx / std::max(1.f, d) * 5, sim->parts[ID(r)].vy = -4 - 4 * sim->rng.uniform01();
					}
		}
		for (int k = 0; k < int(14 * size); k++)
			if (int s = sim->create_part(-1, std::clamp(ix + sim->rng.between(-3, 3), 0, XRES - 1), std::clamp(iy - sim->rng.between(1, 5), 0, YRES - 1), k % 3 == 2 && size > 1 ? PT_SMKE : PT_EMBR); s >= 0)
				sim->parts[s].vx = (sim->rng.uniform01() - 0.5f) * 6, sim->parts[s].vy = -1 - 3 * sim->rng.uniform01();
	}
	overlayImpacts.clear();
	// the chunks fly: gravity, a little air, and they bounce off windows, then lie there and fade
	for (auto &d : debris)
	{
		d.vy += 0.25f, d.vx *= 0.99f, d.angle += d.spin, d.life--;
		float nx = d.x + d.vx, ny = d.y + d.vy;
		int ix = int(nx), iy = int(ny);
		if (ix >= 0 && iy >= 0 && ix < XRES && iy < YRES && mask[size_t(iy) * XRES + ix])
		{
			if (!(int(d.y) >= 0 && int(d.y) < YRES && ix >= 0 && ix < XRES && mask[size_t(int(d.y)) * XRES + ix])) d.vy = -d.vy * 0.3f; // hit from above or below
			else d.vx = -d.vx * 0.4f; // a side
			d.vx *= 0.6f, d.spin *= 0.5f;
			if (std::abs(d.vy) < 0.8f) d.vy = 0, d.spin = 0, d.vx *= 0.5f, d.life = std::min(d.life, 60); // landed
			continue;
		}
		d.x = nx, d.y = ny;
	}
	debris.erase(std::remove_if(debris.begin(), debris.end(), [](const Debris &d) { return d.life <= 0 || d.y > YRES + 10 || d.x < -10 || d.x > XRES + 10; }), debris.end());
	// a blast's shock wave (pressure jumping up, not pressure that lingers after) tears out a ragged round chunk, bigger
	// the harder it hits (a small C-4 charge peaks ~6 in its own cells)
	static std::vector<float> lastPressure;
	lastPressure.resize(size_t(XCELLS) * YCELLS, 0.f);
	// and shakes the windows it hits, by the biggest jump in pressure on or just around them
	overlayKicks.clear();
	if (overlayFeel.shake > 0)
		for (auto &w : walls)
		{
			float kick = 0;
			for (int cy = std::max(0, w.y0 / CELL - 1); cy < std::min(YCELLS, w.y1 / CELL + 2); cy++)
				for (int cx = std::max(0, w.x0 / CELL - 1); cx < std::min(XCELLS, w.x1 / CELL + 2); cx++)
					kick = std::max(kick, sim->pv[cy][cx] - lastPressure[size_t(cy) * XCELLS + cx]);
			if (w.id && kick > 1) overlayKicks[w.id] = std::max(overlayKicks[w.id], kick);
		}
	if (overlayFeel.destructible && overlayFeel.blast > 0)
		for (int cy = 0; cy < YCELLS; cy++)
			for (int cx = 0; cx < XCELLS; cx++)
			{
				float pv = sim->pv[cy][cx], jump = pv - lastPressure[size_t(cy) * XCELLS + cx];
				float r = std::min(24.f, (pv - 3) * 5 * overlayFeel.blast);
				if (r > 0 && jump > 1) chunk(cx * CELL + CELL / 2, cy * CELL + CELL / 2, r, Blasted);
			}
	for (int cy = 0; cy < YCELLS; cy++)
		for (int cx = 0; cx < XCELLS; cx++) lastPressure[size_t(cy) * XCELLS + cx] = sim->pv[cy][cx];
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
		float jolt = 0; // how sharply its speed changed (px per tick): steady moves carry everything, jolts shear
		if (w.id && !w.solid && std::abs(mx) <= 40 && std::abs(my) <= 40) // icons jump when dropped: nothing to fling
		{
			auto &h = moves[w.id] = lastMoves[w.id];
			std::rotate(h.begin(), h.begin() + 1, h.end());
			h.back() = { mx, my };
			float ax = 0, ay = 0, bx = 0, by = 0;
			for (int i = 0; i < 4; i++) ax += h[i].first / 4.f, ay += h[i].second / 4.f;
			for (int i = 4; i < 6; i++) bx += h[i].first / 2.f, by += h[i].second / 2.f;
			float was = std::hypot(ax, ay), now = std::hypot(bx, by);
			jolt = std::hypot(bx - ax, by - ay);
			if (was >= 3 && (now < was * 0.4f || ax * bx + ay * by < 0)) // it stopped short or turned back
			{
				fx = int(std::lround(ax - bx)), fy = int(std::lround(ay - by));
				h.fill({ int(std::lround(bx)), int(std::lround(by)) }); // flung once; the speed it has now carries on
			}
		}
		// a dropped icon takes its pile with it however far it went; a window jumping (snapping, maximising) leaves it
		if (!w.id || (!mx && !my && !fx && !fy) || (!w.solid && (std::abs(mx) > 40 || std::abs(my) > 40))) continue;
		// Friction, layer by layer: what touches the window grips it; each layer up is a little more likely to slip (more so
		// when the window starts, stops or turns) and only gets a nudge, so even a slow drag shears a tall pile. Solids go as one block, heavy powders grip better than light ones,
		// liquids drag only their bottom and slosh, gas isn't carried. A window going down carries nothing down: what's
		// on it falls on its own (and is only dragged sideways while it still presses on a slow one).
		auto &elements = SimulationData::CRef().elements;
		int cy = my < 0 ? my : 0;
		bool dragged = my <= 2;
		struct Rider { int r, x, y, dx, dy; };
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
			float hold = overlayFeel.grip; // chance this layer moves with the one under it
			for (int y = top; y >= 0 && sim->pmap[y][x]; y--)
			{
				int r = sim->pmap[y][x];
				auto &el = elements[TYP(r)];
				if (el.Properties & (TYPE_GAS | TYPE_ENERGY)) break;
				if (el.Properties & TYPE_LIQUID) hold *= y == top ? std::pow(0.9f, jolt * 0.35f) : std::pow(0.6f, 1 + jolt * 0.35f); // above the wet layer it sloshes even when steady
				else if (!(el.Properties & TYPE_SOLID) && y != top) hold *= std::pow(0.93f + 0.05f * std::clamp(el.Weight / 100.f, 0.f, 1.f), 0.4f + jolt * 0.35f);
				bool held = dragged && sim->rng.uniform01() < hold;
				if (!held && mx && dragged) sim->parts[ID(r)].vx += mx * 0.5f * std::sqrt(hold); // slipped: still tugged a little
				if (held || cy) riders.push_back({ r, x, y, held ? mx : 0, cy });
			}
		}
		// front ones first: a rider's old spot is then never taken by one moved before it, so a blocked one stays put
		std::sort(riders.begin(), riders.end(), [mx, my](const Rider &a, const Rider &b) { return a.x * mx + a.y * my > b.x * mx + b.y * my; });
		for (auto &p : riders) sim->pmap[p.y][p.x] = 0;
		auto open = [&](int x, int y, int r) {
			return x >= 0 && y >= 0 && x < XRES && y < YRES && !sim->pmap[y][x] && !mask[size_t(y) * XRES + x] && !sim->IsWallBlocking(x, y, TYP(r));
		};
		for (auto &p : riders)
		{
			int tx = p.x + p.dx, ty = p.y + p.dy;
			if (!open(tx, ty, p.r)) tx = p.x; // blocked (another window, a wall): scraped off sideways
			if (!open(tx, ty, p.r)) ty = p.y;
			while (ty > 0 && sim->pmap[ty][tx]) ty--; // its own spot taken by a neighbour that moved: on top of it
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
		// pets stand on their legs, out of reach of the scan above: one whose feet are on the old top edge rides along
		if (!w.solid && std::abs(mx) <= 40 && std::abs(my) <= 40)
			for (int i = 0; i <= sim->parts.active; i++)
			{
				auto &p = sim->parts[i];
				if (p.type != PT_FIGH) continue;
				auto &f = sim->fighters[(unsigned char)p.tmp];
				float feet = std::max(f.legs[5], f.legs[13]), fx = (f.legs[4] + f.legs[12]) / 2;
				if (fx < w.x0 - mx || fx >= w.x1 - mx || feet < w.y0 - my - 4 || feet > w.y0 - my + 1) continue;
				p.x += mx, p.y += my;
				for (int k = 0; k < 16; k += 2) f.legs[k] += mx, f.legs[k + 1] += my;
			}
	}
	lastMoves = std::move(moves);
	// a window that sat still at the same size since the last tick has nothing new under it to shove
	static std::map<uintptr_t, std::array<int, 4>> lastRects;
	std::map<uintptr_t, std::array<int, 4>> rects;
	for (auto &w : walls)
	{
		std::array<int, 4> rect{ w.x0, w.y0, w.x1, w.y1 };
		if (w.id) rects[w.id] = rect;
		if (auto it = lastRects.find(w.id); w.id && !w.mx && !w.my && it != lastRects.end() && it->second == rect) continue;
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
	lastRects = std::move(rects);
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
			auto &a = Kind(m, kind);
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
	constexpr uint32_t colour[] = { 0x8AA8C8, 0xE4F0FF, 0x7A9A2A, 0x221A12, 0x16191C };
	constexpr int opacity[] = { 110, 170, 190, 224, 235 };
	for (auto &[id, m] : sim->windowMarks)
	{
		if (std::none_of(m.any.begin(), m.any.end(), [](bool b) { return b; })) continue;
		// each pixel is one window's (OwnerAt), so rows of a big window go on all cores
		auto rows = [&](int j0, int j1) {
		for (int j = j0; j < j1; j++)
			for (int i = 0; i < m.w; i++)
			{
				int x = m.x + i, y = m.y + j;
				if (x < 0 || y < 0 || x >= XRES || y >= YRES) continue;
				if (m.any[Simulation::MarkHole] && m.a[Simulation::MarkHole][size_t(j) * m.w + i]) continue; // below
				if (!(occupied[size_t(y) * XRES + x] & wallBit) || OwnerAt(x, y) != id) continue; // hidden behind another window
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
				if (a) // over what is there (premultiplied)
				{
					auto &p = argb[size_t(y) * stride + x];
					auto over = [&](uint32_t s, int sh) { return std::min(255u, s + ((p >> sh) & 0xFF) * (255 - a) / 255) << sh; };
					p = over(a, 24) | over(r, 16) | over(g, 8) | over(b, 0);
				}
			}
		};
		if (size_t(m.w) * m.h < 100000) rows(0, m.h);
		else OverlayParallel((m.h + 31) / 32, [&](int k) { rows(k * 32, std::min(m.h, k * 32 + 32)); });
		if (!m.any[Simulation::MarkHole] || !overlayFeel.destructible) continue;
		// Holes: what is behind the window shows through (what falls through them is drawn over it), ringed by what
		// made them: acid a corroded yellow-green, a blast charred black with a glowing edge.
		auto &hole = m.a[Simulation::MarkHole];
		int z = ZOf(id);
		auto behind = [&](int x, int y) { // what is behind, under whatever is drawn here (particles, a glow, the marks of the window behind)
			auto &p = argb[size_t(y) * stride + x];
			if (uint32_t k = uint32_t((1 - std::clamp(overlayFeel.holeOpacity, 0.f, 1.f)) * 255), a = p >> 24; k && a < 255)
			{
				uint32_t c = backdrop ? backdrop[size_t(y) * XRES + x] : 0x1A1A1A, f = k * (255 - a) / 255;
				auto under = [&](int sh) { return std::min(255u, ((p >> sh) & 0xFF) + ((c >> sh) & 0xFF) * f / 255) << sh; };
				p = (a + f) << 24 | under(16) | under(8) | under(0);
			}
		};
		for (int j = 0; j < m.h; j++)
			for (int i = 0; i < m.w; i++)
			{
				uint8_t how = hole[size_t(j) * m.w + i];
				int x = m.x + i, y = m.y + j;
				if (!how || x < 0 || y < 0 || x >= XRES || y >= YRES) continue;
				if (int oz = owner.size() ? int(owner[size_t(y) * XRES + x]) - 1 : -1; z < 0 || (oz >= 0 && oz < z)) continue; // a window in front covers this hole
				behind(x, y);
				// the window's real border (Windows draws a thin line round it, just at or past where the sim has its edge)
				// is covered too where the hole reaches the edge, out to 2 px where no other window is
				if (overlayFeel.holeBorder)
					for (int s = 1; s <= 2; s++)
					{
						int ox = i == 0 ? -s : i == m.w - 1 ? s : 0, oy = j == 0 ? -s : j == m.h - 1 ? s : 0;
						auto open = [&](int u, int v) { return u >= 0 && v >= 0 && u < XRES && v < YRES && !(owner.size() && owner[size_t(v) * XRES + u]); };
						if (ox && open(x + ox, y)) behind(x + ox, y);
						if (oy && open(x, y + oy)) behind(x, y + oy);
						if (ox && oy && open(x + ox, y + oy)) behind(x + ox, y + oy);
					}
				for (int dy = -2; dy <= 2; dy++)
					for (int dx = -2; dx <= 2; dx++)
					{
						int u = i + dx, v = j + dy, d = std::max(std::abs(dx), std::abs(dy));
						if (!d || u < 0 || v < 0 || u >= m.w || v >= m.h || hole[size_t(v) * m.w + u] || x + dx < 0 || y + dy < 0 || x + dx >= XRES || y + dy >= YRES) continue;
						if (!(occupied[size_t(y + dy) * XRES + x + dx] & wallBit) || OwnerAt(x + dx, y + dy) != id) continue;
						uint32_t rim = how >= 200 ? (d == 1 ? 0xF0C8E040 : 0xA0708A20)  // acid
						                          : (d == 1 ? 0xF0E0661A : 0xE0140A06); // blast: ember, then char
						auto &q = argb[size_t(y + dy) * stride + x + dx];
						if (d == 1 || (q >> 24) < (rim >> 24)) q = rim; // the nearer ring wins
					}
			}
	}
	// flying chunks: a jagged piece in the window's colour, catching the light as it turns, with a darker broken edge
	for (auto &d : debris)
	{
		std::array<std::pair<float, float>, 6> v;
		for (int k = 0; k < 6; k++)
			v[k] = { d.x + std::cos(d.angle + k * 1.047f) * d.radius[k], d.y + std::sin(d.angle + k * 1.047f) * d.radius[k] };
		auto inside = [&](float px, float py) {
			for (int k = 0; k < 6; k++)
			{
				auto [ax, ay] = v[k];
				auto [bx, by] = v[(k + 1) % 6];
				if ((bx - ax) * (py - ay) - (by - ay) * (px - ax) < 0) return false;
			}
			return true;
		};
		float shade = 0.85f + 0.3f * std::cos(d.angle * 2);
		uint32_t alpha = uint32_t(255 * std::min(1, d.life) * std::min(1.f, d.life / 30.f));
		int n = int(overlayFeel.shardSize * 1.3f) + 2;
		for (int y = std::max(0, int(d.y) - n); y <= std::min(YRES - 1, int(d.y) + n); y++)
			for (int x = std::max(0, int(d.x) - n); x <= std::min(XRES - 1, int(d.x) + n); x++)
			{
				float px = x + 0.5f, py = y + 0.5f;
				if (!inside(px, py)) continue;
				bool edge = !inside(px + 1, py) || !inside(px - 1, py) || !inside(px, py + 1) || !inside(px, py - 1);
				float k = edge ? shade * 0.55f : shade;
				uint32_t r = std::min(255u, uint32_t((d.colour >> 16 & 0xFF) * k)), g = std::min(255u, uint32_t((d.colour >> 8 & 0xFF) * k)), b = std::min(255u, uint32_t((d.colour & 0xFF) * k));
				argb[size_t(y) * stride + x] = alpha << 24 | (r * alpha / 255) << 16 | (g * alpha / 255) << 8 | b * alpha / 255;
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
