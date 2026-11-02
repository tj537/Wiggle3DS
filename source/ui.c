#include <math.h>
#include <string.h>
#include "app.h"
#include "draw.h"
#include "export.h"
#include "filter.h"
#include "qrcodegen.h"

// Color themes; RGB triples, converted when a theme is picked.
typedef struct
{
	const char *name;
	u8 c[9][3]; // bg top, bg bottom, surface, surface2, stroke, text, dim, accent 0, accent 1
} Theme;

static const Theme themes[THEME_COUNT] = {
	{"Purple", {{30, 18, 52}, {11, 7, 20}, {36, 26, 58}, {52, 40, 80}, {84, 66, 122}, {248, 245, 255}, {160, 148, 190}, {124, 77, 255}, {214, 110, 255}}},
	{"Neon", {{22, 20, 34}, {9, 9, 15}, {30, 29, 44}, {44, 42, 62}, {70, 66, 96}, {246, 245, 250}, {150, 147, 172}, {255, 61, 154}, {255, 140, 64}}},
	{"Ocean", {{14, 24, 44}, {6, 10, 20}, {20, 34, 58}, {30, 50, 80}, {50, 78, 120}, {244, 248, 255}, {140, 160, 190}, {40, 120, 255}, {40, 210, 230}}},
	{"Mint", {{12, 32, 30}, {5, 13, 13}, {18, 44, 42}, {28, 62, 58}, {48, 94, 88}, {240, 255, 250}, {140, 180, 170}, {20, 180, 130}, {130, 220, 100}}},
	{"Sunset", {{40, 16, 20}, {16, 6, 8}, {54, 24, 30}, {76, 36, 44}, {110, 56, 64}, {255, 246, 240}, {190, 150, 150}, {240, 60, 70}, {250, 170, 50}}},
	{"Mono", {{30, 30, 32}, {10, 10, 12}, {38, 38, 42}, {54, 54, 60}, {82, 82, 90}, {250, 250, 250}, {160, 160, 168}, {96, 96, 112}, {150, 150, 166}}},
};

static u16 palette[9];
static bool bgReady;

#define C_BG_TOP palette[0]
#define C_BG_BOTTOM palette[1]
#define C_SURFACE palette[2]
#define C_SURFACE2 palette[3]
#define C_STROKE palette[4]
#define C_TEXT palette[5]
#define C_DIM palette[6]
#define C_ACC0 palette[7]
#define C_ACC1 palette[8]
#define C_RED COL565(255, 76, 92)
#define C_BLACK 0
#define C_WHITE 0xFFFF

#define FILTER_PILL_X (CARD_X + 8)
#define FILTER_PILL_Y (CARD_Y + CARD_H - 28)

#define FADE_MS 160
#define FLASH_MS 220
#define TOAST_MS 2600
#define UNDO_MS 5000 // a toast with an Undo button
#define SLIDE_PX 14

// Frame time of the last drawn frame per screen; see uiTopAnimating.
static u64 topDrawnAt, bottomDrawnAt;

static int lineH(const Font *f) { return f->ascent + f->descent; }
static u64 since(const App *a, u64 t) { return a->now > t ? a->now - t : 0; }

void uiSetTheme(int theme)
{
	const Theme *t = &themes[theme >= 0 && theme < THEME_COUNT ? theme : 0];
	for (int i = 0; i < 9; i++)
		palette[i] = COL565(t->c[i][0], t->c[i][1], t->c[i][2]);
	bgReady = false;
}

const char *uiThemeName(int theme) { return themes[theme].name; }

u32 uiThemeRgb(int theme, int which)
{
	const u8 *c = themes[theme].c[which];
	return (c[0] << 16) | (c[1] << 8) | c[2];
}

// Rendered once per theme; dithering every pixel each frame is not free.
static u16 bgCache[320 * 240];

static void background(u16 *fb)
{
	if (!bgReady)
	{
		drawGradientV(bgCache, 320, 0, 0, 320, 240, C_BG_TOP, C_BG_BOTTOM);
		bgReady = true;
	}
	memcpy(fb, bgCache, sizeof(bgCache));
}

// The background again over rows y0..y1-1 (to clip what scrolls under them).
static void backgroundRows(u16 *fb, int y0, int y1)
{
	for (int x = 0; x < 320; x++) // columns run bottom to top
		memcpy(fb + x * 240 + 240 - y1, bgCache + x * 240 + 240 - y1, (y1 - y0) * sizeof(u16));
}

// Translucent dark pill with small text, centered on cx.
static void overlayPill(u16 *fb, int sw, int cx, int y, const char *text)
{
	int w = textWidth(&fontSmall, text) + 20, h = 20;
	drawRoundRect(fb, sw, cx - w / 2, y, w, h, h / 2, C_BLACK, 150);
	drawTextCentered(fb, sw, &fontSmall, cx, y + (h - lineH(&fontSmall)) / 2, C_TEXT, 255, text);
}

static void overlayPillLeft(u16 *fb, int sw, int x, int y, const char *text)
{
	overlayPill(fb, sw, x + (textWidth(&fontSmall, text) + 20) / 2, y, text);
}

static void overlayPillRight(u16 *fb, int sw, int x, int y, const char *text)
{
	overlayPill(fb, sw, x - (textWidth(&fontSmall, text) + 20) / 2, y, text);
}

// A pill button with a button hint inside; filled with a gradient when `primary`.
static void button(u16 *fb, int x, int y, int w, const char *btn, const char *label, bool primary, u16 fill)
{
	if (primary)
		drawRoundRectGrad(fb, 320, x, y, w, BTN_H, BTN_H / 2, C_ACC0, C_ACC1, 255);
	else
		drawRoundRect(fb, 320, x, y, w, BTN_H, BTN_H / 2, fill, 255);
	int hw = hintWidth(btn, label);
	drawHint(fb, 320, x + (w - hw) / 2, y + (BTN_H - lineH(&fontSmall)) / 2, btn, label, C_TEXT, 255);
}

static void centeredHints(u16 *fb, int y, int n, const char *const *btns, const char *const *labels)
{
	int gap = 14, w = gap * (n - 1);
	for (int i = 0; i < n; i++)
		w += hintWidth(btns[i], labels[i]);
	int x = 160 - w / 2;
	for (int i = 0; i < n; i++)
		x += drawHint(fb, 320, x, y, btns[i], labels[i], C_DIM, 255) + gap;
}

static void dialog(u16 *fb, int x, int y, int w, int h)
{
	drawFill(fb, 320, 0, 0, 320, 240, C_BLACK, 160);
	drawShadow(fb, 320, x, y, w, h, 16, 200);
	drawRoundRect(fb, 320, x, y, w, h, 16, C_SURFACE, 255);
	drawRoundRectOutline(fb, 320, x, y, w, h, 16, 1, C_STROKE, 255);
}

// Fits a w*h image into the top screen.
static void fitTop(int w, int h, int *dx, int *dy, int *dw, int *dh)
{
	*dw = w * 240 / h;
	*dh = 240;
	if (*dw > 400)
	{
		*dw = 400;
		*dh = h * 400 / w;
	}
	*dx = (400 - *dw) / 2;
	*dy = (240 - *dh) / 2;
}

// Glitch filters work on the image itself, and a filter at less than full
// strength is mixed with the original: the image is scaled to screen size
// with its colors first, glitched and mixed, and then drawn.
#define GLITCH_PIXELS (400 * 240)
static u16 glitchL[GLITCH_PIXELS], glitchR[GLITCH_PIXELS], original[GLITCH_PIXELS];

static void scaleGlitched(u16 *dst, int dw, int dh, const u16 *img, int stride, int sx, int sy, int w, int h, int filter,
                          int frame, int strength)
{
	const u16 *lut = filterLut(filter);
	bool mixed = lut && strength < 100;
	for (int y = 0; y < dh; y++)
	{
		const u16 *src = img + (sy + y * h / dh) * stride + sx;
		u16 *out = dst + y * dw, *orig = original + y * dw;
		for (int x = 0; x < dw; x++)
		{
			u16 p = src[x * w / dw];
			out[x] = lut ? lut[p] : p;
			if (mixed)
				orig[x] = p;
		}
	}
	filterGlitch(dst, dw, dh, filter, frame);
	if (mixed)
		blendRows(dst, original, dst, dw * dh, strength * 255 / 100);
}

static bool layered(int filter, int strength, int dw, int dh)
{
	return (filterHasGlitch(filter) || (filter && strength < 100)) && dw * dh <= GLITCH_PIXELS;
}

static int vignetteAt(int filter, int strength) { return filterVignette(filter) * (strength < 100 ? strength : 100) / 100; }

