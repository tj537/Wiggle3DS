#include <string.h>
#include "app.h"
#include "draw.h"
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

static void background(u16 *fb)
{
	// Rendered once per theme; dithering every pixel each frame is not free.
	static u16 cache[320 * 240];
	if (!bgReady)
	{
		drawGradientV(cache, 320, 0, 0, 320, 240, C_BG_TOP, C_BG_BOTTOM);
		bgReady = true;
	}
	memcpy(fb, cache, sizeof(cache));
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

// drawImage with a filter's colors and vignette.
static void drawFiltered(u16 *fb, int sw, int dx, int dy, int dw, int dh,
                         const u16 *img, int stride, int sx, int sy, int w, int h, int filter, int radius)
{
	drawImage(fb, sw, dx, dy, dw, dh, img, stride, sx, sy, w, h, filterLut(filter), radius);
	int v = filterVignette(filter);
	if (v)
		drawVignette(fb, sw, dx, dy, dw, dh, v, radius);
}

// One frame of the wiggle loop: the left eye mixed with the right one.
static void drawWiggle(u16 *fb, int sw, int dx, int dy, int dw, int dh, const u16 *left, const u16 *right, int stride,
                       int lx, int ly, int rx, int ry, int w, int h, int filter, int mix)
{
	const u16 *lut = filterLut(filter);
	if (mix < 255)
		drawImage(fb, sw, dx, dy, dw, dh, left, stride, lx, ly, w, h, lut, 0);
	drawImageAlpha(fb, sw, dx, dy, dw, dh, right, stride, rx, ry, w, h, lut, mix);
	int v = filterVignette(filter);
	if (v)
		drawVignette(fb, sw, dx, dy, dw, dh, v, 0);
}

// The right eye's share in the current loop frame.
static int frameMix(const App *a, int smooth, int delayMs)
{
	int mix[MAX_WIGGLE_FRAMES], n = wiggleFrames(smooth, mix);
	return mix[(a->now / (u64)delayMs) % n];
}

static const ShotInfo *selectedInfo(const App *a)
{
	int cur = a->sel % GRID_PAGE;
	return a->gridOk[cur] ? &a->gridInfo[cur] : NULL;
}

static int shotDelay(const ShotInfo *info) { return info && info->delayMs > 0 ? info->delayMs : 120; }

bool uiTopWiggles(const App *a, int *delayMs)
{
	if (a->mode == MODE_REVIEW)
	{
		*delayMs = a->delayMs;
		return true;
	}
	if (a->browsing && a->playing && a->shotCount)
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
	if (!a->browsing)
	{
		int filter = a->filterMenu ? a->menuSel : a->filter;
		if (a->camOk && a->liveL)
			drawFiltered(fb, 400, 40, 0, 320, 240, eye ? a->liveR : a->liveL, CAM_W, 0, 0, CAM_W, CAM_H, filter, 0);
		else
			drawTextCentered(fb, 400, &fontBody, 200, 112, C_DIM, 255, "Camera unavailable");
		if (stereo)
			overlayPillLeft(fb, 400, 48, 8, "3D");
		if (a->filterMenu)
			overlayPill(fb, 400, 200, 212, tfmt("Preview · %s", filterName(filter)));
		else if (filter)
			overlayPill(fb, 400, 200, 212, filterName(filter));
		return;
	}

	if (!a->shotCount)
		return;
	int cur = a->sel % GRID_PAGE;
	const ShotInfo *info = selectedInfo(a);
	int delay = shotDelay(info);
	int filter = info ? SHOT_FILTER(info) : 0, smooth = info ? SHOT_SMOOTH(info) : 0;
	int mix = stereo ? (eye ? 255 : 0) : a->playing ? frameMix(a, smooth, delay) : 0;
	if (a->previewNum == a->shots[a->sel])
		drawWiggle(fb, 400, 40, 0, PREVIEW_W, PREVIEW_H, a->previewL, a->previewR, PREVIEW_W, 0, 0, 0, 0,
		           PREVIEW_W, PREVIEW_H, filter, mix);
	else if (a->gridOk[cur])
		drawFiltered(fb, 400, 40, 0, PREVIEW_W, PREVIEW_H, a->grid[cur], THUMB_W, 0, 0, THUMB_W, THUMB_H, filter, 0);

	overlayPillLeft(fb, 400, 48, 8, tfmt("WIG_%04d", a->shots[a->sel]));
	overlayPillRight(fb, 400, 352, 8, tfmt("%d / %d", a->sel + 1, a->shotCount));
	if (stereo)
		overlayPill(fb, 400, 200, 212, "3D view");
	else if (info)
		overlayPill(fb, 400, 200, 212, a->playing ? tfmt("%s · playing · %d ms", filterName(filter), delay)
		                                          : tfmt("%s · paused", filterName(filter)));
}

static void reviewTop(App *a, u16 *fb, int eye, bool stereo)
{
	drawClear(fb, 400, C_BLACK);
	Crop c = wiggleCrop(a->offset);
	int dx, dy, dw, dh;
	fitTop(c.w, c.h, &dx, &dy, &dw, &dh);
	int mix = stereo ? (eye ? 255 : 0) : frameMix(a, a->shotSmooth, a->delayMs);
	drawWiggle(fb, 400, dx, dy, dw, dh, a->shotL, a->shotR, CAM_W, c.lx, c.ly, c.rx, c.ry, c.w, c.h, a->shotFilter, mix);

	overlayPillLeft(fb, 400, dx + 8, 8, a->shotNum < 0 ? "New" : tfmt("WIG_%04d", a->shotNum));
	overlayPill(fb, 400, 200, 212, stereo ? "3D view" : tfmt("%s · %s · %d ms", filterName(a->shotFilter),
	                                                          wiggleSmoothName(a->shotSmooth), a->delayMs));
}

static void shareTop(App *a, u16 *fb);

void uiDrawTop(App *a, u16 *left, u16 *right, bool stereo)
{
	for (int eye = 0; eye <= (stereo ? 1 : 0); eye++)
	{
		u16 *fb = eye ? right : left;
		if (a->mode == MODE_HOME || a->mode == MODE_SETTINGS)
			homeTop(a, fb, eye, stereo);
		else if (a->mode == MODE_REVIEW)
			reviewTop(a, fb, eye, stereo);
		else
			shareTop(a, fb);

		u64 flash = a->now - a->flashAt, fade = a->now - a->modeSince;
		if (a->flashAt && flash < FLASH_MS)
			drawFill(fb, 400, 0, 0, 400, 240, C_WHITE, 220 - (int)flash * 220 / FLASH_MS);
		else if (fade < FADE_MS)
			drawFill(fb, 400, 0, 0, 400, 240, C_BLACK, 255 - (int)fade * 255 / FADE_MS);
	}
}

// ---- bottom screen: home ----

static int shareButtonWidth(void) { return hintWidth("SELECT", "Share") + 16; }

// cos/sin of the gear's 8 teeth.
static const float cosTable[8] = {1, 0.7071f, 0, -0.7071f, -1, -0.7071f, 0, 0.7071f};
static const float sinTable[8] = {0, 0.7071f, 1, 0.7071f, 0, -0.7071f, -1, -0.7071f};

bool uiGearHit(int x, int y)
{
	int cx = 308 - shareButtonWidth() - 8 - GEAR_R, cy = HEADER_BTN_Y + HEADER_BTN_H / 2;
	return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= (GEAR_R + 5) * (GEAR_R + 5);
}

static int filterButtonWidth(const App *a)
{
	return 23 + textWidth(&fontSmall, filterName(a->filter)) + 5 + textWidth(&fontSmall, "▼") + 10;
}

bool uiFilterButtonHit(const App *a, int x, int y)
{
	int fw = filterButtonWidth(a), fx = FILTER_BTN_X;
	return x >= fx - 3 && x < fx + fw + 3 && y >= HEADER_BTN_Y - 4 && y < HEADER_BTN_Y + HEADER_BTN_H + 4;
}

int uiMenuTileAt(int x, int y)
{
	for (int i = 0; i < FILTER_COUNT; i++)
	{
		int bx = MENU_X + (i % MENU_COLS) * (MENU_BTN_W + MENU_GAP), by = MENU_Y + (i / MENU_COLS) * MENU_ROW;
		if (x >= bx - MENU_GAP / 2 && x < bx + MENU_BTN_W + MENU_GAP / 2 && y >= by - MENU_GAP / 2 && y < by + MENU_BTN_H + MENU_GAP / 2)
			return i;
	}
	return -1;
}

bool uiShareButtonHit(int x, int y)
{
	int w = shareButtonWidth();
	return x >= 308 - w - 4 && x < 312 && y >= HEADER_BTN_Y - 4 && y < HEADER_BTN_Y + HEADER_BTN_H + 4;
}

static void homeEmpty(u16 *fb)
{
	// A little stack of photos.
	drawRoundRectOutline(fb, 320, 140, 66, 56, 42, 9, 2, C_STROKE, 255);
	drawShadow(fb, 320, 128, 76, 56, 42, 9, 160);
	drawRoundRectGrad(fb, 320, 128, 76, 56, 42, 9, C_ACC0, C_ACC1, 255);
	drawCircle(fb, 320, 142, 88, 4, C_TEXT, 230);
	drawRoundRect(fb, 320, 136, 104, 40, 6, 3, C_TEXT, 160);

	drawTextCentered(fb, 320, &fontBody, 160, 136, C_TEXT, 255, "No wigglegrams yet");
	drawTextCentered(fb, 320, &fontSmall, 160, 156, C_DIM, 255, "Take a photo and save it to see it here.");
}

// Filter picker over the whole bottom screen: plain buttons; the live view
// on top previews the selected one.
static void filterMenu(App *a, u16 *fb)
{
	// Each button shows what its filter does to skin, sky and leaves.
	static const u16 samples[3] = {COL565(226, 168, 138), COL565(92, 150, 222), COL565(84, 160, 92)};

	background(fb);
	drawText(fb, 320, &fontTitle, 16, 9, C_TEXT, 255, "Filter");
	const char *note = "Preview on the top screen";
	drawText(fb, 320, &fontSmall, 304 - textWidth(&fontSmall, note), 17, C_DIM, 255, note);

	for (int i = 0; i < FILTER_COUNT; i++)
	{
		int x = MENU_X + (i % MENU_COLS) * (MENU_BTN_W + MENU_GAP), y = MENU_Y + (i / MENU_COLS) * MENU_ROW;
		bool sel = i == a->menuSel, current = i == a->filter;
		if (sel)
			drawRoundRectGrad(fb, 320, x, y, MENU_BTN_W, MENU_BTN_H, 12, C_ACC0, C_ACC1, 255);
		else
			drawRoundRect(fb, 320, x, y, MENU_BTN_W, MENU_BTN_H, 12, C_SURFACE2, 255);
		if (current && !sel)
			drawRoundRectOutline(fb, 320, x, y, MENU_BTN_W, MENU_BTN_H, 12, 1, C_ACC1, 255);

		const u16 *lut = filterLut(i);
		for (int k = 0; k < 3; k++)
		{
			float cx = x + 13 + k * 7, cy = y + MENU_BTN_H / 2.0f;
			drawCircle(fb, 320, cx, cy, 4.5f, C_BLACK, 90);
			drawCircle(fb, 320, cx, cy, 3.5f, lut ? lut[samples[k]] : samples[k], 255);
		}
		drawText(fb, 320, &fontSmall, x + 36, y + (MENU_BTN_H - lineH(&fontSmall)) / 2, sel || current ? C_TEXT : C_DIM, 255,
		         current ? tfmt("%s ✓", filterName(i)) : filterName(i));
	}

	static const char *const btns[] = {"A", "B"}, *const labels[] = {"Apply", "Cancel"};
	centeredHints(fb, 218, 2, btns, labels);
}

static void homeBottom(App *a, u16 *fb)
{
	background(fb);

	int sw = shareButtonWidth(), ty = HEADER_BTN_Y + (HEADER_BTN_H - lineH(&fontSmall)) / 2;
	drawRoundRect(fb, 320, 308 - sw, HEADER_BTN_Y, sw, HEADER_BTN_H, HEADER_BTN_H / 2, C_SURFACE2, 255);
	drawHint(fb, 320, 308 - sw + 8, ty, "SELECT", "Share", C_TEXT, 255);

	// Settings: a little gear left of Share.
	float gx = 308 - sw - 8 - GEAR_R, gy = HEADER_BTN_Y + HEADER_BTN_H / 2.0f;
	drawCircle(fb, 320, gx, gy, GEAR_R, C_SURFACE2, 255);
	for (int i = 0; i < 8; i++)
		drawCircle(fb, 320, gx + 6.2f * cosTable[i], gy + 6.2f * sinTable[i], 1.7f, C_TEXT, 255);
	drawRing(fb, 320, gx, gy, 5.2f, 2.2f, C_TEXT, 255);

	// Filter button: a gradient dot, the current filter and a caret.
	int fw = filterButtonWidth(a), fx = FILTER_BTN_X;
	drawRoundRect(fb, 320, fx, HEADER_BTN_Y, fw, HEADER_BTN_H, HEADER_BTN_H / 2, a->filterMenu ? C_STROKE : C_SURFACE2, 255);
	drawCircleGrad(fb, 320, fx + 13, HEADER_BTN_Y + HEADER_BTN_H / 2.0f, 4.5f, C_ACC0, C_ACC1, 255);
	int nx = fx + 23 + drawText(fb, 320, &fontSmall, fx + 23, ty, C_TEXT, 255, filterName(a->filter));
	drawText(fb, 320, &fontSmall, nx + 5, ty, C_DIM, 255, "▼");

	if (!a->shotCount)
		homeEmpty(fb);
	else
	{
		int page = a->sel / GRID_PAGE;

		for (int i = 0; i < GRID_PAGE && page * GRID_PAGE + i < a->shotCount; i++)
		{
			int x = GRID_X + (i % GRID_COLS) * (CELL_W + CELL_GAP), y = GRID_Y + (i / GRID_COLS) * (CELL_H + CELL_GAP);
			if (a->browsing && i == a->sel % GRID_PAGE)
				drawRoundRectGrad(fb, 320, x - 3, y - 3, CELL_W + 6, CELL_H + 6, 9, C_ACC0, C_ACC1, 255);
			if (a->gridOk[i])
				drawFiltered(fb, 320, x, y, CELL_W, CELL_H, a->grid[i], THUMB_W, 0, 0, THUMB_W, THUMB_H, SHOT_FILTER(&a->gridInfo[i]), 6);
			else
			{
				drawRoundRect(fb, 320, x, y, CELL_W, CELL_H, 6, C_SURFACE2, 255);
				drawTextCentered(fb, 320, &fontSmall, x + CELL_W / 2, y + CELL_H / 2 - 6, C_DIM, 255, "old");
			}
		}
	}

	if (a->browsing)
	{
		static const char *const btns[] = {"A", "Y", "X", "B"}, *const labels[] = {"Edit", "Play", "Delete", "Camera"};
		centeredHints(fb, 218, 4, btns, labels);
	}
	else
	{
		static const char *const btns[] = {"A", "Y", "◀▶", "START"}, *const labels[] = {"Capture", "Filter", "Browse", "Quit"};
		if (!a->shotCount)
			centeredHints(fb, 218, 3, (const char *const[]){"A", "Y", "START"}, (const char *const[]){"Capture", "Filter", "Quit"});
		else
			centeredHints(fb, 218, 4, btns, labels);
	}

	if (a->filterMenu)
		filterMenu(a, fb);

	if (a->confirmDelete)
	{
		dialog(fb, 40, 66, 240, 104);
		drawTextCentered(fb, 320, &fontBody, 160, 82, C_TEXT, 255, tfmt("Delete WIG_%04d?", a->shots[a->sel]));
		drawTextCentered(fb, 320, &fontSmall, 160, 104, C_DIM, 255, "The photo and its GIF will be removed.");
		button(fb, 56, 132, 100, "B", "Cancel", false, C_SURFACE2);
		button(fb, 164, 132, 100, "X", "Delete", false, C_RED);
	}
}

// ---- bottom screen: review ----

static void reviewBottom(App *a, u16 *fb)
{
	background(fb);

	drawShadow(fb, 320, CARD_X, CARD_Y, CARD_W, CARD_H, 10, 160);
	drawFiltered(fb, 320, CARD_X, CARD_Y, CARD_W, CARD_H, a->shotL, CAM_W, 0, 0, CAM_W, CAM_H, a->shotFilter, 10);
	const char *fl = tfmt("▲▼  %s", filterName(a->shotFilter));
	int fw = textWidth(&fontSmall, fl) + 20;
	drawRoundRect(fb, 320, FILTER_PILL_X, FILTER_PILL_Y, fw, 20, 10, C_BLACK, 150);
	drawText(fb, 320, &fontSmall, FILTER_PILL_X + 10, FILTER_PILL_Y + (20 - lineH(&fontSmall)) / 2, C_TEXT, 255, fl);
	const char *sm = wiggleSmoothName(a->shotSmooth);
	int smw = hintWidth("X", sm) + 16, smx = CARD_X + CARD_W - 8 - smw;
	drawRoundRect(fb, 320, smx, FILTER_PILL_Y, smw, 20, 10, C_BLACK, 150);
	drawHint(fb, 320, smx + 8, FILTER_PILL_Y + (20 - lineH(&fontSmall)) / 2, "X", sm, C_TEXT, 255);
	float px = CARD_X + (a->pivotX + 0.5f) * CARD_W / CAM_W, py = CARD_Y + (a->pivotY + 0.5f) * CARD_H / CAM_H;
	drawCircle(fb, 320, px, py, 12, C_BLACK, 70);
	drawRing(fb, 320, px, py, 10, 2, C_TEXT, 240);
	drawCircleGrad(fb, 320, px, py, 3.5f, C_ACC0, C_ACC1, 255);

	if (!a->toast[0] || since(a, a->toastAt) >= TOAST_MS) // the toast sits here
	{
		const char *hint = "Tap your subject · L/R to fine-tune";
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

	button(fb, BACK_X, BTN_Y, BACK_W, "B", "Back", false, C_SURFACE2);
	button(fb, SAVE_X, BTN_Y, SAVE_W, "A", "Save GIF", true, 0);

	if (a->saving)
	{
		dialog(fb, 60, 84, 200, 72);
		drawTextCentered(fb, 320, &fontBody, 160, 100, C_TEXT, 255, "Saving GIF …");
		drawTextCentered(fb, 320, &fontSmall, 160, 122, C_DIM, 255, "One moment");
		drawRoundRectGrad(fb, 320, 100, 140, 120, 3, 1, C_ACC0, C_ACC1, 255);
	}
}

// ---- settings ----

static const char *const settingLabels[SET_COUNT] = {"Theme", "Transitions", "Speed", "GIF size", "Viewfinder", "Capture flash"};

const char *uiSettingValue(const Settings *st, int row)
{
	switch (row)
	{
	case SET_THEME: return uiThemeName(st->theme);
	case SET_SMOOTH: return wiggleSmoothName(st->smooth);
	case SET_SPEED: return tfmt("%d ms", st->delayMs);
	case SET_GIF_SIZE: return st->gifHalf ? "Half" : "Full";
	case SET_FPS: return tfmt("%d fps", st->fps);
	case SET_FLASH: return st->flash ? "On" : "Off";
	}
	return "";
}

#define SET_VALUE_X0 176 // left arrow
#define SET_VALUE_X1 300 // right arrow

int uiSettingsRowAt(int x, int y, int *step)
{
	if (y < SET_Y || y >= SET_Y + SET_COUNT * SET_ROW)
		return -1;
	*step = x >= SET_VALUE_X0 - 10 && x < SET_VALUE_X0 + 22 ? -1 : x >= SET_VALUE_X1 - 22 ? 1 : 0;
	return (y - SET_Y) / SET_ROW;
}

static void settingsBottom(App *a, u16 *fb)
{
	static const char *const notes[SET_COUNT] = {
		"", "for new photos", "for new photos", "", "saves battery", "",
	};
	background(fb);
	drawText(fb, 320, &fontTitle, 16, 9, C_TEXT, 255, "Settings");

	for (int i = 0; i < SET_COUNT; i++)
	{
		int y = SET_Y + i * SET_ROW, cy = y + SET_ROW_H / 2;
		bool sel = i == a->settingsSel;
		drawRoundRect(fb, 320, 12, y, 296, SET_ROW_H, SET_ROW_H / 2, sel ? C_SURFACE2 : C_SURFACE, 255);
		if (sel)
			drawRoundRectOutline(fb, 320, 12, y, 296, SET_ROW_H, SET_ROW_H / 2, 1, C_ACC1, 255);

		int lx = 24 + drawText(fb, 320, &fontSmall, 24, cy - lineH(&fontSmall) / 2, C_TEXT, 255, settingLabels[i]);
		if (notes[i][0])
			drawText(fb, 320, &fontSmall, lx + 6, cy - lineH(&fontSmall) / 2, C_DIM, 200, notes[i]);

		u16 arrow = sel ? C_TEXT : C_DIM;
		drawText(fb, 320, &fontSmall, SET_VALUE_X0, cy - lineH(&fontSmall) / 2, arrow, 255, "◀");
		drawText(fb, 320, &fontSmall, SET_VALUE_X1 - textWidth(&fontSmall, "▶"), cy - lineH(&fontSmall) / 2, arrow, 255, "▶");
		int vx = (SET_VALUE_X0 + SET_VALUE_X1) / 2;
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
		if (a->sharedCount)
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

// A pill in the accent gradient (red for errors) that fades in and out.
static void toast(App *a, u16 *fb)
{
	u64 t = since(a, a->toastAt);
	if (!a->toast[0] || t >= TOAST_MS)
		return;
	int alpha = t < 150 ? (int)t * 255 / 150 : t > TOAST_MS - 400 ? (int)(TOAST_MS - t) * 255 / 400 : 255;
	const char *text = tfmt("%s %s", a->toastError ? "×" : "✓", a->toast);
	int w = textWidth(&fontBody, text) + 32, h = 30, x = 160 - w / 2, y = 10;
	drawShadow(fb, 320, x, y, w, h, h / 2, alpha * 160 / 255);
	if (a->toastError)
		drawRoundRect(fb, 320, x, y, w, h, h / 2, C_RED, alpha);
	else
		drawRoundRectGrad(fb, 320, x, y, w, h, h / 2, C_ACC0, C_ACC1, alpha);
	drawTextCentered(fb, 320, &fontBody, 160, y + (h - lineH(&fontBody)) / 2, C_WHITE, alpha, text);
}

void uiDrawBottom(App *a, u16 *fb)
{
	if (a->mode == MODE_HOME)
		homeBottom(a, fb);
	else if (a->mode == MODE_REVIEW)
		reviewBottom(a, fb);
	else if (a->mode == MODE_SETTINGS)
		settingsBottom(a, fb);
	else
		shareBottom(a, fb);
	toast(a, fb);

	u64 t = a->now - a->modeSince;
	if (t < FADE_MS)
		drawFill(fb, 320, 0, 0, 320, 240, C_BLACK, 255 - (int)t * 255 / FADE_MS);
}

bool uiTopAnimating(const App *a)
{
	return a->now - a->modeSince < FADE_MS + 20 || (a->flashAt && a->now - a->flashAt < FLASH_MS + 20);
}

bool uiBottomAnimating(const App *a)
{
	return since(a, a->modeSince) < FADE_MS + 20 || (a->toast[0] && since(a, a->toastAt) < TOAST_MS + 20);
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

int uiCellAt(int x, int y)
{
	for (int i = 0; i < GRID_PAGE; i++)
	{
		int cx = GRID_X + (i % GRID_COLS) * (CELL_W + CELL_GAP), cy = GRID_Y + (i / GRID_COLS) * (CELL_H + CELL_GAP);
		if (x >= cx - CELL_GAP / 2 && x < cx + CELL_W + CELL_GAP / 2 && y >= cy - CELL_GAP / 2 && y < cy + CELL_H + CELL_GAP / 2)
			return i;
	}
	return -1;
}
