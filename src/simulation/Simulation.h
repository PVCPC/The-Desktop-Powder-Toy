#pragma once
#include "Particle.h"
#include "Stickman.h"
#include "WallType.h"
#include "Sign.h"
#include "ElementDefs.h"
#include "BuiltinGOL.h"
#include "MenuSection.h"
#include "AccessProperty.h"
#include "CoordStack.h"
#include "common/tpt-rand.h"
#include "gravity/Gravity.h"
#include "graphics/RendererFrame.h"
#include "Element.h"
#include "SimulationConfig.h"
#include "SimulationSettings.h"
#include <cstring>
#include <cstddef>
#include <vector>
#include <array>
#include <memory>
#include <optional>
#include <map>
#include <cstdint>
#include <mutex>

constexpr int CHANNELS = int(MAX_TEMP - 73) / 100 + 2;

class FrameTime;
class Snapshot;
class Brush;
struct SimulationSample;
struct matrix2d;
struct vector2d;

class Simulation;
class Renderer;
class Air;
class GameSave;

class Parts
{
	int pfree;

public:
	std::vector<Particle> data = std::vector<Particle>(NPART);
	// initialized in clear_sim
	int active;

	operator const Particle *() const
	{
		return data.data();
	}

	operator Particle *()
	{
		return data.data();
	}

	Parts()
	{
		Reset();
	}

	Parts(const Parts &other) = default;

	Parts &operator =(const Parts &other)
	{
		std::copy(other.data.begin(), other.data.begin() + other.active, data.begin());
		active = other.active;
		pfree = other.pfree;
		return *this;
	}

	Parts(const Parts &&other) = delete;
	Parts &operator =(const Parts &&other) = delete;

	void Reset();
	void Free(int i);
	int Alloc();
	void Flatten();

	bool MaxPartsReached() const
	{
		return pfree == -1 && active >= NPART;
	}
};

struct RenderableSimulation
{
	GravityInput gravIn;
	GravityOutput gravOut; // invariant: when grav is empty, this is in its default-constructed state
	bool gravForceRecalc = true;
	std::vector<sign> signs;

	int currentTick = 0;
	int emp_decor = 0;

	playerst player;
	playerst player2;
	playerst fighters[MAX_FIGHTERS]; //Defined in Stickman.h

	SimGrid<float> vx{ CELLS };
	SimGrid<float> vy{ CELLS };
	SimGrid<float> pv{ CELLS };
	SimGrid<float> hv{ CELLS };

	SimGrid<unsigned char> bmap{ CELLS };
	SimGrid<unsigned char> emap{ CELLS };

	Parts parts;
	SimGrid<int> pmap{ RES };
	SimGrid<int> photons{ RES };

	int aheat_enable = 0;

	bool useLuaCallbacks = false;
};

// Simulation::rng. While plain particles update on several threads (Simulation::parallelUpdate), each thread draws from
// its own RNG instead.
class SimRNG : public RNG
{
public:
	static thread_local RNG *local;
	SimRNG &operator=(const RNG &other) { RNG::operator=(other); return *this; }
	unsigned int operator()() { return local ? (*local)() : RNG::operator()(); }
	unsigned int gen() { return local ? local->gen() : RNG::gen(); }
	int between(int lower, int upper) { return local ? local->between(lower, upper) : RNG::between(lower, upper); }
	bool chance(int numerator, unsigned int denominator) { return local ? local->chance(numerator, denominator) : RNG::chance(numerator, denominator); }
	float uniform01() { return local ? local->uniform01() : RNG::uniform01(); }
	double uniform01Double() { return local ? local->uniform01Double() : RNG::uniform01Double(); }
};

class Simulation : public RenderableSimulation
{
public:
	GravityPtr grav;
	std::unique_ptr<Air> air;

	SimRNG rng;
	// Plain particles (no update function of their own) update on every core, in blocks far enough apart not to
	// touch; the rest after them, one at a time. Off: TPT's original single-threaded order.
	bool petWander = false; // fighters with no stickman to chase stroll about (the overlay's desktop pets)
	struct { int x = -1, y = -1; float speed = 0; } petPointer; // the mouse pointer the pets notice (sim pixels, per tick)
	int petGreet = 0; // pets hop until this tick: the user is back
	bool petNap = false;    // pets stand still asleep (the overlay draws their z's)
	bool petGather = false; // the user is away: pets make for the middle of the screen
	bool petHoles = false;  // pets drop through holes in windows and stop to look out of them
	int birdRest = 600; // ticks a landed bird rests, about
	float petFetchRange = 0, petKick = 1; // pets run after balls this far (0: no fetch) and kick them back this hard
	struct { bool on = false; int type = 0, body = 0; float x = 0, y = 0; } grab; // a BALL or JOIN body held by the mouse
	bool parallelUpdate = true;
	bool parallelPhase = false; // the blocks are running: creating, killing and changing particles take parallelLock
	std::recursive_mutex parallelLock;