// drawImage with a filter's colors and vignette.
static void drawFiltered(u16 *fb, int sw, int dx, int dy, int dw, int dh,
                         const u16 *img, int stride, int sx, int sy, int w, int h, int filter, int radius, int frame,
                         int strength)
{
	if (layered(filter, strength, dw, dh))
	{
		scaleGlitched(glitchL, dw, dh, img, stride, sx, sy, w, h, filter, frame, strength);
		drawImage(fb, sw, dx, dy, dw, dh, glitchL, dw, 0, 0, dw, dh, NULL, radius);
	}
	else
		drawImage(fb, sw, dx, dy, dw, dh, img, stride, sx, sy, w, h, filterLut(filter), radius);
	int v = vignetteAt(filter, strength);
	if (v)
		drawVignette(fb, sw, dx, dy, dw, dh, v, radius);
}

// One frame of the wiggle loop: the left eye mixed with the right one.
static void drawWiggle(u16 *fb, int sw, int dx, int dy, int dw, int dh, const u16 *left, const u16 *right, int stride,
                       int lx, int ly, int rx, int ry, int w, int h, int filter, int mix, int frame, int strength)
{
	const u16 *lut = filterLut(filter);
	if (layered(filter, strength, dw, dh))
	{
		if (mix < 255)
		{
			scaleGlitched(glitchL, dw, dh, left, stride, lx, ly, w, h, filter, frame, strength);
			drawImage(fb, sw, dx, dy, dw, dh, glitchL, dw, 0, 0, dw, dh, NULL, 0);
		}
		if (mix > 0)
		{
			scaleGlitched(glitchR, dw, dh, right, stride, rx, ry, w, h, filter, frame, strength);
			drawImageAlpha(fb, sw, dx, dy, dw, dh, glitchR, dw, 0, 0, dw, dh, NULL, mix);
		}
	}
	else
	{
		if (mix < 255)
			drawImage(fb, sw, dx, dy, dw, dh, left, stride, lx, ly, w, h, lut, 0);
		drawImageAlpha(fb, sw, dx, dy, dw, dh, right, stride, rx, ry, w, h, lut, mix);
	}
	int v = vignetteAt(filter, strength);
	if (v)
		drawVignette(fb, sw, dx, dy, dw, dh, v, 0);
}

// The right eye's share in the current loop frame.
static int frameMix(const App *a, int smooth, int delayMs)
{
	int mix[MAX_WIGGLE_FRAMES], n = wiggleFrames(smooth, mix);
	return mix[(a->now / (u64)delayMs) % n];
}

// The step of the loop now: with a moving glitch the loop is longer (whole
// wiggle loops), and the step is its frame number, as in the GIF.
static int loopStep(const App *a, int filter, int smooth, int delayMs)
{
	int mix[MAX_WIGGLE_FRAMES], n = wiggleFrames(smooth, mix);
	return (int)((a->now / (u64)delayMs) % (u64)filterLoopFrames(filter, n));
}

#define LIVE_GLITCH_MS 140 // a moving glitch on the viewfinder changes this often

static const ShotInfo *selectedInfo(const App *a)
{
	int slot = uiThumbSlot(a, a->sel);
	return slot >= 0 && a->gridOk[slot] ? &a->gridInfo[slot] : NULL;
}

static int shotDelay(const ShotInfo *info) { return info && info->delayMs > 0 ? info->delayMs : 120; }

bool uiTopWiggles(const App *a, int *delayMs)
{
	if (a->mode == MODE_REVIEW)
	{
		*delayMs = a->delayMs;
		return !uiShotIsPhoto(a) || filterMoves(a->shotFilter);
	}
	if (a->mode == MODE_GALLERY && a->playing && a->shotCount)
	{
		*delayMs = shotDelay(selectedInfo(a));
		return true;
	}
	return false;
}

// ---- top screen ----

static void homeTop(App *a, u16 *fb, int eye, bool stereo)
{
	drawClear(fb, 400, C_BLACK);
	int filter = a->filterMenu ? a->menuSel : a->settings.filter;
	if (a->camOk && a->liveL)
		drawFiltered(fb, 400, 40, 0, 320, 240, eye ? a->liveR : a->liveL, CAM_W, 0, 0, CAM_W, CAM_H, filter, 0,
		             (int)(a->now / LIVE_GLITCH_MS), a->settings.strength);
	else if (!a->camStarting)
		drawTextCentered(fb, 400, &fontBody, 200, 112, C_DIM, 255, "Camera unavailable");
	if (a->haveFirst) // the first exposure over the live view, to line up the second
		drawImageAlpha(fb, 400, 40, 0, 320, 240, eye ? a->firstR : a->firstL, CAM_W, 0, 0, CAM_W, CAM_H, filterLut(filter), 110);
	if (a->settings.grid)
		for (int k = 1; k <= 2; k++)
		{
			drawFill(fb, 400, 40 + k * 320 / 3, 0, 1, 240, C_WHITE, 70);
			drawFill(fb, 400, 40, k * 240 / 3, 320, 1, C_WHITE, 70);
		}
	if (a->stacking)
	{
		// Viewfinder corners and a card with one segment per frame taken.
		for (int k = 0; k < 4; k++)
		{
			int cx = k & 1 ? 344 : 56, cy = k & 2 ? 224 : 16, sx = k & 1 ? -1 : 1, sy = k & 2 ? -1 : 1;
			drawFill(fb, 400, sx > 0 ? cx : cx - 18, sy > 0 ? cy : cy - 2, 18, 2, C_WHITE, 220);
			drawFill(fb, 400, sx > 0 ? cx : cx - 2, sy > 0 ? cy : cy - 18, 2, 18, C_WHITE, 220);
		}
		int cw = 150, ch = 52, cx = 200 - cw / 2, cy = 120 - ch / 2;
		drawRoundRect(fb, 400, cx, cy, cw, ch, ch / 2, C_BLACK, 170);
		drawTextCentered(fb, 400, &fontBody, 200, cy + 8, C_WHITE, 255, "Hold still");
		int n = a->stackFrames, sw = 22, gap = 5, sx = 200 - (n * sw + (n - 1) * gap) / 2;
		for (int k = 0; k < n; k++)
		{
			if (k < a->stacking)
				drawRoundRectGrad(fb, 400, sx + k * (sw + gap), cy + ch - 16, sw, 5, 2, C_ACC0, C_ACC1, 255);
			else
				drawRoundRect(fb, 400, sx + k * (sw + gap), cy + ch - 16, sw, 5, 2, C_WHITE, 70);
		}
	}
	if (a->timerAt)
	{
		int left = a->timerAt > a->now ? (int)((a->timerAt - a->now + 999) / 1000) : 0;
		drawCircle(fb, 400, 200, 120, 34, C_BLACK, 150);
		drawRing(fb, 400, 200, 120, 34, 3, C_ACC1, 255);
		drawTextCentered(fb, 400, &fontTitle, 200, 120 - lineH(&fontTitle) / 2, C_WHITE, 255, tfmt("%d", left));
	}
	(void)stereo; // a clean viewfinder: what is on shows on the bottom screen
}

static void galleryTop(App *a, u16 *fb, int eye, bool stereo)
{
	drawClear(fb, 400, C_BLACK);
	if (!a->shotCount)
		return;
	int cur = uiThumbSlot(a, a->sel);
	const ShotInfo *info = selectedInfo(a);
	int delay = shotDelay(info);
	int filter = info ? SHOT_FILTER(info) : 0, smooth = info ? SHOT_SMOOTH(info) : 0;
	bool photo = info && SHOT_IS_PHOTO(info);
	int mix = stereo ? (eye ? 255 : 0) : photo ? (a->settings.photoEye ? 255 : 0) : a->playing ? frameMix(a, smooth, delay) : 0;
	if (a->previewNum == a->shots[a->sel])
		drawWiggle(fb, 400, 40, 0, PREVIEW_W, PREVIEW_H, a->previewL, a->previewR, PREVIEW_W, 0, 0, 0, 0, PREVIEW_W, PREVIEW_H, filter, mix,
		           a->playing ? loopStep(a, filter, smooth, delay) : 0, info ? SHOT_STRENGTH(info) : 100);
	else if (info)
		drawImage(fb, 400, 40, 0, PREVIEW_W, PREVIEW_H, a->grid[cur], THUMB_W, 0, 0, THUMB_W, THUMB_H, NULL, 0); // filtered already

	overlayPillLeft(fb, 400, 48, 8, tfmt("WIG_%04d", a->shots[a->sel]));
	overlayPillRight(fb, 400, 352, 8, tfmt("%d / %d", a->sel + 1, a->shotCount));
	if (stereo)
		overlayPill(fb, 400, 200, 212, "3D view");
	else if (info)
		overlayPill(fb, 400, 200, 212, a->playing ? tfmt("%s · %s · %d ms", filterName(filter), wiggleSmoothName(smooth), delay)
		                                          : tfmt("%s · paused", filterName(filter)));
}

