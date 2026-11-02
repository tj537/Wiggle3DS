#include <stdlib.h>
#include "camera.h"
#include "develop.h"

#define PIXELS (CAM_W * CAM_H)

static const u16 *refs[2];
static u16 *sums[2]; // per pixel: r, g, b sums
static u8 *counts[2];

static const u8 bayer4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

static void release(void)
{
	for (int e = 0; e < 2; e++)
	{
		free(sums[e]);
		free(counts[e]);
		sums[e] = NULL;
		counts[e] = NULL;
	}
}

bool stackBegin(const u16 *left, const u16 *right)
{
	refs[0] = left;
	refs[1] = right;
	for (int e = 0; e < 2; e++)
	{
		sums[e] = malloc(PIXELS * 3 * sizeof(u16));
		counts[e] = malloc(PIXELS);
		if (!sums[e] || !counts[e])
		{
			release();
			return false;
		}
		const u16 *src = refs[e];
		u16 *s = sums[e];
		for (int i = 0; i < PIXELS; i++)
		{
			u16 p = src[i];
			s[3 * i] = p >> 11;
			s[3 * i + 1] = p >> 5 & 63;
			s[3 * i + 2] = p & 31;
			counts[e][i] = 1;
		}
	}
	return true;
}

static int absd(int a, int b) { return a > b ? a - b : b - a; }

void stackAdd(const u16 *left, const u16 *right)
{
	if (!sums[0])
		return;
	const u16 *frames[2] = {left, right};
	for (int e = 0; e < 2; e++)
	{
		const u16 *ref = refs[e], *src = frames[e];
		u16 *s = sums[e];
		u8 *n = counts[e];
		for (int i = 0; i < PIXELS; i++)
		{
			u16 p = src[i], q = ref[i];
			int r = p >> 11, g = p >> 5 & 63, b = p & 31;
			// Only where it matches the first frame within the noise.
			if (absd(r, q >> 11) > 4 || absd(g, q >> 5 & 63) > 8 || absd(b, q & 31) > 4 || n[i] == 255)
				continue;
			s[3 * i] += r;
			s[3 * i + 1] += g;
			s[3 * i + 2] += b;
			n[i]++;
		}
	}
}

void stackFinish(u16 *left, u16 *right)
{
	if (!sums[0])
		return;
	u16 *out[2] = {left, right};
	for (int e = 0; e < 2; e++)
	{
		const u16 *s = sums[e];
		const u8 *n = counts[e];
		u16 *dst = out[e];
		for (int y = 0, i = 0; y < CAM_H; y++)
			for (int x = 0; x < CAM_W; x++, i++)
			{
				// Averages with 4 extra bits, rounded with an ordered dither
				// so the gained precision shows as smooth gradients.
				int c = n[i], d = bayer4[(y & 3) * 4 + (x & 3)];
				int r = (s[3 * i] * 16 / c + d) >> 4, g = (s[3 * i + 1] * 16 / c + d) >> 4, b = (s[3 * i + 2] * 16 / c + d) >> 4;
				r = r > 31 ? 31 : r;
				g = g > 63 ? 63 : g;
				b = b > 31 ? 31 : b;
				dst[i] = r << 11 | g << 5 | b;
			}
	}
	release();
}

// Channel means over the middle of the frame, where both eyes see the same.
static void means(const u16 *img, u32 *m)
{
	m[0] = m[1] = m[2] = 0;
	for (int y = CAM_H / 8; y < CAM_H * 7 / 8; y += 2)
		for (int x = CAM_W / 8; x < CAM_W * 7 / 8; x += 2)
		{
			u16 p = img[y * CAM_W + x];
			m[0] += p >> 11;
			m[1] += p >> 5 & 63;
			m[2] += p & 31;
		}
}

void matchEyes(const u16 *left, u16 *right)
{
	u32 ml[3], mr[3];
	means(left, ml);
	means(right, mr);
	static const int maxv[3] = {31, 63, 31};
	u8 lut[3][64];
	bool same = true;
	for (int c = 0; c < 3; c++)
	{
		// Gains outside 0.8 to 1.25 mean the eyes see different things, not a metering difference.
		int gain = mr[c] ? (int)(ml[c] * 256ULL / mr[c]) : 256;
		gain = gain < 205 ? 205 : gain > 320 ? 320 : gain;
		same = same && gain > 252 && gain < 260;
		for (int v = 0; v <= maxv[c]; v++)
		{
			int o = (v * gain + 128) >> 8;
			lut[c][v] = o > maxv[c] ? maxv[c] : o;
		}
	}
	if (same)
		return;
	for (int i = 0; i < PIXELS; i++)
	{
		u16 p = right[i];
		right[i] = lut[0][p >> 11] << 11 | lut[1][p >> 5 & 63] << 5 | lut[2][p & 31];
	}
}

void doubleExpose(u16 *shot, const u16 *first)
{
	// Per channel: a and b at 85 %, then screen: a + b - a * b.
	static u8 lut5[32], lut6[64];
	static bool ready;
	if (!ready)
	{
		for (int v = 0; v < 32; v++)
			lut5[v] = v * 85 / 100;
		for (int v = 0; v < 64; v++)
			lut6[v] = v * 85 / 100;
		ready = true;
	}
	for (int i = 0; i < PIXELS; i++)
	{
		u16 p = shot[i], q = first[i];
		int ar = lut5[p >> 11], ag = lut6[p >> 5 & 63], ab = lut5[p & 31];
		int br = lut5[q >> 11], bg = lut6[q >> 5 & 63], bb = lut5[q & 31];
		int r = ar + br - ar * br / 31, g = ag + bg - ag * bg / 63, b = ab + bb - ab * bb / 31;
		shot[i] = r << 11 | g << 5 | b;
	}
}
