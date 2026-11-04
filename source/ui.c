#include "ui_priv.h"

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

u16 uiPalette[9];
static bool bgReady;

// Frame time of the last drawn frame per screen; see uiTopAnimating.
static u64 topDrawnAt, bottomDrawnAt;

int lineH(const Font *f) { return f->ascent + f->descent; }
u64 since(const App *a, u64 t) { return a->now > t ? a->now - t : 0; }

void uiSetTheme(int theme)
{
	const Theme *t = &themes[theme >= 0 && theme < THEME_COUNT ? theme : 0];
	for (int i = 0; i < 9; i++)
		uiPalette[i] = COL565(t->c[i][0], t->c[i][1], t->c[i][2]);
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

void background(u16 *fb)
{
	if (!bgReady)
	{
		drawGradientV(bgCache, 320, 0, 0, 320, 240, C_BG_TOP, C_BG_BOTTOM);
		bgReady = true;
	}
	memcpy(fb, bgCache, sizeof(bgCache));
}

// The background again over rows y0..y1-1 (to clip what scrolls under them).
void backgroundRows(u16 *fb, int y0, int y1)
{
	for (int x = 0; x < 320; x++) // columns run bottom to top
		memcpy(fb + x * 240 + 240 - y1, bgCache + x * 240 + 240 - y1, (y1 - y0) * sizeof(u16));
}

// With the 3D slider up, what lies over the picture on the top screen
// (pills, cards, messages) floats a little in front of it: each eye draws
// it shifted toward the other. Set per eye in uiDrawTop.
static int uiPop;

// Translucent dark pill with small text, centered on cx.
void overlayPill(u16 *fb, int sw, int cx, int y, const char *text)
{
	if (sw == 400)
		cx += uiPop;
	int w = textWidth(&fontSmall, text) + 20, h = 20;
	drawRoundRect(fb, sw, cx - w / 2, y, w, h, h / 2, C_BLACK, 150);
	drawTextCentered(fb, sw, &fontSmall, cx, y + (h - lineH(&fontSmall)) / 2, C_TEXT, 255, text);
}

void overlayPillLeft(u16 *fb, int sw, int x, int y, const char *text)
{
	overlayPill(fb, sw, x + (textWidth(&fontSmall, text) + 20) / 2, y, text);
}

void overlayPillRight(u16 *fb, int sw, int x, int y, const char *text)
{
	overlayPill(fb, sw, x - (textWidth(&fontSmall, text) + 20) / 2, y, text);
}

// A pill button with a button hint inside; filled with a gradient when `primary`.
void button(u16 *fb, int x, int y, int w, const char *btn, const char *label, bool primary, u16 fill)
{
	if (primary)
		drawRoundRectGrad(fb, 320, x, y, w, BTN_H, BTN_H / 2, C_ACC0, C_ACC1, 255);
	else
		drawRoundRect(fb, 320, x, y, w, BTN_H, BTN_H / 2, fill, 255);
	int hw = hintWidth(btn, label);
	drawHint(fb, 320, x + (w - hw) / 2, y + (BTN_H - lineH(&fontSmall)) / 2, btn, label, C_TEXT, 255);
}

void centeredHints(u16 *fb, int y, int n, const char *const *btns, const char *const *labels)
{
	int gap = 14, w = gap * (n - 1);
	for (int i = 0; i < n; i++)
		w += hintWidth(btns[i], labels[i]);
	int x = 160 - w / 2;
	for (int i = 0; i < n; i++)
		x += drawHint(fb, 320, x, y, btns[i], labels[i], C_DIM, 255) + gap;
}

void dialog(u16 *fb, int x, int y, int w, int h)
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
void drawFiltered(u16 *fb, int sw, int dx, int dy, int dw, int dh,
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
		return !uiShotIsPhoto(a); // a photo stands still, moving filter or not
	}
	const ShotInfo *info = selectedInfo(a);
	if (a->mode == MODE_GALLERY && a->playing && a->shotCount && !(info && SHOT_IS_PHOTO(info)))
	{
		*delayMs = shotDelay(info);
		return true;
	}
	return false;
}

// ---- top screen ----

// Plain photos keep a moving filter still, as in the JPG.
bool photoMode(const App *a) { return a->selfie || a->settings.smooth == SMOOTH_OFF; }