// A filter's name, with its strength when below 100 %.
static const char *filterLabel(int filter, int strength)
{
	return filter && strength < 100 ? tfmt("%s %d%%", filterName(filter), strength) : filterName(filter);
}

static void reviewTop(App *a, u16 *fb, int eye, bool stereo)
{
	drawClear(fb, 400, C_BLACK);
	Crop c = wiggleCrop(a->offset);
	if (uiShotIsPhoto(a) && !stereo) // the whole frame of the photo's camera
		c = (Crop){0, 0, 0, 0, CAM_W, CAM_H};
	int dx, dy, dw, dh;
	fitTop(c.w, c.h, &dx, &dy, &dw, &dh);
	int mix = stereo ? (eye ? 255 : 0)
	          : uiShotIsPhoto(a) ? (a->settings.photoEye ? 255 : 0)
	                             : frameMix(a, a->shotSmooth, a->delayMs);
	int filter = a->compare ? 0 : a->shotFilter;
	drawWiggle(fb, 400, dx, dy, dw, dh, a->shotL, a->shotR, CAM_W, c.lx, c.ly, c.rx, c.ry, c.w, c.h, filter, mix,
	           loopStep(a, filter, a->shotSmooth, a->delayMs), a->shotStrength);

	if (a->shotNum >= 0)
		overlayPillLeft(fb, 400, dx + 8, 8, tfmt("WIG_%04d", a->shotNum));
	if (a->compare)
		overlayPill(fb, 400, 200, 212, "Original");
	else
		overlayPill(fb, 400, 200, 212, uiShotIsPhoto(a) ? filterLabel(a->shotFilter, a->shotStrength) : stereo ? "3D view" : tfmt("%s · %s · %d ms", filterLabel(a->shotFilter, a->shotStrength),
	                                                          wiggleSmoothName(a->shotSmooth), a->delayMs));
}

static void shareTop(App *a, u16 *fb);

static void toast(App *a, u16 *fb);

void uiDrawTop(App *a, u16 *left, u16 *right, bool stereo)
{
	topDrawnAt = a->now;
	for (int eye = 0; eye <= (stereo ? 1 : 0); eye++)
	{
		u16 *fb = eye ? right : left;
		if (a->mode == MODE_HOME || a->mode == MODE_SETTINGS)
			homeTop(a, fb, eye, stereo);
		else if (a->mode == MODE_GALLERY)
			galleryTop(a, fb, eye, stereo);
		else if (a->mode == MODE_REVIEW)
			reviewTop(a, fb, eye, stereo);
		else
			shareTop(a, fb);

		toast(a, fb);
		u64 flash = a->now - a->flashAt, fade = a->now - a->modeSince;
		if (a->flashAt && flash < FLASH_MS)
			drawFill(fb, 400, 0, 0, 400, 240, C_WHITE, 220 - (int)flash * 220 / FLASH_MS);
		else if (fade < FADE_MS)
			drawFill(fb, 400, 0, 0, 400, 240, C_BLACK, 255 - (int)fade * 255 / FADE_MS);
	}
}

// ---- bottom screen: home ----

#define HEADER_CY (HEADER_BTN_Y + HEADER_BTN_H / 2)

// cos/sin of the gear's 8 teeth.
static const float cosTable[8] = {1, 0.7071f, 0, -0.7071f, -1, -0.7071f, 0, 0.7071f};
static const float sinTable[8] = {0, 0.7071f, 1, 0.7071f, 0, -0.7071f, -1, -0.7071f};

// Header pills, right to left: settings, wiggle, share, night.
static int headerX(int i) { return 308 - (HB_COUNT - i) * HEADER_PILL_W - (HB_COUNT - 1 - i) * HEADER_PILL_GAP; }

int uiHeaderAt(int x, int y)
{
	if (y < HEADER_BTN_Y - 5 || y >= HEADER_BTN_Y + HEADER_BTN_H + 5)
		return -1;
	for (int i = 0; i < HB_COUNT; i++)
		if (x >= headerX(i) - HEADER_PILL_GAP / 2 && x < headerX(i) + HEADER_PILL_W + HEADER_PILL_GAP / 2)
			return i;
	return -1;
}

// Filter menu layout, in content coordinates (0 = the top of the list):
// each group is a heading and its buttons, three to a row.
static int groupTop(int g)
{
	int y = 0;
	for (int k = 0; k < g; k++)
		y += MENU_HEAD + (filterGroupSize(k) + MENU_COLS - 1) / MENU_COLS * MENU_ROW + MENU_GROUP_GAP;
	return y;
}

static int menuHeight(void) { return groupTop(filterGroupCount()) - MENU_GROUP_GAP; }

// Group and place of a filter in the menu.
static void menuPlace(int filter, int *group, int *index)
{
	for (int g = 0; g < filterGroupCount(); g++)
		for (int i = 0; i < filterGroupSize(g); i++)
			if (filterGroupItem(g, i) == filter)
			{
				*group = g;
				*index = i;
				return;
			}
	*group = *index = 0;
}

static void menuItemPos(int g, int i, int *x, int *y)
{
	*x = MENU_X + (i % MENU_COLS) * (MENU_BTN_W + MENU_GAP);
	*y = groupTop(g) + MENU_HEAD + i / MENU_COLS * MENU_ROW;
}

int uiMenuTileAt(const App *a, int x, int y)
{
	if (y < MENU_Y || y >= MENU_Y + MENU_VIEW_H)
		return -1;
	for (int g = 0; g < filterGroupCount(); g++)
		for (int i = 0; i < filterGroupSize(g); i++)
		{
			int bx, by;
			menuItemPos(g, i, &bx, &by);
			by += MENU_Y - a->menuScroll;
			if (x >= bx - MENU_GAP / 2 && x < bx + MENU_BTN_W + MENU_GAP / 2 && y >= by - MENU_GAP / 2 &&
			    y < by + MENU_BTN_H + MENU_GAP / 2)
				return filterGroupItem(g, i);
		}
	return -1;
}

int uiMenuMove(int filter, int dx, int dy)
{
	if (dx)
		return filterNext(filter, dx);
	int g, i;
	menuPlace(filter, &g, &i);
	int row = i / MENU_COLS + dy, col = i % MENU_COLS, rows = (filterGroupSize(g) + MENU_COLS - 1) / MENU_COLS;
	if (row < 0) // the last row of the group above
	{
		if (--g < 0)
			return filter;
		rows = (filterGroupSize(g) + MENU_COLS - 1) / MENU_COLS;
		row = rows - 1;
	}
	else if (row >= rows) // the first row of the group below
	{
		if (++g >= filterGroupCount())
			return filter;
		row = 0;
	}
	int k = row * MENU_COLS + col, n = filterGroupSize(g);
	return filterGroupItem(g, k < n ? k : n - 1);
}

int uiMenuMaxScroll(void) { return menuHeight() > MENU_VIEW_H ? menuHeight() - MENU_VIEW_H : 0; }

void uiMenuReveal(App *a)
{
	int g, i, x, y;
	menuPlace(a->menuSel, &g, &i);
	menuItemPos(g, i, &x, &y);
	int top = i < MENU_COLS ? groupTop(g) : y; // a first row shows its heading too
	if (top < a->menuScroll)
		a->menuScroll = top;
	if (y + MENU_BTN_H > a->menuScroll + MENU_VIEW_H)
		a->menuScroll = y + MENU_BTN_H - MENU_VIEW_H;
	int max = uiMenuMaxScroll();
	a->menuScroll = a->menuScroll < 0 ? 0 : a->menuScroll > max ? max : a->menuScroll;
}

static void tileRect(int i, int *x, int *y)
{
	*x = TILE_X + (i % 2) * (TILE_W + TILE_GAP);
	*y = TILE_Y + (i / 2) * (TILE_H + TILE_GAP);
}

// Speed tile: ◀ and ▶ buttons at its right end.
#define SPEED_PLUS_X (TILE_W - 20)
#define SPEED_MINUS_X (TILE_W - 46)

int uiTileAt(int x, int y, int *step)
{
	*step = 0;
	for (int i = 0; i < TILE_COUNT; i++)
	{
		int tx, ty;
		tileRect(i, &tx, &ty);
		if (x >= tx - TILE_GAP / 2 && x < tx + TILE_W + TILE_GAP / 2 && y >= ty - TILE_GAP / 2 && y < ty + TILE_H + TILE_GAP / 2)
		{
			int rx = x - tx;
			*step = rx >= SPEED_PLUS_X - 13 ? 1 : rx >= SPEED_MINUS_X - 13 ? -1 : 0;
			return i;
		}
	}
	return -1;
}

static int recentX(int i) { return 12 + i * (RECENT_W + RECENT_GAP); }

