#pragma once
#include "simulation/ElementCommon.h"
#include <cmath>

// Flocking for BIRD and FISH: steer toward the neighbours' heading and middle, away from ones too close, wander a
// little, and turn before running into anything `free` says no to. Sets the particle's velocity at `speed`.
// social scales how much it follows the others (0 = a loner that only keeps its distance).
template<class Free>
static void Flock(Simulation *sim, Particle &p, int i, int x, int y, float speed, float social, Free free)
{
	float ax = 0, ay = 0, cx = 0, cy = 0, sx = 0, sy = 0;
	int n = 0;
	for (int ry = -12; ry <= 12; ry += 2) // every other cell: cheaper, enough to see the flock
		for (int rx = -12 + (ry & 2) / 2; rx <= 12; rx += 2)
		{
			int nx = x + rx, ny = y + ry;
			if ((!rx && !ry) || nx < 0 || ny < 0 || nx >= XRES || ny >= YRES) continue;
			int r = sim->pmap[ny][nx];
			if (!r || TYP(r) != p.type) continue;
			auto &q = sim->parts[ID(r)];
			ax += q.vx, ay += q.vy, cx += rx, cy += ry, n++;
			if (rx * rx + ry * ry < 10) sx -= rx, sy -= ry;
		}
	float vx = p.vx, vy = p.vy;
	if (n)
	{
		vx += ((ax / n - vx) * 0.15f + cx / n * 0.03f) * social + sx * 0.06f;
		vy += ((ay / n - vy) * 0.15f + cy / n * 0.03f) * social + sy * 0.06f;
	}
	float turn = (sim->rng.uniform01() - 0.5f) * 0.12f; // wander
	float c = std::cos(turn), s = std::sin(turn);
	float tx = vx * c - vy * s, ty = vx * s + vy * c;
	vx = tx, vy = ty;
	// look ahead; if blocked, try turning left or right a bit more each time, else reverse
	for (int k = 0; k < 8; k++)
	{
		float len = std::hypot(vx, vy);
		if (len < 0.01f) vx = 1, vy = 0, len = 1;
		int lx = x + int(std::lround(vx / len * 5)), ly = y + int(std::lround(vy / len * 5));
		if (free(lx, ly) && free(x + int(std::lround(vx / len * 2)), y + int(std::lround(vy / len * 2)))) break;
		float a = (k / 2 + 1) * 0.6f * ((k + i) % 2 ? 1 : -1);
		c = std::cos(a), s = std::sin(a);
		tx = vx * c - vy * s, ty = vx * s + vy * c;
		vx = tx, vy = ty;
		if (k == 7) vx = -p.vx, vy = -p.vy;
	}
	float len = std::hypot(vx, vy);
	if (len > 0.01f) p.vx = vx / len * speed, p.vy = vy / len * speed;
}