static void homeTop(App *a, u16 *fb, int eye, bool stereo)
{
	drawClear(fb, 400, C_BLACK);
	int filter = a->filterMenu ? a->menuSel : a->settings.filter;
	bool still = photoMode(a);
	int frame = still ? 0 : (int)(a->now / LIVE_GLITCH_MS);
	if (a->camOk && a->liveL && a->wigglePreview && !still)
	{
		// The wiggle before the shot: the eyes in turn, lined up on the
		// middle, at the set speed; the same in both eyes of the 3D screen.
		Crop c = wiggleCrop(a->liveOffset);
		bool r = a->now / (a->settings.delayMs > 0 ? a->settings.delayMs : 120) & 1;
		drawFiltered(fb, 400, 40, 0, 320, 240, r ? a->liveR : a->liveL, CAM_W, r ? c.rx : c.lx, r ? c.ry : c.ly, c.w, c.h,
		             filter, 0, frame, a->settings.strength);
	}
	else if (a->camOk && a->liveL)
		drawFiltered(fb, 400, 40, 0, 320, 240, eye ? a->liveR : a->liveL, CAM_W, 0, 0, CAM_W, CAM_H, filter, 0,
		             frame, a->settings.strength);
	else if (!a->camStarting)
		drawTextCentered(fb, 400, &fontBody, 200, 112, C_DIM, 255, "Camera unavailable");
	if (a->settings.grid)
		for (int k = 1; k <= 2; k++)
		{
			drawFill(fb, 400, 40 + k * 320 / 3, 0, 1, 240, C_WHITE, 70);
			drawFill(fb, 400, 40, k * 240 / 3, 320, 1, C_WHITE, 70);
		}
	if (a->stacking || a->waitSteady)
	{
		// Night shot: viewfinder corners, and a card with a moon. While
		// waiting for a still hand a bar shows how still it is; then one
		// segment per frame taken.
		for (int k = 0; k < 4; k++)
		{
			int cx = k & 1 ? 344 : 56, cy = k & 2 ? 224 : 16, sx = k & 1 ? -1 : 1, sy = k & 2 ? -1 : 1;
			drawFill(fb, 400, sx > 0 ? cx : cx - 18, sy > 0 ? cy : cy - 2, 18, 2, C_WHITE, 220);
			drawFill(fb, 400, sx > 0 ? cx : cx - 2, sy > 0 ? cy : cy - 18, 2, 18, C_WHITE, 220);
		}
		int mid = 200 + uiPop, cw = 176, ch = 60, cx = mid - cw / 2, cy = 120 - ch / 2;
		drawRoundRect(fb, 400, cx, cy, cw, ch, 18, C_BLACK, 180);
		drawIcon(fb, 400, ICON_MOON, cx + 26, cy + ch / 2.0f, 22, C_ACC1, 255);
		int tx = cx + 48;
		drawText(fb, 400, &fontBody, tx, cy + 10, C_WHITE, 255, a->waitSteady ? "Hold still …" : "Hold still");
		int bw = cw - 48 - 16, by = cy + ch - 18;
		if (a->waitSteady)
		{
			int still = 255 - (a->shake > 64 ? 255 : a->shake * 4);
			drawRoundRect(fb, 400, tx, by, bw, 6, 3, C_WHITE, 60);
			drawRoundRectGrad(fb, 400, tx, by, 6 + (bw - 6) * still / 255, 6, 3, C_ACC0, C_ACC1, 255);
		}
		else
		{
			int n = a->stackFrames, gap = 4, sw = (bw - (n - 1) * gap) / n;
			for (int k = 0; k < n; k++)
			{
				if (k < a->stacking)
					drawRoundRectGrad(fb, 400, tx + k * (sw + gap), by, sw, 6, 3, C_ACC0, C_ACC1, 255);
				else
					drawRoundRect(fb, 400, tx + k * (sw + gap), by, sw, 6, 3, C_WHITE, 60);
			}
		}
	}
	else if (a->tooClose && !a->selfie && !still && !a->timerAt && !a->filterMenu)
	{
		// The most common reason for a poor wiggle, said before the shot.
		int mid = 200 + uiPop, w = 176, x = mid - w / 2;
		drawRoundRect(fb, 400, x, 10, w, 26, 13, C_BLACK, 170);
		drawTextCentered(fb, 400, &fontSmall, mid, 10 + (26 - lineH(&fontSmall)) / 2, C_WHITE, 255, "Too close · step back");
	}
	if (a->timerAt)
	{
		int left = a->timerAt > a->now ? (int)((a->timerAt - a->now + 999) / 1000) : 0;
		// The ring runs down within each second.
		float part = a->timerAt > a->now ? ((a->timerAt - a->now) % 1000) / 1000.0f : 0;
		int mid = 200 + uiPop;
		drawCircle(fb, 400, mid, 120, 36, C_BLACK, 160);
		drawRing(fb, 400, mid, 120, 36, 2, C_WHITE, 60);
		drawRing(fb, 400, mid, 120, 36, 4, C_ACC1, (int)(80 + 175 * part));
		drawTextCentered(fb, 400, &fontTitle, mid, 120 - lineH(&fontTitle) / 2, C_WHITE, 255, tfmt("%d", left));
	}
	if (a->wigglePreview && !still && !a->stacking && !a->waitSteady)
		overlayPill(fb, 400, 200, 212, "Wiggle preview");
	if (a->haveFirst) // the first exposure over the live view, to line up the second
		drawImageAlpha(fb, 400, 40, 0, 320, 240, eye ? a->firstR : a->firstL, CAM_W, 0, 0, CAM_W, CAM_H, filterLut(filter), 110);
	(void)stereo; // a clean viewfinder: what is on shows on the bottom screen
}

