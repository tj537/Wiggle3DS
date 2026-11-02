#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include "draw.h"

// The framebuffers are rotated: memory runs column by column, bottom to top.
#define PIX(fb, x, y) (fb)[(x) * 240 + (239 - (y))]

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static inline u16 blend(u16 d, u16 s, int a)
{
	if (a >= 255)
		return s;
	if (a <= 0)
		return d;
	a += a >> 7; // 0..256
	int dr = d >> 11, dg = (d >> 5) & 63, db = d & 31;
	int sr = s >> 11, sg = (s >> 5) & 63, sb = s & 31;
	return ((dr + (((sr - dr) * a) >> 8)) << 11) | ((dg + (((sg - dg) * a) >> 8)) << 5) | (db + (((sb - db) * a) >> 8));
}

static inline u16 lerpColor(u16 c0, u16 c1, int t) { return blend(c0, c1, t); }

static inline void plot(u16 *fb, int sw, int x, int y, u16 c, int a)
{
	if (x < 0 || y < 0 || x >= sw || y >= 240 || a <= 0)
		return;
	u16 *p = &PIX(fb, x, y);
	*p = blend(*p, c, a);
}

void drawClear(u16 *fb, int sw, u16 color)
{
	for (int i = 0; i < sw * 240; i++)
		fb[i] = color;
}

void drawFill(u16 *fb, int sw, int x, int y, int w, int h, u16 color, int alpha)
{
	int x0 = clampi(x, 0, sw), x1 = clampi(x + w, 0, sw), y0 = clampi(y, 0, 240), y1 = clampi(y + h, 0, 240);
	for (int px = x0; px < x1; px++)
		for (int py = y0; py < y1; py++)
		{
			u16 *p = &PIX(fb, px, py);
			*p = blend(*p, color, alpha);
		}
}

// Ordered dithering hides the banding of dark RGB565 gradients.
static const u8 bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

void drawGradientV(u16 *fb, int sw, int x, int y, int w, int h, u16 top, u16 bottom)
{
	int r0 = (top >> 11) * 255 / 31, g0 = ((top >> 5) & 63) * 255 / 63, b0 = (top & 31) * 255 / 31;
	int r1 = (bottom >> 11) * 255 / 31, g1 = ((bottom >> 5) & 63) * 255 / 63, b1 = (bottom & 31) * 255 / 31;
	for (int py = 0; py < h; py++)
	{
		int t = h > 1 ? py * 256 / (h - 1) : 0;
		int r = r0 + (r1 - r0) * t / 256, g = g0 + (g1 - g0) * t / 256, b = b0 + (b1 - b0) * t / 256;
		for (int px = 0; px < w; px++)
		{
			int d = (bayer[(y + py) & 3][(x + px) & 3] * 2 + 1) * 255 / 32;
			u16 c = (((r * 31 + d) / 255) << 11) | (((g * 63 + d) / 255) << 5) | ((b * 31 + d) / 255);
			plot(fb, sw, x + px, y + py, c, 255);
		}
	}
}

// Coverage (0..1) of the pixel at (px, py) by a rounded rect.
static inline float roundRectCov(int px, int py, int x, int y, int w, int h, float r)
{
	float fx = px + 0.5f, fy = py + 0.5f;
	float cx = fx < x + r ? x + r : fx > x + w - r ? x + w - r : fx;
	float cy = fy < y + r ? y + r : fy > y + h - r ? y + h - r : fy;
	float dx = fx - cx, dy = fy - cy;
	if (dx == 0 && dy == 0)
		return 1;
	float c = r - sqrtf(dx * dx + dy * dy) + 0.5f;
	return c < 0 ? 0 : c > 1 ? 1 : c;
}

static void roundRect(u16 *fb, int sw, int x, int y, int w, int h, int r, u16 c0, u16 c1, int alpha)
{
	if (r * 2 > w) r = w / 2;
	if (r * 2 > h) r = h / 2;
	for (int px = x; px < x + w; px++)
	{
		if (px < 0 || px >= sw)
			continue;
		u16 c = c0 == c1 ? c0 : lerpColor(c0, c1, w > 1 ? (px - x) * 255 / (w - 1) : 0);
		bool edgeX = px < x + r || px >= x + w - r;
		for (int py = y; py < y + h; py++)
		{
			bool edge = edgeX && (py < y + r || py >= y + h - r);
			int a = edge ? (int)(alpha * roundRectCov(px, py, x, y, w, h, r)) : alpha;
			plot(fb, sw, px, py, c, a);
		}
	}
}

