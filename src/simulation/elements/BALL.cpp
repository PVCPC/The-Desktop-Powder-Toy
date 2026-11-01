#include "simulation/ElementCommon.h"
#include <cmath>
#include <map>

static int update(UPDATE_FUNC_ARGS);
static void create(ELEMENT_CREATE_FUNC_ARGS);

void Element::Element_BALL()
{
	Identifier = "DEFAULT_PT_BALL";
	Name = "BALL";
	Colour = 0xE04A4A_rgb;
	MenuVisible = 1;
	MenuSection = SC_SPECIAL;
	Enabled = 1;

	// the pixels never move on their own: the ball moves them all together (update)
	Advection = 0.0f;
	AirDrag = 0.00f * CFDS;
	AirLoss = 0.90f;
	Loss = 0.00f;
	Collision = 0.0f;
	Gravity = 0.0f;
	Diffusion = 0.00f;
	HotAir = 0.000f	* CFDS;
	Falldown = 0;

	Flammable = 0;
	Explosive = 0;
	Meltable = 0;
	Hardness = 5;

	Weight = 100;

	HeatConduct = 40;
	Description = "Ball. Each click makes one ball the shape and size of the brush. Bounces, rolls; wind and explosions push it.";

	Properties = TYPE_SOLID;

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

// Fields: tmp = which ball (0 = just painted, not in one yet), ctype = the ball's colour. The rest lives here,
// rebuilt from the pixels when a save loads.
namespace
{
	struct Ball
	{
		float cx, cy, vx = 0, vy = 0, angle = 0, radius = 1;
		std::vector<std::pair<int, std::pair<int, int>>> members; // particle, offset from the centre
		int lastTick = -1;
	};
	std::map<int, Ball> balls;
	int nextId = 1;
}

static void create(ELEMENT_CREATE_FUNC_ARGS)
{
	sim->parts[i].tmp = 0;
}

static bool Mine(Simulation *sim, int r, int id)
{
	return TYP(r) == PT_BALL && sim->parts[ID(r)].tmp == id;
}

// Makes a ball of every BALL pixel touching pixel i with the same tmp: what one brush click painted (tmp 0, given a
// new id and colour here), or a ball from a loaded save
static Ball &Gather(Simulation *sim, int i)
{
	static const int colours[] = { 0xE04A4A, 0x3A8FE0, 0xFFD21A, 0x5AC85A, 0xFF8C1A, 0xB45AE0 };
	int tmp = sim->parts[i].tmp, id = tmp ? tmp : nextId++, colour = tmp ? sim->parts[i].ctype : colours[sim->rng.between(0, 5)];
	std::vector<std::pair<int, std::pair<int, int>>> found;
	std::vector<std::pair<int, int>> todo{ { int(sim->parts[i].x + 0.5f), int(sim->parts[i].y + 0.5f) } };
	sim->parts[i].tmp = -1; // marks visited until the end
	found.push_back({ i, todo[0] });
	float sx = 0, sy = 0;
	while (!todo.empty())
	{
		auto [x, y] = todo.back();
		todo.pop_back();
		for (int dy = -1; dy <= 1; dy++)
			for (int dx = -1; dx <= 1; dx++)
			{
				int nx = x + dx, ny = y + dy;
				if (nx < 0 || ny < 0 || nx >= XRES || ny >= YRES) continue;
				if (int r = sim->pmap[ny][nx]; r && Mine(sim, r, tmp))
				{
					sim->parts[ID(r)].tmp = -1;
					found.push_back({ ID(r), { nx, ny } });
					todo.push_back({ nx, ny });
				}
			}
	}
	for (auto &[j, at] : found)
	{
		sim->parts[j].tmp = id, sim->parts[j].ctype = colour;
		sx += at.first, sy += at.second;
	}
	auto &b = balls[id] = Ball{};
	b.cx = std::round(sx / found.size()), b.cy = std::round(sy / found.size());
	b.radius = std::max(1.f, std::sqrt(found.size() / 3.14159f));
	for (auto &[j, at] : found)
		b.members.push_back({ j, { at.first - int(b.cx), at.second - int(b.cy) } });
	nextId = std::max(nextId, id + 1);
	return b;
}

static void Step(Simulation *sim, int id, Ball &b)
{
	// gravity, a little drag, and the air (wind, explosions) pushing it
	int ccx = std::clamp(int(b.cx) / CELL, 0, XCELLS - 1), ccy = std::clamp(int(b.cy) / CELL, 0, YCELLS - 1);
	b.vy += 0.12f;
	b.vx += (sim->vx[ccy][ccx] - b.vx) * 0.04f;
	b.vy += (sim->vy[ccy][ccx] - b.vy) * 0.04f;
	b.vx *= 0.998f, b.vy *= 0.998f;
	if (auto &g = sim->grab; g.on && g.type == PT_BALL && g.body == id) // held: chases the pointer, keeps that speed when let go
	{
		b.vx = std::clamp((g.x - b.cx) * 0.35f, -12.f, 12.f);
		b.vy = std::clamp((g.y - b.cy) * 0.35f, -12.f, 12.f);
	}

	int other = 0; // another ball in the way, if that's what it hit
	auto blocked = [&](int X, int Y, float &nx, float &ny) {
		int n = 0;
		nx = ny = 0;
		other = 0;
		for (auto &[j, o] : b.members)
		{
			int x = X + o.first, y = Y + o.second;
			bool hit = x < 0 || y < 0 || x >= XRES || y >= YRES || sim->PixelWall(x, y) || sim->IsWallBlocking(x, y, PT_BALL);
			if (!hit)
				if (int r = sim->pmap[y][x]; r && !Mine(sim, r, id))
				{
					hit = !(SimulationData::CRef().elements[TYP(r)].Properties & TYPE_GAS);
					if (TYP(r) == PT_BALL) other = sim->parts[ID(r)].tmp;
				}
			if (hit) nx -= o.first, ny -= o.second, n++;
		}
		float len = std::hypot(nx, ny);
		if (n && len > 0) nx /= len, ny /= len;
		else if (n) nx = 0, ny = -1;
		return n > 0;
	};

	int X = int(b.cx), Y = int(b.cy);
	float nx, ny;
	if (blocked(X, Y, nx, ny)) // something moved into it (a window): get pushed out
	{
		b.cx += nx, b.cy += ny;
		b.vx += nx * 0.5f, b.vy += ny * 0.5f;
	}
	else
	{
		int steps = std::max(1, int(std::ceil(std::max(std::abs(b.vx), std::abs(b.vy)))));
		for (int s = 0; s < steps; s++)
		{
			float tx = b.cx + b.vx / steps, ty = b.cy + b.vy / steps;
			if (!blocked(int(std::lround(tx)), int(std::lround(ty)), nx, ny))
			{
				b.cx = tx, b.cy = ty;
				continue;
			}
			// another ball: knock it, both bounce by their sizes (pixel counts as mass)
			auto hitBall = other ? balls.find(other) : balls.end();
			if (hitBall != balls.end())
			{
				auto &o = hitBall->second;
				float rel = (b.vx - o.vx) * nx + (b.vy - o.vy) * ny;
				if (rel < 0)
				{
					float ma = float(b.members.size()), mb = float(o.members.size());
					float j = -1.8f * rel / (1 / ma + 1 / mb);
					b.vx += j / ma * nx, b.vy += j / ma * ny;
					o.vx -= j / mb * nx, o.vy -= j / mb * ny;
				}
			}
			else if (float vn = b.vx * nx + b.vy * ny; vn < 0) // bounce off the surface; small knocks just settle
			{
				float e = std::abs(vn) > 1 ? 0.7f : 0;
				b.vx -= (1 + e) * vn * nx, b.vy -= (1 + e) * vn * ny;
			}
			// keep most of the sliding; rolling turns it
			float vt = b.vx * -ny + b.vy * nx;
			b.vx -= vt * 0.01f * -ny, b.vy -= vt * 0.01f * nx; // rolling friction
			b.angle += vt / b.radius;
			// then slide along the surface with what's left (rolling down a slope)
			tx = b.cx + b.vx / steps, ty = b.cy + b.vy / steps;
			float sx, sy;
			for (int lift = 0; lift <= 1; lift++) // a pixel slope is steps: hop up one when the next is in the way
				if (!blocked(int(std::lround(tx)), int(std::lround(ty)) - lift, sx, sy))
				{
					b.cx = tx, b.cy = ty - lift;
					break;
				}
			break;
		}
	}

	// put every pixel at the ball's new place, shaded by the spin so it visibly rolls
	X = int(std::lround(b.cx)), Y = int(std::lround(b.cy));
	for (auto &[j, o] : b.members)
	{
		auto &p = sim->parts[j];
		int px = int(p.x + 0.5f), py = int(p.y + 0.5f);
		if (px >= 0 && py >= 0 && px < XRES && py < YRES && sim->pmap[py][px] == PMAP(j, PT_BALL)) sim->pmap[py][px] = 0;
	}
	float c = std::cos(b.angle), s = std::sin(b.angle);
	for (auto &[j, o] : b.members)
	{
		int x = X + o.first, y = Y + o.second;
		auto &p = sim->parts[j];
		if (x < 0 || y < 0 || x >= XRES || y >= YRES) continue;
		if (int r = sim->pmap[y][x]; r && !Mine(sim, r, id)) sim->kill_part(ID(r)); // a gas in the way
		p.x = float(x), p.y = float(y), p.vx = p.vy = 0;
		sim->pmap[y][x] = PMAP(j, PT_BALL);
		bool light = o.first * c + o.second * s > 0;
		int col = p.ctype;
		if (!light) col = ((col >> 16 & 0xFF) * 3 / 5) << 16 | ((col >> 8 & 0xFF) * 3 / 5) << 8 | (col & 0xFF) * 3 / 5;
		p.dcolour = 0xFF000000 | col;
	}
}

static int update(UPDATE_FUNC_ARGS)
{
	auto &p = parts[i];
	auto it = p.tmp ? balls.find(p.tmp) : balls.end();
	Ball *b = it == balls.end() ? nullptr : &it->second;
	if (b)
	{
		// drop pixels that burned or were eaten; a stale record (cleared sim, loaded save) is rebuilt
		std::erase_if(b->members, [sim, &p](auto &m) { return sim->parts[m.first].type != PT_BALL || sim->parts[m.first].tmp != p.tmp; });
		if (std::none_of(b->members.begin(), b->members.end(), [i](auto &m) { return m.first == i; })) b = nullptr;
	}
	if (!b)
		b = &Gather(sim, i);
	if (b->lastTick != sim->currentTick)
	{
		b->lastTick = sim->currentTick;
		Step(sim, p.tmp, *b);
	}
	return 0;
}