// A filter's name, with its strength when below 100 %.
const char *filterLabel(int filter, int strength)
{
	return filter && strength < 100 ? tfmt("%s %d%%", filterName(filter), strength) : filterName(filter);
}

// The transition by name; "Photo" when off.
const char *smoothLabel(int smooth) { return smooth == SMOOTH_OFF ? "Photo" : wiggleSmoothName(smooth); }

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
		           a->playing && !photo ? loopStep(a, filter, smooth, delay) : 0, info ? SHOT_STRENGTH(info) : 100);
	else if (info)
		drawImage(fb, 400, 40, 0, PREVIEW_W, PREVIEW_H, a->grid[cur], THUMB_W, 0, 0, THUMB_W, THUMB_H, NULL, 0); // filtered already

	overlayPillLeft(fb, 400, 48, 8, tfmt("WIG_%04d", a->shots[a->sel]));
	overlayPillRight(fb, 400, 352, 8, tfmt("%d / %d", a->sel + 1, a->shotCount));
	if (stereo)
		overlayPill(fb, 400, 200, 212, "3D view");
	else if (info)
		overlayPill(fb, 400, 200, 212, photo ? tfmt("%s · photo", filterLabel(filter, SHOT_STRENGTH(info)))
		                               : a->playing ? tfmt("%s · %s · %d ms", filterLabel(filter, SHOT_STRENGTH(info)), smoothLabel(smooth), delay)
		                                          : tfmt("%s · paused", filterName(filter)));
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
	           uiShotIsPhoto(a) ? 0 : loopStep(a, filter, a->shotSmooth, a->delayMs), a->shotStrength);

	if (a->shotNum >= 0)
		overlayPillLeft(fb, 400, dx + 8, 8, tfmt("WIG_%04d", a->shotNum));
	if (a->compare)
		overlayPill(fb, 400, 200, 212, "Original");
	else
		overlayPill(fb, 400, 200, 212, uiShotIsPhoto(a) ? filterLabel(a->shotFilter, a->shotStrength) : stereo ? "3D view" : tfmt("%s · %s · %d ms", filterLabel(a->shotFilter, a->shotStrength),
	                                                          smoothLabel(a->shotSmooth), a->delayMs));
}

static void toast(App *a, u16 *fb);

void uiDrawTop(App *a, u16 *left, u16 *right, bool stereo)
{
	topDrawnAt = a->now;
	for (int eye = 0; eye <= (stereo ? 1 : 0); eye++)
	{
		u16 *fb = eye ? right : left;
		uiPop = stereo ? (eye ? -1 : 1) * (int)(a->depth * 5 + 0.5f) : 0;
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
	int w = 14 + 10 + 8 + tw + 16 + uw, h = TOAST_H, x = 200 + uiPop - w / 2, y = TOAST_Y + rise;
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
	// Home's little animations: the filter name sliding in, the shutter
	// coming back up, a switch fading in.
	bool home = a->mode == MODE_HOME && !a->filterMenu &&
	            (during(bottomDrawnAt, a->filterAt, 240) || during(bottomDrawnAt, a->shutterAt, 220) || during(bottomDrawnAt, a->switchAt, 200) ||
	             during(bottomDrawnAt, a->keyHitAt, 170));
	return during(bottomDrawnAt, a->modeSince, FADE_MS) || home ||
	       (a->mode == MODE_REVIEW && !a->settings.tipDone && !uiShotIsPhoto(a));
}

// ---- touch targets ----

bool uiFilterPillHit(int x, int y)
{
	return x >= FILTER_PILL_X - 4 && x < FILTER_PILL_X + 90 && y >= FILTER_PILL_Y - 4 && y < FILTER_PILL_Y + 24;
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
