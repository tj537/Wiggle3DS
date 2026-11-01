#include <string.h>
#include "app.h"
#include "draw.h"
#include "qrcodegen.h"

// Dark purple theme.
#define C_BG_TOP COL565(30, 18, 52)
#define C_BG_BOTTOM COL565(11, 7, 20)
#define C_SURFACE COL565(36, 26, 58)
#define C_SURFACE2 COL565(52, 40, 80)
#define C_STROKE COL565(84, 66, 122)
#define C_TEXT COL565(248, 245, 255)
#define C_DIM COL565(160, 148, 190)
#define C_ACC0 COL565(124, 77, 255)
#define C_ACC1 COL565(214, 110, 255)
#define C_RED COL565(255, 76, 92)
#define C_BLACK 0
#define C_WHITE 0xFFFF

#define FADE_MS 160
#define FLASH_MS 220
#define TOAST_MS 2600

static int lineH(const Font *f) { return f->ascent + f->descent; }

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

static bool phase(const App *a, int delayMs) { return (a->now / (u64)delayMs) & 1; }

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
		if (a->camOk && a->liveL)
			drawImage(fb, 400, 40, 0, 320, 240, eye ? a->liveR : a->liveL, CAM_W, 0, 0, CAM_W, CAM_H, NULL, 0);
		else
			drawTextCentered(fb, 400, &fontBody, 200, 112, C_DIM, 255, "Camera unavailable");
		if (stereo)
			overlayPillLeft(fb, 400, 48, 8, "3D");
		return;
	}

	if (!a->shotCount)
		return;
	int cur = a->sel % GRID_PAGE;
	const ShotInfo *info = selectedInfo(a);
	int delay = shotDelay(info);
	bool right = stereo ? eye : a->playing && phase(a, delay);
	if (a->previewNum == a->shots[a->sel])
		drawImage(fb, 400, 40, 0, PREVIEW_W, PREVIEW_H, right ? a->previewR : a->previewL, PREVIEW_W, 0, 0, PREVIEW_W, PREVIEW_H, NULL, 0);
	else if (a->gridOk[cur])
		drawImage(fb, 400, 40, 0, PREVIEW_W, PREVIEW_H, a->grid[cur], THUMB_W, 0, 0, THUMB_W, THUMB_H, NULL, 0);

	overlayPillLeft(fb, 400, 48, 8, tfmt("WIG_%04d", a->shots[a->sel]));
	overlayPillRight(fb, 400, 352, 8, tfmt("%d / %d", a->sel + 1, a->shotCount));
	if (stereo)
		overlayPill(fb, 400, 200, 212, "3D view");
	else if (info)
		overlayPill(fb, 400, 200, 212, a->playing ? tfmt("Playing · %d ms", delay) : "Paused");
}

static void reviewTop(App *a, u16 *fb, int eye, bool stereo)
{
	drawClear(fb, 400, C_BLACK);
	Crop c = wiggleCrop(a->offset);
	int dx, dy, dw, dh;
	fitTop(c.w, c.h, &dx, &dy, &dw, &dh);
	bool right = stereo ? eye : phase(a, a->delayMs);
	drawImage(fb, 400, dx, dy, dw, dh, right ? a->shotR : a->shotL, CAM_W,
	          right ? c.rx : c.lx, right ? c.ry : c.ly, c.w, c.h, NULL, 0);

	overlayPillLeft(fb, 400, dx + 8, 8, a->shotNum < 0 ? "New" : tfmt("WIG_%04d", a->shotNum));
	overlayPill(fb, 400, 200, 212, stereo ? "3D view" : tfmt("%d ms", a->delayMs));
}

static void shareTop(App *a, u16 *fb);

void uiDrawTop(App *a, u16 *left, u16 *right, bool stereo)
{
	for (int eye = 0; eye <= (stereo ? 1 : 0); eye++)
	{
		u16 *fb = eye ? right : left;
		if (a->mode == MODE_HOME)
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

bool uiShareButtonHit(int x, int y)
{
	int w = shareButtonWidth();
	return x >= 308 - w - 4 && x < 312 && y >= SHARE_BTN_Y - 4 && y < SHARE_BTN_Y + SHARE_BTN_H + 4;
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

static void homeBottom(App *a, u16 *fb)
{
	background(fb);
	drawTextGrad(fb, 320, &fontTitle, 16, 9, C_ACC0, C_ACC1, "Wiggle3DS");

	int sw = shareButtonWidth();
	drawRoundRect(fb, 320, 308 - sw, SHARE_BTN_Y, sw, SHARE_BTN_H, SHARE_BTN_H / 2, C_SURFACE2, 255);
	drawHint(fb, 320, 308 - sw + 8, SHARE_BTN_Y + (SHARE_BTN_H - lineH(&fontSmall)) / 2, "SELECT", "Share", C_TEXT, 255);

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
				drawImage(fb, 320, x, y, CELL_W, CELL_H, a->grid[i], THUMB_W, 0, 0, THUMB_W, THUMB_H, NULL, 6);
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
		static const char *const btns[] = {"A", "◀▶", "START"}, *const labels[] = {"Capture", "Browse", "Quit"};
		if (!a->shotCount)
			centeredHints(fb, 218, 2, (const char *const[]){"A", "START"}, (const char *const[]){"Capture", "Quit"});
		else
			centeredHints(fb, 218, 3, btns, labels);
	}

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
	drawImage(fb, 320, CARD_X, CARD_Y, CARD_W, CARD_H, a->shotL, CAM_W, 0, 0, CAM_W, CAM_H, NULL, 10);
	float px = CARD_X + (a->pivotX + 0.5f) * CARD_W / CAM_W, py = CARD_Y + (a->pivotY + 0.5f) * CARD_H / CAM_H;
	drawCircle(fb, 320, px, py, 12, C_BLACK, 70);
	drawRing(fb, 320, px, py, 10, 2, C_TEXT, 240);
	drawCircleGrad(fb, 320, px, py, 3.5f, C_ACC0, C_ACC1, 255);

	if (!a->toast[0] || a->now - a->toastAt >= TOAST_MS) // the toast sits here
	{
		const char *hint = "Tap your subject · L/R to fine-tune";
		drawRoundRect(fb, 320, CARD_X + 8, CARD_Y + 8, textWidth(&fontSmall, hint) + 20, 20, 10, C_BLACK, 150);
		drawText(fb, 320, &fontSmall, CARD_X + 18, CARD_Y + 8 + (20 - lineH(&fontSmall)) / 2, C_TEXT, 255, hint);
	}

	// Speed slider.
	drawText(fb, 320, &fontSmall, 16, SLIDER_Y - lineH(&fontSmall) / 2, C_DIM, 255, "Speed");
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

void uiDrawBottom(App *a, u16 *fb)
{
	if (a->mode == MODE_HOME)
		homeBottom(a, fb);
	else if (a->mode == MODE_REVIEW)
		reviewBottom(a, fb);
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
	return a->now - a->modeSince < FADE_MS + 20 || (a->toast[0] && a->now - a->toastAt < TOAST_MS + 20);
}

// ---- touch targets ----

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
