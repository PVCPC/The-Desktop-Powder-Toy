#include "simulation/ElementCommon.h"

static int update(UPDATE_FUNC_ARGS);

void Element::Element_BLOD()
{
	Identifier = "DEFAULT_PT_BLOD";
	Name = "BLOD";
	Colour = 0x8A0A0A_rgb;
	MenuVisible = 1;
	MenuSection = SC_LIQUID;
	Enabled = 1;

	// thicker than water: slower to flow and to be carried by air
	Advection = 0.3f;
	AirDrag = 0.01f * CFDS;
	AirLoss = 0.95f;
	Loss = 0.9f;
	Collision = 0.0f;
	Gravity = 0.1f;
	Diffusion = 0.00f;
	HotAir = 0.000f	* CFDS;
	Falldown = 2;

	Flammable = 0;
	Explosive = 0;
	Meltable = 0;
	Hardness = 20;

	Weight = 32;

	DefaultProperties.temp = 310.15f; // body temperature
	HeatConduct = 29;
	Description = "Blood. Thick: stains what it touches, soaks into powders and sponges, clouds water, feeds plants.";

	Properties = TYPE_LIQUID | PROP_CONDUCTS | PROP_LIFE_DEC | PROP_NEUTPASS | PROP_PHOTPASS;

	LowPressure = IPL;
	LowPressureTransition = NT;
	HighPressure = IPH;
	HighPressureTransition = NT;
	LowTemperature = 272.6f;
	LowTemperatureTransition = PT_ICEI; //@ BLOD -> ICEI (melts back to BLOD)
	HighTemperature = 373.0f;
	HighTemperatureTransition = PT_WTRV; //@ BLOD -> WTRV

	Update = &update;
}

// tints a particle toward blood red (its decoration colour, over its own): k of 255 a touch, up to mostly red
static void stain(Particle &p, int k)
{
	int a = p.dcolour >> 24, r = (p.dcolour >> 16) & 0xFF, g = (p.dcolour >> 8) & 0xFF, b = p.dcolour & 0xFF;
	if (!a) r = 0x6A, g = 0x08, b = 0x08;
	r += (0x6A - r) * k / 255, g += (0x08 - g) * k / 255, b += (0x08 - b) * k / 255;
	a = std::min(0xC0, a + k);
	p.dcolour = (unsigned(a) << 24) | (r << 16) | (g << 8) | b;
}

static int update(UPDATE_FUNC_ARGS)
{
	auto &elements = SimulationData::CRef().elements;
	for (auto rx = -1; rx <= 1; rx++)
		for (auto ry = -1; ry <= 1; ry++)
		{
			if (!rx && !ry) continue;
			auto r = pmap[y + ry][x + rx];
			if (!r) continue;
			auto t = TYP(r);
			auto &q = parts[ID(r)];
			if (t == PT_BLOD) continue;
			if (t == PT_WATR || t == PT_DSTW || t == PT_SLTW || t == PT_CBNW)
			{
				//@ BLOD + water: clouds it red, and thins into it
				if (sim->rng.chance(1, 8)) stain(q, 40);
				if (sim->rng.chance(1, 120))
				{
					sim->part_change_type(i, x, y, t);
					parts[i].dcolour = 0xA0700A0A;
					return 1;
				}
			}
			else if (t == PT_SPNG)
			{
				//@ BLOD + SPNG: soaked up (squeezes out as water)
				stain(q, 60);
				q.life++;
				sim->kill_part(i);
				return 1;
			}
			else if (t == PT_PLNT || t == PT_VINE)
			{
				//@ BLOD + PLNT: feeds it (grows into it)
				if (sim->rng.chance(1, 60))
				{
					sim->part_change_type(i, x, y, PT_PLNT);
					parts[i].dcolour = 0;
					return 1;
				}
			}
			else if (elements[t].Properties & TYPE_PART)
			{
				//@ BLOD + powders: stains them and soaks in
				if (sim->rng.chance(1, 3)) stain(q, 30);
				if (sim->rng.chance(1, 40))
				{
					stain(q, 80);
					sim->kill_part(i);
					return 1;
				}
			}
			else if (elements[t].Properties & TYPE_SOLID && sim->rng.chance(1, 10))
				stain(q, 15); //@ BLOD + solids: stains them
		}
	return 0;
}