int uiRecentAt(int x, int y)
{
	if (y < RECENT_Y - 4 || y >= RECENT_Y + RECENT_H + 4)
		return -1;
	for (int i = 0; i < RECENT_COUNT; i++)
		if (x >= recentX(i) - RECENT_GAP / 2 && x < recentX(i) + RECENT_W + RECENT_GAP / 2)
			return i;
	return -1;
}

#define ALL_BTN_X (308 - ALL_BTN_W)
#define ALL_BTN_Y (RECENT_LABEL_Y - 8)

bool uiAllButtonHit(int x, int y)
{
	return x >= ALL_BTN_X - 4 && x < 312 && y >= ALL_BTN_Y - 4 && y < ALL_BTN_Y + ALL_BTN_H + 4;
}

static int backButtonWidth(void) { return hintWidth("B", "Home") + 20; }

bool uiGalleryBackHit(int x, int y)
{
	return x >= BACK_BTN_X - 4 && x < BACK_BTN_X + backButtonWidth() + 4 && y >= HEADER_BTN_Y - 4 &&
	       y < HEADER_BTN_Y + HEADER_BTN_H + 4;
}

int uiMaxScroll(const App *a)
{
	int cols = (a->shotCount + GRID_ROWS - 1) / GRID_ROWS, max = cols * COL_W - CELL_GAP - (320 - 2 * GRID_X);
	return max > 0 ? max : 0;
}

static void cellPos(const App *a, int idx, int *x, int *y)
{
	*x = GRID_X + (idx / GRID_ROWS) * COL_W - (int)a->scrollX;
	*y = GRID_Y + (idx % GRID_ROWS) * (CELL_H + CELL_GAP);
}

int uiCellAt(const App *a, int x, int y)
{
	int col = (x - GRID_X + (int)a->scrollX + CELL_GAP / 2) / COL_W, row = (y - GRID_Y + CELL_GAP / 2) / (CELL_H + CELL_GAP);
	int idx = col * GRID_ROWS + row;
	if (x < GRID_X - CELL_GAP / 2 || y < GRID_Y - CELL_GAP / 2 || row >= GRID_ROWS || idx >= a->shotCount)
		return -1;
	return idx;
}

static void homeEmpty(u16 *fb)
{
	drawRoundRect(fb, 320, 12, RECENT_Y, 296, RECENT_H, 10, C_SURFACE, 255);
	drawRoundRectOutline(fb, 320, 12, RECENT_Y, 296, RECENT_H, 10, 1, C_STROKE, 255);
	drawTextCentered(fb, 320, &fontBody, 160, RECENT_Y + 9, C_TEXT, 255, "No wigglegrams yet");
	drawTextCentered(fb, 320, &fontSmall, 160, RECENT_Y + 28, C_DIM, 255, "Press A to take your first one");
}

// Each filter swatch shows what the filter does to skin, sky and leaves.
static const u16 samples[3] = {COL565(226, 168, 138), COL565(92, 150, 222), COL565(84, 160, 92)};

static void swatches(u16 *fb, float x, float cy, int filter)
{
	const u16 *lut = filterLut(filter);
	for (int k = 0; k < 3; k++)
	{
		drawCircle(fb, 320, x + k * 7, cy, 4.5f, C_BLACK, 90);
		drawCircle(fb, 320, x + k * 7, cy, 3.5f, lut ? lut[samples[k]] : samples[k], 255);
	}
}

// The header of home and gallery: settings gear and share button on the right.
// A line of small dots, for icons.
static void iconLine(u16 *fb, float x0, float y0, float x1, float y1, u16 color)
{
	int n = (int)(fabsf(x1 - x0) + fabsf(y1 - y0)) + 1;
	for (int k = 0; k <= n; k++)
		drawCircle(fb, 320, x0 + (x1 - x0) * k / n, y0 + (y1 - y0) * k / n, 0.9f, color, 255);
}

static void headerButtons(App *a, u16 *fb)
{
	for (int i = 0; i < HB_COUNT; i++)
	{
		int x = headerX(i);
		bool on = (i == HB_NIGHT && a->settings.night) || (i == HB_SELFIE && a->selfie) || (i == HB_DOUBLE && a->doubleExp);
		if (on)
			drawRoundRectGrad(fb, 320, x, HEADER_BTN_Y, HEADER_PILL_W, HEADER_BTN_H, HEADER_BTN_H / 2, C_ACC0, C_ACC1, 255);
		else
			drawRoundRect(fb, 320, x, HEADER_BTN_Y, HEADER_PILL_W, HEADER_BTN_H, HEADER_BTN_H / 2, C_SURFACE2, 255);
		u16 ink = on ? C_WHITE : C_TEXT;
		float cx = x + HEADER_PILL_W / 2.0f, cy = HEADER_CY;
		switch (i)
		{
		case HB_DOUBLE: // two exposures overlapping, the second filled once the first is taken
			drawRing(fb, 320, cx - 3.5f, cy, 6, 1.4f, ink, 255);
			if (a->haveFirst)
				drawCircle(fb, 320, cx + 3.5f, cy, 6, ink, 200);
			else
				drawRing(fb, 320, cx + 3.5f, cy, 6, 1.4f, ink, 255);
			break;
		case HB_SELFIE: // forth and back: turned around
			iconLine(fb, cx - 7, cy - 3, cx + 7, cy - 3, ink);
			iconLine(fb, cx + 7, cy - 3, cx + 3, cy - 7, ink);
			iconLine(fb, cx + 7, cy + 3, cx - 7, cy + 3, ink);
			iconLine(fb, cx - 7, cy + 3, cx - 3, cy + 7, ink);
			break;
		case HB_NIGHT: // crescent
			drawCrescent(fb, 320, cx - 0.5f, cy, 6.5f, cx + 3, cy - 2.5f, 5.5f, ink, 255);
			break;
		case HB_SHARE: // three linked dots
			iconLine(fb, cx - 5, cy, cx + 5, cy - 5, ink);
			iconLine(fb, cx - 5, cy, cx + 5, cy + 5, ink);
			drawCircle(fb, 320, cx - 5, cy, 2.6f, ink, 255);
			drawCircle(fb, 320, cx + 5, cy - 5, 2.6f, ink, 255);
			drawCircle(fb, 320, cx + 5, cy + 5, 2.6f, ink, 255);
			break;
		default: // gear
			for (int t = 0; t < 8; t++)
				drawCircle(fb, 320, cx + 6.6f * cosTable[t], cy + 6.6f * sinTable[t], 1.9f, ink, 255);
			drawRing(fb, 320, cx, cy, 5.6f, 2.4f, ink, 255);
		}
	}
}

static void tile(u16 *fb, int i, const char *label, const char *value)
{
	int x, y;
	tileRect(i, &x, &y);
	drawRoundRect(fb, 320, x, y, TILE_W, TILE_H, 10, C_SURFACE2, 255);
	drawText(fb, 320, &fontSmall, x + 10, y + 4, C_DIM, 255, label);
	drawText(fb, 320, &fontBody, x + 10, y + 17, C_TEXT, 255, value);
}

static void roundButton(u16 *fb, float cx, float cy, const char *glyph, bool on)
{
	drawCircle(fb, 320, cx, cy, 10, on ? C_STROKE : C_SURFACE, 255);
	drawTextCentered(fb, 320, &fontSmall, (int)cx, (int)cy - lineH(&fontSmall) / 2, on ? C_TEXT : C_DIM, 255, glyph);
}

static void defaultsPanel(App *a, u16 *fb)
{
	const Settings *st = &a->settings;
	drawRoundRect(fb, 320, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 14, C_SURFACE, 255);
	drawRoundRectOutline(fb, 320, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 14, 1, C_STROKE, 255);
	int x, y;

	tile(fb, TILE_FILTER, "Filter", st->filter && st->strength < 100 ? tfmt("%s %d%%", filterName(st->filter), st->strength)
	                                                                 : filterName(st->filter));
	tileRect(TILE_FILTER, &x, &y);
	swatches(fb, x + TILE_W - 28, y + TILE_H / 2.0f, st->filter);

	// Off (a plain photo), then the smoothing levels: one dot more each.
	bool off = st->smooth == SMOOTH_OFF;
	tile(fb, TILE_SMOOTH, "Transition", off ? "Off · photo" : wiggleSmoothName(st->smooth));
	tileRect(TILE_SMOOTH, &x, &y);
	for (int k = 0; k <= SMOOTH_LEVELS; k++)
	{
		float cx = x + TILE_W - 38 + k * 9, cy = y + TILE_H / 2.0f;
		if (!off && k <= st->smooth + 1)
			drawCircleGrad(fb, 320, cx, cy, 3.5f, C_ACC0, C_ACC1, 255);
		else
			drawCircle(fb, 320, cx, cy, 3.5f, C_STROKE, 255);
	}

	tile(fb, TILE_SPEED, "Speed", tfmt("%d ms", st->delayMs));
	tileRect(TILE_SPEED, &x, &y);
	roundButton(fb, x + SPEED_MINUS_X, y + TILE_H / 2.0f, "◀", st->delayMs > DELAY_MIN);
	roundButton(fb, x + SPEED_PLUS_X, y + TILE_H / 2.0f, "▶", st->delayMs < DELAY_MAX);

	tile(fb, TILE_QUICK, "Quick capture", st->quick ? "On" : "Off");
	tileRect(TILE_QUICK, &x, &y);
	int sx = x + TILE_W - 44, sy = y + (TILE_H - 18) / 2;
	if (st->quick)
		drawRoundRectGrad(fb, 320, sx, sy, 34, 18, 9, C_ACC0, C_ACC1, 255);
	else
		drawRoundRect(fb, 320, sx, sy, 34, 18, 9, C_STROKE, 255);
	drawCircle(fb, 320, st->quick ? sx + 25 : sx + 9, sy + 9, 7, C_TEXT, 255);
}

