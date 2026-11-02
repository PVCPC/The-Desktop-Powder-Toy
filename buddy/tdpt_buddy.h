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
	// the sim over rect: w * h bytes, rows top down, nonzero where it's solid for him
	void tdpt_buddy_ground(Buddy *b, const int32_t rect[4], const uint8_t *cells);
	void tdpt_buddy_close(Buddy *b);
}
