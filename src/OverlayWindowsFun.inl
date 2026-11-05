// Part of OverlayWindows.cpp (included by OverlayWindows.cpp): the Fun tab's effects, once per sim tick (pets, weather, music...)

// The Fun tab's pets, weather and music, once per sim tick (not while paused)
// A cold spark (EMBR at room temperature: it sets nothing alight) in a glitter colour, drifting up
static void ColdSpark(Simulation *sim, int x, int y)
{
	if (x < CELL || y < CELL || x >= XRES - CELL || y >= YRES - CELL || sim->pmap[y][x] || sim->PixelWall(x, y)) return;
	if (int i = sim->create_part(-1, x, y, PT_EMBR); i >= 0)
	{
		auto &p = sim->parts[i];
		p.temp = 295.15f, p.life = sim->rng.between(20, 40);
		p.vx = (sim->rng.uniform01() - 0.5f) * 1.5f, p.vy = -sim->rng.uniform01();
		static const uint32_t glitter[] = { 0xFFFFF0A0, 0xFFA0E0FF, 0xFFFFA0E0, 0xFFC0FFA0 };
		p.dcolour = glitter[sim->rng.between(0, 3)];
	}
}

static void TickFun(Simulation *sim)
{
	static int last = -1;
	if (sim->currentTick == last) return;
	last = sim->currentTick;
	auto &elements = SimulationData::CRef().elements;

	// Pets: fighters that wander (FIGH's PetComm), dropped in from the top every two seconds while there are too few
	sim->petWander = fun.pets >= 1;
	static std::vector<int> pets;
	std::erase_if(pets, [sim](int i) { return sim->parts[i].type != PT_FIGH; });
	while (int(pets.size()) > int(fun.pets))
	{
		sim->kill_part(pets.back());
		pets.pop_back();
	}
	if (sim->currentTick % 120 == 0 && int(pets.size()) < int(fun.pets))
	{
		int i = sim->create_part(-1, sim->rng.between(XRES / 10, XRES * 9 / 10), 8, PT_FIGH);
		if (i >= 0) pets.push_back(i);
	}

	// Weather from the top edge; each flake, drop or leaf goes after a while (snow even once melted)
	static std::deque<std::tuple<int, int, int>> fallen; // particle, type, the tick it goes
	static int rainLast = -1, rainSince = -1; // the last tick rain fell, and since when it has been falling
	LASTINPUTINFO input{ sizeof(input) };
	// back after a minute or more away: the pets hop to greet you
	static bool away = false;
	if (GetLastInputInfo(&input))
	{
		DWORD idleMs = GetTickCount() - input.dwTime;
		if (idleMs >= 60000) away = true;
		else if (away && idleMs < 1000) away = false, sim->petGreet = sim->currentTick + int(fun.welcome * 60);
		sim->petGather = fun.gather >= 1 && idleMs >= DWORD(fun.gather * 60000);
	}
	// Day and night, by the clock: fireflies from 19:00 to 05:00 (and darker leaves), a flock of birds now and then
	// from 06:00 to 10:00
	int hour;
	{
		time_t now = time(nullptr);
		tm local;
		localtime_s(&local, &now);
		hour = local.tm_hour;
	}
	bool dusk = fun.dayNight && (hour >= 19 || hour < 5);
	// Pets nap: in the nap hours, once the mouse and keys have been still 10 s
	{
		int from = int(fun.napFrom) % 24, until = int(fun.napUntil);
		bool napHours = fun.napFrom >= 1 && (from < until ? hour >= from && hour < until : hour >= from || hour < until);
		LASTINPUTINFO still{ sizeof(still) };
		sim->petNap = napHours && GetLastInputInfo(&still) && GetTickCount() - still.dwTime >= 10000;
	}
	static std::vector<std::pair<int, int>> visitors; // particle, the tick it goes (fireflies: never, while it's dark)
	std::erase_if(visitors, [&](auto &v) {
		auto &p = sim->parts[v.first];
		bool firefly = v.second < 0;
		if (p.type != PT_BIRD || (p.tmp == 1) != firefly) return true; // gone, or the slot reused
		if ((firefly && !dusk) || (!firefly && sim->currentTick >= v.second)) return sim->kill_part(v.first), true;
		if (firefly) p.dcolour = (sim->currentTick + v.first * 37) % 160 < 50 ? 0xFFFFF070 : 0xFF404020; // blink
		return false;
	});
	auto open = [sim](int x, int y) { // on screen, not in a window or on anything
		return x >= CELL && y >= CELL && x < XRES - CELL && y < YRES - CELL && !sim->pmap[y][x] && !sim->PixelWall(x, y);
	};
	if (dusk && visitors.size() < 12 && sim->rng.chance(1, 60))
	{
		int x = sim->rng.between(XRES / 10, XRES * 9 / 10), y = YRES - sim->rng.between(40, 160);
		if (open(x, y))
			if (int i = sim->create_part(-1, x, y, PT_BIRD); i >= 0) sim->parts[i].tmp = 1, visitors.push_back({ i, -1 });
	}
	if (fun.dayNight && hour >= 6 && hour < 10 && sim->rng.chance(1, 7200))
	{
		// somewhere open in the top half (windows are walls: one spawned inside would be stuck)
		float vx = sim->rng.chance(1, 2) ? 1.5f : -1.5f;
		int x0 = 0, y0 = 0;
		for (int tries = 0, made = 0; tries < 60 && made < 7; tries++)
		{
			if (!made) x0 = sim->rng.between(XRES / 10, XRES * 9 / 10), y0 = sim->rng.between(CELL, std::max(CELL, YRES / 2));
			int x = x0 + sim->rng.between(-10, 10), y = y0 + sim->rng.between(-6, 6);
			if (open(x, y))
				if (int i = sim->create_part(-1, x, y, PT_BIRD); i >= 0)
					sim->parts[i].vx = vx, visitors.push_back({ i, sim->currentTick + 3600 }), made++;
		}
	}
	bool idle = fun.weatherIdle < 1 || (GetLastInputInfo(&input) && GetTickCount() - input.dwTime >= DWORD(fun.weatherIdle * 60000));
	if (fun.weather > 0 && idle)
	{
		int kind = fun.weatherKind;
		if (!kind)
		{
			time_t now = time(nullptr);
			tm local;
			localtime_s(&local, &now);
			int m = local.tm_mon + 1;
			kind = m == 12 || m <= 2 ? 1 : m >= 9 && m <= 11 ? 3 : 2;
			// Weekday weather: Monday rain, Tuesday fog, Wednesday hail, Thursday snow, Friday leaves; weekends by the season
			if (fun.weekday && local.tm_wday >= 1 && local.tm_wday <= 5) kind = std::array{ 2, 5, 4, 1, 3 }[local.tm_wday - 1];
		}
		static const uint32_t autumn[] = { 0xFFC0501A, 0xFFD9822B, 0xFFB8860B, 0xFF8B4513, 0xFFCC3300 };
		float n = fun.weather * XRES / 600.f;
		int count = int(n) + (sim->rng.uniform01() < n - int(n));
		for (int k = 0; k < count; k++)
		{
			int type = kind == 1 || kind == 4 ? PT_SNOW : kind == 2 ? PT_WATR : kind == 5 ? PT_FOG : PT_SAWD;
			if (kind == 5 && k % 4) continue; // fog: fewer, it lingers
			int y0 = kind == 5 ? sim->rng.between(YRES * 2 / 3, YRES - CELL - 1) : CELL; // fog forms low down
			int i = sim->create_part(-1, sim->rng.between(CELL, XRES - CELL - 1), y0, type); // the outer cell kills particles
			if (i < 0) continue;
			auto &p = sim->parts[i];
			if (kind == 1) p.temp = 263.15f, p.vx = sim->rng.uniform01() - 0.5f;
			if (kind == 2) p.vy = 3;
			if (kind == 3) p.dcolour = autumn[sim->rng.between(0, 4)], p.vx = (sim->rng.uniform01() - 0.5f) * 2;
			if (kind == 3 && dusk) p.dcolour = 0xFF000000 | ((p.dcolour >> 1) & 0x7F7F7F); // leaves after dark
			if (kind == 4) p.temp = 253.15f, p.vy = 4 + 2 * sim->rng.uniform01(), p.dcolour = overlayHailColour;
			float life = std::max(10.f, fun.weatherLast) * 60 * (0.6f + 0.8f * sim->rng.uniform01()); // spread: they thin out
			if (kind == 2) life = std::min(life, 300.f); // rain soaks away quickly
			if (kind == 5) life = std::min(life, 1200.f), p.temp = 288.15f, p.life = int(life) + 60; // fog: no rime while it lasts
			fallen.push_back({ i, type, sim->currentTick + int(life) });
		}
		if (kind == 2) rainLast = sim->currentTick;
		if (kind == 2 && sim->rng.uniform01() < fun.weather / 1200) // a storm: now and then a bolt from the top
			sim->create_part(-1, sim->rng.between(XRES / 20, XRES * 19 / 20), CELL, PT_LIGH, 20);
	}
	for (size_t k = 0, n = fallen.size(); k < n; k++) // all of them: their times aren't in order
	{
		auto [i, type, until] = fallen.front();
		fallen.pop_front();
		if (until > sim->currentTick)
		{
			fallen.push_back({ i, type, until });
			continue;
		}
		int t = sim->parts[i].type, x = int(sim->parts[i].x + 0.5f), y = int(sim->parts[i].y + 0.5f);
		if (type == PT_BRCK && t == PT_BRCK) // a sandcastle crumbles back into sand
		{
			sim->part_change_type(i, x, y, PT_SAND);
			continue;
		}
		if (type == PT_WATR && t == PT_WATR && fun.puddles >= 1 && y + 1 < YRES && (sim->pmap[y + 1][x] || sim->PixelWall(x, y + 1)))
		{
			// rain lying on something: a puddle, for a while longer (marked so it isn't kept twice)
			fallen.push_back({ i, PT_WATR | 0x10000, sim->currentTick + int(fun.puddles * 60 * (0.5f + sim->rng.uniform01())) });
			continue;
		}
		type &= 0xFFFF;
		if (t == type || (type == PT_SNOW && t == PT_WATR)) sim->kill_part(i);
	}
	// Holidays, by the date: pumpkins (balls) on Halloween, jelly hearts (JOIN) on Valentine's Day, fireworks in the
	// first hour of New Year's Day
	if (fun.holidays)
	{
		time_t now = time(nullptr);
		tm local;
		localtime_s(&local, &now);
		int month = local.tm_mon + 1, day = local.tm_mday;
		int cx = sim->rng.between(XRES / 10, XRES * 9 / 10);
		if (month == 10 && day == 31 && sim->currentTick % 900 == 0)
		{
			float vx = (sim->rng.chance(1, 2) ? 1 : -1) * (1.5f + sim->rng.uniform01());
			for (int y = -4; y <= 4; y++)
				for (int x = -4; x <= 4; x++)
					if (x * x + y * y <= 18)
						if (int i = sim->create_part(-1, cx + x, 12 + y, PT_BALL); i >= 0) sim->parts[i].ctype = 0xFF7518, sim->parts[i].tmp2 = int(vx * 100);
		}
		if (month == 2 && day == 14 && sim->currentTick % 300 == 0)
		{
			static const char *heart[] = { ".XX.XX.", "XXXXXXX", "XXXXXXX", ".XXXXX.", "..XXX..", "...X..." };
			for (int y = 0; y < 12; y++)
				for (int x = 0; x < 14; x++)
					if (heart[y / 2][x / 2] == 'X')
						if (int i = sim->create_part(-1, cx + x, 10 + y, PT_JOIN); i >= 0) sim->parts[i].dcolour = 0xFFE0305A;
		}
		if (month == 1 && day == 1 && local.tm_hour == 0 && sim->rng.chance(1, 40))
		{
			int y = YRES - 30; // from the first free spot up from the bottom (over the taskbar)
			while (y > YRES / 2 && (sim->pmap[y][cx] || sim->PixelWall(cx, y))) y--;
			if (int i = sim->create_part(-1, cx, y, PT_FIRW); i >= 0)
			{
				auto &p = sim->parts[i];
				p.tmp = 1, p.life = sim->rng.between(20, 29); // lit, as by fire (FIRW.cpp)
				p.vy = -(p.life + 20) * 0.2f, p.vx = sim->rng.uniform01() - 0.5f;
			}
		}
	}
	// Wind: a gust that turns every few seconds; falling leaves flutter and drift with it, snow a little, rain not
	static float gust = 0, gustTo = 0;
	if (sim->currentTick % 240 == 0) gustTo = (sim->rng.uniform01() * 2 - 1) * 1.5f;
	gust += (gustTo - gust) * 0.01f;
	for (auto &[i, type, until] : fallen)
	{
		auto &p = sim->parts[i];
		int x = int(p.x + 0.5f), y = int(p.y + 0.5f);
		if (p.type != type || type == PT_WATR || x < 0 || x >= XRES || y < 0 || y + 1 >= YRES || sim->pmap[y + 1][x] || sim->PixelWall(x, y + 1))
			continue; // gone, rain, or resting
		float k = type == PT_SAWD ? 1 : 0.4f;
		p.vx += (k * (gust + 0.6f * std::sin(sim->currentTick * 0.07f + i)) - p.vx) * 0.08f;
		if (type == PT_SAWD) p.vy = std::min(p.vy, 0.8f + 0.5f * std::sin(sim->currentTick * 0.11f + i * 1.7f)); // flutter
	}

	// Music: the speakers' peak level (Windows' own meter, nothing recorded); a jump over the recent level is a beat
	// that makes resting sand and liquid hop and fire flare
	if (fun.music > 0)
	{
#ifdef _WIN32
		static IAudioMeterInformation *meter = nullptr;
		static int retry = 0;
		if (!meter && --retry <= 0)
		{
			retry = 300;
			CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
			IMMDeviceEnumerator *devices = nullptr;
			IMMDevice *speakers = nullptr;
			if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices))) &&
			    SUCCEEDED(devices->GetDefaultAudioEndpoint(eRender, eConsole, &speakers)))
			{
				speakers->Activate(__uuidof(IAudioMeterInformation), CLSCTX_ALL, nullptr, (void **)&meter);
			}
			if (speakers) speakers->Release();
			if (devices) devices->Release();
		}
		float peak = 0;
		if (meter && FAILED(meter->GetPeakValue(&peak)))
		{
			meter->Release(); // the speakers changed: find the new ones
			meter = nullptr;
		}
