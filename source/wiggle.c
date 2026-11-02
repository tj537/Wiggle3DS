#include <stdlib.h>
#include <string.h>
#include "camera.h"
#include "wiggle.h"

// Matching runs coarse-to-fine: a wide search on half-size images, then a
// small refinement at full size. The two cameras expose differently, so
// patches are compared with their mean brightness taken out.
#define HALF_W (CAM_W / 2)
#define HALF_H (CAM_H / 2)
#define PATCH 12      // patch radius at half size: 25x25 pixels
#define PATCH_FLAT 20 // for patches with little texture
#define REFINE_PATCH 16
#define SEARCH_X 64 // in half-size pixels
#define SEARCH_Y 5 // the cameras sit level, so big vertical shifts are false matches
#define PROBE 8    // matches around the touch point, combined by median
#define REFINE 2
#define FLAT_SAD 4 // mean deviation from the patch mean below which a patch counts as flat

static u8 lumaL[CAM_W * CAM_H], lumaR[CAM_W * CAM_H];
static u8 halfL[HALF_W * HALF_H], halfR[HALF_W * HALF_H];
// Summed-area tables of the right images, for the mean of any patch: (w+1)*(h+1).
static u32 sumR[(CAM_W + 1) * (CAM_H + 1)], sumHalfR[(HALF_W + 1) * (HALF_H + 1)];

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

static void summedArea(u32 *sat, const u8 *img, int w, int h)
{
	memset(sat, 0, (w + 1) * sizeof(u32));
	for (int y = 0; y < h; y++)
	{
		u32 row = 0, *out = sat + (y + 1) * (w + 1), *above = out - (w + 1);
		out[0] = 0;
		for (int x = 0; x < w; x++)
		{
			row += img[y * w + x];
			out[x + 1] = above[x + 1] + row;
		}
	}
}

void wiggleSetImages(const u16 *left, const u16 *right)
{
	toLuma(lumaL, left);
	toLuma(lumaR, right);
	toHalf(halfL, lumaL);
	toHalf(halfR, lumaR);
	summedArea(sumR, lumaR, CAM_W, CAM_H);
	summedArea(sumHalfR, halfR, HALF_W, HALF_H);
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int patchSum(const u8 *img, int w, int cx, int cy, int r)
{
	int sum = 0;
	for (int y = -r; y <= r; y++)
		for (int x = -r; x <= r; x++)
			sum += img[(cy + y) * w + cx + x];
	return sum;
}

// Mean absolute deviation of a patch from its mean: how much texture it has.
static int patchTexture(const u8 *img, int w, int cx, int cy, int r)
{
	int n = (2 * r + 1) * (2 * r + 1), mean = patchSum(img, w, cx, cy, r) / n, dev = 0;
	for (int y = -r; y <= r; y++)
		for (int x = -r; x <= r; x++)
			dev += abs(img[(cy + y) * w + cx + x] - mean);
	return dev / n;
}

// Best offset of the patch (radius r) around (px, py) of `a` within the
// given search box of `b`; `sat` is b's summed-area table.
static Offset search(const u8 *a, const u8 *b, const u32 *sat, int w, int h, int r, int px, int py,
                     int x0, int x1, int y0, int y1)
{
	px = clampi(px, r, w - 1 - r);
	py = clampi(py, r, h - 1 - r);
	int n = (2 * r + 1) * (2 * r + 1), sumA = patchSum(a, w, px, py, r);

	Offset best = {0, 0};
	u32 bestSad = 0xFFFFFFFF;
	for (int dy = y0; dy <= y1; dy++)
	{
		int ry = py + dy;
		if (ry < r || ry > h - 1 - r)
			continue;
		for (int dx = x0; dx <= x1; dx++)
		{
			int rx = px + dx;
			if (rx < r || rx > w - 1 - r)
				continue;
			const u32 *top = sat + (ry - r) * (w + 1), *bottom = sat + (ry + r + 1) * (w + 1);
			int sumB = bottom[rx + r + 1] - bottom[rx - r] - top[rx + r + 1] + top[rx - r];
			int d = (sumA - sumB + (sumA > sumB ? n / 2 : -n / 2)) / n; // mean difference
			u32 sad = abs(dy) * n; // a small cost for vertical shift
			for (int y = -r; y <= r && sad < bestSad; y++)
			{
				const u8 *pa = a + (py + y) * w + px - r;
				const u8 *pb = b + (ry + y) * w + rx - r;
				for (int x = 0; x <= 2 * r; x++)
					sad += abs(pa[x] - pb[x] - d);
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

static Offset matchAt(int px, int py)
{
	int hx = clampi(px / 2, PATCH_FLAT, HALF_W - 1 - PATCH_FLAT), hy = clampi(py / 2, PATCH_FLAT, HALF_H - 1 - PATCH_FLAT);
	bool flat = patchTexture(halfL, HALF_W, hx, hy, PATCH) < FLAT_SAD;
	int r = flat ? PATCH_FLAT : PATCH;
	Offset c = search(halfL, halfR, sumHalfR, HALF_W, HALF_H, r, px / 2, py / 2, -SEARCH_X, SEARCH_X, -SEARCH_Y, SEARCH_Y);
	return search(lumaL, lumaR, sumR, CAM_W, CAM_H, flat ? 2 * PATCH_FLAT : REFINE_PATCH, px, py,
	              c.dx * 2 - REFINE, c.dx * 2 + REFINE, c.dy * 2 - REFINE, c.dy * 2 + REFINE);
}

static int median5(int *v)
{
	for (int i = 1; i < 5; i++)
		for (int j = i; j > 0 && v[j - 1] > v[j]; j--)
		{
			int t = v[j];
			v[j] = v[j - 1];
			v[j - 1] = t;
		}
	return v[2];
}

// On edges and repeating patterns a single patch can lock onto the wrong
// spot; the median of five nearby matches follows the majority.
Offset wiggleMatch(int px, int py)
{
	static const int probe[5][2] = {{0, 0}, {-PROBE, 0}, {PROBE, 0}, {0, -PROBE}, {0, PROBE}};
	int dx[5], dy[5];
	for (int i = 0; i < 5; i++)
	{
		Offset o = matchAt(clampi(px + probe[i][0], 0, CAM_W - 1), clampi(py + probe[i][1], 0, CAM_H - 1));
		dx[i] = o.dx;
		dy[i] = o.dy;
	}
	return (Offset){median5(dx), median5(dy)};
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

