#include <stdlib.h>
#include "camera.h"
#include "wiggle.h"

#define PATCH 12   // patch radius: 25x25 pixels
#define SEARCH_X 64
#define SEARCH_Y 8

static u8 lumaL[CAM_W * CAM_H], lumaR[CAM_W * CAM_H];

static void toLuma(u8 *dst, const u16 *src)
{
	for (int i = 0; i < CAM_W * CAM_H; i++)
	{
		u16 p = src[i];
		int r = (p >> 11) << 3, g = ((p >> 5) & 0x3F) << 2, b = (p & 0x1F) << 3;
		dst[i] = (r * 77 + g * 150 + b * 29) >> 8;
	}
}

void wiggleSetImages(const u16 *left, const u16 *right)
{
	toLuma(lumaL, left);
	toLuma(lumaR, right);
}

Offset wiggleMatch(int px, int py)
{
	// Keep the patch inside the left image.
	if (px < PATCH) px = PATCH;
	if (py < PATCH) py = PATCH;
	if (px > CAM_W - 1 - PATCH) px = CAM_W - 1 - PATCH;
	if (py > CAM_H - 1 - PATCH) py = CAM_H - 1 - PATCH;

	Offset best = {0, 0};
	u32 bestSad = 0xFFFFFFFF;
	for (int dy = -SEARCH_Y; dy <= SEARCH_Y; dy++)
	{
		int ry = py + dy;
		if (ry < PATCH || ry > CAM_H - 1 - PATCH)
			continue;
		for (int dx = -SEARCH_X; dx <= SEARCH_X; dx++)
		{
			int rx = px + dx;
			if (rx < PATCH || rx > CAM_W - 1 - PATCH)
				continue;
			u32 sad = 0;
			for (int y = -PATCH; y <= PATCH && sad < bestSad; y++)
			{
				const u8 *a = lumaL + (py + y) * CAM_W + px - PATCH;
				const u8 *b = lumaR + (ry + y) * CAM_W + rx - PATCH;
				for (int x = 0; x <= 2 * PATCH; x++)
					sad += abs(a[x] - b[x]);
			}
			// Prefer small shifts on ties (flat areas).
			if (sad < bestSad || (sad == bestSad && abs(dx) + abs(dy) < abs(best.dx) + abs(best.dy)))
			{
				bestSad = sad;
				best.dx = dx;
				best.dy = dy;
			}
		}
	}
	return best;
}

Crop wiggleCrop(Offset o)
{
	Crop c;
	c.lx = o.dx < 0 ? -o.dx : 0;
	c.ly = o.dy < 0 ? -o.dy : 0;
	c.rx = c.lx + o.dx;
	c.ry = c.ly + o.dy;
	c.w = CAM_W - abs(o.dx);
	c.h = CAM_H - abs(o.dy);
	return c;
}
