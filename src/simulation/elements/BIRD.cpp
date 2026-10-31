#include "simulation/ElementCommon.h"
#include "Boids.h"

static int update(UPDATE_FUNC_ARGS);

void Element::Element_BIRD()
{
	Identifier = "DEFAULT_PT_BIRD";
	Name = "BIRD";
	Colour = 0xC8B49A_rgb;
	MenuVisible = 1;
	MenuSection = SC_SPECIAL;
	Enabled = 1;

	Advection = 0.2f; // wind carries them a little
	AirDrag = 0.00f * CFDS;
	AirLoss = 0.99f;
	Loss = 1.00f;
	Collision = -0.5f;
	Gravity = 0.0f;
	Diffusion = 0.00f;
	HotAir = 0.000f	* CFDS;
	Falldown = 0;

	Flammable = 10;
	Explosive = 0;
	Meltable = 0;
	Hardness = 20;

	Weight = 20;

	HeatConduct = 20;
	Description = "Birds. Fly in flocks and steer around walls and windows.";

	Properties = TYPE_PART | PROP_NEUTPASS;

	LowPressure = IPL;
	LowPressureTransition = NT;
	HighPressure = IPH;
	HighPressureTransition = NT;
	LowTemperature = ITL;
	LowTemperatureTransition = NT;
	HighTemperature = 373.15f;
	HighTemperatureTransition = PT_DUST;

	Update = &update;
}

static int update(UPDATE_FUNC_ARGS)
{
	Flock(sim, parts[i], i, x, y, 1.5f, 1, [sim](int fx, int fy) {
		unsigned r = 0;
		return sim->eval_move(PT_BIRD, fx, fy, &r) || TYP(r) == PT_BIRD;
	});
	return 0;
}
