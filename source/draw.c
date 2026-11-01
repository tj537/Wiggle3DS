#include <3ds.h>
#include <stdarg.h>
#include <stdio.h>
#include "draw.h"

// The framebuffers are rotated: memory runs column by column, bottom to top.
static inline void put(u16 *fb, int sw, int x, int y, u16 c)
{
	if (x < 0 || y < 0 || x >= sw || y >= 240)
		return;
	fb[x * 240 + (239 - y)] = c;
}

u16 *drawFramebuffer(int top, int rightEye)
{
	return (u16 *)gfxGetFramebuffer(top ? GFX_TOP : GFX_BOTTOM, rightEye ? GFX_RIGHT : GFX_LEFT, NULL, NULL);
}

void drawClear(u16 *fb, int sw, u16 color)
{
	for (int i = 0; i < sw * 240; i++)
		fb[i] = color;
}

void drawRect(u16 *fb, int sw, int x, int y, int w, int h, u16 color)
{
	for (int i = 0; i < w; i++)
		for (int j = 0; j < h; j++)
			put(fb, sw, x + i, y + j, color);
}

void drawCross(u16 *fb, int sw, int x, int y, int size, u16 color)
{
	drawRect(fb, sw, x - size, y, size * 2 + 1, 1, color);
	drawRect(fb, sw, x, y - size, 1, size * 2 + 1, color);
}

void drawImage(u16 *fb, int sw, int dx, int dy, const u16 *img, int stride, int sx, int sy, int w, int h)
{
	for (int x = 0; x < w; x++)
	{
		int fx = dx + x;
		if (fx < 0 || fx >= sw)
			continue;
		u16 *col = fb + fx * 240;
		const u16 *src = img + sy * stride + sx + x;
		for (int y = 0; y < h; y++)
		{
			int fy = dy + y;
			if (fy >= 0 && fy < 240)
				col[239 - fy] = src[y * stride];
		}
	}
}

void drawImageScaled(u16 *fb, int sw, int dx, int dy, int dw, int dh, const u16 *img, int stride, int sx, int sy, int w, int h)
{
	for (int x = 0; x < dw; x++)
	{
		const u16 *src = img + sy * stride + sx + x * w / dw;
		for (int y = 0; y < dh; y++)
			put(fb, sw, dx + x, dy + y, src[(y * h / dh) * stride]);
	}
}

void drawText(u16 *fb, int sw, int x, int y, u16 color, const char *fmt, ...)
{
	char buf[64];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	const ConsoleFont *font = &consoleGetDefault()->font;
	for (const char *s = buf; *s; s++, x += 8)
	{
		unsigned idx = (u8)*s - font->asciiOffset;
		if (idx >= font->numChars)
			continue;
		const u8 *glyph = font->gfx + idx * 8;
		for (int row = 0; row < 8; row++)
			for (int col = 0; col < 8; col++)
				if (glyph[row] & (0x80 >> col))
					put(fb, sw, x + col, y + row, color);
	}
}
