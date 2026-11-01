#include <3ds.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "filter.h"
#include "gif.h"

#define MAX_DX 200
#define PREVIEW_DELAY_MS 120 // wait before loading the selected gallery preview

static App app;
static u16 shotL[CAM_W * CAM_H], shotR[CAM_W * CAM_H];

// Touch: where the current touch started and where it was last seen.
static bool touching, released;
static int touchX, touchY, startX, startY;
static u64 selChangedAt;
static int previewTried = -1; // shot whose preview load was last attempted

static bool inRect(int x, int y, int rx, int ry, int rw, int rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }
static bool inCircle(int x, int y, int cx, int cy, int r) { return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r; }
static bool tapped(bool (*hit)(int, int)) { return released && hit(startX, startY) && hit(touchX, touchY); }

static void setMode(Mode m)
{
	app.mode = m;
	app.modeSince = osGetTime();
}

static void showToast(bool error, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(app.toast, sizeof(app.toast), fmt, ap);
	va_end(ap);
	app.toastError = error;
	app.toastAt = osGetTime();
}

static void render(void)
{
	bool stereo = osGet3DSliderState() > 0.0f;
	gfxSet3D(stereo);
	app.now = osGetTime();
	uiDraw(&app, (u16 *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL),
	       stereo ? (u16 *)gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL) : NULL,
	       (u16 *)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL), stereo);
	gfxFlushBuffers();
	gfxSwapBuffers();
	gspWaitForVBlank();
}

static void refreshLastThumb(void)
{
	static int nums[MAX_SHOTS];
	app.lastThumbOk = storageList(nums, MAX_SHOTS) > 0 && storageLoadThumb(nums[0], app.lastThumb, NULL);
}

static void setFilter(int f)
{
	app.filter = (f + FILTER_COUNT) % FILTER_COUNT;
	app.filterChangedAt = osGetTime();
}

// ---- camera ----

static bool onShutter(int x, int y) { return inCircle(x, y, SHUTTER_X, SHUTTER_Y, SHUTTER_R + 6); }
static bool onGalleryButton(int x, int y) { return inRect(x, y, GALLERY_BTN_X - 4, GALLERY_BTN_Y - 4, GALLERY_BTN_S + 8, GALLERY_BTN_S + 24); }
static bool onFilterButton(int x, int y) { return inCircle(x, y, FILTER_BTN_X, FILTER_BTN_Y, FILTER_BTN_R + 8); }

static void openGallery(void);

static void capture(void)
{
	if (!app.camOk)
		return;
	CAMU_PlayShutterSound(SHUTTER_SOUND_TYPE_NORMAL);
	memcpy(shotL, camLeft(), sizeof(shotL));
	memcpy(shotR, camRight(), sizeof(shotR));
	camPause();
	wiggleSetImages(shotL, shotR);
	app.shotNum = -1;
	app.pivotX = CAM_W / 2;
	app.pivotY = CAM_H / 2;
	app.offset = wiggleMatch(app.pivotX, app.pivotY);
	app.reviewReturn = MODE_CAMERA;
	setMode(MODE_REVIEW);
}

static void updateCamera(u32 down, u32 held)
{
	if (app.camOk)
		camPoll();
	app.shutterHeld = (held & (KEY_A | KEY_L | KEY_R)) || (touching && onShutter(touchX, touchY));

	if (down & KEY_LEFT)
		setFilter(app.filter - 1);
	if (down & KEY_RIGHT)
		setFilter(app.filter + 1);
	if (down & KEY_TOUCH)
	{
		int chip = uiChipAt(&app, touchX, touchY);
		if (chip >= 0 && chip != app.filter)
			setFilter(chip);
	}

	if ((down & (KEY_A | KEY_L | KEY_R)) || tapped(onShutter))
		capture();
	else if ((down & KEY_Y) || tapped(onGalleryButton))
	{
		camPause();
		openGallery();
	}
	else if (tapped(onFilterButton))
		setFilter(app.filter + 1);
}

// ---- review ----

static void backFromReview(void)
{
	if (app.reviewReturn == MODE_GALLERY)
		openGallery();
	else
	{
		camResume();
		setMode(MODE_CAMERA);
	}
}