	int replaceModeSelected = 0;
	int replaceModeFlags = 0;
	int debug_nextToUpdate = 0;
	int debug_mostRecentlyUpdated = -1; // -1 when between full update loops
	int elementCount[PT_NUM];
	int ISWIRE = 0;
	bool force_stacking_check = false;
	int emp_trigger_count = 0;
	bool etrd_count_valid = false;
	int etrd_life0_count = 0;
	int lightningRecreate = 0;
	bool gravWallChanged = false;

	Particle portalp[CHANNELS][8][80];
	int wireless[CHANNELS][2];

	int CGOL = 0;
	int GSPEED = 1;
	SimGrid<std::array<unsigned int, 5>> gol{ RES };

	SimGrid<float> fvx{ CELLS };
	SimGrid<float> fvy{ CELLS };
	SimGrid<int> Element_LOLZ_lolz{ Vec2(YRES / 9, XRES / 9) }; // [x][y]
	SimGrid<int> Element_LOVE_love{ Vec2(YRES / 9, XRES / 9) }; // [x][y]
	std::vector<int> Element_PSTN_tempParts = std::vector<int>(std::max(XRES, YRES));
	int Element_PPIP_ppip_changed;

	SimGrid<unsigned int> pmap_count{ RES };

	int edgeMode = EDGE_VOID;
	// The playable area, in cells. The rest of the (build-time) sim is off limits: solid wall with solid or loop
	// edges, an air and particle sink with void edges. Full size = stock behaviour. Saves cover just this area.
	Rect<int> playArea = CELLS.OriginRect();
	void ApplyPlayArea();
	Rect<int> PlayRect() const
	{
		return RectSized(playArea.pos * CELL, playArea.size * CELL);
	}
	// Loop edges: wraps a position into the play area, less its outer ring of cells (as stock TPT does at the
	// screen edge). True if it wrapped.
	bool LoopWrap(float &x, float &y) const;
	// XRES x YRES, or empty: 1 where something outside the sim (the overlay's desktop windows) blocks particles,
	// pixel by pixel. Nothing moves or is drawn into it; unlike walls it never deletes what's already there.
	std::vector<uint8_t> pixelWalls;
	// Marks particles left on those windows, by window (OverlayWall::id): per kind, 0-255 per window pixel from its
	// top-left (x, y: where that is now, in sim pixels, followed every frame; wx, wy: where the sim's walls had it on
	// the last tick). Cleared with the sim.
	enum WindowMark { MarkWater, MarkFrost, MarkAcid, MarkSoot, MarkCrack, MarkHole, MarkKinds }; // drawn in this order (holes: 255 = eaten away)
	struct WindowMarks { int x = 0, y = 0, w = 0, h = 0, wx = 0, wy = 0; std::array<std::vector<uint8_t>, MarkKinds> a; std::array<bool, MarkKinds> any{}; };
	std::map<uintptr_t, WindowMarks> windowMarks;
	bool PixelWall(int x, int y) const
	{
		return !pixelWalls.empty() && pixelWalls[size_t(y) * XRES + x];
	}
	bool HoleAt(int x, int y) const // a hole broken in a window or icon there
	{
		for (auto &[id, m] : windowMarks)
			if (m.any[MarkHole] && x >= m.x && y >= m.y && x < m.x + m.w && y < m.y + m.h && m.a[MarkHole][size_t(y - m.y) * m.w + (x - m.x)]) return true;
		return false;
	}
	int gravityMode = GRAV_VERTICAL;
	float customGravityX = 0;
	float customGravityY = 0;
	int legacy_enable = 0;
	int water_equal_test = 0;
	int pretty_powder = 0;
	int sandcolour_frame = 0;
	int deco_space = DECOSPACE_SRGB;

	// initialized in clear_sim
	bool elementRecount;
	unsigned char fighcount; //Contains the number of fighters
	uint64_t frameCount;
	bool ensureDeterminism;

	// initialized very late >_>
	int NUM_PARTS;
	int sandcolour;
	int sandcolour_interface;

	void Load(const GameSave *save, bool includePressure, Vec2<int> blockP); // block coordinates
	std::unique_ptr<GameSave> Save(bool includePressure, Rect<int> partR); // particle coordinates
	void SaveSimOptions(GameSave &gameSave);
	SimulationSample GetSample(int x, int y);

	std::unique_ptr<Snapshot> CreateSnapshot() const;
	void Restore(const Snapshot &snap);