// A thumbnail with the shot's filter; a small badge while its GIF is made.
static void thumb(App *a, u16 *fb, int idx, int x, int y, int w, int h)
{
	int i = uiThumbSlot(a, idx);
	if (i < 0)
		drawRoundRect(fb, 320, x, y, w, h, 6, C_SURFACE, 255); // still loading
	else if (a->gridOk[i])
		drawImage(fb, 320, x, y, w, h, a->grid[i], THUMB_W, 0, 0, THUMB_W, THUMB_H, NULL, 6); // filtered on load
	else
	{
		drawRoundRect(fb, 320, x, y, w, h, 6, C_SURFACE2, 255);
		drawTextCentered(fb, 320, &fontSmall, x + w / 2, y + h / 2 - 6, C_DIM, 255, "old");
	}
	if (a->exporting && exportIsPending(a->shots[idx]))
	{
		drawRoundRect(fb, 320, x + w - 26, y + h - 17, 22, 13, 6, C_BLACK, 170);
		drawTextCentered(fb, 320, &fontSmall, x + w - 15, y + h - 17 + (13 - lineH(&fontSmall)) / 2 - 2, C_TEXT, 255, "…");
	}
}

// Filter picker over the whole bottom screen: plain buttons; the live view
// on top previews the selected one.
static void filterMenu(App *a, u16 *fb)
{
	background(fb);
	int off = MENU_Y - a->menuScroll;
	for (int g = 0; g < filterGroupCount(); g++)
	{
		int hy = off + groupTop(g);
		if (hy > MENU_Y + MENU_VIEW_H)
			break;
		drawText(fb, 320, &fontSmall, MENU_X + 4, hy + (MENU_HEAD - lineH(&fontSmall)) / 2 - 2, C_DIM, 255,
		         filterGroupName(g));
		for (int i = 0; i < filterGroupSize(g); i++)
		{
			int f = filterGroupItem(g, i), x, y;
			menuItemPos(g, i, &x, &y);
			y += off;
			if (y + MENU_BTN_H < MENU_Y - 8 || y > MENU_Y + MENU_VIEW_H)
				continue;
			bool sel = f == a->menuSel, current = f == a->settings.filter;
			if (sel)
				drawRoundRectGrad(fb, 320, x, y, MENU_BTN_W, MENU_BTN_H, 12, C_ACC0, C_ACC1, 255);
			else
				drawRoundRect(fb, 320, x, y, MENU_BTN_W, MENU_BTN_H, 12, C_SURFACE2, 255);
			if (current && !sel)
				drawRoundRectOutline(fb, 320, x, y, MENU_BTN_W, MENU_BTN_H, 12, 1, C_ACC1, 255);
			if (f >= FILTER_FIRST_GLITCH)
			{
				// Glitches: a little zigzag instead of color dots.
				u16 ink = sel ? C_WHITE : C_ACC1;
				for (int k = 0; k < 4; k++)
					iconLine(fb, x + 7 + k * 4, y + MENU_BTN_H / 2.0f + (k & 1 ? 3 : -3), x + 11 + k * 4,
					         y + MENU_BTN_H / 2.0f + (k & 1 ? -3 : 3), ink);
			}
			else
				swatches(fb, x + 13, y + MENU_BTN_H / 2.0f, f);
			drawText(fb, 320, &fontSmall, x + 36, y + (MENU_BTN_H - lineH(&fontSmall)) / 2, sel || current ? C_TEXT : C_DIM,
			         255, current ? tfmt("%s ✓", filterName(f)) : filterName(f));
		}
	}

	// Title and hints over what scrolls under them.
	backgroundRows(fb, 0, MENU_Y - 4);
	backgroundRows(fb, MENU_Y + MENU_VIEW_H + 2, 240);
	drawText(fb, 320, &fontTitle, 16, 9, C_TEXT, 255, "Filter");
	int g, i;
	menuPlace(a->menuSel, &g, &i);
	const char *note = tfmt("%s%s · L R %d%%", filterGroupName(g), filterMoves(a->menuSel) ? " · moves" : "", a->settings.strength);
	drawText(fb, 320, &fontSmall, 304 - textWidth(&fontSmall, note), 17, C_DIM, 255, note);
	int max = uiMenuMaxScroll();
	if (max > 0)
	{
		int track = MENU_VIEW_H, bh = track * MENU_VIEW_H / menuHeight(), by = MENU_Y + (track - bh) * a->menuScroll / max;
		drawRoundRect(fb, 320, 310, MENU_Y, 3, track, 1, C_SURFACE, 255);
		drawRoundRectGrad(fb, 320, 310, by, 3, bh, 1, C_ACC0, C_ACC1, 255);
	}
	static const char *const btns[] = {"A", "B"}, *const labels[] = {"Apply", "Cancel"};
	centeredHints(fb, 218, 2, btns, labels);
}

static void homeBottom(App *a, u16 *fb)
{
	background(fb);

	// Battery: one LED, green, orange or red; a ring around it while charging.
	if (a->battery >= 0)
	{
		u16 led = a->battery > 50 ? COL565(70, 210, 110) : a->battery > 20 ? COL565(255, 160, 50) : C_RED;
		drawCircle(fb, 320, 22, HEADER_CY, 4.5f, led, 255);
		if (a->charging)
			drawRing(fb, 320, 22, HEADER_CY, 7.5f, 1.2f, led, 200);
	}
	headerButtons(a, fb);
	defaultsPanel(a, fb);

	// Newest shots and the way to all of them.
	int ly = ALL_BTN_Y + (ALL_BTN_H - lineH(&fontSmall)) / 2;
	int lx = 16 + drawText(fb, 320, &fontSmall, 16, ly, C_TEXT, 255, "Recent");
	const char *warn = a->freeBytes < MIN_FREE ? "SD card full" : a->freeBytes < LOW_FREE ? "SD card almost full"
	                   : NULL; // low battery shows in the header
	if (warn)
	{
		int wx = lx + 8, ww = textWidth(&fontSmall, warn) + 14, cy = ALL_BTN_Y + ALL_BTN_H / 2;
		drawRoundRect(fb, 320, wx, cy - 9, ww, 18, 9, C_RED, 255);
		drawText(fb, 320, &fontSmall, wx + 7, cy - lineH(&fontSmall) / 2, C_WHITE, 255, warn);
	}
	else if (a->exporting)
		drawText(fb, 320, &fontSmall, lx + 6, ly, C_DIM, 255,
		         tfmt(a->exporting == 1 ? "· making 1 GIF …" : "· making %d GIFs …", a->exporting));
	if (!a->shotCount)
		homeEmpty(fb);
	else
	{
		drawRoundRect(fb, 320, ALL_BTN_X, ALL_BTN_Y, ALL_BTN_W, ALL_BTN_H, ALL_BTN_H / 2, C_SURFACE2, 255);
		int hw = hintWidth("B", tfmt("All %d", a->shotCount));
		drawHint(fb, 320, ALL_BTN_X + (ALL_BTN_W - hw) / 2, ALL_BTN_Y + (ALL_BTN_H - lineH(&fontSmall)) / 2, "B",
		         tfmt("All %d", a->shotCount), C_TEXT, 255);
		for (int i = 0; i < RECENT_COUNT && i < a->shotCount; i++)
			thumb(a, fb, i, recentX(i), RECENT_Y, RECENT_W, RECENT_H);
	}

	if (a->timerAt)
		centeredHints(fb, 220, 1, (const char *const[]){"B"}, (const char *const[]){"Stop self-timer"});
	else if (a->haveFirst)
	{
		static const char *const btns[] = {"A", "B"}, *const labels[] = {"Second exposure", "Drop the first"};
		centeredHints(fb, 220, 2, btns, labels);
	}
	else
	{
		static const char *const btns[] = {"A", "Y", "X", "START"};
		const char *const labels[] = {a->settings.timer ? tfmt("In %d s", a->settings.timer)
		                              : a->selfie ? "Selfie" : a->settings.smooth == SMOOTH_OFF ? "Photo"
		                              : a->settings.quick ? "Quick shot" : "Capture",
		                              "Filter", a->settings.night ? "Night off" : "Night", "Quit"};
		centeredHints(fb, 220, 4, btns, labels);
	}

	if (a->filterMenu)
		filterMenu(a, fb);
}

