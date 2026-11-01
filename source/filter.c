#include <math.h>
#include "filter.h"

static const char *names[FILTER_COUNT] = {"Original", "S/W", "Noir", "Sepia", "Vintage", "Pop", "Kühl"};
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
	case 1: // S/W
		y = contrast(luma(*r, *g, *b), 1.1f);
		*r = *g = *b = y;
		break;
	case 2: // Noir: hard black and white
		y = smooth(smooth(luma(*r, *g, *b)));
		*r = *g = *b = y;
		break;
	case 3: // Sepia
		y = luma(*r, *g, *b);
		*r = clampf(y * 1.07f + 0.08f);
		*g = clampf(y * 0.95f + 0.03f);
		*b = clampf(y * 0.78f);
		break;
	case 4: // Vintage: faded blacks, warm highlights, muted colors
		saturate(r, g, b, 0.75f);
		*r = clampf(0.10f + *r * 0.88f * 1.06f);
		*g = clampf(0.09f + *g * 0.85f);
		*b = clampf(0.12f + *b * 0.72f);
		break;
	case 5: // Pop: punchy contrast and color
		*r = smooth(*r) * 0.6f + *r * 0.4f;
		*g = smooth(*g) * 0.6f + *g * 0.4f;
		*b = smooth(*b) * 0.6f + *b * 0.4f;
		saturate(r, g, b, 1.45f);
		break;
	case 6: // Kühl: cool blue tint
		*r = clampf(*r * 0.88f);
		*g = clampf(*g * 0.98f + 0.01f);
		*b = clampf(*b * 1.08f + 0.05f);
		saturate(r, g, b, 0.9f);
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

const char *filterName(int filter) { return names[filter]; }
const u16 *filterLut(int filter) { return filter > 0 && filter < FILTER_COUNT ? luts[filter] : NULL; }
