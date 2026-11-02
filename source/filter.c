#include "draw.h"
#include "filter.h"

typedef struct
{
	const char *name;
	int vignette;
} FilterDef;

static const FilterDef defs[FILTER_COUNT] = {
	{"Original", 0},
	{"Vivid", 0},
	{"Warm", 0},
	{"Cool", 0},
	{"Dream", 60},
	{"Cinema", 70},
	{"Vintage", 90},
	{"Lomo", 170},
	{"Sepia", 80},
	{"Mono", 0},
	{"Noir", 130},
};

static u16 luts[FILTER_COUNT][65536];

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
	}
}

void filterInit(void)
{
	for (int f = 1; f < FILTER_COUNT; f++)
		for (int c = 0; c < 65536; c++)
		{
			float r = (c >> 11) / 31.0f, g = ((c >> 5) & 63) / 63.0f, b = (c & 31) / 31.0f;
			apply(f, &r, &g, &b);
			luts[f][c] = ((int)(r * 31 + 0.5f) << 11) | ((int)(g * 63 + 0.5f) << 5) | (int)(b * 31 + 0.5f);
		}
}

const char *filterName(int filter) { return defs[filter].name; }
const u16 *filterLut(int filter) { return filter > 0 && filter < FILTER_COUNT ? luts[filter] : NULL; }
int filterVignette(int filter) { return filter >= 0 && filter < FILTER_COUNT ? defs[filter].vignette : 0; }

void filterApply(u16 *dst, const u16 *img, int stride, int x, int y, int w, int h, int filter)
{
	const u16 *lut = filterLut(filter);
	for (int row = 0; row < h; row++)
	{
		const u16 *src = img + (y + row) * stride + x;
		u16 *out = dst + row * w;
		for (int col = 0; col < w; col++)
			out[col] = lut ? lut[src[col]] : src[col];
	}
	int v = filterVignette(filter);
	if (v)
		vignetteRows(dst, w, h, v);
}