static void galleryBottom(App *a, u16 *fb)
{
	background(fb);

	// Home button, title, position.
	int bw = backButtonWidth();
	drawRoundRect(fb, 320, BACK_BTN_X, HEADER_BTN_Y, bw, HEADER_BTN_H, HEADER_BTN_H / 2, C_SURFACE2, 255);
	drawHint(fb, 320, BACK_BTN_X + 10, HEADER_CY - lineH(&fontSmall) / 2, "B", "Home", C_TEXT, 255);
	int tx = BACK_BTN_X + bw + 12, tw = drawText(fb, 320, &fontTitle, tx, 9, C_TEXT, 255, "Gallery");
	const char *pos = a->picking ? tfmt("%d selected", a->pickedCount)
	                  : a->selDate[0] ? tfmt("%s · %d / %d", a->selDate, a->sel + 1, a->shotCount)
	                                  : tfmt("%d / %d", a->sel + 1, a->shotCount);
	drawText(fb, 320, &fontSmall, 308 - textWidth(&fontSmall, pos), 9 + fontTitle.ascent - fontSmall.ascent, C_DIM, 255, pos);
	(void)tw;

	int first = (int)a->scrollX / COL_W * GRID_ROWS;
	for (int idx = first; idx < a->shotCount && idx < first + (VISIBLE_COLS + 2) * GRID_ROWS; idx++)
	{
		int x, y;
		cellPos(a, idx, &x, &y);
		if (x >= 320)
			break;
		if (idx == a->sel)
			drawRoundRectGrad(fb, 320, x - 3, y - 3, CELL_W + 6, CELL_H + 6, 9, C_ACC0, C_ACC1, 255);
		thumb(a, fb, idx, x, y, CELL_W, CELL_H);
		if (a->picking)
		{
			// A check mark in the corner, empty until picked.
			float cx = x + CELL_W - 10, cy = y + 10;
			if (a->picked[idx])
			{
				drawFill(fb, 320, x, y, CELL_W, CELL_H, C_BLACK, 70);
				drawCircleGrad(fb, 320, cx, cy, 7, C_ACC0, C_ACC1, 255);
				drawTextCentered(fb, 320, &fontSmall, cx, cy - lineH(&fontSmall) / 2, C_WHITE, 255, "✓");
			}
			else
			{
				drawCircle(fb, 320, cx, cy, 7, C_BLACK, 110);
				drawRing(fb, 320, cx, cy, 7, 1.5f, C_WHITE, 220);
			}
		}
	}

	// Scroll bar when the strip is wider than the screen.
	int max = uiMaxScroll(a);
	if (max > 0)
	{
		int track = 296, content = max + 320 - 2 * GRID_X, w = track * (320 - 2 * GRID_X) / content;
		w = w < 24 ? 24 : w;
		int x = 12 + (int)((track - w) * a->scrollX / max);
		drawRoundRect(fb, 320, 12, 214, track, 3, 1, C_SURFACE, 255);
		drawRoundRectGrad(fb, 320, x, 214, w, 3, 1, C_ACC0, C_ACC1, 255);
	}

	if (a->picking)
	{
		static const char *const btns[] = {"A", "X", "B"};
		const char *const labels[] = {"Pick", "Delete", "Done"};
		centeredHints(fb, 222, 3, btns, labels);
	}
	else
	{
		static const char *const btns[] = {"A", "Y", "X", "B"};
		const char *const labels[] = {"Edit", a->playing ? "Pause" : "Play", "Delete", "Home"};
		centeredHints(fb, 222, 4, btns, labels);
	}

	if (a->confirmDelete)
	{
		dialog(fb, 40, 66, 240, 104);
		if (a->picking)
			drawTextCentered(fb, 320, &fontBody, 160, 82, C_TEXT, 255,
			                 a->pickedCount == 1 ? "Delete 1 shot?" : tfmt("Delete %d shots?", a->pickedCount));
		else
			drawTextCentered(fb, 320, &fontBody, 160, 82, C_TEXT, 255, tfmt("Delete WIG_%04d?", a->shots[a->sel]));
		drawTextCentered(fb, 320, &fontSmall, 160, 104, C_DIM, 255, "Photos and GIFs go · you can undo");
		button(fb, 56, 132, 100, "B", "Cancel", false, C_SURFACE2);
		button(fb, 164, 132, 100, "X", "Delete", false, C_RED);
	}
}

// ---- bottom screen: review ----

bool uiReviewChanged(const App *a)
{
	ShotInfo now = {a->pivotX, a->pivotY, a->offset.dx, a->offset.dy, a->delayMs,
	                SHOT_STYLE(a->shotFilter, a->shotSmooth, a->shotStrength)};
	return a->shotNum < 0 || memcmp(&now, &a->savedInfo, sizeof(now)) != 0;
}

// Back and save, and the saving dialog.
static void reviewButtons(App *a, u16 *fb)
{
	button(fb, BACK_X, BTN_Y, BACK_W, "B", "Back", false, C_SURFACE2);
	if (uiReviewChanged(a))
		button(fb, SAVE_X, BTN_Y, SAVE_W, "A", a->shotNum < 0 ? (uiShotIsPhoto(a) ? "Save photo" : "Save GIF") : "Save changes", true, 0);
	else
	{
		drawRoundRect(fb, 320, SAVE_X, BTN_Y, SAVE_W, BTN_H, BTN_H / 2, C_SURFACE, 255);
		drawRoundRectOutline(fb, 320, SAVE_X, BTN_Y, SAVE_W, BTN_H, BTN_H / 2, 1, C_STROKE, 255);
		drawTextCentered(fb, 320, &fontSmall, SAVE_X + SAVE_W / 2, BTN_Y + (BTN_H - lineH(&fontSmall)) / 2, C_DIM, 255,
		                 tfmt("✓ Saved as WIG_%04d", a->shotNum));
	}

	if (a->saving)
	{
		dialog(fb, 60, 84, 200, 72);
		drawTextCentered(fb, 320, &fontBody, 160, 100, C_TEXT, 255, "Saving …");
		drawTextCentered(fb, 320, &fontSmall, 160, 122, C_DIM, 255, "One moment");
		drawRoundRectGrad(fb, 320, 100, 140, 120, 3, 1, C_ACC0, C_ACC1, 255);
	}
}