static void saveShot(void)
{
	app.saving = true;
	render();

	storageInit();
	if (app.shotNum < 0)
		app.shotNum = storageNextNumber();
	ShotInfo info = {app.pivotX, app.pivotY, app.offset.dx, app.offset.dy, app.delayMs, app.filter};
	char path[64];
	storageGifPath(app.shotNum, path, sizeof(path));
	Crop c = wiggleCrop(app.offset);
	const u16 *lut = filterLut(app.filter);
	GifFrame frames[2] = {
		{shotL, CAM_W, c.lx, c.ly, lut},
		{shotR, CAM_W, c.rx, c.ry, lut},
	};
	if (storageSave(app.shotNum, shotL, shotR, &info) && gifWrite(path, frames, 2, c.w, c.h, app.delayMs))
		showToast(false, "Gespeichert · WIG_%04d.gif", app.shotNum);
	else
		showToast(true, "Speichern fehlgeschlagen");

	app.saving = false;
	app.gridPage = -1; // gallery thumbnails may have changed
	app.previewNum = -1;
	refreshLastThumb();
}

static void updateReview(u32 down)
{
	if (touching && inRect(startX, startY, CARD_X, CARD_Y, CARD_W, CARD_H))
	{
		int tx = touchX < CARD_X ? CARD_X : touchX >= CARD_X + CARD_W ? CARD_X + CARD_W - 1 : touchX;
		int ty = touchY < CARD_Y ? CARD_Y : touchY >= CARD_Y + CARD_H ? CARD_Y + CARD_H - 1 : touchY;
		int x = (tx - CARD_X) * CAM_W / CARD_W, y = (ty - CARD_Y) * CAM_H / CARD_H;
		if (x != app.pivotX || y != app.pivotY)
		{
			app.pivotX = x;
			app.pivotY = y;
			app.offset = wiggleMatch(x, y);
		}
	}
	if (touching && inRect(startX, startY, SLIDER_X0 - 20, SLIDER_Y - 12, SLIDER_X1 - SLIDER_X0 + 40, 24))
	{
		int x = touchX < SLIDER_X0 ? SLIDER_X0 : touchX > SLIDER_X1 ? SLIDER_X1 : touchX;
		int v = DELAY_MIN + (x - SLIDER_X0) * (DELAY_MAX - DELAY_MIN) / (SLIDER_X1 - SLIDER_X0);
		app.delayMs = (v + DELAY_STEP / 2) / DELAY_STEP * DELAY_STEP;
	}

	Hit hit = released && uiReviewHit(startX, startY) == uiReviewHit(touchX, touchY) ? uiReviewHit(touchX, touchY) : HIT_NONE;
	if ((down & KEY_LEFT) || hit == HIT_FILTER_PREV)
		app.filter = (app.filter + FILTER_COUNT - 1) % FILTER_COUNT;
	if ((down & KEY_RIGHT) || hit == HIT_FILTER_NEXT)
		app.filter = (app.filter + 1) % FILTER_COUNT;
	if ((down & KEY_L) && app.offset.dx > -MAX_DX)
		app.offset.dx--;
	if ((down & KEY_R) && app.offset.dx < MAX_DX)
		app.offset.dx++;
	if ((down & KEY_UP) && app.delayMs > DELAY_MIN)
		app.delayMs -= DELAY_STEP;
	if ((down & KEY_DOWN) && app.delayMs < DELAY_MAX)
		app.delayMs += DELAY_STEP;
	if ((down & KEY_A) || hit == HIT_SAVE)
		saveShot();
	else if ((down & KEY_B) || hit == HIT_BACK)
		backFromReview();
}

// ---- gallery ----

static void loadPage(void)
{
	int page = app.sel / GRID_PAGE;
	if (page == app.gridPage)
		return;
	for (int i = 0; i < GRID_PAGE; i++)
	{
		int idx = page * GRID_PAGE + i;
		app.gridOk[i] = idx < app.shotCount && storageLoadThumb(app.shots[idx], app.grid[i], &app.gridInfo[i]);
	}
	app.gridPage = page;
}

static void openGallery(void)
{
	app.shotCount = storageList(app.shots, MAX_SHOTS);
	if (app.sel >= app.shotCount)
		app.sel = app.shotCount ? app.shotCount - 1 : 0;
	app.gridPage = -1;
	app.previewNum = -1;
	previewTried = -1;
	app.confirmDelete = false;
	selChangedAt = osGetTime();
	loadPage();
	setMode(MODE_GALLERY);
}

static int cellAt(int x, int y)
{
	for (int i = 0; i < GRID_PAGE; i++)
	{
		int cx = GRID_X + (i % GRID_COLS) * (CELL_W + CELL_GAP), cy = GRID_Y + (i / GRID_COLS) * (CELL_H + CELL_GAP);
		if (inRect(x, y, cx - CELL_GAP / 2, cy - CELL_GAP / 2, CELL_W + CELL_GAP, CELL_H + CELL_GAP))
			return i;
	}
	return -1;
}

