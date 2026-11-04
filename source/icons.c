#include <math.h>
#include "draw.h"
#include "icons.h"

// Icons are drawn on a 16x16 grid scaled to the box: lines are rows of
// small discs, shapes come from draw.c.
typedef struct
{
	u16 *fb;
	int sw, alpha;
	float x, y, k; // top-left of the grid and its scale
	u16 c;
} Pen;

static float X(const Pen *p, float u) { return p->x + u * p->k; }
static float Y(const Pen *p, float v) { return p->y + v * p->k; }

static void line(const Pen *p, float u0, float v0, float u1, float v1)
{
	float x0 = X(p, u0), y0 = Y(p, v0), x1 = X(p, u1), y1 = Y(p, v1), r = p->k * 0.85f;
	int n = (int)(fabsf(x1 - x0) + fabsf(y1 - y0)) * 2 + 1;
	for (int i = 0; i <= n; i++)
		drawCircle(p->fb, p->sw, x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n, r, p->c, p->alpha);
}

static void ring(const Pen *p, float u, float v, float r) { drawRing(p->fb, p->sw, X(p, u), Y(p, v), r * p->k, 1.7f * p->k, p->c, p->alpha); }
static void dot(const Pen *p, float u, float v, float r) { drawCircle(p->fb, p->sw, X(p, u), Y(p, v), r * p->k, p->c, p->alpha); }

// Part of a ring, from angle a0 to a1 (radians, 0 = right, clockwise).
static void arc(const Pen *p, float u, float v, float r, float a0, float a1)
{
	int n = (int)((a1 - a0) * r * p->k * 2) + 2;
	for (int i = 0; i <= n; i++)
	{
		float a = a0 + (a1 - a0) * i / n;
		dot(p, u + cosf(a) * r, v + sinf(a) * r, 0.85f);
	}
}

static void box(const Pen *p, float u, float v, float w, float h, float r)
{
	drawRoundRect(p->fb, p->sw, (int)X(p, u), (int)Y(p, v), (int)(w * p->k + 0.5f), (int)(h * p->k + 0.5f), (int)(r * p->k), p->c, p->alpha);
}
static void frame(const Pen *p, float u, float v, float w, float h, float r)
{
	drawRoundRectOutline(p->fb, p->sw, (int)X(p, u), (int)Y(p, v), (int)(w * p->k + 0.5f), (int)(h * p->k + 0.5f), (int)(r * p->k),
	                     p->k > 1.1f ? 2 : 1, p->c, p->alpha);
}
static void poly(const Pen *p, const float *uv, int n, bool closed)
{
	for (int i = 0; i + 1 < n; i++)
		line(p, uv[2 * i], uv[2 * i + 1], uv[2 * i + 2], uv[2 * i + 3]);
	if (closed)
		line(p, uv[2 * n - 2], uv[2 * n - 1], uv[0], uv[1]);
}
// A filled triangle, by lines from one corner across the opposite side.
static void tri(const Pen *p, float u0, float v0, float u1, float v1, float u2, float v2)
{
	for (int i = 0; i <= 8; i++)
		line(p, u0, v0, u1 + (u2 - u1) * i / 8, v1 + (v2 - v1) * i / 8);
}

