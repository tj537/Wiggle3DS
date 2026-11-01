#pragma once
#include <3ds/types.h>

// One animation frame: a w*h crop at (x, y) of a row-major RGB565 image,
// optionally recolored through a 65536-entry lookup table.
typedef struct
{
	const u16 *img;
	int stride, x, y;
	const u16 *lut;
} GifFrame;

// Writes a looping GIF; all frames share one palette fitted to their colors.
bool gifWrite(const char *path, const GifFrame *frames, int count, int w, int h, int delayMs);