void drawRoundRect(u16 *fb, int sw, int x, int y, int w, int h, int r, u16 color, int alpha)
{
	roundRect(fb, sw, x, y, w, h, r, color, color, alpha);
}

void drawRoundRectGrad(u16 *fb, int sw, int x, int y, int w, int h, int r, u16 c0, u16 c1, int alpha)
{
	roundRect(fb, sw, x, y, w, h, r, c0, c1, alpha);
}

void drawRoundRectOutline(u16 *fb, int sw, int x, int y, int w, int h, int r, int thick, u16 color, int alpha)
{
	for (int px = x; px < x + w; px++)
		for (int py = y; py < y + h; py++)
		{
			float outer = roundRectCov(px, py, x, y, w, h, r);
			float inner = roundRectCov(px, py, x + thick, y + thick, w - 2 * thick, h - 2 * thick, r > thick ? r - thick : 0);
			float c = outer - inner;
			if (c > 0)
				plot(fb, sw, px, py, color, (int)(alpha * c));
		}
}

void drawShadow(u16 *fb, int sw, int x, int y, int w, int h, int r, int alpha)
{
	for (int i = 6; i >= 1; i--)
		roundRect(fb, sw, x - i, y - i + 3, w + 2 * i, h + 2 * i, r + i, 0, 0, alpha / 8);
}

static void circle(u16 *fb, int sw, float cx, float cy, float r, float inner, u16 c0, u16 c1, int alpha)
{
	int x0 = (int)floorf(cx - r - 1), x1 = (int)ceilf(cx + r + 1);
	int y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
	for (int px = x0; px <= x1; px++)
		for (int py = y0; py <= y1; py++)
		{
			float dx = px + 0.5f - cx, dy = py + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
			float c = r - d + 0.5f;
			c = c < 0 ? 0 : c > 1 ? 1 : c;
			if (inner > 0)
			{
				float ci = inner - d + 0.5f;
				c -= ci < 0 ? 0 : ci > 1 ? 1 : ci;
			}
			if (c <= 0)
				continue;
			// Diagonal gradient, top-left to bottom-right.
			int t = (int)((dx + dy + 2 * r) * 255 / (4 * r));
			plot(fb, sw, px, py, c0 == c1 ? c0 : lerpColor(c0, c1, clampi(t, 0, 255)), (int)(alpha * c));
		}
}

void drawCircle(u16 *fb, int sw, float cx, float cy, float r, u16 color, int alpha)
{
	circle(fb, sw, cx, cy, r, 0, color, color, alpha);
}

void drawCircleGrad(u16 *fb, int sw, float cx, float cy, float r, u16 c0, u16 c1, int alpha)
{
	circle(fb, sw, cx, cy, r, 0, c0, c1, alpha);
}

void drawRing(u16 *fb, int sw, float cx, float cy, float r, float thick, u16 color, int alpha)
{
	circle(fb, sw, cx, cy, r, r - thick, color, color, alpha);
}

static inline u16 avg4(u16 a, u16 b, u16 c, u16 d)
{
	int r = (a >> 11) + (b >> 11) + (c >> 11) + (d >> 11);
	int g = ((a >> 5) & 63) + ((b >> 5) & 63) + ((c >> 5) & 63) + ((d >> 5) & 63);
	int bl = (a & 31) + (b & 31) + (c & 31) + (d & 31);
	return (((r + 2) >> 2) << 11) | (((g + 2) >> 2) << 5) | ((bl + 2) >> 2);
}