static void reviewBottom(App *a, u16 *fb)
{
	background(fb);

	drawShadow(fb, 320, CARD_X, CARD_Y, CARD_W, CARD_H, 10, 160);
	const u16 *card = uiShotIsPhoto(a) && a->settings.photoEye ? a->shotR : a->shotL;
	drawFiltered(fb, 320, CARD_X, CARD_Y, CARD_W, CARD_H, card, CAM_W, 0, 0, CAM_W, CAM_H, a->shotFilter, 10, 0, a->shotStrength);
	const char *fl = a->shotFilter && a->shotStrength < 100 ? tfmt("▲▼  %s %d%%", filterName(a->shotFilter), a->shotStrength)
	                                                       : tfmt("▲▼  %s", filterName(a->shotFilter));
	int fw = textWidth(&fontSmall, fl) + 20;
	drawRoundRect(fb, 320, FILTER_PILL_X, FILTER_PILL_Y, fw, 20, 10, C_BLACK, 150);
	drawText(fb, 320, &fontSmall, FILTER_PILL_X + 10, FILTER_PILL_Y + (20 - lineH(&fontSmall)) / 2, C_TEXT, 255, fl);
	bool photo = uiShotIsPhoto(a);
	const char *sm = photo ? "Off · photo" : wiggleSmoothName(a->shotSmooth);
	int smw = hintWidth("X", sm) + 16, smx = CARD_X + CARD_W - 8 - smw;
	drawRoundRect(fb, 320, smx, FILTER_PILL_Y, smw, 20, 10, C_BLACK, 150);
	drawHint(fb, 320, smx + 8, FILTER_PILL_Y + (20 - lineH(&fontSmall)) / 2, "X", sm, C_TEXT, 255);
	if (photo)
	{
		// A plain photo: no point and no speed; X brings the wiggle back.
		drawTextCentered(fb, 320, &fontSmall, 160, SLIDER_Y - lineH(&fontSmall) / 2, C_DIM, 255,
		                 "Photo · saved as JPG · X for a wiggle");
		reviewButtons(a, fb);
		return;
	}
	float px = CARD_X + (a->pivotX + 0.5f) * CARD_W / CAM_W, py = CARD_Y + (a->pivotY + 0.5f) * CARD_H / CAM_H;
	drawCircle(fb, 320, px, py, 12, C_BLACK, 70);
	drawRing(fb, 320, px, py, 10, 2, C_TEXT, a->aligning ? 140 : 240);
	if (!a->aligning)
		drawCircleGrad(fb, 320, px, py, 3.5f, C_ACC0, C_ACC1, 255);

	if (!a->settings.tipDone)
	{
		// First time: what the point is for, with a pulse at the center.
		float pulse = (a->now % 1200) / 1200.0f;
		float cx = CARD_X + CARD_W / 2.0f, cy = CARD_Y + CARD_H / 2.0f - 14;
		drawRing(fb, 320, cx, cy, 10 + pulse * 18, 2, C_WHITE, (int)(200 * (1 - pulse)));
		drawRoundRect(fb, 320, CARD_X + 16, CARD_Y + CARD_H / 2 + 8, CARD_W - 32, 50, 14, C_BLACK, 190);
		drawTextCentered(fb, 320, &fontBody, 160, CARD_Y + CARD_H / 2 + 14, C_TEXT, 255, "Tap your subject");
		drawTextCentered(fb, 320, &fontSmall, 160, CARD_Y + CARD_H / 2 + 36, C_DIM, 255, "It stays still, the rest wiggles");
	}
	else
	{
		const char *hint = "L R strength · hold Y for the original";
		drawRoundRect(fb, 320, CARD_X + 8, CARD_Y + 8, textWidth(&fontSmall, hint) + 20, 20, 10, C_BLACK, 150);
		drawText(fb, 320, &fontSmall, CARD_X + 18, CARD_Y + 8 + (20 - lineH(&fontSmall)) / 2, C_TEXT, 255, hint);
	}

	// Speed slider.
	drawHint(fb, 320, 12, SLIDER_Y - lineH(&fontSmall) / 2, "◀▶", "", C_DIM, 255);
	drawText(fb, 320, &fontSmall, 12 + hintWidth("◀▶", "") + 4, SLIDER_Y - lineH(&fontSmall) / 2, C_DIM, 255, "Speed");
	float k = (float)(a->delayMs - DELAY_MIN) / (DELAY_MAX - DELAY_MIN);
	int knob = SLIDER_X0 + (int)(k * (SLIDER_X1 - SLIDER_X0));
	drawRoundRect(fb, 320, SLIDER_X0, SLIDER_Y - 2, SLIDER_X1 - SLIDER_X0, 4, 2, C_SURFACE2, 255);
	drawRoundRectGrad(fb, 320, SLIDER_X0, SLIDER_Y - 2, knob - SLIDER_X0 + 2, 4, 2, C_ACC0, C_ACC1, 255);
	drawCircle(fb, 320, knob, SLIDER_Y + 1.5f, 9, C_BLACK, 70);
	drawCircle(fb, 320, knob, SLIDER_Y, 8, C_TEXT, 255);
	drawText(fb, 320, &fontBody, SLIDER_X1 + 14, SLIDER_Y - lineH(&fontBody) / 2, C_TEXT, 255, tfmt("%d ms", a->delayMs));

	reviewButtons(a, fb);
}

// ---- settings ----

static const char *const settingLabels[SET_COUNT] = {
	"Theme", "Self-timer", "Grid lines", "Shutter sound", "Capture flash",
	"Viewfinder", "Photo camera", "Gallery autoplay", "GIF size", "GIF shape",
};

const char *uiSettingValue(const Settings *st, int row)
{
	switch (row)
	{
	case SET_THEME: return uiThemeName(st->theme);
	case SET_TIMER: return st->timer ? tfmt("%d s", st->timer) : "Off";
	case SET_GRID: return st->grid ? "On" : "Off";
	case SET_SOUND: return st->sound ? "On" : "Off";
	case SET_FLASH: return st->flash ? "On" : "Off";
	case SET_FPS: return tfmt("%d fps", st->fps);
	case SET_PHOTO_EYE: return st->photoEye ? "Right" : "Left";
	case SET_AUTOPLAY: return st->autoplay ? "On" : "Off";
	case SET_GIF_SIZE: return st->gifHalf ? "Half" : "Full";
	case SET_GIF_SHAPE: return st->gifSquare ? "Square 1:1" : "4:3";
	}
	return "";
}

#define SET_VALUE_X0 176 // left arrow
#define SET_VALUE_X1 300 // right arrow

int uiSettingsRowAt(const App *a, int x, int y, int *step)
{
	if (y < SET_Y || y >= SET_Y + SET_VISIBLE * SET_ROW)
		return -1;
	int row = a->settingsTop + (y - SET_Y) / SET_ROW;
	if (row >= SET_COUNT)
		return -1;
	*step = x >= SET_VALUE_X0 - 10 && x < SET_VALUE_X0 + 22 ? -1 : x >= SET_VALUE_X1 - 22 ? 1 : 0;
	return row;
}

static const char *sizeText(u64 bytes)
{
	return bytes >= (1ULL << 30) ? tfmt("%.1f GB", bytes / (double)(1ULL << 30)) : tfmt("%d MB", (int)(bytes >> 20));
}

static void settingsBottom(App *a, u16 *fb)
{
	static const char *const notes[SET_COUNT] = {
		[SET_GRID] = "viewfinder", [SET_FPS] = "saves battery", [SET_PHOTO_EYE] = "transition off", [SET_GIF_SIZE] = "messengers",
	};
	background(fb);
	int tw = drawText(fb, 320, &fontTitle, 16, 9, C_TEXT, 255, "Settings");
	const char *info = tfmt("%s · %d shots · %s free", APP_VERSION, a->shotCount, sizeText(a->freeBytes));
	(void)tw;
	drawText(fb, 320, &fontSmall, 308 - textWidth(&fontSmall, info), 9 + fontTitle.ascent - fontSmall.ascent, C_DIM, 255, info);

	for (int v = 0; v < SET_VISIBLE; v++)
	{
		int i = a->settingsTop + v;
		if (i >= SET_COUNT)
			break;
		int y = SET_Y + v * SET_ROW, cy = y + SET_ROW_H / 2;
		bool sel = i == a->settingsSel;
		drawRoundRect(fb, 320, 12, y, 290, SET_ROW_H, SET_ROW_H / 2, sel ? C_SURFACE2 : C_SURFACE, 255);
		if (sel)
			drawRoundRectOutline(fb, 320, 12, y, 290, SET_ROW_H, SET_ROW_H / 2, 1, C_ACC1, 255);

		int lx = 24 + drawText(fb, 320, &fontSmall, 24, cy - lineH(&fontSmall) / 2, C_TEXT, 255, settingLabels[i]);
		if (notes[i])
			drawText(fb, 320, &fontSmall, lx + 6, cy - lineH(&fontSmall) / 2, C_DIM, 200, notes[i]);

		u16 arrow = sel ? C_TEXT : C_DIM;
		drawText(fb, 320, &fontSmall, SET_VALUE_X0, cy - lineH(&fontSmall) / 2, arrow, 255, "◀");
		drawText(fb, 320, &fontSmall, SET_VALUE_X1 - 6 - textWidth(&fontSmall, "▶"), cy - lineH(&fontSmall) / 2, arrow, 255, "▶");
		int vx = (SET_VALUE_X0 + SET_VALUE_X1 - 6) / 2;
		const char *value = uiSettingValue(&a->settings, i);
		if (i == SET_THEME)
		{
			// The theme's two accent colors next to its name.
			vx += 8;
			int tx = vx - textWidth(&fontSmall, value) / 2 - 12;
			drawCircleGrad(fb, 320, tx, cy, 5, C_ACC0, C_ACC1, 255);
		}
		drawTextCentered(fb, 320, &fontSmall, vx, cy - lineH(&fontSmall) / 2, C_TEXT, 255, value);
	}

	// Scroll bar for the rows below or above the screen.
	int track = SET_VISIBLE * SET_ROW - 3, h = track * SET_VISIBLE / SET_COUNT;
	int y = SET_Y + (track - h) * a->settingsTop / (SET_COUNT - SET_VISIBLE);
	drawRoundRect(fb, 320, 307, SET_Y, 3, track, 1, C_SURFACE, 255);
	drawRoundRectGrad(fb, 320, 307, y, 3, h, 1, C_ACC0, C_ACC1, 255);

	static const char *const btns[] = {"▲▼", "◀▶", "B"}, *const labels[] = {"Select", "Change", "Done"};
	centeredHints(fb, 218, 3, btns, labels);
}

// ---- share ----

