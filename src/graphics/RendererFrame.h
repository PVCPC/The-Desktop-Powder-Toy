#pragma once
#include "SimulationConfig.h"
#include "Pixel.h"
#include "common/Plane.h"
#include <vector>

// The sim plus the side bar's width, sized when made.
struct RendererFrame : PlaneAdapter<std::vector<pixel>>
{
	RendererFrame() : PlaneAdapter<std::vector<pixel>>(Vec2(WINDOW.X, RES.Y), pixel(0))
	{
	}
};

struct RendererStats
{
	int foundParticles = 0;
	float hdispLimitMin = 0;
	float hdispLimitMax = 0;
	bool hdispLimitValid = false;
};