static void image(u16 *fb, int sw, int dx, int dy, int dw, int dh,
                  const u16 *img, int stride, int sx, int sy, int w, int h, const u16 *lut, int radius, int alpha)
{
	// 16.16 fixed point: sample at 1/4 and 3/4 of each target pixel.
	u32 stepX = ((u32)w << 16) / dw, stepY = ((u32)h << 16) / dh;
	for (int x = 0; x < dw; x++)
	{
		int px = dx + x;
		if (px < 0 || px >= sw)
			continue;
		int ax = sx + (int)((x * stepX + stepX / 4) >> 16), bx = sx + (int)((x * stepX + stepX * 3 / 4) >> 16);
		bool edgeX = radius && (x < radius || x >= dw - radius);
		u16 *col = fb + px * 240;
		for (int y = 0; y < dh; y++)
		{
			int py = dy + y;
			if (py < 0 || py >= 240)
				continue;
			const u16 *ra = img + (sy + (int)((y * stepY + stepY / 4) >> 16)) * stride;
			const u16 *rb = img + (sy + (int)((y * stepY + stepY * 3 / 4) >> 16)) * stride;
			u16 c = avg4(ra[ax], ra[bx], rb[ax], rb[bx]);
			if (lut)
				c = lut[c];
			int a = alpha;
			if (edgeX && (y < radius || y >= dh - radius))
				a = (int)(a * roundRectCov(x, y, 0, 0, dw, dh, radius));
			col[239 - py] = a >= 255 ? c : blend(col[239 - py], c, a);
		}
	}
}

void drawImage(u16 *fb, int sw, int dx, int dy, int dw, int dh,
               const u16 *img, int stride, int sx, int sy, int w, int h, const u16 *lut, int radius)
{
	image(fb, sw, dx, dy, dw, dh, img, stride, sx, sy, w, h, lut, radius, 255);
}

void drawImageAlpha(u16 *fb, int sw, int dx, int dy, int dw, int dh,
                    const u16 *img, int stride, int sx, int sy, int w, int h, const u16 *lut, int alpha)
{
	if (alpha > 0)
		image(fb, sw, dx, dy, dw, dh, img, stride, sx, sy, w, h, lut, 0, alpha);
}

void halveRows(u16 *dst, const u16 *src, int w, int h)
{
	for (int y = 0; y < h / 2; y++)
		for (int x = 0; x < w / 2; x++)
		{
			const u16 *p = src + y * 2 * w + x * 2;
			dst[y * (w / 2) + x] = avg4(p[0], p[1], p[w], p[w + 1]);
		}
}

void blendRows(u16 *dst, const u16 *a, const u16 *b, int pixels, int t)
{
	for (int i = 0; i < pixels; i++)
		dst[i] = blend(a[i], b[i], t);
}

// ---- vignette ----

// Darkening (0..255) at normalized squared distance t from the center
// (0 = center, 1 = edge midpoint, 2 = corner).
static inline int vignetteAlpha(float t, int strength)
{
	float k = (t - 0.35f) / 1.65f;
	if (k <= 0)
		return 0;
	if (k > 1)
		k = 1;
	return (int)(strength * k * k * (3 - 2 * k));
}

static inline float axis(int i, int n)
{
	float d = (i + 0.5f - n / 2.0f) / (n / 2.0f);
	return d * d;
}

void drawVignette(u16 *fb, int sw, int x, int y, int w, int h, int strength, int radius)
{
	for (int px = 0; px < w; px++)
	{
		int fx = x + px;
		if (fx < 0 || fx >= sw)
			continue;
		float ax = axis(px, w);
		for (int py = 0; py < h; py++)
		{
			int fy = y + py;
			if (fy < 0 || fy >= 240)
				continue;
			int a = vignetteAlpha(ax + axis(py, h), strength);
			if (a && radius && (px < radius || px >= w - radius) && (py < radius || py >= h - radius))
				a = (int)(a * roundRectCov(px, py, 0, 0, w, h, radius));
			if (a)
			{
				u16 *p = &PIX(fb, fx, fy);
				*p = blend(*p, 0, a);
			}
		}
	}
}

void vignetteRows(u16 *img, int w, int h, int strength)
{
	for (int py = 0; py < h; py++)
	{
		float ay = axis(py, h);
		for (int px = 0; px < w; px++)
		{
			int a = vignetteAlpha(axis(px, w) + ay, strength);
			if (a)
				img[py * w + px] = blend(img[py * w + px], 0, a);
		}
	}
}

// ---- text ----