void drawIcon(u16 *fb, int sw, int icon, float cx, float cy, float size, u16 color, int alpha)
{
	Pen pen = {fb, sw, alpha, cx - size / 2, cy - size / 2, size / 16, color}, *p = &pen;
	switch (icon)
	{
	case ICON_CAMERA:
		frame(p, 1, 4, 14, 10, 2);
		box(p, 5, 2, 6, 3, 1);
		ring(p, 8, 9, 2.8f);
		break;
	case ICON_FILTER: // three overlapping circles, like color filters
		ring(p, 8, 5.5f, 3.6f);
		ring(p, 5.5f, 10, 3.6f);
		ring(p, 10.5f, 10, 3.6f);
		break;
	case ICON_WAVE: // the wiggle: a wave
		for (int i = 0; i < 12; i++)
			line(p, 1.5f + i * 13 / 12.0f, 8 - 4.5f * sinf(i * 0.57f), 1.5f + (i + 1) * 13 / 12.0f, 8 - 4.5f * sinf((i + 1) * 0.57f));
		break;
	case ICON_SPEED: // stopwatch
		ring(p, 8, 9, 5.5f);
		line(p, 8, 9, 8, 6);
		line(p, 8, 9, 10, 10);
		line(p, 6.5f, 1.8f, 9.5f, 1.8f);
		break;
	case ICON_BOLT:
		tri(p, 10, 1, 4, 9.5f, 9, 9.5f);
		tri(p, 6, 15, 12, 6.5f, 7, 6.5f);
		break;
	case ICON_TIMER: // clock face
		ring(p, 8, 8, 6.5f);
		line(p, 8, 8, 8, 4);
		line(p, 8, 8, 11, 8);
		break;
	case ICON_GRID:
		frame(p, 1.5f, 2.5f, 13, 11, 1.5f);
		line(p, 6, 3, 6, 13);
		line(p, 10, 3, 10, 13);
		line(p, 2, 6.2f, 14, 6.2f);
		line(p, 2, 9.8f, 14, 9.8f);
		break;
	case ICON_SOUND:
		box(p, 2, 6, 4, 4, 0.5f);
		tri(p, 9, 2.5f, 4, 8, 9, 13.5f);
		line(p, 11.5f, 5.5f, 12.5f, 8);
		line(p, 12.5f, 8, 11.5f, 10.5f);
		line(p, 13.5f, 3.5f, 15, 8);
		line(p, 15, 8, 13.5f, 12.5f);
		break;
	case ICON_FLASH: // a burst
		dot(p, 8, 8, 2.6f);
		for (int i = 0; i < 8; i++)
		{
			float a = i * 0.785f;
			line(p, 8 + cosf(a) * 4.8f, 8 + sinf(a) * 4.8f, 8 + cosf(a) * 7, 8 + sinf(a) * 7);
		}
		break;
	case ICON_MOON:
		drawCrescent(fb, sw, X(p, 7.5f), Y(p, 8.5f), 6.5f * p->k, X(p, 11), Y(p, 5.5f), 5.2f * p->k, color, alpha);
		break;
	case ICON_EYE:
		line(p, 1, 8, 5, 4.5f);
		line(p, 5, 4.5f, 11, 4.5f);
		line(p, 11, 4.5f, 15, 8);
		line(p, 1, 8, 5, 11.5f);
		line(p, 5, 11.5f, 11, 11.5f);
		line(p, 11, 11.5f, 15, 8);
		dot(p, 8, 8, 2.4f);
		break;
	case ICON_PLAY:
		tri(p, 4, 2, 4, 14, 14, 8);
		break;
	case ICON_PAUSE:
		box(p, 3, 2, 3.6f, 12, 1);
		box(p, 9.4f, 2, 3.6f, 12, 1);
		break;
	case ICON_STAR:
		drawStar(fb, sw, cx, cy + 0.5f * p->k, 7.5f * p->k, color, alpha);
		break;
	case ICON_DICE:
		frame(p, 1.5f, 1.5f, 13, 13, 3);
		dot(p, 5, 5, 1.4f);
		dot(p, 8, 8, 1.4f);
		dot(p, 11, 11, 1.4f);
		dot(p, 11, 5, 1.4f);
		dot(p, 5, 11, 1.4f);
		break;
	case ICON_SUN: // exposure: half dark, half light
		ring(p, 8, 8, 4);
		box(p, 8, 4.5f, 3.4f, 7, 3.4f);
		for (int i = 0; i < 8; i++)
		{
			float a = i * 0.785f;
			line(p, 8 + cosf(a) * 6, 8 + sinf(a) * 6, 8 + cosf(a) * 7.4f, 8 + sinf(a) * 7.4f);
		}
		break;
	case ICON_TRASH:
		line(p, 2, 4, 14, 4);
		line(p, 6, 2, 10, 2);
		poly(p, (const float[]){3.5f, 4, 4.5f, 14.5f, 11.5f, 14.5f, 12.5f, 4}, 4, false);
		line(p, 6.8f, 7, 6.8f, 12);
		line(p, 9.2f, 7, 9.2f, 12);
		break;
	case ICON_CHECK:
		line(p, 2, 8.5f, 6, 12.5f);
		line(p, 6, 12.5f, 14, 3.5f);
		break;
	case ICON_CROSS:
		line(p, 3, 3, 13, 13);
		line(p, 13, 3, 3, 13);
		break;
	case ICON_BACK:
		line(p, 2, 8, 14, 8);
		line(p, 2, 8, 6.5f, 3.5f);
		line(p, 2, 8, 6.5f, 12.5f);
		break;
	case ICON_HOME:
		poly(p, (const float[]){1.5f, 8, 8, 2, 14.5f, 8}, 3, false);
		poly(p, (const float[]){3.5f, 7, 3.5f, 14, 12.5f, 14, 12.5f, 7}, 4, false);
		box(p, 6.8f, 10, 2.4f, 4, 0.5f);
		break;
	case ICON_GALLERY: // a stack of photos
		frame(p, 4, 1.5f, 11, 9, 1.5f);
		box(p, 1, 5.5f, 11, 9, 1.5f);
		break;
	case ICON_SAVE: // into the tray
		line(p, 8, 1.5f, 8, 10);
		line(p, 8, 10, 4.5f, 6.5f);
		line(p, 8, 10, 11.5f, 6.5f);
		poly(p, (const float[]){2, 10, 2, 14, 14, 14, 14, 10}, 4, false);
		break;
	case ICON_STRENGTH: // a half filled circle
	case ICON_COMPARE:
		ring(p, 8, 8, 6.2f);
		drawCrescent(fb, sw, X(p, 8), Y(p, 8), 5.3f * p->k, X(p, icon == ICON_COMPARE ? 13.3f : 2.7f), Y(p, 8), 5.3f * p->k, color, alpha);
		break;
	case ICON_PALETTE:
		ring(p, 8, 8, 6.5f);
		dot(p, 5, 6, 1.5f);
		dot(p, 8.5f, 4.5f, 1.5f);
		dot(p, 11.5f, 7, 1.5f);
		dot(p, 6, 10.5f, 1.5f);
		break;
	case ICON_FILM:
		frame(p, 1.5f, 3, 13, 10, 1.5f);
		for (int i = 0; i < 4; i++)
		{
			dot(p, 4 + i * 2.7f, 4.8f, 0.8f);
			dot(p, 4 + i * 2.7f, 11.2f, 0.8f);
		}
		break;
	case ICON_3D: // two views, side by side
		frame(p, 1, 4, 8, 8, 1.5f);
		frame(p, 7, 4, 8, 8, 1.5f);
		break;
	case ICON_SQUARE:
		frame(p, 3, 3, 10, 10, 1.5f);
		break;
	case ICON_HALF:
		frame(p, 1, 2, 14, 12, 1.5f);
		box(p, 1, 8, 7, 6, 1.5f);
		break;
	case ICON_RESET:
	case ICON_UNDO:
		arc(p, 8, 8.5f, 5.5f, -1.9f, 3.4f);
		tri(p, 1.5f, 2, 7, 3, 2.5f, 7.5f);
		break;
	case ICON_SHARE:
		line(p, 4, 8, 12, 3.5f);
		line(p, 4, 8, 12, 12.5f);
		dot(p, 4, 8, 2.3f);
		dot(p, 12, 3.5f, 2.3f);
		dot(p, 12, 12.5f, 2.3f);
		break;
	case ICON_GEAR:
		for (int i = 0; i < 8; i++)
			dot(p, 8 + 6.2f * cosf(i * 0.785f), 8 + 6.2f * sinf(i * 0.785f), 1.6f);
		ring(p, 8, 8, 4.6f);
		break;
	case ICON_POWER:
		arc(p, 8, 9, 5.5f, -1.0f, 4.14f);
		line(p, 8, 1.5f, 8, 8);
		break;
	case ICON_TOUCH: // a tap: a dot with rings
		dot(p, 8, 8, 2.4f);
		drawRing(fb, sw, X(p, 8), Y(p, 8), 5 * p->k, 1.2f * p->k, color, alpha * 2 / 3);
		drawRing(fb, sw, X(p, 8), Y(p, 8), 7.3f * p->k, 1 * p->k, color, alpha / 3);
		break;
	case ICON_PHOTO: // a picture: sun and mountains
		frame(p, 1, 2.5f, 14, 11, 1.5f);
		dot(p, 11, 6, 1.5f);
		tri(p, 6, 6.5f, 2.5f, 12, 9.5f, 12);
		tri(p, 10.5f, 9, 8, 12, 13.5f, 12);
		break;
	case ICON_WAIT: // hourglass
		poly(p, (const float[]){3.5f, 1.5f, 12.5f, 1.5f, 3.5f, 14.5f, 12.5f, 14.5f}, 4, true);
		break;
	case ICON_SD:
		poly(p, (const float[]){4, 1.5f, 10, 1.5f, 13, 4.5f, 13, 14.5f, 4, 14.5f}, 5, true);
		line(p, 7, 4, 7, 6);
		line(p, 9.5f, 4, 9.5f, 6);
		break;
	case ICON_LIST:
		for (int i = 0; i < 3; i++)
		{
			dot(p, 2.5f, 4 + i * 4, 1.2f);
			line(p, 5.5f, 4 + i * 4, 14, 4 + i * 4);
		}
		break;
	case ICON_EDIT: // a pencil
		line(p, 3, 13, 12, 4);
		line(p, 5, 15, 14, 6);
		line(p, 12, 4, 14, 6);
		line(p, 3, 13, 2, 15.5f);
		line(p, 2, 15.5f, 5, 15);
		break;
	case ICON_ORIGINAL: // no filter: a circle struck through
		ring(p, 8, 8, 6);
		line(p, 3.5f, 12.5f, 12.5f, 3.5f);
		break;
	case ICON_TURTLE: // slow: a shell, a head, little feet
		for (int i = 0; i < 8; i++) // the dome, as a fan of triangles
		{
			float a0 = 3.1416f + i * 0.3927f, a1 = a0 + 0.3927f;
			tri(p, 7, 10, 7 + cosf(a0) * 5.5f, 10 + sinf(a0) * 5.5f, 7 + cosf(a1) * 5.5f, 10 + sinf(a1) * 5.5f);
		}
		box(p, 1, 9.5f, 12, 1.8f, 0.9f);
		dot(p, 13.5f, 8.5f, 1.8f);
		dot(p, 3.5f, 12, 1.2f);
		dot(p, 10, 12, 1.2f);
		break;
	case ICON_HARE: // fast: a hare running to the right, ears laid back
		dot(p, 5.8f, 10.2f, 3.5f); // body, long and low
		dot(p, 8.8f, 9.8f, 3.1f);
		dot(p, 12.4f, 7.6f, 2.3f); // head
		line(p, 11.6f, 5.8f, 8.6f, 1.6f); // ears
		line(p, 12.8f, 5.6f, 11.2f, 1.2f);
		dot(p, 9.4f, 2.6f, 1.1f);
		dot(p, 11.6f, 2.2f, 1.0f);
		line(p, 4.2f, 12.8f, 6.8f, 13.8f); // hind foot
		line(p, 10.8f, 12.0f, 12.2f, 13.6f); // front leg
		dot(p, 2.0f, 9.0f, 1.4f);  // tail
		break;
	case ICON_QR: // three finder squares and a few modules
		frame(p, 1, 1, 5.5f, 5.5f, 1);
		frame(p, 9.5f, 1, 5.5f, 5.5f, 1);
		frame(p, 1, 9.5f, 5.5f, 5.5f, 1);
		box(p, 2.8f, 2.8f, 2, 2, 0.4f);
		box(p, 11.3f, 2.8f, 2, 2, 0.4f);
		box(p, 2.8f, 11.3f, 2, 2, 0.4f);
		box(p, 9.5f, 9.5f, 2.2f, 2.2f, 0.4f);
		box(p, 12.8f, 12.8f, 2.2f, 2.2f, 0.4f);
		box(p, 12.8f, 9.5f, 2.2f, 2.2f, 0.4f);
		box(p, 9.5f, 12.8f, 2.2f, 2.2f, 0.4f);
		break;
	case ICON_SPARKLE: // a filter: a big four-pointed sparkle and a small one
		tri(p, 7, 1.5f, 5.2f, 7.2f, 8.8f, 7.2f);
		tri(p, 7, 14.5f, 5.2f, 8.8f, 8.8f, 8.8f);
		tri(p, 0.5f, 8, 6.2f, 6.2f, 6.2f, 9.8f);
		tri(p, 13.5f, 8, 7.8f, 6.2f, 7.8f, 9.8f);
		box(p, 5.4f, 6.4f, 3.2f, 3.2f, 0.5f);
		tri(p, 13, 0.5f, 12.1f, 3, 13.9f, 3);
		tri(p, 13, 5.5f, 12.1f, 3, 13.9f, 3);
		tri(p, 10.5f, 3, 13, 2.1f, 13, 3.9f);
		tri(p, 15.5f, 3, 13, 2.1f, 13, 3.9f);
		break;
	case ICON_SELFIE: // turn around
		line(p, 1, 5, 12, 5);
		line(p, 12, 5, 9, 2);
		line(p, 15, 11, 4, 11);
		line(p, 4, 11, 7, 14);
		break;
	}
}
