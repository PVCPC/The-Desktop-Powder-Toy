#include "simulation/ElementCommon.h"
#include "STKM.h"

static int update(UPDATE_FUNC_ARGS);
static bool createAllowed(ELEMENT_CREATE_ALLOWED_FUNC_ARGS);
static void changeType(ELEMENT_CHANGETYPE_FUNC_ARGS);
static void Free(Simulation *sim, unsigned char i);

void Element::Element_FIGH()
{
	Identifier = "DEFAULT_PT_FIGH";
	Name = "FIGH";
	Colour = 0xFFE0A0_rgb;
	MenuVisible = 1;
	MenuSection = SC_SPECIAL;
	Enabled = 1;

	Advection = 0.5f;
	AirDrag = 0.00f * CFDS;
	AirLoss = 0.2f;
	Loss = 1.0f;
	Collision = 0.0f;
	Gravity = 0.0f;
	NewtonianGravity = 0.0f;
	Diffusion = 0.0f;
	HotAir = 0.00f	* CFDS;
	Falldown = 0;

	Flammable = 0;
	Explosive = 0;
	Meltable = 0;
	Hardness = 0;

	Weight = 50;

	DefaultProperties.temp = R_TEMP + 14.6f + 273.15f;
	HeatConduct = 0;
	Description = "Fighter. Tries to kill stickmen. You must first give it an element to kill him with.";

	Properties = PROP_NOCTYPEDRAW;
	CarriesTypeIn = 1U << FIELD_CTYPE;

	LowPressure = IPL;
	LowPressureTransition = NT;
	HighPressure = IPH;
	HighPressureTransition = NT;
	LowTemperature = ITL;
	LowTemperatureTransition = NT;
	HighTemperature = 620.0f;
	HighTemperatureTransition = PT_FIRE;

	DefaultProperties.life = 100;

	Update = &update;
	Graphics = &Element_STKM_graphics;
	CreateAllowed = &createAllowed;
	ChangeType = &changeType;
}

