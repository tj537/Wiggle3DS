#include <math.h>
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
	for (int e = 0; e < (right ? 2 : 1); e++)
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
	for (int e = 0; e < (sums[1] && right ? 2 : 1); e++)
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
	// Divisions are slow without a divider: one reciprocal per count instead.
	static u32 recip[256];
	for (int c = 1; c < 256; c++)
		recip[c] = (16u << 16) / c;
	u16 *out[2] = {left, right};
	for (int e = 0; e < (sums[1] && right ? 2 : 1); e++)
	{
		const u16 *s = sums[e];
		const u8 *n = counts[e];
		u16 *dst = out[e];
		for (int y = 0, i = 0; y < CAM_H; y++)
			for (int x = 0; x < CAM_W; x++, i++)
			{
				// Averages with 4 extra bits, rounded with an ordered dither
				// so the gained precision shows as smooth gradients.
				u32 k = recip[n[i]];
				int d = bayer4[(y & 3) * 4 + (x & 3)];
				int r = ((s[3 * i] * k >> 16) + d) >> 4, g = ((s[3 * i + 1] * k >> 16) + d) >> 4, b = ((s[3 * i + 2] * k >> 16) + d) >> 4;
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

static int luma8(u16 p) { return (77 * ((p >> 11) << 3) + 150 * ((p >> 5 & 63) << 2) + 29 * ((p & 31) << 3)) >> 8; }

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

// Brightness 0..255 at a fraction of the histogram, from the dark end.
static int percentile(const u32 *hist, u32 total, float at)
{
	u32 want = (u32)(total * at), acc = 0;
	for (int v = 0; v < 256; v++)
		if ((acc += hist[v]) > want)
			return v;
	return 255;
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

void autoDevelop(u16 *left, u16 *right, bool night)
{
	// White balance: means of the near-neutral midtones; all midtones when
	// too few are neutral (a red wall, a green lawn).
	u32 hist[256] = {0}, total = 0;
	u64 neutral[3] = {0}, all[3] = {0};
	u32 nNeutral = 0, nAll = 0;
	for (int y = 2; y < CAM_H; y += 4)
		for (int x = 2; x < CAM_W; x += 4)
		{
			u16 p = left[y * CAM_W + x];
			int r = (p >> 11) << 3, g = (p >> 5 & 63) << 2, b = (p & 31) << 3, l = luma8(p);
			hist[l]++;
			total++;
			if (l < 24 || l > 232)
				continue;
			int hi = r > g ? (r > b ? r : b) : (g > b ? g : b), lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
			all[0] += r, all[1] += g, all[2] += b, nAll++;
			if ((hi - lo) * 100 < hi * 30)
				neutral[0] += r, neutral[1] += g, neutral[2] += b, nNeutral++;
		}
	if (!nAll)
		return; // black or white all over: nothing to go by
	bool sure = nNeutral * 20 >= nAll;
	const u64 *m = sure ? neutral : all;
	float wb[3] = {1, 1, 1};
	if (m[0] && m[2])
	{
		// 70 % of the way to gray (in log terms): sunsets and lamps keep some
		// of their glow. Without neutral pixels the colors may just be the
		// scene's own (grass, sky), so only a nudge.
		float k = sure ? 0.7f : 0.3f, lo = sure ? 0.7f : 0.88f, hi = sure ? 1.5f : 1.15f;
		wb[0] = clampf(powf((float)m[1] / m[0], k), lo, hi);
		wb[2] = clampf(powf((float)m[1] / m[2], k), lo, hi);
	}


	// Exposure: the median toward middle gray, unless the highlights would
	// go far past white; the shoulder takes the rest.
	int black = percentile(hist, total, 0.005f), median = percentile(hist, total, 0.5f), top = percentile(hist, total, 0.99f);
	black = black > 20 ? 14 : black * 7 / 10; // lifts haze, keeps the shadows' depth
	const float target = 112;
	// Measured from the black point, which the curve takes off first.
	float lift = (median - black > 4 ? median - black : 4) * 255.0f / (255 - black);
	float gain = clampf(target / lift, 0.45f, night ? 3.0f : 2.2f);
	float roof = 300.0f / (top > 1 ? top : 1);
	if (gain > 1 && gain > roof)
		gain = roof > 1 ? roof : 1;
	if (gain < 1 && median < 150)
		gain = 1; // only darkens what is clearly too bright


	// One table per channel, in 1/16 steps for the dither.
	static const int maxv[3] = {31, 63, 31};
	u16 lut[3][64];
	bool flat = gain > 0.98f && gain < 1.02f && black < 3 && fabsf(wb[0] - 1) < 0.03f && fabsf(wb[2] - 1) < 0.03f;
	if (flat)
		return;
	for (int c = 0; c < 3; c++)
		for (int v = 0; v <= maxv[c]; v++)
		{
			float x = (v * 255.0f / maxv[c] * wb[c] - black) / (255 - black) * gain;
			const float knee = 0.75f;
			if (x > knee)
				x = knee + (1 - knee) * (1 - expf(-(x - knee) / (1 - knee)));
			lut[c][v] = (u16)(clampf(x, 0, 1) * maxv[c] * 16 + 0.5f);
		}
	u16 *imgs[2] = {left, right};
	for (int e = 0; e < (right && right != left ? 2 : 1); e++)
	{
		u16 *img = imgs[e];
		for (int y = 0, i = 0; y < CAM_H; y++)
			for (int x = 0; x < CAM_W; x++, i++)
			{
				u16 p = img[i];
				int d = bayer4[(y & 3) * 4 + (x & 3)];
				int r = (lut[0][p >> 11] + d) >> 4, g = (lut[1][p >> 5 & 63] + d) >> 4, b = (lut[2][p & 31] + d) >> 4;
				img[i] = (r > 31 ? 31 : r) << 11 | (g > 63 ? 63 : g) << 5 | (b > 31 ? 31 : b);
			}
	}
}
