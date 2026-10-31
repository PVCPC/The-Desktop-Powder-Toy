#pragma once
#include <functional>
#include <vector>

class Simulation;

// A window or desktop icon the sim treats as solid: its box in sim pixels, how far it moved since the last tick (sim
// pixels), and for shaped things (icons) which pixels of the box are solid (null: all of it).
struct OverlayWall
{
	int x0, y0, x1, y1, mx, my;
	std::function<bool(int, int)> solid;
};

// Rebuilds sim->pixelWalls from the walls, carries what rests on the ones that moved and shoves what they moved onto.
void OverlayApplyWalls(Simulation *sim, const std::vector<OverlayWall> &walls);