#else
		float peak = OverlayMac::AudioPeak();
#endif
		static float level = 0;
		static int since = 0;
		bool beat = ++since > 6 && peak > 0.03f && peak > level * 1.15f;
		level = level * 0.9f + peak * 0.1f;
		if (beat)
		{
			since = 0;
			float hop = fun.music * (0.4f + peak), chance = std::min(1.f, 0.3f + hop * 0.5f);
			for (int i = 0; i < std::min(NPART, sim->parts.active + 1); i++)
			{
				auto &p = sim->parts[i];
				if (p.type == PT_FIRE)
				{
					p.life += int(20 * hop);
				}
				else if (p.type && (elements[p.type].Properties & (TYPE_PART | TYPE_LIQUID)) && std::abs(p.vy) < 0.3f && sim->rng.uniform01() < chance)
				{
					p.vy -= 1.5f + 3 * hop;
					p.vx += (sim->rng.uniform01() - 0.5f) * hop * 2;
				}
			}
		}
	}
	// Sparkle trail: a moving pointer sheds a few cold sparks (EMBR at room temperature: they set nothing alight)
	if (auto &m = sim->petPointer; fun.trail && m.speed > 2 && m.x > CELL && m.y > CELL && m.x < XRES - CELL && m.y < YRES - CELL)
		for (int k = 0; k < 2; k++) ColdSpark(sim, m.x + sim->rng.between(-2, 2), m.y + sim->rng.between(-2, 2));
	// Confetti: the pointer wiggled fast (4 turns within 40 ticks) throws a burst of coloured flakes (sawdust, so the
	// wind flutters them like leaves) that go like the weather does
	static int lastX = -1, lastSign = 0, cooldown = 0;
	static std::deque<int> turns;
	if (auto &m = sim->petPointer; fun.confetti && m.x >= 0)
	{
		int dx = lastX >= 0 ? m.x - lastX : 0, sign = dx >= 4 ? 1 : dx <= -4 ? -1 : 0;
		if (sign && lastSign && sign != lastSign) turns.push_back(sim->currentTick);
		if (sign) lastSign = sign;
		while (!turns.empty() && turns.front() < sim->currentTick - 40) turns.pop_front();
		if (turns.size() >= 4 && sim->currentTick >= cooldown)
		{
			turns.clear(), cooldown = sim->currentTick + 60;
			static const uint32_t colours[] = { 0xFFFF4060, 0xFFFFC020, 0xFF40C0FF, 0xFF60E060, 0xFFC060FF, 0xFFFFFFFF };
			for (int k = 0; k < 40; k++)
			{
				int x = m.x + sim->rng.between(-6, 6), y = m.y + sim->rng.between(-6, 6);
				if (x < CELL || y < CELL || x >= XRES - CELL || y >= YRES - CELL || sim->pmap[y][x] || sim->PixelWall(x, y)) continue;
				if (int i = sim->create_part(-1, x, y, PT_SAWD); i >= 0)
				{
					auto &p = sim->parts[i];
					p.dcolour = colours[sim->rng.between(0, 5)];
					p.vx = (sim->rng.uniform01() - 0.5f) * 4, p.vy = -1 - sim->rng.uniform01() * 3;
					// ponytail: the queue goes in order, so behind long-lived leaves confetti waits for them
					fallen.push_back({ i, PT_SAWD, sim->currentTick + sim->rng.between(300, 500) });
				}
			}
		}
	}
	lastX = sim->petPointer.x;
	sim->petFetchRange = fun.fetch, sim->petKick = fun.kick, sim->birdRest = int(fun.birdRest * 60), sim->petHoles = fun.petHoles;
	// Rainbow: rain that fell half a minute or more, then stopped for two seconds
	if (rainLast == sim->currentTick)
	{
		if (rainSince < 0) rainSince = sim->currentTick;
	}
	else if (rainSince >= 0 && sim->currentTick - rainLast > 120)
	{
		if (rainLast - rainSince >= 1800 && fun.rainbow >= 1) rainbowUntil = GetTickCount64() + ULONGLONG(fun.rainbow * 1000), rainbowTwice = sim->rng.uniform01() < fun.rainbowDouble;
		rainSince = -1;
	}
	// Snowmen (ice, snow-white) on snow at least snowDepth deep, checked every 5 s
	static std::vector<std::pair<int, int>> snowmen; // bottom middle
	std::erase_if(snowmen, [sim](auto &s) { int r = sim->pmap[s.second - 5][s.first]; return !r || TYP(r) != PT_ICEI; });
	if (fun.snowmen >= 1 && sim->currentTick % 300 == 0 && int(snowmen.size()) < int(fun.snowmen))
		for (int tries = 0; tries < 20; tries++)
		{
			auto play = sim->PlayRect();
			int x = sim->rng.between(std::max(CELL, play.pos.X) + 8, std::min(XRES - CELL, play.pos.X + play.size.X) - 9);
			int y = std::max(CELL, play.pos.Y);
			while (y < YRES - CELL && !sim->pmap[y][x] && !sim->PixelWall(x, y)) y++;
			int depth = 0;
			while (y + depth < YRES && sim->pmap[y + depth][x] && TYP(sim->pmap[y + depth][x]) == PT_SNOW) depth++;
			if (depth < int(fun.snowDepth) || y < CELL + 26) continue;
			bool room = true;
			for (int yy = y - 24; yy < y && room; yy++)
				for (int xx = x - 6; xx <= x + 6 && room; xx++) room = !sim->pmap[yy][xx] && !sim->PixelWall(xx, yy);
			if (!room) continue;
			auto put = [&](int px, int py, int type, uint32_t colour) {
				if (sim->pmap[py][px]) return;
				if (int i = sim->create_part(-1, px, py, type); i >= 0) sim->parts[i].temp = 263.15f, sim->parts[i].dcolour = colour;
			};
			// the face and buttons first (the balls fill round them): coal eyes, a carrot nose (wood), coal buttons
			put(x - 1, y - 20, PT_COAL, 0xFF202020), put(x + 1, y - 20, PT_COAL, 0xFF202020);
			for (int k = 1; k <= 3; k++) put(x + k, y - 19, PT_WOOD, 0xFFFF7A1A);
			put(x, y - 12, PT_COAL, 0xFF202020), put(x, y - 14, PT_COAL, 0xFF202020);
			for (auto [r, my] : { std::pair{ 5, y - 5 }, { 4, y - 13 }, { 3, y - 19 } })
				for (int dy = -r; dy <= r; dy++)
					for (int dx = -r; dx <= r; dx++)
						if (dx * dx + dy * dy <= r * r + r) put(x + dx, my + dy, PT_ICEI, 0xFFF4F8FF);
			snowmen.push_back({ x, y });
			break;
		}
	// Paper planes: thrown now and then from a pet (else an edge), glide down slowly, land, and fade after 10 s
	static ULONGLONG nextPlane = 0;
	if (fun.planes < 1) nextPlane = 0;
	else if (!nextPlane) nextPlane = GetTickCount64() + ULONGLONG(fun.planes * 60000);
	else if (GetTickCount64() >= nextPlane && paperPlanes.size() < 8)
	{
		nextPlane = GetTickCount64() + ULONGLONG(fun.planes * 60000);
		bool right = sim->rng.chance(1, 2);
		PaperPlane pl{ right ? float(CELL + 2) : float(XRES - CELL - 3), float(YRES / 4), 0, -0.5f, -1 };
		for (int f = 0; f < MAX_FIGHTERS; f++)
			if (sim->fighters[f].spwn)
			{
				pl.x = (sim->fighters[f].legs[4] + sim->fighters[f].legs[12]) / 2, pl.y = sim->fighters[f].legs[5] - 18;
				right = pl.x < XRES / 2;
				break;
			}
		pl.vx = (right ? 1.5f : -1.5f) * fun.planeSpeed;
		paperPlanes.push_back(pl);
	}
	for (auto &pl : paperPlanes)
	{
		if (pl.landed >= 0)
		{
			pl.landed++;
			continue;
		}
		float sink = 0.25f * fun.planeSpeed;
		pl.vy += (sink - pl.vy) * 0.02f;
		if (std::abs(pl.vx) > 0.6f * fun.planeSpeed) pl.vx *= 0.999f;
		int nx = int(pl.x + pl.vx), ny = int(pl.y + pl.vy);
		if (nx < CELL || ny < CELL || nx >= XRES - CELL || ny >= YRES - CELL)
		{
			pl.landed = 600; // off the edge: gone
			continue;
		}
		if (sim->pmap[ny + 1][nx] || sim->PixelWall(nx, ny + 1) || sim->pmap[ny][nx] || sim->PixelWall(nx, ny)) pl.landed = 0;
		else pl.x += pl.vx, pl.y += pl.vy;
	}
	std::erase_if(paperPlanes, [](auto &pl) { return pl.landed >= 600; });
	funHour = hour;
	// Birds land and rest: a morning bird with something below it now and then glides down onto it (BIRD lands it)
	if (fun.perch)
		for (auto &[i, until] : visitors)
			if (auto &p = sim->parts[i]; until >= 0 && p.tmp2 == 0 && sim->rng.chance(1, 900))
			{
				int x = int(p.x + 0.5f), y = int(p.y + 0.5f);
				for (int yy = y + 1; yy < std::min(YRES - CELL, y + 80); yy++)
					if (sim->pmap[yy][x] || sim->PixelWall(x, yy))
					{
						p.tmp2 = -1;
						break;
					}
			}
	// Mouse wind: a fast pointer blows the loose stuff near it along (not while a button is held: drawing or grabbing)
	static int windX = -1, windY = -1;
	if (auto &m = sim->petPointer; fun.wind > 0 && m.x >= 0 && windX >= 0 && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000))
	{
		float dx = float(m.x - windX), dy = float(m.y - windY);
		if (dx * dx + dy * dy > 9)
		{
			auto &els = SimulationData::CRef().elements;
			for (int y = std::max(0, m.y - 24); y < std::min(YRES, m.y + 25); y++)
				for (int x = std::max(0, m.x - 24); x < std::min(XRES, m.x + 25); x++)
					if (int r = sim->pmap[y][x]; r && (els[TYP(r)].Properties & (TYPE_PART | TYPE_LIQUID | TYPE_GAS)) && (x - m.x) * (x - m.x) + (y - m.y) * (y - m.y) < 576)
					{
						auto &p = sim->parts[ID(r)];
						p.vx = std::clamp(p.vx + dx * 0.12f * fun.wind, -10.f, 10.f);
						p.vy = std::clamp(p.vy + dy * 0.12f * fun.wind, -10.f, 10.f);
					}
		}
	}
	windX = sim->petPointer.x, windY = sim->petPointer.y;
	// Sandcastles: now and then a pet beside a sand pile builds a little castle (sand-coloured brick) from its sand
	static int castles = 0, castleCheck = 0;
	if (fun.castles && ++castleCheck >= 600)
	{
		castleCheck = 0;
		for (int f = 0; f < MAX_FIGHTERS && castles < 3; f++)
		{
			auto &fg = sim->fighters[f];
			if (!fg.spwn) continue;
			int fx = int(fg.legs[4] + fg.legs[12]) / 2, fy = int(fg.legs[5]);
			std::vector<int> sand;
			for (int y = std::max(0, fy - 24); y < std::min(YRES, fy + 6); y++)
				for (int x = std::max(0, fx - 30); x < std::min(XRES, fx + 31); x++)
					if (int r = sim->pmap[y][x]; r && TYP(r) == PT_SAND) sand.push_back(ID(r));
			// the castle: 11 wide, 8 tall; a door in the base, two towers with battlements
			static const char *shape[] = { // top row first
				"#.#.....#.#",
				"###.....###",
				"###.#.#.###",
				"###########",
				"###########",
				"###########",
				"####...####",
				"####...####",
			};
			int cw = 11, ch = int(std::size(shape)), need = 0;
			for (auto row : shape) for (int k = 0; k < cw; k++) need += row[k] == '#';
			if (int(sand.size()) < need + 30) continue;
			int cx = fx + (sim->rng.chance(1, 2) ? 18 : -18) - cw / 2, base = -1;
			for (int y = std::max(CELL, fy - 30); y < std::min(YRES - CELL, fy + 10) && base < 0; y++)
				if (sim->pmap[y][cx + cw / 2] || sim->PixelWall(cx + cw / 2, y)) base = y - 1; // just above the ground
			if (base < 0 || cx < CELL || cx + cw >= XRES - CELL || base - ch < CELL) continue;
			bool room = true;
			for (int r = 0; r < ch && room; r++)
				for (int k = 0; k < cw && room; k++) room = !sim->pmap[base - ch + 1 + r][cx + k] && !sim->PixelWall(cx + k, base - ch + 1 + r);
			if (!room) continue;
			for (int k = 0; k < need; k++) sim->kill_part(sand[k]); // the pile pays for it
			for (int r = 0; r < ch; r++)
				for (int k = 0; k < cw; k++)
					if (shape[r][k] == '#')
						if (int i = sim->create_part(-1, cx + k, base - ch + 1 + r, PT_BRCK); i >= 0)
						{
							sim->parts[i].dcolour = 0xFFD9B86A;
							fallen.push_back({ i, PT_BRCK, sim->currentTick + 18000 });
						}
			castles++;
		}
	}
	if (castles && sim->currentTick % 18000 == 0) castles = 0; // ponytail: a rough count; they crumble after 5 min anyway
}