// Desktop pets: with no stickman to chase, a fighter strolls, stops to look round, hops up steps and mostly turns
// back at a drop (now and then it jumps off anyway).
static int PetComm(Simulation *sim, playerst *figh, int f)
{
	static int dir[MAX_FIGHTERS], left[MAX_FIGHTERS], flee[MAX_FIGHTERS], shy[MAX_FIGHTERS];
	if (--left[f] <= 0)
	{
		dir[f] = sim->rng.chance(1, 3) ? 0 : (sim->rng.chance(1, 2) ? 1 : 2);
		left[f] = sim->rng.between(40, 240);
	}
	// fire, lava or anything hot nearby: run the other way, off ledges if need be
	flee[f]--;
	int cx = int(figh->legs[4] + figh->legs[12]) / 2, cy = int(figh->legs[5]) - 6;
	for (int k = 0; k < 8; k++)
	{
		int sx = cx + sim->rng.between(-24, 24), sy = cy + sim->rng.between(-16, 10);
		if (sx < 0 || sy < 0 || sx >= XRES || sy >= YRES) continue;
		int r = sim->pmap[sy][sx] ? sim->pmap[sy][sx] : sim->photons[sy][sx];
		if (r && TYP(r) != PT_FIGH && (TYP(r) == PT_FIRE || TYP(r) == PT_LAVA || TYP(r) == PT_PLSM || sim->parts[ID(r)].temp > 423.15f))
		{
			dir[f] = sx < cx ? 2 : 1;
			left[f] = flee[f] = 60;
			break;
		}
	}
	// the mouse pointer: a quick one close by startles them away, a slow one they walk over to, under a still one they
	// stop; and they hop when the user comes back
	auto &m = sim->petPointer;
	if (--shy[f] <= 0 && flee[f] <= 0 && m.x >= 0 && std::abs(m.x - cx) < 80 && m.y > cy - 60 && m.y < cy + 20)
	{
		left[f] = 30;
		if (m.speed > 6) dir[f] = m.x < cx ? 2 : 1, left[f] = shy[f] = 90;
		else dir[f] = std::abs(m.x - cx) < 10 ? 0 : (m.x < cx ? 1 : 2);
	}
	// a firefly (the overlay's evening BIRD, tmp 1) close by: run after it and jump at it when under it
	else if (flee[f] <= 0)
	{
		static int fireflyX[MAX_FIGHTERS], fireflyY[MAX_FIGHTERS], seen[MAX_FIGHTERS];
		if ((sim->currentTick + f) % 20 == 0)
		{
			seen[f] = 0;
			int best = 1 << 30;
			for (int y = std::max(0, cy - 50); y < std::min(YRES, cy + 10); y++)
				for (int x = std::max(0, cx - 80); x < std::min(XRES, cx + 80); x++)
					if (int r = sim->pmap[y][x]; r && TYP(r) == PT_BIRD && sim->parts[ID(r)].tmp == 1 && std::abs(x - cx) < best)
						best = std::abs(x - cx), fireflyX[f] = x, fireflyY[f] = y, seen[f] = 1;
		}
		if (seen[f])
		{
			left[f] = 20;
			dir[f] = std::abs(fireflyX[f] - cx) < 4 ? 0 : (fireflyX[f] < cx ? 1 : 2);
			if (!dir[f] && fireflyY[f] < cy - 4 && sim->rng.chance(1, 8)) return 0x04;
		}
		// fetch: run after a ball (not one already by the pointer); BALL kicks it back toward the pointer
		static int ballX[MAX_FIGHTERS], hasBall[MAX_FIGHTERS];
		if (!seen[f] && sim->petFetchRange >= 1)
		{
			if ((sim->currentTick + f) % 20 == 10)
			{
				hasBall[f] = 0;
				int range = int(sim->petFetchRange), best = 1 << 30;
				for (int y = std::max(0, cy - 30); y < std::min(YRES, cy + 8); y++)
					for (int x = std::max(0, cx - range); x < std::min(XRES, cx + range); x++)
						if (int r = sim->pmap[y][x]; r && TYP(r) == PT_BALL && std::abs(x - cx) < best && !(m.x >= 0 && std::abs(x - m.x) < 30))
							best = std::abs(x - cx), ballX[f] = x, hasBall[f] = 1;
			}
			if (hasBall[f])
			{
				left[f] = 20;
				dir[f] = std::abs(ballX[f] - cx) < 3 ? 0 : (ballX[f] < cx ? 1 : 2);
			}
		}
	}
	if (sim->petNap && flee[f] <= 0)
	{
		return 0;
	}
	// standing in a hole broken in a window: stop and look out of it a few seconds (then not again for a while)
	static int peek[MAX_FIGHTERS], peekCool[MAX_FIGHTERS];
	if (sim->petHoles && flee[f] <= 0)
	{
		if (--peekCool[f] <= 0 && sim->HoleAt(cx, cy)) peek[f] = sim->rng.between(120, 300), peekCool[f] = 900;
		if (peek[f] > 0 && peek[f]--) return 0;
	}
	if (sim->petGather && flee[f] <= 0)
	{
		left[f] = 30;
		auto play = sim->PlayRect();
		int mid = play.pos.X + play.size.X / 2;
		dir[f] = std::abs(cx - mid) < 30 ? 0 : (cx > mid ? 1 : 2);
	}
	if (sim->petGreet > sim->currentTick)
	{
		return (sim->currentTick + f * 7) % 30 < 4 ? 0x04 : 0;
	}
	if (!dir[f])
	{
		return 0;
	}
	bool goLeft = dir[f] == 1;
	int fx = goLeft ? int(figh->legs[4]) - 6 : int(figh->legs[12]) + 6, fy = int(goLeft ? figh->legs[5] : figh->legs[13]);
	if (sim->eval_move(PT_FIGH, fx, fy + 6, nullptr) && sim->eval_move(PT_FIGH, fx, fy + 14, nullptr) && flee[f] <= 0 && !sim->rng.chance(1, 30) && !(sim->petHoles && sim->HoleAt(fx, fy + 6) && sim->rng.chance(1, 3)))
	{
		dir[f] = goLeft ? 2 : 1;
		return 0;
	}
	int comm = goLeft ? 0x01 : 0x02;
	if (!sim->eval_move(PT_FIGH, fx, fy - 1, nullptr) || !sim->eval_move(PT_FIGH, fx, fy - 4, nullptr))
	{
		comm |= 0x04;
	}
	return comm;
}

