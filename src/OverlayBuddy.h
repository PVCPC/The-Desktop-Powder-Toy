#pragma once
#include <cstdint>
#include <vector>

struct OverlayWall;

// Interactive Buddy (Fun tab, Overlay.Fun.Buddy): the user's own copy of the game run by Ruffle (buddy/), drawn over
// the sim in sim pixels, so his stage is the sim's XRES x YRES and the two share coordinates.
namespace OverlayBuddy
{
	// Runs him a frame and draws him over the sim area of the overlay frame (argb, w wide, premultiplied), the mouse at
	// (mx, my) frame pixels. True while he is on (the overlay keeps drawing at full rate).
	bool Draw(uint32_t *argb, int w, double mx, double my);
	// GameView's mouse (0 move, 1 left down, 2 up), frame pixels: true when it is his (a press on him, his toys or his
	// menus, until the release), so TPT doesn't draw.
	bool Mouse(int x, int y, int what);
	// A right-click at (x, y) frame pixels on him: the game's own menus (Items picks the Fist, which drags him). True
	// when it was his.
	bool Menu(int x, int y);
	// His body parts as solids the sim feels (put in front of walls): sand rests on him, he shoves what he moves into
	void Walls(std::vector<OverlayWall> &walls);
	// Shuts him down, which writes the game's saves (TDPT's exit)
	void Close();
}
