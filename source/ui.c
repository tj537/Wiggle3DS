#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "draw.h"
#include "filter.h"

// Dark neon theme.
#define C_BG_TOP COL565(22, 20, 34)
#define C_BG_BOTTOM COL565(9, 9, 15)
#define C_SURFACE COL565(30, 29, 44)
#define C_SURFACE2 COL565(44, 42, 62)
#define C_STROKE COL565(70, 66, 96)
#define C_TEXT COL565(246, 245, 250)
#define C_DIM COL565(150, 147, 172)
#define C_ACC0 COL565(255, 61, 154)
#define C_ACC1 COL565(255, 140, 64)
#define C_RED COL565(255, 76, 92)
#define C_BLACK 0

#define FADE_MS 180
#define TOAST_MS 2600

static int lineH(const Font *f) { return f->ascent + f->descent; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void background(u16 *fb)
{
	// Rendered once; dithering every pixel each frame is not free.
	static u16 cache[320 * 240];
	static bool ready;
	if (!ready)
	{
		drawGradientV(cache, 320, 0, 0, 320, 240, C_BG_TOP, C_BG_BOTTOM);
		ready = true;
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

static int buttonWidth(const char *btn, const char *label) { return hintWidth(btn, label) + 22; }

static void centeredHints(u16 *fb, int y, int n, const char *const *btns, const char *const *labels)
{
	int gap = 14, w = gap * (n - 1);
	for (int i = 0; i < n; i++)
		w += hintWidth(btns[i], labels[i]);
	int x = 160 - w / 2;
	for (int i = 0; i < n; i++)
		x += drawHint(fb, 320, x, y, btns[i], labels[i], C_DIM, 255) + gap;
}

// Fits a w*h image into the top screen; returns the box.
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

static bool wigglePhase(const App *a, int delayMs) { return (a->now / (u64)delayMs) & 1; }

// ---- filter chips (camera) ----

static int chipWidth(int i) { return textWidth(&fontBody, filterName(i)) + 24; }

static float chipTarget(int sel)
{
	int total = CHIP_GAP * (FILTER_COUNT - 1), pos = 0;
	for (int i = 0; i < FILTER_COUNT; i++)
	{
		if (i < sel)
			pos += chipWidth(i) + CHIP_GAP;
		total += chipWidth(i);
	}
	if (total + 24 <= 320)
		return (320 - total) / 2;
	float t = 160 - (pos + chipWidth(sel) / 2.0f);
	if (t > 12) t = 12;
	if (t < 308 - total) t = 308 - total;
	return t;
}

int uiChipAt(const App *a, int x, int y)
{
	if (y < CHIPS_Y - 4 || y >= CHIPS_Y + CHIP_H + 4)
		return -1;
	int cx = (int)a->chipScroll;
	for (int i = 0; i < FILTER_COUNT; i++)
	{
		int w = chipWidth(i);
		if (x >= cx - CHIP_GAP / 2 && x < cx + w + CHIP_GAP / 2)
			return i;
		cx += w + CHIP_GAP;
	}
	return -1;
}

static void drawChips(App *a, u16 *fb)
{
	a->chipScroll += (chipTarget(a->filter) - a->chipScroll) * 0.3f;
	int x = (int)a->chipScroll, ty = CHIPS_Y + (CHIP_H - lineH(&fontBody)) / 2;
	for (int i = 0; i < FILTER_COUNT; i++)
	{
		int w = chipWidth(i);
		if (i == a->filter)
		{
			drawRoundRectGrad(fb, 320, x, CHIPS_Y, w, CHIP_H, CHIP_H / 2, C_ACC0, C_ACC1, 255);
			drawTextCentered(fb, 320, &fontBody, x + w / 2, ty, C_TEXT, 255, filterName(i));
		}
		else
		{
			drawRoundRect(fb, 320, x, CHIPS_Y, w, CHIP_H, CHIP_H / 2, C_SURFACE2, 255);
			drawTextCentered(fb, 320, &fontBody, x + w / 2, ty, C_DIM, 255, filterName(i));
		}
		x += w + CHIP_GAP;
	}
	// Fade the row out at the screen edges.
	u16 edge = COL565(15, 14, 24);
	for (int i = 0; i < 18; i++)
	{
		int alpha = 255 - i * 255 / 18;
		drawFill(fb, 320, i, CHIPS_Y, 1, CHIP_H, edge, alpha);
		drawFill(fb, 320, 319 - i, CHIPS_Y, 1, CHIP_H, edge, alpha);
	}
}

// ---- camera ----

static void cameraTop(App *a, u16 *fb, int eye, bool stereo)
{
	drawClear(fb, 400, C_BLACK);
	const u16 *lut = filterLut(a->filter);
	if (a->camOk && a->liveL)
		drawImage(fb, 400, 40, 0, 320, 240, eye ? a->liveR : a->liveL, CAM_W, 0, 0, CAM_W, CAM_H, lut, 0);
	else
		drawTextCentered(fb, 400, &fontBody, 200, 112, C_DIM, 255, "Kamera nicht verfügbar");

	if (stereo)
		overlayPillLeft(fb, 400, 48, 8, "3D");
	overlayPill(fb, 400, 200, 212, filterName(a->filter));

	// Big filter name right after switching, fading out.
	u64 t = a->now - a->filterChangedAt;
	if (a->filterChangedAt && t < 900)
	{
		int alpha = t < 400 ? 255 : 255 - (int)(t - 400) * 255 / 500;
		drawTextCentered(fb, 400, &fontHuge, 202, 104, C_BLACK, alpha / 2, filterName(a->filter));
		drawTextCentered(fb, 400, &fontHuge, 200, 102, C_TEXT, alpha, filterName(a->filter));
	}
}

static void cameraBottom(App *a, u16 *fb)
{
	background(fb);
	drawTextGrad(fb, 320, &fontTitle, 16, 10, C_ACC0, C_ACC1, "Wiggle3DS");
	int hw = hintWidth("START", "Beenden");
	drawHint(fb, 320, 304 - hw, 16, "START", "Beenden", C_DIM, 255);

	// Gallery button with the newest shot.
	int gx = GALLERY_BTN_X, gy = GALLERY_BTN_Y, gs = GALLERY_BTN_S;
	drawShadow(fb, 320, gx, gy, gs, gs, 14, 120);
	if (a->lastThumbOk)
		drawImage(fb, 320, gx, gy, gs, gs, a->lastThumb, THUMB_W, (THUMB_W - THUMB_H) / 2, 0, THUMB_H, THUMB_H, NULL, 14);
	else
	{
		drawRoundRect(fb, 320, gx, gy, gs, gs, 14, C_SURFACE2, 255);
		drawRoundRectOutline(fb, 320, gx + 14, gy + 16, 24, 20, 4, 2, C_DIM, 255);
		drawCircle(fb, 320, gx + 21.5f, gy + 23.5f, 2.5f, C_DIM, 255);
	}
	drawRoundRectOutline(fb, 320, gx, gy, gs, gs, 14, 1, C_STROKE, 200);
	hw = hintWidth("Y", "Galerie");
	drawHint(fb, 320, gx + gs / 2 - hw / 2, 136, "Y", "Galerie", C_DIM, 255);

	// Shutter.
	drawCircle(fb, 320, SHUTTER_X, SHUTTER_Y, SHUTTER_R + 8, C_ACC0, 28);
	drawRing(fb, 320, SHUTTER_X, SHUTTER_Y, SHUTTER_R, 3, C_TEXT, 235);
	drawCircleGrad(fb, 320, SHUTTER_X, SHUTTER_Y, a->shutterHeld ? SHUTTER_R - 10 : SHUTTER_R - 7, C_ACC0, C_ACC1, 255);
	hw = hintWidth("A", "Foto");
	drawHint(fb, 320, SHUTTER_X - hw / 2, 142, "A", "Foto", C_DIM, 255);

	// Filter button: round live preview with the current filter.
	int fr = FILTER_BTN_R;
	drawRing(fb, 320, FILTER_BTN_X, FILTER_BTN_Y, fr + 3, 2, C_STROKE, 255);
	if (a->camOk && a->liveL)
		drawImage(fb, 320, FILTER_BTN_X - fr, FILTER_BTN_Y - fr, fr * 2, fr * 2, a->liveL, CAM_W,
		          (CAM_W - CAM_H) / 2, 0, CAM_H, CAM_H, filterLut(a->filter), fr);
	else
		drawCircle(fb, 320, FILTER_BTN_X, FILTER_BTN_Y, fr, C_SURFACE2, 255);
	hw = hintWidth("◀▶", "Filter");
	drawHint(fb, 320, FILTER_BTN_X - hw / 2, 136, "◀▶", "Filter", C_DIM, 255);

	drawChips(a, fb);

	if (a->camOk)
		drawTextCentered(fb, 320, &fontSmall, 160, 214, C_DIM, 200, "Tipp: Motiv 0,5–2 m entfernt, mit Hintergrund");
	else
		drawTextCentered(fb, 320, &fontSmall, 160, 214, C_RED, 255, "Kamera konnte nicht gestartet werden");
}

// ---- review ----

static void reviewTop(App *a, u16 *fb, int eye, bool stereo)
{
	drawClear(fb, 400, C_BLACK);
	Crop c = wiggleCrop(a->offset);
	int dx, dy, dw, dh;
	fitTop(c.w, c.h, &dx, &dy, &dw, &dh);
	bool right = stereo ? eye : wigglePhase(a, a->delayMs);
	drawImage(fb, 400, dx, dy, dw, dh, right ? a->shotR : a->shotL, CAM_W,
	          right ? c.rx : c.lx, right ? c.ry : c.ly, c.w, c.h, filterLut(a->filter), 0);

	overlayPillLeft(fb, 400, dx + 8, 8, a->shotNum < 0 ? "Neu" : tfmt("WIG_%04d", a->shotNum));
	if (stereo)
		overlayPill(fb, 400, 200, 212, "3D-Ansicht");
	else
		overlayPill(fb, 400, 200, 212, tfmt("%s · %d ms", filterName(a->filter), a->delayMs));
}

static void pivotMarker(App *a, u16 *fb, float x, float y)
{
	float t = (a->now % 1400) / 1400.0f;
	drawRing(fb, 320, x, y, 10 + t * 12, 2, C_TEXT, (int)((1 - t) * 170));
	drawCircle(fb, 320, x, y, 12, C_BLACK, 60);
	drawRing(fb, 320, x, y, 10, 2, C_TEXT, 240);
	drawCircleGrad(fb, 320, x, y, 3.5f, C_ACC0, C_ACC1, 255);
}

static void reviewBottom(App *a, u16 *fb)
{
	background(fb);

	drawShadow(fb, 320, CARD_X, CARD_Y, CARD_W, CARD_H, 10, 160);
	drawImage(fb, 320, CARD_X, CARD_Y, CARD_W, CARD_H, a->shotL, CAM_W, 0, 0, CAM_W, CAM_H, filterLut(a->filter), 10);
	pivotMarker(a, fb, CARD_X + (a->pivotX + 0.5f) * CARD_W / CAM_W, CARD_Y + (a->pivotY + 0.5f) * CARD_H / CAM_H);

	u64 t = a->now - a->modeSince;
	if (t < 5000)
	{
		int alpha = t < 4400 ? 255 : 255 - (int)(t - 4400) * 255 / 600;
		const char *hint = "Tippe auf dein Motiv · L/R fein";
		int w = textWidth(&fontSmall, hint) + 20;
		drawRoundRect(fb, 320, CARD_X + 8, CARD_Y + 8, w, 20, 10, C_BLACK, alpha * 160 / 255);
		drawText(fb, 320, &fontSmall, CARD_X + 18, CARD_Y + 8 + (20 - lineH(&fontSmall)) / 2, C_TEXT, alpha, hint);
	}

	// Tempo slider.
	drawText(fb, 320, &fontSmall, 16, SLIDER_Y - lineH(&fontSmall) / 2, C_DIM, 255, "Tempo");
	float k = (float)(a->delayMs - DELAY_MIN) / (DELAY_MAX - DELAY_MIN);
	int knob = SLIDER_X0 + (int)(k * (SLIDER_X1 - SLIDER_X0));
	drawRoundRect(fb, 320, SLIDER_X0, SLIDER_Y - 2, SLIDER_X1 - SLIDER_X0, 4, 2, C_SURFACE2, 255);
	drawRoundRectGrad(fb, 320, SLIDER_X0, SLIDER_Y - 2, knob - SLIDER_X0 + 2, 4, 2, C_ACC0, C_ACC1, 255);
	drawCircle(fb, 320, knob, SLIDER_Y + 1.5f, 9, C_BLACK, 70);
	drawCircle(fb, 320, knob, SLIDER_Y, 8, C_TEXT, 255);
	drawText(fb, 320, &fontBody, SLIDER_X1 + 14, SLIDER_Y - lineH(&fontBody) / 2, C_TEXT, 255, tfmt("%d ms", a->delayMs));

	// Buttons: back, filter switch, save.
	int bw = buttonWidth("B", "Zurück"), sw = buttonWidth("A", "Speichern");
	button(fb, 12, BTN_Y, bw, "B", "Zurück", false, C_SURFACE2);
	button(fb, 308 - sw, BTN_Y, sw, "A", "Speichern", true, 0);
	int fx0 = 12 + bw + 8, fx1 = 308 - sw - 8;
	drawRoundRect(fb, 320, fx0, BTN_Y, fx1 - fx0, BTN_H, BTN_H / 2, C_SURFACE, 255);
	int ty = BTN_Y + (BTN_H - lineH(&fontSmall)) / 2;
	drawText(fb, 320, &fontSmall, fx0 + 9, ty, C_DIM, 255, "◀");
	drawText(fb, 320, &fontSmall, fx1 - 9 - textWidth(&fontSmall, "▶"), ty, C_DIM, 255, "▶");
	drawTextCentered(fb, 320, &fontBody, (fx0 + fx1) / 2, BTN_Y + (BTN_H - lineH(&fontBody)) / 2, C_TEXT, 255, filterName(a->filter));

	if (a->saving)
	{
		drawFill(fb, 320, 0, 0, 320, 240, C_BLACK, 150);
		drawShadow(fb, 320, 60, 84, 200, 72, 16, 200);
		drawRoundRect(fb, 320, 60, 84, 200, 72, 16, C_SURFACE, 255);
		drawRoundRectOutline(fb, 320, 60, 84, 200, 72, 16, 1, C_STROKE, 255);
		drawTextCentered(fb, 320, &fontBody, 160, 100, C_TEXT, 255, "Speichere GIF …");
		drawTextCentered(fb, 320, &fontSmall, 160, 122, C_DIM, 255, "Einen Moment");
		drawRoundRectGrad(fb, 320, 100, 140, 120, 3, 1, C_ACC0, C_ACC1, 255);
	}
}

// ---- gallery ----

static void galleryTop(App *a, u16 *fb, int eye, bool stereo)
{
	drawClear(fb, 400, C_BLACK);
	if (!a->shotCount)
	{
		drawTextGrad(fb, 400, &fontHuge, 200 - textWidth(&fontHuge, "Galerie") / 2, 92, C_ACC0, C_ACC1, "Galerie");
		drawTextCentered(fb, 400, &fontBody, 200, 134, C_DIM, 255, "Hier landen deine Wigglegrams");
		return;
	}

	int cur = a->sel % GRID_PAGE;
	const ShotInfo *info = &a->gridInfo[cur];
	const u16 *lut = a->gridOk[cur] ? filterLut(info->filter) : NULL;
	int delay = a->gridOk[cur] && info->delayMs > 0 ? info->delayMs : 120;
	bool right = stereo ? eye : wigglePhase(a, delay);
	if (a->previewNum == a->shots[a->sel])
		drawImage(fb, 400, 40, 0, PREVIEW_W, PREVIEW_H, right ? a->previewR : a->previewL, PREVIEW_W, 0, 0, PREVIEW_W, PREVIEW_H, lut, 0);
	else if (a->gridOk[cur])
		drawImage(fb, 400, 40, 0, PREVIEW_W, PREVIEW_H, a->grid[cur], THUMB_W, 0, 0, THUMB_W, THUMB_H, lut, 0);

	overlayPillLeft(fb, 400, 48, 8, tfmt("WIG_%04d", a->shots[a->sel]));
	const char *count = tfmt("%d / %d", a->sel + 1, a->shotCount);
	overlayPill(fb, 400, 352 - (textWidth(&fontSmall, count) + 20) / 2, 8, count);
	if (a->gridOk[cur])
		overlayPill(fb, 400, 200, 212, stereo ? "3D-Ansicht" : tfmt("%s · %d ms", filterName(info->filter), delay));
}

static void galleryEmpty(u16 *fb)
{
	// A little stack of photos.
	drawRoundRectOutline(fb, 320, 140, 62, 56, 42, 9, 2, C_STROKE, 255);
	drawShadow(fb, 320, 128, 72, 56, 42, 9, 160);
	drawRoundRectGrad(fb, 320, 128, 72, 56, 42, 9, C_ACC0, C_ACC1, 255);
	drawCircle(fb, 320, 142, 84, 4, C_TEXT, 230);
	drawRoundRect(fb, 320, 136, 100, 40, 6, 3, C_TEXT, 160);

	drawTextCentered(fb, 320, &fontBody, 160, 132, C_TEXT, 255, "Noch keine Wigglegrams");
	drawTextCentered(fb, 320, &fontSmall, 160, 152, C_DIM, 255, "Mach ein Foto und speichere es.");
	int hw = hintWidth("B", "Zur Kamera");
	drawHint(fb, 320, 160 - hw / 2, 216, "B", "Zur Kamera", C_DIM, 255);
}

static void galleryBottom(App *a, u16 *fb)
{
	background(fb);
	drawText(fb, 320, &fontTitle, 16, 9, C_TEXT, 255, "Galerie");
	if (!a->shotCount)
	{
		galleryEmpty(fb);
		return;
	}

	int pages = (a->shotCount + GRID_PAGE - 1) / GRID_PAGE, page = a->sel / GRID_PAGE;
	const char *info = pages > 1 ? tfmt("%d Wigglegrams · Seite %d/%d", a->shotCount, page + 1, pages)
	                             : tfmt(a->shotCount == 1 ? "%d Wigglegram" : "%d Wigglegrams", a->shotCount);
	drawText(fb, 320, &fontSmall, 304 - textWidth(&fontSmall, info), 17, C_DIM, 255, info);

	for (int i = 0; i < GRID_PAGE && page * GRID_PAGE + i < a->shotCount; i++)
	{
		int x = GRID_X + (i % GRID_COLS) * (CELL_W + CELL_GAP), y = GRID_Y + (i / GRID_COLS) * (CELL_H + CELL_GAP);
		if (i == a->sel % GRID_PAGE)
			drawRoundRectGrad(fb, 320, x - 3, y - 3, CELL_W + 6, CELL_H + 6, 9, C_ACC0, C_ACC1, 255);
		if (a->gridOk[i])
			drawImage(fb, 320, x, y, CELL_W, CELL_H, a->grid[i], THUMB_W, 0, 0, THUMB_W, THUMB_H, filterLut(a->gridInfo[i].filter), 6);
		else
		{
			drawRoundRect(fb, 320, x, y, CELL_W, CELL_H, 6, C_SURFACE2, 255);
			drawTextCentered(fb, 320, &fontSmall, x + CELL_W / 2, y + CELL_H / 2 - 6, C_DIM, 255, "alt");
		}
	}

	static const char *const btns[] = {"A", "X", "B"}, *const labels[] = {"Öffnen", "Löschen", "Kamera"};
	centeredHints(fb, 218, 3, btns, labels);

	if (a->confirmDelete)
	{
		drawFill(fb, 320, 0, 0, 320, 240, C_BLACK, 160);
		drawShadow(fb, 320, 40, 66, 240, 104, 16, 200);
		drawRoundRect(fb, 320, 40, 66, 240, 104, 16, C_SURFACE, 255);
		drawRoundRectOutline(fb, 320, 40, 66, 240, 104, 16, 1, C_STROKE, 255);
		drawTextCentered(fb, 320, &fontBody, 160, 82, C_TEXT, 255, tfmt("WIG_%04d löschen?", a->shots[a->sel]));
		drawTextCentered(fb, 320, &fontSmall, 160, 104, C_DIM, 255, "Foto und GIF werden entfernt.");
		button(fb, 56, 132, 100, "B", "Abbrechen", false, C_SURFACE2);
		button(fb, 164, 132, 100, "X", "Löschen", false, C_RED);
	}
}

Hit uiReviewHit(int x, int y)
{
	if (y < BTN_Y - 4 || y >= BTN_Y + BTN_H + 4)
		return HIT_NONE;
	int bw = buttonWidth("B", "Zurück"), sw = buttonWidth("A", "Speichern");
	if (x >= 12 && x < 12 + bw)
		return HIT_BACK;
	if (x >= 308 - sw && x < 308)
		return HIT_SAVE;
	if (x >= 12 + bw + 8 && x < 308 - sw - 8)
		return x < 160 ? HIT_FILTER_PREV : HIT_FILTER_NEXT;
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

// ---- shared ----

static void toast(App *a, u16 *fb)
{
	u64 t = a->now - a->toastAt;
	if (!a->toast[0] || t >= TOAST_MS)
		return;
	int slide = t < 220 ? (int)(220 - t) * 34 / 220 : 0;
	int alpha = t > TOAST_MS - 400 ? (int)(TOAST_MS - t) * 255 / 400 : 255;
	const char *text = tfmt("%s %s", a->toastError ? "×" : "✓", a->toast);
	int w = textWidth(&fontBody, text) + 28, h = 28, x = 160 - w / 2, y = 8 - slide;
	drawShadow(fb, 320, x, y, w, h, h / 2, alpha * 200 / 255);
	drawRoundRect(fb, 320, x, y, w, h, h / 2, C_SURFACE, alpha * 245 / 255);
	drawRoundRectOutline(fb, 320, x, y, w, h, h / 2, 1, a->toastError ? C_RED : C_ACC0, alpha);
	drawTextCentered(fb, 320, &fontBody, 160, y + (h - lineH(&fontBody)) / 2, C_TEXT, alpha, text);
}

void uiDraw(App *a, u16 *topL, u16 *topR, u16 *bottom, bool stereo)
{
	for (int eye = 0; eye <= (stereo ? 1 : 0); eye++)
	{
		u16 *fb = eye ? topR : topL;
		switch (a->mode)
		{
		case MODE_CAMERA: cameraTop(a, fb, eye, stereo); break;
		case MODE_REVIEW: reviewTop(a, fb, eye, stereo); break;
		case MODE_GALLERY: galleryTop(a, fb, eye, stereo); break;
		}
	}
	switch (a->mode)
	{
	case MODE_CAMERA: cameraBottom(a, bottom); break;
	case MODE_REVIEW: reviewBottom(a, bottom); break;
	case MODE_GALLERY: galleryBottom(a, bottom); break;
	}
	toast(a, bottom);

	u64 t = a->now - a->modeSince;
	if (t < FADE_MS)
	{
		int alpha = clampi(255 - (int)t * 255 / FADE_MS, 0, 255);
		drawFill(bottom, 320, 0, 0, 320, 240, C_BLACK, alpha);
		for (int eye = 0; eye <= (stereo ? 1 : 0); eye++)
			drawFill(eye ? topR : topL, 400, 0, 0, 400, 240, C_BLACK, alpha);
	}
}
