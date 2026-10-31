#include "simulation/ElementCommon.h"
#include "Boids.h"

static int update(UPDATE_FUNC_ARGS);
static void create(ELEMENT_CREATE_FUNC_ARGS);

void Element::Element_FISH()
{
	Identifier = "DEFAULT_PT_FISH";
	Name = "FISH";
	Colour = 0xFF8C1A_rgb;
	MenuVisible = 1;
	MenuSection = SC_SPECIAL;
	Enabled = 1;

	Advection = 0.3f;
	AirDrag = 0.00f * CFDS;
	AirLoss = 0.98f;
	Loss = 0.95f;
	Collision = -0.1f;
	Gravity = 0.1f;
	Diffusion = 0.00f;
	HotAir = 0.000f	* CFDS;
	Falldown = 1;

	Flammable = 0;
	Explosive = 0;
	Meltable = 0;
	Hardness = 20;

	Weight = 36; // heavier than water so it can swim through it

	HeatConduct = 20;
	Description = "Fish. Swim in schools in any liquid; flop and die after a while out of it.";

	Properties = TYPE_PART | PROP_NEUTPASS;

	LowPressure = IPL;
	LowPressureTransition = NT;
	HighPressure = IPH;
	HighPressureTransition = NT;
	LowTemperature = ITL;
	LowTemperatureTransition = NT;
	HighTemperature = 343.15f;
	HighTemperatureTransition = PT_DUST;

	Update = &update;
	Create = &create;
}

static void create(ELEMENT_CREATE_FUNC_ARGS)
{
	static const uint32_t kinds[] = { 0xFFFF8C1A, 0xFFFFD21A, 0xFFC0C8D0, 0xFF3A8FE0, 0xFFE04A4A, 0xFF9AE05A };
	sim->parts[i].dcolour = kinds[sim->rng.between(0, 5)];
	sim->parts[i].tmp = sim->rng.chance(1, 5); // a loner
}

// life: ticks spent out of liquid; tmp 1: a loner
static int update(UPDATE_FUNC_ARGS)
{
	auto &elements = SimulationData::CRef().elements;
	auto wet = [sim, &elements](int fx, int fy) {
		if (fx < 0 || fy < 0 || fx >= XRES || fy >= YRES || sim->PixelWall(fx, fy)) return false;
		int r = sim->pmap[fy][fx];
		return r && (TYP(r) == PT_FISH || (elements[TYP(r)].Properties & TYPE_LIQUID));
	};
	if (wet(x, y - 1) || wet(x, y + 1) || wet(x - 1, y) || wet(x + 1, y))
	{
		parts[i].life = 0;
		Flock(sim, parts[i], i, x, y, 0.7f, parts[i].tmp ? 0 : 0.35f, wet);
		parts[i].vy -= elements[PT_FISH].Gravity; // swimming, not sinking
		return 0;
	}
	if (++parts[i].life > 900)
	{
		sim->kill_part(i);
		return 1;
	}
	if (sim->rng.chance(1, 40)) // flop
	{
		parts[i].vx = (sim->rng.uniform01() - 0.5f) * 3;
		parts[i].vy = -1.5f;
	}
	return 0;
}
