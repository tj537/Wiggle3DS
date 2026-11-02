#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "draw.h"
#include "filter.h"

// Glitches work on the image itself, after the colors and before the
// vignette; their sizes are relative to the image, so a thumbnail looks like
// the GIF. Each has a fixed random pattern, the same in both eyes.
typedef enum
{
	G_NONE, G_CHROMA, G_VHS, G_MOSH, G_SORT, G_TEAR, G_MOSAIC, G_CRT, G_BITROT, G_ECHO, G_NIGHT, G_MELT,
	G_INTERLACE, G_CIRCUIT, G_PRISM, G_GHOST, G_TRAILS, G_BLEED, G_WOBBLE, G_SIGNAL, G_HOLO, G_BROADCAST,
	G_WORN, G_ANAGLYPH, G_SMEAR, G_SWIRL, G_COMPRESS, G_DRIFT, G_KEYFRAME,
} Glitch;

// Moving glitches get a new pattern every frame of the loop (see
// filterLoopFrames), the same in the GIF as on screen.
#define MOVES 1

typedef struct
{
	const char *name;
	int vignette;
	Glitch glitch;
	int moves;
} FilterDef;

static const FilterDef defs[FILTER_COUNT] = {
	{"Original", 0, 0},
	{"Vivid", 0, 0},
	{"Warm", 0, 0},
	{"Cool", 0, 0},
	{"Dream", 60, 0},
	{"Cinema", 70, 0},
	{"Vintage", 90, 0},
	{"Lomo", 170, 0},
	{"Sepia", 80, 0},
	{"Mono", 0, 0},
	{"Noir", 130, 0},
	// more colors
	{"Chrome", 40, 0},
	{"Bleach", 60, 0},
	{"Cross", 50, 0},
	{"Pastel", 0, 0},
	{"Duotone", 0, 0},
	{"Infrared", 40, 0},
	{"Thermal", 0, 0},
	{"Acid", 0, 0},
	{"Solar", 0, 0},
	{"Game Boy", 0, 0},
	{"Bent", 0, 0},
	// glitches
	{"RGB Split", 0, G_CHROMA, MOVES},
	{"VHS", 90, G_VHS, MOVES},
	{"Datamosh", 0, G_MOSH, MOVES},
	{"Pixel Sort", 0, G_SORT},
	{"Tear", 0, G_TEAR, MOVES},
	{"Mosaic", 0, G_MOSAIC},
	{"CRT", 150, G_CRT},
	{"Bit Rot", 0, G_BITROT, MOVES},
	{"Echo", 40, G_ECHO},
	{"Night Vision", 200, G_NIGHT, MOVES},
	{"Melt", 0, G_MELT, MOVES},
	{"Interlace", 0, G_INTERLACE},
	{"Circuit", 0, G_CIRCUIT, MOVES},
	{"Prism", 0, G_PRISM, MOVES},
	{"Ghost", 30, G_GHOST, MOVES},
	{"Trails", 0, G_TRAILS},
	{"Bleed", 60, G_BLEED},
	{"Wobble", 0, G_WOBBLE, MOVES},
	{"Signal Loss", 0, G_SIGNAL, MOVES},
	{"Hologram", 120, G_HOLO, MOVES},
	{"Broadcast", 0, G_BROADCAST, MOVES},
	{"VHS Worn", 140, G_WORN, MOVES},
	{"Anaglyph", 0, G_ANAGLYPH},
	{"Smear", 0, G_SMEAR, MOVES},
	{"Swirl", 0, G_SWIRL, MOVES},
	{"Compress", 0, G_COMPRESS},
	{"Drift", 0, G_DRIFT, MOVES},
	{"Keyframe", 0, G_KEYFRAME, MOVES},
};

// The filter menu's groups, in order; every filter is in one.
static const struct
{
	const char *name;
	int count;
	u8 items[12];
} groups[] = {
	{"Classic", 11, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10}},
	{"Color", 11, {11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21}},
	{"RGB & Ghost", 6, {22, 35, 36, 37, 30, 44}},
	{"Tape & Screen", 8, {23, 43, 38, 39, 28, 33, 41, 31}},
	{"Data Mosh", 8, {24, 45, 46, 49, 47, 48, 40, 42}},
	{"Bits & Pixels", 6, {25, 29, 34, 26, 32, 27}},
};
#define GROUP_COUNT (int)(sizeof(groups) / sizeof(groups[0]))

int filterGroupCount(void) { return GROUP_COUNT; }
const char *filterGroupName(int g) { return groups[g].name; }
int filterGroupSize(int g) { return groups[g].count; }
int filterGroupItem(int g, int i) { return groups[g].items[i]; }

int filterNext(int filter, int step)
{
	// Position in menu order, then step through it.
	int order[FILTER_COUNT], n = 0, at = 0;
	for (int g = 0; g < GROUP_COUNT; g++)
		for (int i = 0; i < groups[g].count; i++)
		{
			if (groups[g].items[i] == filter)
				at = n;
			order[n++] = groups[g].items[i];
		}
	return order[(at + step % n + n) % n];
}

bool filterMoves(int filter) { return filter > 0 && filter < FILTER_COUNT && defs[filter].moves; }

int filterLoopFrames(int filter, int n)
{
	// At least 8 steps, whole wiggle loops, so the glitch has time to move.
	return filterMoves(filter) && n < 8 ? (8 + n - 1) / n * n : n;
}