static void openShot(int num)
{
	ShotInfo info;
	if (!storageLoad(num, shotL, shotR, &info))
	{
		showToast(true, "WIG_%04d konnte nicht geladen werden", num);
		return;
	}
	wiggleSetImages(shotL, shotR);
	app.shotNum = num;
	app.pivotX = info.pivotX;
	app.pivotY = info.pivotY;
	app.offset = (Offset){info.dx, info.dy};
	app.delayMs = info.delayMs;
	app.filter = info.filter;
	app.reviewReturn = MODE_GALLERY;
	setMode(MODE_REVIEW);
}

static void deleteSelected(void)
{
	int num = app.shots[app.sel], keep = app.sel;
	storageDelete(num);
	openGallery();
	app.sel = keep < app.shotCount ? keep : (app.shotCount ? app.shotCount - 1 : 0);
	app.modeSince = 0; // no fade, we stay in the gallery
	app.gridPage = -1;
	loadPage();
	refreshLastThumb();
	showToast(false, "WIG_%04d gelöscht", num);
}

static void updateGallery(u32 down)
{
	if (app.confirmDelete)
	{
		Hit hit = released ? uiDeleteDialogHit(touchX, touchY) : HIT_NONE;
		if ((down & KEY_X) || hit == HIT_DELETE)
		{
			app.confirmDelete = false;
			deleteSelected();
		}
		else if ((down & KEY_B) || hit == HIT_CANCEL)
			app.confirmDelete = false;
		return;
	}

	if (down & KEY_B)
	{
		camResume();
		setMode(MODE_CAMERA);
		return;
	}
	if (!app.shotCount)
		return;

	int prev = app.sel;
	if (down & KEY_RIGHT) app.sel++;
	if (down & KEY_LEFT) app.sel--;
	if (down & KEY_DOWN) app.sel += GRID_COLS;
	if (down & KEY_UP) app.sel -= GRID_COLS;
	if (down & KEY_TOUCH)
	{
		int cell = cellAt(touchX, touchY);
		int idx = (app.sel / GRID_PAGE) * GRID_PAGE + cell;
		if (cell >= 0 && idx < app.shotCount)
		{
			// Tapping the selected shot opens it.
			if (idx == app.sel)
			{
				openShot(app.shots[idx]);
				return;
			}
			app.sel = idx;
		}
	}
	if (app.sel < 0) app.sel = 0;
	if (app.sel >= app.shotCount) app.sel = app.shotCount - 1;
	if (app.sel != prev)
	{
		selChangedAt = osGetTime();
		loadPage();
	}

	if (down & KEY_A)
		openShot(app.shots[app.sel]);
	else if (down & KEY_X)
		app.confirmDelete = true;
	else if (previewTried != app.shots[app.sel] && osGetTime() - selChangedAt > PREVIEW_DELAY_MS)
	{
		previewTried = app.shots[app.sel];
		app.previewNum = storageLoadPreview(previewTried, app.previewL, app.previewR) ? previewTried : -1;
	}
}

int main(void)
{
	osSetSpeedupEnable(true);
	gfxInitDefault();
	gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
	gfxSetScreenFormat(GFX_BOTTOM, GSP_RGB565_OES);
	gfxSetDoubleBuffering(GFX_TOP, true);
	gfxSetDoubleBuffering(GFX_BOTTOM, true);

	filterInit();
	storageInit();
	refreshLastThumb();
	app.shotL = shotL;
	app.shotR = shotR;
	app.shotNum = -1;
	app.previewNum = -1;
	app.gridPage = -1;
	app.delayMs = 120;
	app.chipScroll = 12;

	app.camOk = camOpen();
	if (app.camOk)
	{
		camResume();
		app.liveL = camLeft();
		app.liveR = camRight();
	}
	setMode(MODE_CAMERA);

	while (aptMainLoop())
	{
		hidScanInput();
		u32 down = hidKeysDown(), held = hidKeysHeld();
		if (down & KEY_START)
			break;

		bool nowTouching = held & KEY_TOUCH;
		released = touching && !nowTouching;
		if (nowTouching)
		{
			touchPosition t;
			hidTouchRead(&t);
			if (!touching)
			{
				startX = t.px;
				startY = t.py;
			}
			touchX = t.px;
			touchY = t.py;
		}
		touching = nowTouching;

		switch (app.mode)
		{
		case MODE_CAMERA: updateCamera(down, held); break;
		case MODE_REVIEW: updateReview(down); break;
		case MODE_GALLERY: updateGallery(down); break;
		}
		render();
	}

	camClose();
	gfxExit();
	return 0;
}