	int is_blocking(int t, int x, int y) const;
	int is_boundary(int pt, int x, int y) const;
	int find_next_boundary(int pt, int *x, int *y, int dm, int *em, bool reverse) const;
	void photoelectric_effect(int nx, int ny);
	int do_move(int i, int x, int y, float nxf, float nyf);
	bool move(int i, int x, int y, float nxf, float nyf);
	int try_move(int i, int x, int y, int nx, int ny);
	int eval_move(int pt, int nx, int ny, unsigned *rr) const;

	struct PlanMoveResult
	{
		int fin_x, fin_y, clear_x, clear_y;
		float fin_xf, fin_yf, clear_xf, clear_yf;
		float vx, vy;
	};
	template<bool UpdateEmap, class Sim>
	static PlanMoveResult PlanMove(Sim &sim, int i, int x, int y);

	bool IsWallBlocking(int x, int y, int type) const;
	void create_cherenkov_photon(int pp);
	void create_gain_photon(int pp);
	void kill_part(int i);
	bool FloodFillPmapCheck(int x, int y, int type) const;
	int flood_prop(int x, int y, const AccessProperty &changeProperty);
	bool flood_water(int x, int y, int i);
	int FloodINST(int x, int y);
	void detach(int i);
	bool part_change_type(int i, int x, int y, int t);
	//int InCurrentBrush(int i, int j, int rx, int ry);
	//int get_brush_flags();
	int create_part(int p, int x, int y, int t, int v = -1);
	int createPartTempVel(int i, int x, int y, int t);
	void delete_part(int x, int y);
	void get_sign_pos(int i, int *x0, int *y0, int *w, int *h);
	int is_wire(int x, int y);
	int is_wire_off(int x, int y);
	void set_emap(int x, int y);
	int parts_avg(int ci, int ni, int t);
	virtual void UpdateParticles(int start, int end) = 0; // Dispatches an update to the range [start, end).
	void SimulateGoL();
	void RecalcFreeParticles(bool do_life_dec);
	void CheckStacking();
	void BeforeSim(bool willUpdate);
	void AfterSim();
	void clear_area(int area_x, int area_y, int area_w, int area_h);

	void SetEdgeMode(int newEdgeMode);
	void SetDecoSpace(int newDecoSpace);

	//Drawing Deco
	void ApplyDecoration(int x, int y, int colR, int colG, int colB, int colA, int mode);
	void ApplyDecorationPoint(int x, int y, int colR, int colG, int colB, int colA, int mode, Brush const &cBrush);
	void ApplyDecorationLine(int x1, int y1, int x2, int y2, int colR, int colG, int colB, int colA, int mode, Brush const &cBrush);
	void ApplyDecorationBox(int x1, int y1, int x2, int y2, int colR, int colG, int colB, int colA, int mode);
	bool ColorCompare(const RendererFrame &frame, int x, int y, int replaceR, int replaceG, int replaceB);
	void ApplyDecorationFill(const RendererFrame &frame, int x, int y, int colR, int colG, int colB, int colA, int replaceR, int replaceG, int replaceB);

	//Drawing Walls
	int CreateWalls(int x, int y, int rx, int ry, int wall, Brush const *cBrush);
	void CreateWallLine(int x1, int y1, int x2, int y2, int rx, int ry, int wall, Brush const *cBrush);
	void CreateWallBox(int x1, int y1, int x2, int y2, int wall);
	int FloodWalls(int x, int y, int wall, int bm);

	//Drawing Particles
	int CreateParts(int p, int positionX, int positionY, int c, Brush const &cBrush, int flags);
	int CreateParts(int p, int x, int y, int rx, int ry, int c, int flags);
	int CreatePartFlags(int p, int x, int y, int c, int flags);
	void CreateLine(int x1, int y1, int x2, int y2, int c, Brush const &cBrush, int flags);
	void CreateLine(int x1, int y1, int x2, int y2, int c);
	void CreateBox(int p, int x1, int y1, int x2, int y2, int c, int flags);
	int FloodParts(int x, int y, int c, int cm, int flags);

	void GetGravityField(int x, int y, float particleGrav, float newtonGrav, float & pGravX, float & pGravY) const;

	int get_wavelength_bin(int *wm);
	struct GetNormalResult
	{
		bool success;
		float nx, ny;
		int lx, ly, rx, ry;
	};
	GetNormalResult get_normal(int pt, int x, int y, float dx, float dy) const;
	template<bool PhotoelectricEffect, class Sim>
	static GetNormalResult get_normal_interp(Sim &sim, int pt, float x0, float y0, float dx, float dy);
	void clear_sim();
	Simulation();
	virtual ~Simulation();

	void EnableNewtonianGravity(bool enable);

	FrameTime *frameTime = nullptr;

	static std::unique_ptr<Simulation> Factory();

private:
	CoordStack& getCoordStackSingleton();

	void ResetNewtonianGravity(GravityInput newGravIn, GravityOutput newGravOut);
	void DispatchNewtonianGravity();
	void UpdateGravityMask();
};
