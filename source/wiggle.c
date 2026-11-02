#include <stdlib.h>
#include "camera.h"
#include "wiggle.h"

// Matching runs coarse-to-fine: a wide search on half-size images, then a
// small refinement at full size.
#define HALF_W (CAM_W / 2)
#define HALF_H (CAM_H / 2)
#define PATCH 12   // patch radius: 25x25 pixels
#define SEARCH_X 64 // in half-size pixels
#define SEARCH_Y 8
#define REFINE 2

static u8 lumaL[CAM_W * CAM_H], lumaR[CAM_W * CAM_H];
static u8 halfL[HALF_W * HALF_H], halfR[HALF_W * HALF_H];

static void toLuma(u8 *dst, const u16 *src)
{
	for (int i = 0; i < CAM_W * CAM_H; i++)
	{
		u16 p = src[i];
		int r = (p >> 11) << 3, g = ((p >> 5) & 0x3F) << 2, b = (p & 0x1F) << 3;
		dst[i] = (r * 77 + g * 150 + b * 29) >> 8;
	}
}

static void toHalf(u8 *dst, const u8 *src)
{
	for (int y = 0; y < HALF_H; y++)
		for (int x = 0; x < HALF_W; x++)
		{
			const u8 *s = src + y * 2 * CAM_W + x * 2;
			dst[y * HALF_W + x] = (s[0] + s[1] + s[CAM_W] + s[CAM_W + 1] + 2) >> 2;
		}
}

void wiggleSetImages(const u16 *left, const u16 *right)
{
	toLuma(lumaL, left);
	toLuma(lumaR, right);
	toHalf(halfL, lumaL);
	toHalf(halfR, lumaR);
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// Best offset of the patch around (px, py) of `a` within the given search box of `b`.
static Offset search(const u8 *a, const u8 *b, int w, int h, int px, int py,
                     int x0, int x1, int y0, int y1)
{
	px = clampi(px, PATCH, w - 1 - PATCH);
	py = clampi(py, PATCH, h - 1 - PATCH);

	Offset best = {0, 0};
	u32 bestSad = 0xFFFFFFFF;
	for (int dy = y0; dy <= y1; dy++)
	{
		int ry = py + dy;
		if (ry < PATCH || ry > h - 1 - PATCH)
			continue;
		for (int dx = x0; dx <= x1; dx++)
		{
			int rx = px + dx;
			if (rx < PATCH || rx > w - 1 - PATCH)
				continue;
			u32 sad = 0;
			for (int y = -PATCH; y <= PATCH && sad < bestSad; y++)
			{
				const u8 *pa = a + (py + y) * w + px - PATCH;
				const u8 *pb = b + (ry + y) * w + rx - PATCH;
				for (int x = 0; x <= 2 * PATCH; x++)
					sad += abs(pa[x] - pb[x]);
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

Offset wiggleMatch(int px, int py)
{
	Offset c = search(halfL, halfR, HALF_W, HALF_H, px / 2, py / 2, -SEARCH_X, SEARCH_X, -SEARCH_Y, SEARCH_Y);
	return search(lumaL, lumaR, CAM_W, CAM_H, px, py,
	              c.dx * 2 - REFINE, c.dx * 2 + REFINE, c.dy * 2 - REFINE, c.dy * 2 + REFINE);
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

int wiggleFrames(int smooth, int *mix)
{
	static const int seq[SMOOTH_LEVELS][MAX_WIGGLE_FRAMES] = {
		{0, 255},
		{0, 128, 255, 128},
		{0, 85, 170, 255, 170, 85},
	};
	static const int count[SMOOTH_LEVELS] = {2, 4, 6};
	if (smooth < 0 || smooth >= SMOOTH_LEVELS)
		smooth = 0;
	for (int i = 0; i < count[smooth]; i++)
		mix[i] = seq[smooth][i];
	return count[smooth];
}

const char *wiggleSmoothName(int smooth)
{
	static const char *names[SMOOTH_LEVELS] = {"Hard cut", "Smooth", "Extra smooth"};
	return names[smooth >= 0 && smooth < SMOOTH_LEVELS ? smooth : 0];
}
