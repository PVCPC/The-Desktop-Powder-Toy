#include "simulation/ElementCommon.h"
#include <cmath>
#include <map>
#include <random>

static int update(UPDATE_FUNC_ARGS);
static void create(ELEMENT_CREATE_FUNC_ARGS);

void Element::Element_JOIN()
{
	Identifier = "DEFAULT_PT_JOIN";
	Name = "JOIN";
	Colour = 0x5AD2C8_rgb;
	MenuVisible = 1;
	MenuSection = SC_SPECIAL;
	Enabled = 1;

	// gravity, wind and damping are applied by the body (update), so the pinned ends stay put
	Advection = 0.0f;
	AirDrag = 0.00f * CFDS;
	AirLoss = 0.99f;
	Loss = 1.00f;
	Collision = -0.3f;
	Gravity = 0.0f;
	Diffusion = 0.00f;
	HotAir = 0.000f	* CFDS;
	Falldown = 0;

	Flammable = 0;
	Explosive = 0;
	Meltable = 0;
	Hardness = 10;

	Weight = 60;

	HeatConduct = 30;
	Description = "Joints. What you paint in one stroke holds together: blobs wobble like jelly, lines hang like rope. Pinned where it touches a solid.";

	Properties = TYPE_PART;

	LowPressure = IPL;
	LowPressureTransition = NT;
	HighPressure = IPH;
	HighPressureTransition = NT;
	LowTemperature = ITL;
	LowTemperatureTransition = NT;
	HighTemperature = ITH;
	HighTemperatureTransition = NT;

	Update = &update;
	Create = &create;
}

// Fields: tmp = which body (0 = just painted), tmp2 = 1 when pinned, ctype = this run's stamp (a pixel from a save
// has another, so its body gets rebuilt). Springs live here and are rebuilt from the pixels
// (their distances then become the rest lengths) when a save loads.
namespace
{
	struct Spring { int a, b; float rest; };
	struct Body
	{
		std::vector<int> members;
		std::vector<Spring> springs;
		int lastTick = -1;
		int hold = -1; // the pixel the mouse holds, while it does
	};
	std::map<int, Body> bodies;
	int nextId = 1;
	constexpr float reach = 2.0f; // pixels this close are tied together
	std::vector<std::pair<float, float>> vel(NPART), shape(NPART); // shape: where each pixel was painted
	const int stamp = int(std::random_device{}() | 1);
}

static void create(ELEMENT_CREATE_FUNC_ARGS)
{
	sim->parts[i].tmp = 0;
}

static bool Pinned(Simulation *sim, int x, int y)
{
	auto &elements = SimulationData::CRef().elements;
	for (int dy = -1; dy <= 1; dy++)
		for (int dx = -1; dx <= 1; dx++)
		{
			int nx = x + dx, ny = y + dy;
			if (nx < 0 || ny < 0 || nx >= XRES || ny >= YRES || sim->IsWallBlocking(nx, ny, PT_JOIN)) return true;
			int r = sim->pmap[ny][nx];
			if (r && TYP(r) != PT_JOIN && (elements[TYP(r)].Properties & TYPE_SOLID)) return true;
		}
	return false;
}

// Ties the pixels painted with pixel i (tmp 0) into a body: a new one, or the body they touch (a stroke drawn over
// several frames, or one from a loaded save, where every pixel has the body's tmp)
static Body &Gather(Simulation *sim, int i)
{
	int tmp = sim->parts[i].tmp, id = tmp;
	std::vector<int> found{ i };
	std::vector<char> seen(NPART);
	seen[i] = 1;
	for (size_t k = 0; k < found.size(); k++)
	{
		auto &p = sim->parts[found[k]];
		int x = int(p.x + 0.5f), y = int(p.y + 0.5f);
		for (int dy = -2; dy <= 2; dy++)
			for (int dx = -2; dx <= 2; dx++)
			{
				int nx = x + dx, ny = y + dy;
				if (nx < 0 || ny < 0 || nx >= XRES || ny >= YRES) continue;
				int r = sim->pmap[ny][nx];
				if (!r || TYP(r) != PT_JOIN || seen[ID(r)]) continue;
				int t = sim->parts[ID(r)].tmp;
				if (!tmp && t && !id) id = t; // new paint touching an existing body joins it
				if (t != tmp) continue;
				seen[ID(r)] = 1;
				found.push_back(ID(r));
			}
	}
	if (!id) id = nextId++;
	nextId = std::max(nextId, id + 1);
	auto &b = bodies[id];
	std::vector<int> old = b.members;
	for (int j : found)
	{
		auto &p = sim->parts[j];
		p.tmp = id, p.ctype = stamp;
		vel[j] = { p.vx, p.vy };
		shape[j] = { p.x, p.y };
		p.tmp2 = Pinned(sim, int(p.x + 0.5f), int(p.y + 0.5f));
		b.members.push_back(j);
	}
	// springs from each new pixel to every pixel near it, new or old, at today's distance
	for (size_t k = 0; k < found.size(); k++)
	{
		auto &p = sim->parts[found[k]];
		auto tie = [&](int j) {
			auto &q = sim->parts[j];
			float d = std::hypot(q.x - p.x, q.y - p.y);
			if (d <= reach) b.springs.push_back({ found[k], j, d });
		};
		for (size_t m = k + 1; m < found.size(); m++) tie(found[m]);
		for (int j : old) tie(j);
	}
	return b;
}

