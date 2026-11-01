#pragma once
#include <3ds/types.h>

// Simple drawing into RGB565 framebuffers. Screens are 240 px high and
// `sw` px wide (400 top, 320 bottom); x/y are in normal screen orientation.

#define COL565(r, g, b) (u16)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))

u16 *drawFramebuffer(int top, int rightEye);
void drawClear(u16 *fb, int sw, u16 color);
void drawRect(u16 *fb, int sw, int x, int y, int w, int h, u16 color);
void drawCross(u16 *fb, int sw, int x, int y, int size, u16 color);

// Copy a w*h region starting at (sx, sy) of a row-major image to (dx, dy).
void drawImage(u16 *fb, int sw, int dx, int dy, const u16 *img, int stride, int sx, int sy, int w, int h);
// Nearest-neighbour scale the w*h region at (sx, sy) into a dw*dh box at (dx, dy).
void drawImageScaled(u16 *fb, int sw, int dx, int dy, int dw, int dh, const u16 *img, int stride, int sx, int sy, int w, int h);

// 8x8 text using the libctru console font.
void drawText(u16 *fb, int sw, int x, int y, u16 color, const char *fmt, ...);
