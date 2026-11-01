#pragma once
#include <cstdint>
#include <algorithm>
#include <vector>
#include <common/Vec2.h>

constexpr int MENUSIZE = 40;
constexpr int BARSIZE  = 17;

constexpr float M_GRAV = 6.67300e-1f;

//CELL, the size of the pressure, gravity, and wall maps. Larger than 1 to prevent extreme lag
constexpr int CELL = 4;
// The simulation's size, chosen at startup by SetSimSize before anything sized by it exists (the overlay sizes it to
// the screen). Stock TPT is 153x96 cells. The playable area inside it can change at runtime.
inline Vec2<int> CELLS = Vec2(153, 96);
inline Vec2<int> RES = CELLS * CELL;

inline int XCELLS = CELLS.X;
inline int YCELLS = CELLS.Y;
inline int NCELL  = XCELLS * YCELLS;
inline int XRES   = RES.X;
inline int YRES   = RES.Y;
// At most this many particles: pmap holds a particle's index above PMAPBITS (9) bits of type. A bigger sim just
// stops making new particles when it is full, like Powder Game.
constexpr int MAX_NPART = 1 << 22;
inline int NPART  = XRES * YRES;

inline int XCNTR = XRES / 2;
inline int YCNTR = YRES / 2;

inline Vec2<int> WINDOW = RES + Vec2(BARSIZE, MENUSIZE);

inline int WINDOWW = WINDOW.X;
inline int WINDOWH = WINDOW.Y;

inline void SetSimSize(Vec2<int> cells)
{
	CELLS = cells;
	RES = CELLS * CELL;
	XCELLS = CELLS.X;
	YCELLS = CELLS.Y;
	NCELL = XCELLS * YCELLS;
	XRES = RES.X;
	YRES = RES.Y;
	NPART = std::min(XRES * YRES, MAX_NPART);
	XCNTR = XRES / 2;
	YCNTR = YRES / 2;
	WINDOW = RES + Vec2(BARSIZE, MENUSIZE);
	WINDOWW = WINDOW.X;
	WINDOWH = WINDOW.Y;
}

// A height x width table sized when it is made, read like the fixed arrays it replaces: grid[y][x].
// sizeof() is NOT its data size; use size().
template<typename T>
struct SimGrid
{
	std::vector<T> data;
	int width;

	SimGrid(Vec2<int> size, T value = T()) : data(size_t(size.X) * size.Y, value), width(size.X)
	{
	}
	T *operator[](int y)
	{
		return data.data() + size_t(y) * width;
	}
	const T *operator[](int y) const
	{
		return data.data() + size_t(y) * width;
	}
	size_t size() const
	{
		return data.size();
	}
	void Clear()
	{
		std::fill(data.begin(), data.end(), T());
	}
};

constexpr int MAXSIGNS = 16;

constexpr int   ISTP            = CELL / 2;
constexpr float CFDS            = 4.0f / CELL;
constexpr float MAX_VELOCITY = 1e4f;

//Air constants
constexpr float AIR_TSTEPP = 0.3f;
constexpr float AIR_TSTEPV = 0.4f;
constexpr float AIR_VADV   = 0.3f;
constexpr float AIR_VLOSS  = 0.999f;
constexpr float AIR_PLOSS  = 0.9999f;

constexpr int NGOL = 24;

enum DefaultBrushes
{
	BRUSH_CIRCLE,
	BRUSH_SQUARE,
	BRUSH_TRIANGLE,
	NUM_DEFAULTBRUSHES,
};

//Photon constants
constexpr int SURF_RANGE     = 10;
constexpr int NORMAL_MIN_EST =  3;
constexpr int NORMAL_INTERP  = 20;
constexpr int NORMAL_FRAC    = 16;

constexpr auto REFRACT = UINT32_C(0x80000000);

/* heavy flint glass, for awesome refraction/dispersion
   this way you can make roof prisms easily */
constexpr float GLASS_IOR  = 1.9f;
constexpr float GLASS_DISP = 0.07f;

constexpr float R_TEMP = 22;