static void Step(Simulation *sim, int id, Body &b)
{
	auto &parts = sim->parts;
	std::erase_if(b.members, [&](int j) { return parts[j].type != PT_JOIN || parts[j].tmp != id; });
	std::erase_if(b.springs, [&](auto &s) {
		auto &p = parts[s.a], &q = parts[s.b];
		return p.type != PT_JOIN || q.type != PT_JOIN || p.tmp != id || q.tmp != id;
	});
	// The body moves its own pixels (TPT's movement would have them block each other), so their velocity lives in
	// vel and TPT's vx/vy stay 0. Gases in the way are pushed out (killed)
	auto &elements = SimulationData::CRef().elements;
	auto at = [](float v) { return int(v + 0.5f); };
	// while placing, at most 3 of its pixels share a cell (sub-pixel positions round together, but TPT turns big
	// piles into black holes)
	static std::vector<unsigned char> crowd(XRES * YRES);
	bool placing = false;
	auto free = [&](int x, int y) {
		if (x < 0 || y < 0 || x >= XRES || y >= YRES || sim->PixelWall(x, y) || sim->IsWallBlocking(x, y, PT_JOIN)) return false;
		int r = sim->pmap[y][x];
		return !r || (TYP(r) == PT_JOIN && parts[ID(r)].tmp == id && (!placing || crowd[y * XRES + x] < 3)) || (elements[TYP(r)].Properties & TYPE_GAS);
	};
	// a pixel at a time, so thin things (a rope, a bridge) can't be skipped over; blocked, slide with what is left
	auto walk = [&](float &x, float &y, float tx, float ty) {
		int n = int(std::ceil(std::max(std::abs(tx - x), std::abs(ty - y))));
		float x0 = x, y0 = y;
		for (int k = 1; k <= n; k++)
		{
			float nx = x0 + (tx - x0) * k / n, ny = y0 + (ty - y0) * k / n;
			if (!free(at(nx), at(ny))) return false;
			x = nx, y = ny;
		}
		x = tx, y = ty;
		return true;
	};
	auto go = [&](float &x, float &y, float tx, float ty) {
		if (!walk(x, y, tx, ty)) walk(x, y, tx, y), walk(x, y, x, ty);
	};

	// where each pixel would go this tick (gravity, wind), then pull those spots back to the springs' lengths a few
	// times, never into anything solid (or the bottom of a blob gets squashed into the floor and the rest piles up)
	static std::vector<float> px(NPART), py(NPART), sx(NPART), sy(NPART);
	// held by the mouse: the pixel nearest where it grabbed follows the pointer, the rest hangs off it
	auto &g = sim->grab;
	if (!(g.on && g.type == PT_JOIN && g.body == id)) b.hold = -1;
	else if (b.hold < 0 || parts[b.hold].type != PT_JOIN || parts[b.hold].tmp != id)
	{
		float best = 1e9f;
		for (int j : b.members)
			if (float d = std::hypot(parts[j].x - g.x, parts[j].y - g.y); d < best) best = d, b.hold = j;
	}
	auto fixed = [&](int j) { return parts[j].tmp2 || j == b.hold; };
	// the hand's pull, which the rest of the body shares (springs alone let the held pixel run off and snap)
	float hx = b.hold < 0 ? 0 : std::clamp((g.x - parts[b.hold].x) * 0.5f, -8.f, 8.f);
	float hy = b.hold < 0 ? 0 : std::clamp((g.y - parts[b.hold].y) * 0.5f, -8.f, 8.f);
	for (int j : b.members)
	{
		auto &p = parts[j];
		px[j] = p.x, py[j] = p.y;
		if (j == b.hold)
		{
			vel[j] = { hx, hy };
			go(px[j], py[j], p.x + hx, p.y + hy);
		}
		else if (!p.tmp2)
		{
			int cx = std::clamp(at(p.x) / CELL, 0, XCELLS - 1), cy = std::clamp(at(p.y) / CELL, 0, YCELLS - 1);
			auto &[vx, vy] = vel[j];
			vy += 0.1f;
			vx += (sim->vx[cy][cx] - vx) * 0.02f, vy += (sim->vy[cy][cx] - vy) * 0.02f;
			if (b.hold >= 0) vx += (hx - vx) * 0.3f, vy += (hy - vy) * 0.3f;
			vx = std::clamp(vx * 0.99f, -8.f, 8.f), vy = std::clamp(vy * 0.99f, -8.f, 8.f);
			go(px[j], py[j], p.x + vx, p.y + vy);
		}
		sx[j] = px[j], sy[j] = py[j];
	}
	// thick bodies (blobs, planks) keep their shape: find how the painted shape best fits the pixels now (moved and
	// turned) and pull each pixel part way there. Thin ones (ropes, bridges) only have the springs, so they bend
	{
		float cx = 0, cy = 0, sx0 = 0, sy0 = 0, x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
		for (int j : b.members)
		{
			auto [qx, qy] = shape[j];
			cx += px[j], cy += py[j], sx0 += qx, sy0 += qy;
			x0 = std::min(x0, qx), x1 = std::max(x1, qx), y0 = std::min(y0, qy), y1 = std::max(y1, qy);
		}
		float n = float(b.members.size());
		if (n > 4 * (std::max(x1 - x0, y1 - y0) + 1))
		{
			cx /= n, cy /= n, sx0 /= n, sy0 /= n;
			float dot = 0, cross = 0;
			for (int j : b.members)
			{
				float qx = shape[j].first - sx0, qy = shape[j].second - sy0, rx = px[j] - cx, ry = py[j] - cy;
				dot += qx * rx + qy * ry, cross += qx * ry - qy * rx;
			}
			float a = std::atan2(cross, dot), c = std::cos(a), s = std::sin(a);
			for (int j : b.members)
			{
				if (fixed(j)) continue;
				float qx = shape[j].first - sx0, qy = shape[j].second - sy0;
				px[j] += (cx + c * qx - s * qy - px[j]) * 0.25f, py[j] += (cy + s * qx + c * qy - py[j]) * 0.25f;
			}
		}
	}
	for (int it = 0; it < 12; it++)
	{
		for (size_t k = 0; k < b.springs.size(); k++)
		{
			auto &s = b.springs[it & 1 ? b.springs.size() - 1 - k : k]; // alternate directions, or the blob creeps one way
			float wa = fixed(s.a) ? 0.f : 1.f, wb = fixed(s.b) ? 0.f : 1.f;
			float dx = px[s.b] - px[s.a], dy = py[s.b] - py[s.a], d = std::hypot(dx, dy);
			if (wa + wb == 0 || d < 0.01f) continue;
			float c = (d - s.rest) / d / (wa + wb);
			px[s.a] += wa * c * dx, py[s.a] += wa * c * dy;
			px[s.b] -= wb * c * dx, py[s.b] -= wb * c * dy;
		}
		for (int j : b.members)
			if (free(at(px[j]), at(py[j]))) sx[j] = px[j], sy[j] = py[j];
			else px[j] = sx[j], py[j] = sy[j];
	}
	// still stretched after all that: pulled too hard, it snaps (pixels just lagging behind don't count)
	std::erase_if(b.springs, [&](auto &s) { return std::hypot(px[s.b] - px[s.a], py[s.b] - py[s.a]) > s.rest * 2 + 2; });

	for (int j : b.members)
		if (int x = at(parts[j].x), y = at(parts[j].y); sim->pmap[y][x] == PMAP(j, PT_JOIN)) sim->pmap[y][x] = 0;
	placing = true;
	for (size_t k = 0; k < b.members.size(); k++)
	{
		int j = b.members[sim->currentTick & 1 ? b.members.size() - 1 - k : k]; // alternate, or it creeps one way
		auto &p = parts[j];
		p.vx = p.vy = 0;
		if (!p.tmp2)
		{
			float ox = p.x, oy = p.y;
			go(p.x, p.y, px[j], py[j]);
			vel[j] = { p.x - ox, p.y - oy };
		}
		int x = at(p.x), y = at(p.y);
		if (int r = sim->pmap[y][x]; r && TYP(r) != PT_JOIN) sim->kill_part(ID(r));
		sim->pmap[y][x] = PMAP(j, PT_JOIN);
		crowd[y * XRES + x]++;
	}
	for (int j : b.members) crowd[at(parts[j].y) * XRES + at(parts[j].x)] = 0;
}

static int update(UPDATE_FUNC_ARGS)
{
	auto &p = parts[i];
	auto it = p.tmp ? bodies.find(p.tmp) : bodies.end();
	Body *b = it == bodies.end() ? nullptr : &it->second;
	if (b && p.ctype != stamp) b = nullptr;
	if (!b) b = &Gather(sim, i);
	if (b->lastTick != sim->currentTick)
	{
		b->lastTick = sim->currentTick;
		Step(sim, p.tmp, *b);
	}
	return 0;
}