static void shareTop(App *a, u16 *fb)
{
	drawClear(fb, 400, COL565(11, 7, 20));
	if (!a->shareOk)
	{
		drawTextCentered(fb, 400, &fontTitle, 200, 96, C_TEXT, 255, "No WiFi connection");
		drawTextCentered(fb, 400, &fontBody, 200, 128, C_DIM, 255, "Turn on WiFi and try again.");
		return;
	}
	// White card with the code and a quiet zone of 3 modules.
	int n = qrcodegen_getSize(a->qr), quiet = 3, scale = 216 / (n + 2 * quiet);
	int card = (n + 2 * quiet) * scale, x0 = 200 - card / 2, y0 = 120 - card / 2;
	drawShadow(fb, 400, x0, y0, card, card, 14, 200);
	drawRoundRect(fb, 400, x0, y0, card, card, 14, C_WHITE, 255);
	for (int y = 0; y < n; y++)
		for (int x = 0; x < n; x++)
			if (qrcodegen_getModule(a->qr, x, y))
				drawFill(fb, 400, x0 + (quiet + x) * scale, y0 + (quiet + y) * scale, scale, scale, COL565(20, 12, 36), 255);
}

static void step(u16 *fb, int y, int num, const char *text)
{
	drawCircleGrad(fb, 320, 28, y + 9, 9, C_ACC0, C_ACC1, 255);
	drawTextCentered(fb, 320, &fontSmall, 28, y + 9 - lineH(&fontSmall) / 2, C_TEXT, 255, tfmt("%d", num));
	drawText(fb, 320, &fontBody, 46, y + 9 - lineH(&fontBody) / 2, C_TEXT, 255, text);
}

static void shareBottom(App *a, u16 *fb)
{
	background(fb);
	drawTextGrad(fb, 320, &fontTitle, 16, 9, C_ACC0, C_ACC1, "Share to phone");

	if (a->shareOk)
	{
		step(fb, 48, 1, "Put your phone on the same WiFi");
		step(fb, 78, 2, "Scan the code with its camera");
		step(fb, 108, 3, "Hold a GIF, then Save to Photos");

		int w = textWidth(&fontBody, a->shareUrl) + 32;
		drawRoundRect(fb, 320, 160 - w / 2, 146, w, 28, 14, C_SURFACE, 255);
		drawRoundRectOutline(fb, 320, 160 - w / 2, 146, w, 28, 14, 1, C_STROKE, 255);
		drawTextCentered(fb, 320, &fontBody, 160, 146 + (28 - lineH(&fontBody)) / 2, C_TEXT, 255, a->shareUrl);
		if (a->exporting)
			drawTextCentered(fb, 320, &fontSmall, 160, 184, C_DIM, 255,
			                 tfmt(a->exporting == 1 ? "1 GIF is still being made …" : "%d GIFs are still being made …", a->exporting));
		else if (a->sharedCount)
			drawTextCentered(fb, 320, &fontSmall, 160, 184, C_DIM, 255,
			                 tfmt(a->sharedCount == 1 ? "%d GIF sent" : "%d GIFs sent", a->sharedCount));
	}
	else
	{
		drawTextCentered(fb, 320, &fontBody, 160, 90, C_TEXT, 255, "Your 3DS isn't connected to WiFi.");
		drawTextCentered(fb, 320, &fontSmall, 160, 112, C_DIM, 255, "Check the WiFi switch and your connection settings.");
	}

	int hw = hintWidth("B", "Close");
	drawHint(fb, 320, 160 - hw / 2, 218, "B", "Close", C_DIM, 255);
}

// ---- shared ----

// Messages sit on the top screen, below its pills: a dark pill with a
// colored mark, that rises in and fades out; Y undoes a deletion.
static int toastMs(const App *a) { return a->toastUndo ? UNDO_MS : TOAST_MS; }

bool uiToastVisible(const App *a) { return a->toast[0] && since(a, a->toastAt) < (u64)toastMs(a); }

#define TOAST_Y 38
#define TOAST_H 28
#define TOAST_IN_MS 160

static void toast(App *a, u16 *fb)
{
	if (!uiToastVisible(a))
		return;
	u64 t = since(a, a->toastAt);
	int ms = toastMs(a);
	int alpha = t > (u64)ms - 300 ? (int)(ms - t) * 255 / 300 : t < TOAST_IN_MS ? (int)t * 255 / TOAST_IN_MS : 255;
	float k = t < TOAST_IN_MS ? 1 - (float)t / TOAST_IN_MS : 0;
	int rise = (int)(8 * k * k);
	int tw = textWidth(&fontBody, a->toast), uw = a->toastUndo ? hintWidth("Y", "Undo") + 18 : 0;
	int w = 14 + 10 + 8 + tw + 16 + uw, h = TOAST_H, x = 200 - w / 2, y = TOAST_Y + rise;
	drawRoundRect(fb, 400, x, y, w, h, h / 2, C_BLACK, alpha * 200 / 255);
	drawRoundRectOutline(fb, 400, x, y, w, h, h / 2, 1, C_WHITE, alpha * 40 / 255);
	float dx = x + 19, dy = y + h / 2.0f;
	if (a->toastError)
		drawCircle(fb, 400, dx, dy, 5, C_RED, alpha);
	else
		drawCircleGrad(fb, 400, dx, dy, 5, C_ACC0, C_ACC1, alpha);
	drawText(fb, 400, &fontBody, x + 32, y + (h - lineH(&fontBody)) / 2, C_WHITE, alpha, a->toast);
	if (uw)
	{
		int bx = x + w - uw - 4;
		drawRoundRect(fb, 400, bx, y + 4, uw, h - 8, (h - 8) / 2, C_WHITE, alpha * 40 / 255);
		drawHint(fb, 400, bx + 9, y + (h - lineH(&fontSmall)) / 2, "Y", "Undo", C_WHITE, alpha);
	}
}

void uiDrawBottom(App *a, u16 *fb)
{
	bottomDrawnAt = a->now;
	if (a->mode == MODE_HOME)
		homeBottom(a, fb);
	else if (a->mode == MODE_GALLERY)
		galleryBottom(a, fb);
	else if (a->mode == MODE_REVIEW)
		reviewBottom(a, fb);
	else if (a->mode == MODE_SETTINGS)
		settingsBottom(a, fb);
	else
		shareBottom(a, fb);

	// New screens rise into place while they fade in.
	u64 t = a->now - a->modeSince;
	if (t < FADE_MS)
	{
		float k = 1 - (float)t / FADE_MS;
		int d = (int)(SLIDE_PX * k * k);
		for (int x = 0; d > 0 && x < 320; x++)
		{
			u16 *col = fb + x * 240; // column x, from the bottom up
			memmove(col, col + d, (240 - d) * sizeof(u16));
			for (int i = 240 - d; i < 240; i++)
				col[i] = C_BLACK;
		}
		drawFill(fb, 320, 0, 0, 320, 240, C_BLACK, 255 - (int)t * 255 / FADE_MS);
	}
}

// Effects are judged by the last frame that was drawn, not by the clock: a
// screen keeps redrawing until one frame after the fade or toast is over,
// even when a slow step (loading, saving) skipped past its end.
static bool during(u64 drawn, u64 start, int ms) { return (s64)(drawn - start) < ms; }

bool uiTopAnimating(const App *a)
{
	return during(topDrawnAt, a->modeSince, FADE_MS) || (a->flashAt && during(topDrawnAt, a->flashAt, FLASH_MS)) ||
	       (a->toast[0] && during(topDrawnAt, a->toastAt, toastMs(a))) ||
	       a->timerAt;
}

bool uiBottomAnimating(const App *a)
{
	return during(bottomDrawnAt, a->modeSince, FADE_MS) ||
	       (a->mode == MODE_REVIEW && !a->settings.tipDone && !uiShotIsPhoto(a));
}

// ---- touch targets ----

bool uiFilterPillHit(int x, int y)
{
	return x >= FILTER_PILL_X - 4 && x < FILTER_PILL_X + 90 && y >= FILTER_PILL_Y - 4 && y < FILTER_PILL_Y + 24;
}

bool uiSmoothPillHit(int x, int y)
{
	return x >= CARD_X + CARD_W - 112 && x < CARD_X + CARD_W && y >= FILTER_PILL_Y - 4 && y < FILTER_PILL_Y + 24;
}

Hit uiReviewHit(int x, int y)
{
	if (y < BTN_Y - 4 || y >= BTN_Y + BTN_H + 4)
		return HIT_NONE;
	if (x >= BACK_X && x < BACK_X + BACK_W)
		return HIT_BACK;
	if (x >= SAVE_X && x < SAVE_X + SAVE_W)
		return HIT_SAVE;
	return HIT_NONE;
}

Hit uiDeleteDialogHit(int x, int y)
{
	if (y < 128 || y >= 160)
		return HIT_NONE;
	if (x >= 56 && x < 156)
		return HIT_CANCEL;
	if (x >= 164 && x < 264)
		return HIT_DELETE;
	return HIT_NONE;
}