static const Glyph *findGlyph(const Font *f, u32 code)
{
	int lo = 0, hi = f->count - 1;
	while (lo <= hi)
	{
		int mid = (lo + hi) / 2;
		if (f->glyphs[mid].code == code)
			return &f->glyphs[mid];
		if (f->glyphs[mid].code < code)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return NULL;
}

static u32 nextCode(const char **s)
{
	const u8 *p = (const u8 *)*s;
	u32 c = *p++;
	if (c >= 0xF0)
	{
		c = ((c & 7) << 18) | ((p[0] & 63) << 12) | ((p[1] & 63) << 6) | (p[2] & 63);
		p += 3;
	}
	else if (c >= 0xE0)
	{
		c = ((c & 15) << 12) | ((p[0] & 63) << 6) | (p[1] & 63);
		p += 2;
	}
	else if (c >= 0xC0)
	{
		c = ((c & 31) << 6) | (p[0] & 63);
		p += 1;
	}
	*s = (const char *)p;
	return c;
}

int textWidth(const Font *font, const char *text)
{
	int adv = 0;
	while (*text)
	{
		const Glyph *g = findGlyph(font, nextCode(&text));
		if (g)
			adv += g->adv;
	}
	return (adv + 8) >> 4;
}

static int text(u16 *fb, int sw, const Font *font, int x, int y, u16 c0, u16 c1, int alpha, const char *s)
{
	int width = c0 == c1 ? 0 : textWidth(font, s);
	int baseline = y + font->ascent, pen = x << 4;
	while (*s)
	{
		const Glyph *g = findGlyph(font, nextCode(&s));
		if (!g)
			continue;
		int gx = ((pen + 8) >> 4) + g->x, gy = baseline + g->y;
		const u8 *bm = font->bitmap + g->offset;
		for (int col = 0; col < g->w; col++)
		{
			u16 c = c0 == c1 ? c0 : lerpColor(c0, c1, clampi((gx + col - x) * 255 / (width ? width : 1), 0, 255));
			for (int row = 0; row < g->h; row++)
				if (bm[row * g->w + col])
					plot(fb, sw, gx + col, gy + row, c, bm[row * g->w + col] * alpha / 255);
		}
		pen += g->adv;
	}
	return ((pen + 8) >> 4) - x;
}

int drawText(u16 *fb, int sw, const Font *font, int x, int y, u16 color, int alpha, const char *s)
{
	return text(fb, sw, font, x, y, color, color, alpha, s);
}

int drawTextGrad(u16 *fb, int sw, const Font *font, int x, int y, u16 c0, u16 c1, const char *s)
{
	return text(fb, sw, font, x, y, c0, c1, 255, s);
}

void drawTextCentered(u16 *fb, int sw, const Font *font, int cx, int y, u16 color, int alpha, const char *s)
{
	text(fb, sw, font, cx - textWidth(font, s) / 2, y, color, color, alpha, s);
}

const char *tfmt(const char *fmt, ...)
{
	static char bufs[4][96];
	static int next;
	char *b = bufs[next++ & 3];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(b, sizeof(bufs[0]), fmt, ap);
	va_end(ap);
	return b;
}

// ---- button hints ----

#define BADGE_R 7

static int badgeWidth(const char *button)
{
	int tw = textWidth(&fontSmall, button);
	return tw + 6 > BADGE_R * 2 ? tw + 8 : BADGE_R * 2;
}

int hintWidth(const char *button, const char *label)
{
	return badgeWidth(button) + (label && *label ? 5 + textWidth(&fontSmall, label) : 0);
}

int drawHint(u16 *fb, int sw, int x, int y, const char *button, const char *label, u16 color, int alpha)
{
	// Badge: a white pill (circle for single letters) with the button name in dark.
	int bw = badgeWidth(button), lh = fontSmall.ascent + fontSmall.descent;
	int cy = y + lh / 2;
	drawRoundRect(fb, sw, x, cy - BADGE_R, bw, BADGE_R * 2, BADGE_R, color, alpha);
	drawTextCentered(fb, sw, &fontSmall, x + bw / 2, cy - lh / 2, COL565(14, 14, 22), alpha, button);
	if (label && *label)
		drawText(fb, sw, &fontSmall, x + bw + 5, y, color, alpha, label);
	return hintWidth(button, label);
}