// Built on first use (or by the export thread in the background after
// start), so the app starts without waiting for all of them.
static u16 luts[FILTER_COUNT][65536];
static volatile bool built[FILTER_COUNT];
#ifdef __3DS__
#include <3ds/synchronization.h>
static LightLock lock;
#define LOCK() LightLock_Lock(&lock)
#define UNLOCK() LightLock_Unlock(&lock)
#else
#define LOCK()
#define UNLOCK()
#endif

#define R5(p) ((p) >> 11)
#define G6(p) ((p) >> 5 & 63)
#define B5(p) ((p) & 31)
#define RGB(r, g, b) (u16)((r) << 11 | (g) << 5 | (b))

static float clampf(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static float luma(float r, float g, float b) { return 0.299f * r + 0.587f * g + 0.114f * b; }
static float contrast(float v, float k) { return clampf((v - 0.5f) * k + 0.5f); }
static float smooth(float v) { return v * v * (3 - 2 * v); }

static void saturate(float *r, float *g, float *b, float s)
{
	float y = luma(*r, *g, *b);
	*r = clampf(y + (*r - y) * s);
	*g = clampf(y + (*g - y) * s);
	*b = clampf(y + (*b - y) * s);
}

static void apply(int filter, float *r, float *g, float *b)
{
	float y;
	switch (filter)
	{
	case 1: // Vivid: richer color, a touch more contrast
		*r = contrast(*r, 1.08f);
		*g = contrast(*g, 1.08f);
		*b = contrast(*b, 1.08f);
		saturate(r, g, b, 1.4f);
		break;
	case 2: // Warm: golden light
		*r = clampf(*r * 1.06f + 0.03f);
		*g = clampf(*g * 1.01f + 0.01f);
		*b = clampf(*b * 0.86f);
		saturate(r, g, b, 1.08f);
		break;
	case 3: // Cool: crisp blue
		*r = clampf(*r * 0.9f);
		*g = clampf(*g * 0.99f + 0.01f);
		*b = clampf(*b * 1.08f + 0.04f);
		break;
	case 4: // Dream: lifted shadows with a purple haze
		saturate(r, g, b, 0.85f);
		*r = clampf(0.10f + *r * 0.88f);
		*g = clampf(0.04f + *g * 0.84f);
		*b = clampf(0.13f + *b * 0.85f);
		break;
	case 5: // Cinema: teal shadows, orange highlights
		y = luma(*r, *g, *b) - 0.5f;
		*r = contrast(clampf(*r + y * 0.16f), 1.1f);
		*g = contrast(clampf(*g + y * 0.03f), 1.1f);
		*b = contrast(clampf(*b - y * 0.16f), 1.1f);
		break;
	case 6: // Vintage: faded blacks, warm and muted
		saturate(r, g, b, 0.75f);
		*r = clampf(0.10f + *r * 0.93f);
		*g = clampf(0.09f + *g * 0.85f);
		*b = clampf(0.12f + *b * 0.72f);
		break;
	case 7: // Lomo: punchy contrast and color, dark corners
		*r = smooth(*r) * 0.7f + *r * 0.3f;
		*g = smooth(*g) * 0.7f + *g * 0.3f;
		*b = smooth(*b) * 0.7f + *b * 0.3f;
		saturate(r, g, b, 1.3f);
		break;
	case 8: // Sepia
		y = luma(*r, *g, *b);
		*r = clampf(y * 1.07f + 0.08f);
		*g = clampf(y * 0.95f + 0.03f);
		*b = clampf(y * 0.78f);
		break;
	case 9: // Mono
		*r = *g = *b = contrast(luma(*r, *g, *b), 1.1f);
		break;
	case 10: // Noir: hard black and white
		*r = *g = *b = smooth(smooth(luma(*r, *g, *b)));
		break;
	case 11: // Chrome: saturated slide film, deep reds
		*r = contrast(clampf(*r * 1.05f), 1.15f);
		*g = contrast(*g, 1.12f);
		*b = contrast(clampf(*b * 0.95f), 1.12f);
		saturate(r, g, b, 1.25f);
		break;
	case 12: // Bleach: bleach bypass, silvery and hard
		saturate(r, g, b, 0.45f);
		*r = contrast(*r, 1.3f);
		*g = contrast(*g, 1.3f);
		*b = contrast(*b, 1.3f);
		break;
	case 13: // Cross: cross-processed, yellow highlights, blue shadows
		*r = clampf(smooth(*r) * 1.05f);
		*g = clampf(*g * 1.04f + 0.02f);
		*b = clampf(*b * 0.7f + 0.12f);
		saturate(r, g, b, 1.2f);
		break;
	case 14: // Pastel: soft, bright, light colors
		saturate(r, g, b, 0.7f);
		*r = clampf(0.18f + *r * 0.8f);
		*g = clampf(0.16f + *g * 0.8f);
		*b = clampf(0.2f + *b * 0.78f);
		break;
	case 15: // Duotone: deep blue to pink
		y = smooth(luma(*r, *g, *b));
		*r = 0.12f + y * 0.88f;
		*g = 0.08f + y * 0.42f;
		*b = 0.36f + y * 0.26f;
		break;
	case 16: // Infrared: green turns pink, like false-color film
		y = *r;
		*r = clampf(*g * 1.15f + 0.05f);
		*g = clampf(y * 0.55f);
		*b = clampf(*b * 0.95f + 0.05f);
		saturate(r, g, b, 1.2f);
		break;
	case 17: // Thermal: brightness as a heat map
	{
		y = luma(*r, *g, *b);
		static const float stops[6][3] = {{0, 0, 0.1f}, {0.2f, 0, 0.6f}, {0.8f, 0, 0.5f}, {1, 0.4f, 0}, {1, 0.85f, 0}, {1, 1, 1}};
		float t = y * 5;
		int i = t >= 5 ? 4 : (int)t;
		t -= i;
		*r = stops[i][0] + (stops[i + 1][0] - stops[i][0]) * t;
		*g = stops[i][1] + (stops[i + 1][1] - stops[i][1]) * t;
		*b = stops[i][2] + (stops[i + 1][2] - stops[i][2]) * t;
		break;
	}
	case 18: // Acid: the colors turned around, loud
	{
		float t = *r;
		*r = *b;
		*b = *g;
		*g = t;
		saturate(r, g, b, 1.8f);
		break;
	}
	case 19: // Solar: solarized, bright parts flipped
		*r = *r > 0.55f ? (1 - *r) * 1.6f : *r * 1.1f;
		*g = *g > 0.55f ? (1 - *g) * 1.6f : *g * 1.1f;
		*b = *b > 0.45f ? (1 - *b) * 1.4f : *b * 1.2f;
		*r = clampf(*r);
		*g = clampf(*g);
		*b = clampf(*b);
		break;
	case 20: // Game Boy: four greens
	{
		static const float pal[4][3] = {{0.06f, 0.22f, 0.06f}, {0.19f, 0.38f, 0.19f}, {0.55f, 0.67f, 0.06f}, {0.61f, 0.74f, 0.06f}};
		int i = (int)(contrast(luma(*r, *g, *b), 1.2f) * 3.99f);
		*r = pal[i][0];
		*g = pal[i][1];
		*b = pal[i][2];
		break;
	}
	// 21 Bent and 34 Circuit work on the bits, see bendBits.
	case 22: // RGB Split
		*r = contrast(*r, 1.05f);
		*g = contrast(*g, 1.05f);
		*b = contrast(*b, 1.05f);
		break;
	case 23: // VHS: bleeding color, lifted blacks
		saturate(r, g, b, 1.25f);
		*r = clampf(0.05f + *r * 0.92f);
		*g = clampf(0.04f + *g * 0.9f);
		*b = clampf(0.07f + *b * 0.9f);
		break;
	case 24: // Datamosh
	case 32: // Melt
	case 45: // Smear
	case 46: // Swirl
	case 49: // Keyframe
		saturate(r, g, b, 1.3f);
		break;
	case 26: // Tear: cold digital
		*r = clampf(*r * 0.92f);
		*b = clampf(*b * 1.06f + 0.02f);
		break;
	case 28: // CRT: glowing phosphor
		saturate(r, g, b, 1.2f);
		*r = clampf(*r * 1.08f);
		*g = clampf(*g * 1.08f);
		*b = clampf(*b * 1.08f);
		break;
	case 30: // Echo: dreamy
		saturate(r, g, b, 0.9f);
		*r = clampf(0.05f + *r * 0.95f);
		*b = clampf(0.08f + *b * 0.92f);
		break;
	case 35: // Prism
	case 39: // Wobble
		*r = contrast(*r, 1.1f);
		*g = contrast(*g, 1.1f);
		*b = contrast(*b, 1.1f);
		saturate(r, g, b, 1.15f);
		break;
	case 36: // Ghost: pale and cold
		saturate(r, g, b, 0.8f);
		*r = clampf(0.04f + *r * 0.92f);
		*b = clampf(0.08f + *b * 0.95f);
		break;
	case 38: // Bleed
	case 43: // VHS Worn: washed out tape
		saturate(r, g, b, 0.85f);
		*r = clampf(0.08f + *r * 0.88f);
		*g = clampf(0.07f + *g * 0.86f);
		*b = clampf(0.1f + *b * 0.82f);
		break;
	case 41: // Hologram: cyan light
		y = luma(*r, *g, *b);
		*r = clampf(y * 0.35f);
		*g = clampf(0.05f + y * 0.9f);
		*b = clampf(0.15f + y * 1.0f);
		break;
	case 44: // Anaglyph: nearly gray, so the red and cyan show
		saturate(r, g, b, 0.25f);
		break;
	case 31: // Night Vision: green phosphor
		y = contrast(luma(*r, *g, *b), 1.3f);
		*r = y * 0.16f;
		*g = clampf(0.05f + y * 0.82f);
		*b = y * 0.14f;
		break;
	}
}

// Circuit bending: colors made by mixing the bits of the channels, like a
// shorted color bus.
static u16 bendBits(int filter, u16 c)
{
	int r = c >> 11, g = c >> 5 & 63, b = c & 31;
	if (filter == 21) // Bent: each channel xor-ed with its neighbor
		return (r ^ g >> 1) << 11 | (g ^ (b << 1)) << 5 | (b ^ r);
	// Circuit: channels swapped, green rotated by two bits
	int g2 = (g << 2 | g >> 4) & 63;
	return b << 11 | g2 << 5 | ((r ^ g >> 1) & 31);
}

static void build(int f)
{
	for (int c = 0; c < 65536; c++)
	{
		if (f == 21 || f == 34)
		{
			luts[f][c] = bendBits(f, c);
			continue;
		}
		float r = (c >> 11) / 31.0f, g = ((c >> 5) & 63) / 63.0f, b = (c & 31) / 31.0f;
		apply(f, &r, &g, &b);
		luts[f][c] = ((int)(r * 31 + 0.5f) << 11) | ((int)(g * 63 + 0.5f) << 5) | (int)(b * 31 + 0.5f);
	}
}

void filterInit(void)
{
#ifdef __3DS__
	LightLock_Init(&lock);
#endif
}

static const u16 *lutFor(int f)
{
	if (!built[f])
	{
		LOCK();
		if (!built[f])
		{
			build(f);
			__sync_synchronize(); // the table before the flag
			built[f] = true;
		}
		UNLOCK();
	}
	return luts[f];
}

void filterPrebuild(void)
{
	for (int f = 1; f < FILTER_COUNT; f++)
		lutFor(f);
}

const char *filterName(int filter) { return defs[filter].name; }
const u16 *filterLut(int filter) { return filter > 0 && filter < FILTER_COUNT ? lutFor(filter) : NULL; }
int filterVignette(int filter) { return filter >= 0 && filter < FILTER_COUNT ? defs[filter].vignette : 0; }
bool filterHasGlitch(int filter) { return filter > 0 && filter < FILTER_COUNT && defs[filter].glitch; }

void filterApply(u16 *dst, const u16 *img, int stride, int x, int y, int w, int h, int filter, int frame, int strength)
{
	const u16 *lut = filterLut(filter);
	for (int row = 0; row < h; row++)
	{
		const u16 *src = img + (y + row) * stride + x;
		u16 *out = dst + row * w;
		for (int col = 0; col < w; col++)
			out[col] = lut ? lut[src[col]] : src[col];
	}
	filterGlitch(dst, w, h, filter, frame);
	if (lut && strength < 100)
	{
		// Part of the way: mixed with the original.
		int t = (strength < 0 ? 0 : strength) * 256 / 100;
		for (int row = 0; row < h; row++)
		{
			const u16 *src = img + (y + row) * stride + x;
			u16 *out = dst + row * w;
			for (int col = 0; col < w; col++)
			{
				u16 a = src[col], b = out[col];
				out[col] = RGB(R5(a) + ((R5(b) - R5(a)) * t >> 8), G6(a) + ((G6(b) - G6(a)) * t >> 8),
				               B5(a) + ((B5(b) - B5(a)) * t >> 8));
			}
		}
	}
	int v = filterVignette(filter) * (strength < 100 ? strength : 100) / 100;
	if (v)
		vignetteRows(dst, w, h, v);
}

// ---- glitches ----

#define MAXDIM 1024

static u32 next(u32 *s)
{
	*s ^= *s << 13;
	*s ^= *s >> 17;
	*s ^= *s << 5;
	return *s;
}

// Random in [0, n).
static int rnd(u32 *s, int n) { return n > 0 ? (int)(next(s) % (u32)n) : 0; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int maxi(int a, int b) { return a > b ? a : b; }

static u16 darkenPx(u16 p, int k) // k/256 of the brightness
{
	return RGB(R5(p) * k >> 8, G6(p) * k >> 8, B5(p) * k >> 8);
}

static u16 blendPx(u16 a, u16 b) { return RGB((R5(a) + R5(b)) >> 1, (G6(a) + G6(b)) >> 1, (B5(a) + B5(b)) >> 1); }

// A row shifted sideways by d (edges repeat), with red and blue pulled
// apart by split.
static void shiftRow(u16 *row, int w, int d, int split)
{
	u16 tmp[MAXDIM];
	memcpy(tmp, row, w * sizeof(u16));
	for (int x = 0; x < w; x++)
	{
		u16 c = tmp[clampi(x - d, 0, w - 1)];
		if (split)
		{
			u16 l = tmp[clampi(x - d - split, 0, w - 1)], r = tmp[clampi(x - d + split, 0, w - 1)];
			c = RGB(R5(l), G6(c), B5(r));
		}
		row[x] = c;
	}
}

// Scanlines about every two lines of a 240 line screen.
static void scanlines(u16 *img, int w, int h, int k)
{
	for (int y = 0; y < h; y++)
		if ((y * 240 / h) & 1)
			for (int x = 0; x < w; x++)
				img[y * w + x] = darkenPx(img[y * w + x], k);
}

static void pixelSort(u16 *img, int w, int h)
{
	// Runs of bright pixels in each row, sorted dark to bright.
	u16 tmp[MAXDIM];
	for (int y = 0; y < h; y++)
	{
		u16 *row = img + y * w;
		for (int x = 0; x < w;)
		{
			if (G6(row[x]) < 30)
			{
				x++;
				continue;
			}
			int a = x;
			while (x < w && G6(row[x]) >= 24)
				x++;
			int count[64] = {0}, n = x - a;
			for (int i = a; i < x; i++)
				count[G6(row[i])]++;
			for (int v = 1; v < 64; v++)
				count[v] += count[v - 1];
			for (int i = x - 1; i >= a; i--)
				tmp[--count[G6(row[i])]] = row[i];
			memcpy(row + a, tmp, n * sizeof(u16));
		}
	}
}

static void datamosh(u16 *img, int w, int h, u32 *s)
{
	int bs = maxi(4, w / 24);
	for (int by = 0; by + bs <= h; by += bs)
		for (int bx = 0; bx + bs <= w; bx += bs)
		{
			int r = rnd(s, 100);
			if (r < 12 && bx >= bs)
			{
				// The block to the left, slipped down: a motion vector gone wrong.
				int sy = clampi(by - bs / 2, 0, h - bs);
				for (int y = 0; y < bs; y++)
					memmove(img + (by + y) * w + bx, img + (sy + y) * w + bx - bs, bs * sizeof(u16));
			}
			else if (r < 22)
			{
				// Its top row dragged down.
				for (int y = 1; y < bs; y++)
					memcpy(img + (by + y) * w + bx, img + by * w + bx, bs * sizeof(u16));
			}
			else if (r < 27)
			{
				// One flat color, like a dropped macroblock.
				u16 c = img[(by + bs / 2) * w + bx + bs / 2];
				for (int y = 0; y < bs; y++)
					for (int x = 0; x < bs; x++)
						img[(by + y) * w + bx + x] = c;
			}
		}
}

static void mosaic(u16 *img, int w, int h)
{
	int bs = maxi(2, w / 40);
	for (int by = 0; by < h; by += bs)
		for (int bx = 0; bx < w; bx += bs)
		{
			int bw = bx + bs > w ? w - bx : bs, bh = by + bs > h ? h - by : bs;
			u32 r = 0, g = 0, b = 0, n = bw * bh;
			for (int y = 0; y < bh; y++)
				for (int x = 0; x < bw; x++)
				{
					u16 p = img[(by + y) * w + bx + x];
					r += R5(p);
					g += G6(p);
					b += B5(p);
				}
			u16 c = RGB(r / n, g / n, b / n);
			for (int y = 0; y < bh; y++)
				for (int x = 0; x < bw; x++)
					img[(by + y) * w + bx + x] = c;
		}
}

static void crt(u16 *img, int w, int h)
{
	scanlines(img, w, h, 150);
	// Aperture grille: each column favors one of red, green, blue.
	int q = maxi(1, w / 320);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
		{
			u16 p = img[y * w + x];
			int k = (x / q) % 3, r = R5(p), g = G6(p), b = B5(p);
			r = k == 0 ? r : r * 3 / 4;
			g = k == 1 ? g : g * 3 / 4;
			b = k == 2 ? b : b * 3 / 4;
			img[y * w + x] = RGB(r, g, b);
		}
}

static void bitRot(u16 *img, int w, int h, u32 *s, int streaks)
{
	for (int i = 0; i < streaks; i++)
	{
		int y0 = rnd(s, h), x0 = rnd(s, w), len = w / 8 + rnd(s, w / 2), th = 1 + rnd(s, maxi(1, h / 60));
		int mode = rnd(s, 3);
		u16 mask = (u16)(next(s) | 0x8410);
		for (int y = y0; y < y0 + th && y < h; y++)
			for (int x = x0; x < x0 + len && x < w; x++)
			{
				u16 *p = img + y * w + x;
				*p = mode == 0 ? *p ^ mask : mode == 1 ? img[y * w + x0] : (u16)(*p << 1);
			}
	}
}

static void melt(u16 *img, int w, int h, u32 *s)
{
	// Each column runs down by a smoothly varying amount.
	u16 col[MAXDIM];
	float p1 = rnd(s, 628) / 100.0f, p2 = rnd(s, 628) / 100.0f;
	for (int x = 0; x < w; x++)
	{
		float u = (float)x / w;
		float k = (0.5f + 0.5f * sinf(u * 9.0f + p1)) * (0.6f + 0.4f * sinf(u * 31.0f + p2));
		int d = (int)(k * k * h / 5);
		if (d <= 0)
			continue;
		for (int y = 0; y < h; y++)
			col[y] = img[y * w + x];
		for (int y = 0; y < h; y++)
			img[y * w + x] = col[y >= d ? y - d : 0];
	}
}

// Channels sampled from other places: red from (rx, ry) away, blue from
// (bx, by); needs a copy of the whole image.
static void splitChannels(u16 *img, int w, int h, int rx, int ry, int bx, int by)
{
	u16 *src = malloc(w * h * sizeof(u16));
	if (!src)
		return;
	memcpy(src, img, w * h * sizeof(u16));
	for (int y = 0; y < h; y++)
	{
		const u16 *rr = src + clampi(y - ry, 0, h - 1) * w, *br = src + clampi(y - by, 0, h - 1) * w;
		for (int x = 0; x < w; x++)
			img[y * w + x] = RGB(R5(rr[clampi(x - rx, 0, w - 1)]), G6(src[y * w + x]), B5(br[clampi(x - bx, 0, w - 1)]));
	}
	free(src);
}

// A second, lighter copy (screen blend) at (dx, dy), tinted.
static void ghost(u16 *img, int w, int h, int dx, int dy)
{
	u16 *src = malloc(w * h * sizeof(u16));
	if (!src)
		return;
	memcpy(src, img, w * h * sizeof(u16));
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
		{
			u16 a = src[y * w + x], b = src[clampi(y - dy, 0, h - 1) * w + clampi(x - dx, 0, w - 1)];
			// The copy a bit blue: screen of a with 60 % of b.
			int r = R5(b) * 5 / 10, g = G6(b) * 6 / 10, bl = B5(b) * 7 / 10;
			r = R5(a) + r - R5(a) * r / 31;
			g = G6(a) + g - G6(a) * g / 63;
			bl = B5(a) + bl - B5(a) * bl / 31;
			img[y * w + x] = RGB(r, g, bl);
		}
	free(src);
}

// Color smeared to the right while brightness stays sharp, like tape.
static void bleed(u16 *img, int w, int h, int radius, int shift)
{
	int cr[MAXDIM], cb[MAXDIM], yy[MAXDIM];
	for (int y = 0; y < h; y++)
	{
		u16 *row = img + y * w;
		for (int x = 0; x < w; x++)
		{
			int r = R5(row[x]) << 1, g = G6(row[x]), b = B5(row[x]) << 1; // all 0..63
			yy[x] = (r + 2 * g + b) >> 2;
			cr[x] = r - yy[x];
			cb[x] = b - yy[x];
		}
		// Box blur of the color difference, read from a little to the left.
		int sr = 0, sb = 0, n = 2 * radius + 1;
		for (int k = -radius; k <= radius; k++)
		{
			int i = clampi(k - shift, 0, w - 1);
			sr += cr[i];
			sb += cb[i];
		}
		for (int x = 0; x < w; x++)
		{
			int r = yy[x] + sr / n, b = yy[x] + sb / n, g = (4 * yy[x] - r - b) >> 1;
			row[x] = RGB(clampi(r, 0, 63) >> 1, clampi(g, 0, 63), clampi(b, 0, 63) >> 1);
			int out = clampi(x - radius - shift, 0, w - 1), in = clampi(x + radius + 1 - shift, 0, w - 1);
			sr += cr[in] - cr[out];
			sb += cb[in] - cb[out];
		}
	}
}

static void wobble(u16 *img, int w, int h, u32 *s, int amp, int split)
{
	float phase = rnd(s, 628) / 100.0f;
	for (int y = 0; y < h; y++)
	{
		float v = (float)y / h;
		int d = (int)((sinf(v * 37.7f + phase) * 0.7f + sinf(v * 113.0f + phase * 2) * 0.3f) * amp);
		shiftRow(img + y * w, w, d, split);
	}
}

static void signalLoss(u16 *img, int w, int h, u32 *s)
{
	for (int i = 0; i < 9; i++)
	{
		int y0 = rnd(s, h), bh = maxi(1, h / 40 + rnd(s, h / 14)), mode = rnd(s, 4);
		for (int y = y0; y < y0 + bh && y < h; y++)
		{
			u16 *row = img + y * w;
			if (mode == 0) // static
				for (int x = 0; x < w; x++)
				{
					int v = rnd(s, 32);
					row[x] = RGB(v, v * 2, v);
				}
			else if (mode == 1) // the band's first line, again and again
				memcpy(row, img + y0 * w, w * sizeof(u16));
			else if (mode == 2) // gone dark
				for (int x = 0; x < w; x++)
					row[x] = darkenPx(row[x], 40);
			else // slipped and inverted
			{
				shiftRow(row, w, w / 10, 0);
				for (int x = 0; x < w; x++)
					row[x] = ~row[x];
			}
		}
	}
}

static void broadcast(u16 *img, int w, int h, u32 *s)
{
	for (int i = 0; i < 7; i++)
	{
		int y0 = rnd(s, h), bh = h / 24 + rnd(s, h / 9), mode = rnd(s, 4), d = (rnd(s, 2) ? 1 : -1) * (w / 30 + rnd(s, w / 12));
		for (int y = y0; y < y0 + bh && y < h; y++)
		{
			u16 *row = img + y * w;
			if (mode == 0) // red and blue swapped
				for (int x = 0; x < w; x++)
					row[x] = RGB(B5(row[x]), G6(row[x]), R5(row[x]));
			else if (mode == 1)
				shiftRow(row, w, d, maxi(2, w / 40));
			else if (mode == 2) // posterized
				for (int x = 0; x < w; x++)
					row[x] = row[x] & 0xE738;
			else
				for (int x = 0; x < w; x++)
					row[x] = ~row[x];
		}
	}
}

// Datamosh: blocks dragged down in streams, each a little to the side of the
// one above, as when motion is applied to the wrong picture.
static void moshSmear(u16 *img, int w, int h, u32 *s)
{
	int bs = maxi(4, w / 24);
	for (int bx = 0; bx + bs <= w; bx += bs)
	{
		if (rnd(s, 100) >= 40)
			continue;
		int start = rnd(s, h / bs) * bs + bs, len = (2 + rnd(s, 6)) * bs, d = rnd(s, bs / 2 + 1) - bs / 4;
		for (int y = start; y < start + len && y < h; y++)
		{
			const u16 *src = img + (y - bs) * w;
			u16 *dst = img + y * w;
			for (int x = bx; x < bx + bs; x++)
				dst[x] = src[clampi(x - d, 0, w - 1)];
		}
	}
}

// Datamosh: every block taken from where a swirling motion field points.
static void moshSwirl(u16 *img, int w, int h, u32 *s)
{
	u16 *src = malloc(w * h * sizeof(u16));
	if (!src)
		return;
	memcpy(src, img, w * h * sizeof(u16));
	int bs = maxi(4, w / 32);
	float p = rnd(s, 628) / 100.0f, q = rnd(s, 628) / 100.0f;
	for (int by = 0; by < h; by += bs)
		for (int bx = 0; bx < w; bx += bs)
		{
			float u = (float)bx / w, v = (float)by / h;
			float a = sinf(u * 7 + p) * 2.2f + cosf(v * 6 + q) * 2.2f;
			float m = 0.5f + 0.5f * sinf(u * 3 + v * 4 + p);
			int dx = (int)(cosf(a) * m * bs * 1.6f), dy = (int)(sinf(a) * m * bs * 1.6f);
			for (int y = by; y < by + bs && y < h; y++)
				for (int x = bx; x < bx + bs && x < w; x++)
					img[y * w + x] = src[clampi(y + dy, 0, h - 1) * w + clampi(x + dx, 0, w - 1)];
		}
	free(src);
}

// Heavy compression: each block keeps its average and a few steps around it,
// with color over even bigger blocks, and some blocks flat.
static void compress(u16 *img, int w, int h, u32 *s)
{
	int bs = maxi(4, w / 54);
	for (int by = 0; by < h; by += bs)
		for (int bx = 0; bx < w; bx += bs)
		{
			int bw = bx + bs > w ? w - bx : bs, bh = by + bs > h ? h - by : bs, n = bw * bh;
			int r = 0, g = 0, b = 0;
			for (int y = 0; y < bh; y++)
				for (int x = 0; x < bw; x++)
				{
					u16 p = img[(by + y) * w + bx + x];
					r += R5(p);
					g += G6(p);
					b += B5(p);
				}
			r /= n;
			g /= n;
			b /= n;
			bool flat = rnd(s, 100) < 6;
			for (int y = 0; y < bh; y++)
				for (int x = 0; x < bw; x++)
				{
					u16 *p = img + (by + y) * w + bx + x;
					int dg = flat ? 0 : (G6(*p) - g) / 14 * 14; // brightness in coarse steps
					*p = RGB(clampi(r + dg / 2, 0, 31), clampi(g + dg, 0, 63), clampi(b + dg / 2, 0, 31));
				}
		}
}

static void shiftRowWrap(u16 *row, int w, int d)
{
	u16 tmp[MAXDIM];
	memcpy(tmp, row, w * sizeof(u16));
	d = ((d % w) + w) % w;
	for (int x = 0; x < w; x++)
		row[x] = tmp[(x - d + w) % w];
}

// Bands of rows, each slipped a step further, wrapping around.
static void drift(u16 *img, int w, int h, u32 *s)
{
	int band = maxi(2, h / 30), off = 0, step = w / 50 + rnd(s, w / 30);
	for (int y = 0; y < h; y++)
	{
		if (y % band == 0)
		{
			if (rnd(s, 6) == 0)
				step = -step;
			off += rnd(s, 3) ? step : 0;
		}
		if (off)
			shiftRowWrap(img + y * w, w, off);
	}
}

// A keyframe went missing: in wide patches the picture is the same picture
// moved, and where both meet, their difference shows.
static void keyframe(u16 *img, int w, int h, u32 *s)
{
	u16 *src = malloc(w * h * sizeof(u16));
	if (!src)
		return;
	memcpy(src, img, w * h * sizeof(u16));
	int bs = maxi(4, w / 24), mx = w / 9 + rnd(s, w / 9), my = rnd(s, h / 10);
	float p = rnd(s, 628) / 100.0f;
	for (int by = 0; by < h; by += bs)
		for (int bx = 0; bx < w; bx += bs)
		{
			float u = (float)bx / w, v = (float)by / h;
			float f = sinf(u * 5 + p) + sinf(v * 4 - p * 0.7f) + sinf((u + v) * 3 + p * 1.3f);
			if (f < 0.2f)
				continue;
			bool edge = f < 0.7f;
			for (int y = by; y < by + bs && y < h; y++)
				for (int x = bx; x < bx + bs && x < w; x++)
				{
					u16 m = src[((y + my) % h) * w + (x + mx) % w], o = src[y * w + x];
					img[y * w + x] = edge ? RGB(abs(R5(m) - R5(o)) * 2 > 31 ? 31 : abs(R5(m) - R5(o)) * 2,
					                            abs(G6(m) - G6(o)) * 2 > 63 ? 63 : abs(G6(m) - G6(o)) * 2,
					                            abs(B5(m) - B5(o)) * 2 > 31 ? 31 : abs(B5(m) - B5(o)) * 2)
					                      : m;
				}
		}
	free(src);
}

void filterGlitch(u16 *img, int w, int h, int filter, int frame)
{
	if (!filterHasGlitch(filter) || w > MAXDIM || h > MAXDIM)
		return;
	if (!defs[filter].moves)
		frame = 0;
	u32 seed = 0x9E3779B9u ^ (u32)filter * 7919u ^ (u32)frame * 0x27D4EB2Du, *s = &seed;
	if (!seed)
		seed = 1;
	int pulse = 2 + (frame & 3) % 3; // 2..4, for splits that breathe
	switch (defs[filter].glitch)
	{
	case G_CHROMA:
		for (int y = 0; y < h; y++)
			shiftRow(img + y * w, w, 0, maxi(2, w * pulse / 110));
		break;
	case G_VHS:
	{
		// Bands that slip sideways, color bleeding, scanlines.
		int band[4][3];
		for (int i = 0; i < 4; i++)
		{
			band[i][0] = rnd(s, h);
			band[i][1] = h / 30 + rnd(s, h / 12);
			band[i][2] = (rnd(s, 2) ? 1 : -1) * (w / 40 + rnd(s, w / 16));
		}
		for (int y = 0; y < h; y++)
		{
			int d = 0;
			for (int i = 0; i < 4; i++)
				if (y >= band[i][0] && y < band[i][0] + band[i][1])
					d = band[i][2] + rnd(s, 3) * w / 320;
			shiftRow(img + y * w, w, d, maxi(1, w / 120));
		}
		scanlines(img, w, h, 215);
		// A tracking line near the bottom.
		int ty = h - h / 8 - rnd(s, h / 6);
		for (int y = ty; y < ty + maxi(1, h / 60) && y < h; y++)
			for (int x = 0; x < w; x++)
				if (rnd(s, 3))
					img[y * w + x] = blendPx(img[y * w + x], 0xFFFF);
		break;
	}
	case G_MOSH:
		datamosh(img, w, h, s);
		break;
	case G_SORT:
		pixelSort(img, w, h);
		break;
	case G_TEAR:
	{
		// A few waving bands, and one hard tear the lower part slips at.
		int tear = h / 3 + rnd(s, h / 3), slip = w / 12;
		float phase = rnd(s, 628) / 100.0f;
		int b0 = rnd(s, h / 2), b1 = h / 2 + rnd(s, h / 2);
		for (int y = 0; y < h; y++)
		{
			float v = (float)y / h;
			int d = y >= tear ? slip : 0;
			bool inBand = (y >= b0 && y < b0 + h / 8) || (y >= b1 && y < b1 + h / 10);
			if (inBand)
				d += (int)(sinf(v * 60 + phase) * w / 40);
			shiftRow(img + y * w, w, d, inBand ? maxi(1, w / 120) : 0);
		}
		break;
	}
	case G_MOSAIC:
		mosaic(img, w, h);
		break;
	case G_CRT:
		crt(img, w, h);
		break;
	case G_BITROT:
		bitRot(img, w, h, s, 22);
		break;
	case G_ECHO:
	{
		// The picture again, a little to the side and half see-through.
		u16 tmp[MAXDIM];
		int d = maxi(3, w / 14);
		for (int y = 0; y < h; y++)
		{
			u16 *row = img + y * w;
			memcpy(tmp, row, w * sizeof(u16));
			for (int x = 0; x < w; x++)
				row[x] = blendPx(tmp[x], tmp[clampi(x - d, 0, w - 1)]);
		}
		break;
	}
	case G_NIGHT:
		scanlines(img, w, h, 200);
		// Grain at the same places at any size.
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++)
			{
				u32 n = (u32)(x * 640 / w) * 73856093u ^ (u32)(y * 480 / h) * 19349663u;
				n = (n ^ n >> 13) * 1274126177u;
				int g = G6(img[y * w + x]) + (int)(n >> 28) - 8;
				img[y * w + x] = RGB(R5(img[y * w + x]), clampi(g, 0, 63), B5(img[y * w + x]));
			}
		break;
	case G_MELT:
		melt(img, w, h, s);
		break;
	case G_INTERLACE:
		for (int y = 0; y < h; y++)
			if ((y * 240 / h) & 1)
				shiftRow(img + y * w, w, maxi(2, w / 40), 0);
		break;
	case G_CIRCUIT:
		bitRot(img, w, h, s, 10);
		break;
	case G_PRISM: // red up and left, blue down and right
		splitChannels(img, w, h, -maxi(2, w * pulse / 90), -maxi(1, h / 70), maxi(2, w * pulse / 90), maxi(1, h / 70));
		break;
	case G_GHOST:
		ghost(img, w, h, maxi(3, w * (8 + (frame & 3)) / 100), maxi(1, h / 30));
		break;
	case G_TRAILS:
	{
		// Three fading copies, further and further to the left.
		u16 tmp[MAXDIM];
		int d = maxi(2, w / 30);
		for (int y = 0; y < h; y++)
		{
			u16 *row = img + y * w;
			memcpy(tmp, row, w * sizeof(u16));
			for (int x = 0; x < w; x++)
			{
				u16 p0 = tmp[x], p1 = tmp[clampi(x - d, 0, w - 1)], p2 = tmp[clampi(x - 2 * d, 0, w - 1)],
				    p3 = tmp[clampi(x - 3 * d, 0, w - 1)];
				int r = (R5(p0) * 8 + R5(p1) * 4 + R5(p2) * 3 + R5(p3) * 1) >> 4;
				int g = (G6(p0) * 8 + G6(p1) * 4 + G6(p2) * 3 + G6(p3) * 1) >> 4;
				int b = (B5(p0) * 8 + B5(p1) * 4 + B5(p2) * 3 + B5(p3) * 1) >> 4;
				row[x] = RGB(r, g, b);
			}
		}
		break;
	}
	case G_BLEED:
		bleed(img, w, h, maxi(2, w / 40), maxi(1, w / 100));
		break;
	case G_WOBBLE:
		wobble(img, w, h, s, maxi(2, w / 70), maxi(1, w / 110));
		break;
	case G_SIGNAL:
		signalLoss(img, w, h, s);
		break;
	case G_HOLO:
		for (int y = 0; y < h; y++)
		{
			// Flickering bright lines here and there, a slight jitter.
			int d = rnd(s, 12) == 0 ? (rnd(s, 2) ? 1 : -1) * maxi(1, w / 100) : 0;
			shiftRow(img + y * w, w, d, maxi(1, w / 160));
			if (rnd(s, 9) == 0)
				for (int x = 0; x < w; x++)
					img[y * w + x] = blendPx(img[y * w + x], RGB(12, 63, 31));
		}
		scanlines(img, w, h, 170);
		break;
	case G_BROADCAST:
		broadcast(img, w, h, s);
		break;
	case G_WORN:
		bleed(img, w, h, maxi(3, w / 28), maxi(1, w / 70));
		wobble(img, w, h, s, maxi(1, w / 120), maxi(1, w / 90));
		bitRot(img, w, h, s, 4);
		scanlines(img, w, h, 220);
		break;
	case G_SMEAR:
		moshSmear(img, w, h, s);
		break;
	case G_SWIRL:
		moshSwirl(img, w, h, s);
		break;
	case G_COMPRESS:
		compress(img, w, h, s);
		break;
	case G_DRIFT:
		drift(img, w, h, s);
		break;
	case G_KEYFRAME:
		keyframe(img, w, h, s);
		break;
	case G_ANAGLYPH: // red to the left, cyan to the right, like 3D glasses
	{
		u16 tmp[MAXDIM];
		int d = maxi(3, w / 40);
		for (int y = 0; y < h; y++)
		{
			u16 *row = img + y * w;
			memcpy(tmp, row, w * sizeof(u16));
			for (int x = 0; x < w; x++)
			{
				u16 l = tmp[clampi(x + d, 0, w - 1)], r = tmp[clampi(x - d, 0, w - 1)];
				row[x] = RGB(R5(l), G6(r), B5(r));
			}
		}
		break;
	}
	default:
		break;
	}
}