static int update(UPDATE_FUNC_ARGS)
{
	auto &sd = SimulationData::CRef();
	auto &elements = sd.elements;
	if (parts[i].tmp < 0 || parts[i].tmp >= MAX_FIGHTERS)
	{
		sim->kill_part(i);
		return 1;
	}
	playerst* figh = &sim->fighters[(unsigned char)parts[i].tmp];

	int tarx = 0, tary = 0;

	parts[i].tmp2 = 0; //0 - stay in place, 1 - seek a stick man

	//Set target cords
	if (sim->player2.spwn)
	{
		if (sim->player.spwn && (pow((float)sim->player.legs[2]-x, 2) + pow((float)sim->player.legs[3]-y, 2))<=
		   (pow((float)sim->player2.legs[2]-x, 2) + pow((float)sim->player2.legs[3]-y, 2)))
		{
			tarx = (int)sim->player.legs[2];
			tary = (int)sim->player.legs[3];
		}
		else
		{
			tarx = (int)sim->player2.legs[2];
			tary = (int)sim->player2.legs[3];
		}
		parts[i].tmp2 = 1;
	}
	else if (sim->player.spwn)
	{
		tarx = (int)sim->player.legs[2];
		tary = (int)sim->player.legs[3];
		parts[i].tmp2 = 1;
	}

	switch (parts[i].tmp2)
	{
	case 1:
		if ((pow(float(tarx-x), 2) + pow(float(tary-y), 2))<600)
		{
			if (figh->elem == PT_LIGH || figh->elem == PT_NEUT
			    || elements[figh->elem].Properties & (PROP_DEADLY | PROP_RADIOACTIVE)
			    || elements[figh->elem].DefaultProperties.temp >= 323 || elements[figh->elem].DefaultProperties.temp <= 243)
				figh->comm = (int)figh->comm | 0x08;
		}
		else if (tarx<x)
		{
			if(figh->rocketBoots || !(sim->eval_move(PT_FIGH, int(figh->legs[4])-10, int(figh->legs[5])+6, nullptr)
			     && sim->eval_move(PT_FIGH, int(figh->legs[4])-10, int(figh->legs[5])+3, nullptr)))
				figh->comm = 0x01;
			else
				figh->comm = 0x02;

			if (figh->rocketBoots)
			{
				if (tary<y)
					figh->comm = (int)figh->comm | 0x04;
			}
			else if (!sim->eval_move(PT_FIGH, int(figh->legs[4])-4, int(figh->legs[5])-1, nullptr)
			    || !sim->eval_move(PT_FIGH, int(figh->legs[12])-4, int(figh->legs[13])-1, nullptr)
			    || sim->eval_move(PT_FIGH, 2*int(figh->legs[4])-int(figh->legs[6]), int(figh->legs[5])+5, nullptr))
				figh->comm = (int)figh->comm | 0x04;
		}
		else
		{
			if (figh->rocketBoots || !(sim->eval_move(PT_FIGH, int(figh->legs[12])+10, int(figh->legs[13])+6, nullptr)
			      && sim->eval_move(PT_FIGH, int(figh->legs[12])+10, int(figh->legs[13])+3, nullptr)))
				figh->comm = 0x02;
			else
				figh->comm = 0x01;

			if (figh->rocketBoots)
			{
				if (tary<y)
					figh->comm = (int)figh->comm | 0x04;
			}
			else if (!sim->eval_move(PT_FIGH, int(figh->legs[4])+4, int(figh->legs[5])-1, nullptr)
			    || !sim->eval_move(PT_FIGH, int(figh->legs[4])+4, int(figh->legs[5])-1, nullptr)
			    || sim->eval_move(PT_FIGH, 2*int(figh->legs[12])-int(figh->legs[14]), int(figh->legs[13])+5, nullptr))
				figh->comm = (int)figh->comm | 0x04;
		}
		break;
	default:
		figh->comm = sim->petWander ? PetComm(sim, figh, (unsigned char)parts[i].tmp) : 0;
		break;
	}

	figh->pcomm = figh->comm;

	Element_STKM_run_stickman(figh, UPDATE_FUNC_SUBCALL_ARGS);
	return 0;
}

static bool createAllowed(ELEMENT_CREATE_ALLOWED_FUNC_ARGS)
{
	return Element_FIGH_CanAlloc(sim);
}

static void changeType(ELEMENT_CHANGETYPE_FUNC_ARGS)
{
	if (to == PT_FIGH)
	{
		sim->parts[i].tmp = Element_FIGH_Alloc(sim);
		if (sim->parts[i].tmp >= 0)
			Element_FIGH_NewFighter(sim, sim->parts[i].tmp, i, PT_DUST);
	}
	else
	{
		Free(sim, (unsigned char)sim->parts[i].tmp);
	}
}

bool Element_FIGH_CanAlloc(Simulation *sim)
{
	return sim->fighcount < MAX_FIGHTERS;
}

int Element_FIGH_Alloc(Simulation *sim)
{
	if (sim->fighcount >= MAX_FIGHTERS)
		return -1;
	int i = 0;
	while (i < MAX_FIGHTERS && sim->fighters[i].spwn==1)
		i++;
	if (i < MAX_FIGHTERS)
	{
		sim->fighters[i].spwn = 1;
		sim->fighters[i].elem = PT_DUST;
		sim->fighcount++;
		return i;
	}
	else return -1;
}

static void Free(Simulation *sim, unsigned char i)
{
	if (sim->fighters[i].spwn)
	{
		sim->fighters[i].spwn = 0;
		sim->fighcount--;
	}
}

void Element_FIGH_NewFighter(Simulation *sim, int fighterID, int i, int elem)
{
	Element_STKM_init_legs(sim, &sim->fighters[fighterID], i);
	if (elem > 0 && elem < PT_NUM)
		sim->fighters[fighterID].elem = elem;
	sim->fighters[fighterID].spwn = 1;
}
