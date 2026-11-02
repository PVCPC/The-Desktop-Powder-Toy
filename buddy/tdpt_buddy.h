#pragma once
// Interactive Buddy run by Ruffle (buddy/src/lib.rs)
#include <cstdint>
#include <cstddef>
extern "C"
{
	struct Buddy;
	Buddy *tdpt_buddy_open(const char *swfPath, uint32_t w, uint32_t h, char *error, size_t errorLen);
	// his newest frame (w x h premultiplied 0xAARRGGBB, valid until the next call; null: none yet); he runs on his own
	// thread, this only passes the mouse and the stage size
	const uint32_t *tdpt_buddy_frame(Buddy *b, double mouseX, double mouseY, uint32_t w, uint32_t h);
	// a click there lands on him, his toys or the game's windows
	bool tdpt_buddy_hit(Buddy *b, double x, double y);
	void tdpt_buddy_mouse(Buddy *b, double x, double y, int what); // 1 down, 2 up
	// the game's own menus: lines of "depth	flags	command	label" (flags p submenu, g greyed, c ticked, s separator),
	// into out (cut short to len); returns the length needed
	size_t tdpt_buddy_menus(Buddy *b, char *out, size_t len);
	void tdpt_buddy_command(Buddy *b, const char *command); // a command from those lines, for the game
	// the part of the sim he needs next: rect = x, y, w, h sim pixels (maybe partly off the sim)
	void tdpt_buddy_area(Buddy *b, int32_t rect[4]);
	// the sim over rect: w * h bytes, rows top down; low 5 bits 0 nothing, 1 solid for him, 2 + weight/4 liquid, 31 acid, 30 VOID or
	// black hole, 29 portal in;
	// 0x20 60 C or hotter, 0x40 400 C or hotter, 0x60 -50 C or colder; 0x80 a spark
	void tdpt_buddy_ground(Buddy *b, const int32_t rect[4], const uint8_t *cells);
	// the sim's air over rect (x, y, w, h in cells of `cell` sim pixels): w * h of vx, vy, pressure
	void tdpt_buddy_air(Buddy *b, int32_t cell, const int32_t rect[4], const float *v);
	// TPT's gravity where he is, 1 = normal (straight down is 0, 1)
	void tdpt_buddy_gravity(Buddy *b, double gx, double gy);
	// where the portal in his reach comes out (sim pixels); has = 0: none
	void tdpt_buddy_portal(Buddy *b, int32_t has, double x, double y);
	void tdpt_buddy_close(Buddy *b);
}
