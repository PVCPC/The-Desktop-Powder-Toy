#pragma once
#include "SimulationConfig.h"

class Simulation;
struct RenderableSimulation;

class Air
{
public:
	Simulation & sim;
	int airMode;
	float ambientAirTemp;
	float edgePressure;
	float edgeVelocityX;
	float edgeVelocityY;
	float vorticityCoeff;
	int convectionMode;
	SimGrid<float> ovx{ CELLS };
	SimGrid<float> ovy{ CELLS };
	SimGrid<float> opv{ CELLS };
	SimGrid<float> ohv{ CELLS }; // Ambient Heat
	SimGrid<unsigned char> bmap_blockair{ CELLS };
	SimGrid<unsigned char> bmap_blockairh{ CELLS };
	float kernel[9];
	void make_kernel(void);
	static float vorticity(const RenderableSimulation & sm, int y, int x);
	void update_airh(void);
	void update_air(void);
	void Clear();
	void ClearAirH();
	void Invert();
	void ApproximateBlockAirMaps(Rect<int> targetBlocks);
	Air(Simulation & sim);
};
