#pragma once
#include <3ds/types.h>

// One animation frame: a w*h crop at (x, y) of a row-major RGB565 image.
typedef struct
{
	const u16 *img;
	int stride, x, y;
} GifFrame;

// Writes a looping GIF with a fixed 6x7x6 palette and ordered dithering.
bool gifWrite(const char *path, const GifFrame *frames, int count, int w, int h, int delayMs);
