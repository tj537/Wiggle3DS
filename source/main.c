#include <3ds.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "gif.h"

#define MAX_DX 200
#define PREVIEW_DELAY_MS 120 // wait before loading the selected shot's preview

static App app;
static u16 shotL[CAM_W * CAM_H], shotR[CAM_W * CAM_H];

// Touch: where the current touch started and where it was last seen.
static bool touching, released;
static int touchX, touchY, startX, startY;

static u64 selChangedAt;
static int previewTried = -1; // shot whose preview load was last attempted

// Screens are only redrawn when something on them changed.
static bool topDirty = true, bottomDirty = true;

static bool inRect(int x, int y, int rx, int ry, int rw, int rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }

static void setMode(Mode m)
{
	app.mode = m;
	app.modeSince = osGetTime();
	// The faster New 3DS clock is only needed for matching and saving.
	osSetSpeedupEnable(m == MODE_REVIEW);
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
	static bool lastStereo;
	static int lastPhase = -1;
	bool stereo = osGet3DSliderState() > 0.0f;
	app.now = osGetTime();

	int delay, ph = -1;
	if (uiTopWiggles(&app, &delay))
		ph = (app.now / delay) & 1;
	if (stereo != lastStereo || ph != lastPhase || uiTopAnimating(&app))
		topDirty = true;
	if (uiBottomAnimating(&app))
		bottomDirty = true;
	lastStereo = stereo;
	lastPhase = ph;

	if (topDirty || bottomDirty)
	{
		if (topDirty)
		{
			gfxSet3D(stereo);
			uiDrawTop(&app, (u16 *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL),
			          stereo ? (u16 *)gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL) : NULL, stereo);
		}
		if (bottomDirty)
			uiDrawBottom(&app, (u16 *)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL));
		gfxFlushBuffers();
		if (topDirty)
			gfxScreenSwapBuffers(GFX_TOP, stereo);
		if (bottomDirty)
			gfxScreenSwapBuffers(GFX_BOTTOM, false);
		topDirty = bottomDirty = false;
	}
	gspWaitForVBlank();
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

static void refreshGallery(void)
{
	app.shotCount = storageList(app.shots, MAX_SHOTS);
	if (app.sel >= app.shotCount)
		app.sel = app.shotCount ? app.shotCount - 1 : 0;
	app.gridPage = -1;
	app.previewNum = -1;
	previewTried = -1;
	loadPage();
}

static void startBrowsing(void)
{
	if (app.browsing || !app.shotCount)
		return;
	camPause();
	app.browsing = true;
	app.playing = false;
	selChangedAt = osGetTime();
}

static void stopBrowsing(void)
{
	app.browsing = false;
	app.confirmDelete = false;
	camResume();
}

// ---- review ----

static void capture(void)
{
	if (!app.camOk)
		return;
	memcpy(shotL, camLeft(), sizeof(shotL));
	memcpy(shotR, camRight(), sizeof(shotR));
	camPause();
	setMode(MODE_REVIEW);
	app.flashAt = osGetTime();
	wiggleSetImages(shotL, shotR);
	app.shotNum = -1;
	app.pivotX = CAM_W / 2;
	app.pivotY = CAM_H / 2;
	app.offset = wiggleMatch(app.pivotX, app.pivotY);
	app.delayMs = 120;
	app.fromGallery = false;
}

static void openShot(int num)
{
	ShotInfo info;
	if (!storageLoad(num, shotL, shotR, &info))
	{
		showToast(true, "Couldn't load WIG_%04d", num);
		return;
	}
	setMode(MODE_REVIEW);
	wiggleSetImages(shotL, shotR);
	app.shotNum = num;
	app.pivotX = info.pivotX;
	app.pivotY = info.pivotY;
	app.offset = (Offset){info.dx, info.dy};
	app.delayMs = info.delayMs;
	app.fromGallery = true;
}

static void backFromReview(void)
{
	setMode(MODE_HOME);
	refreshGallery();
	if (app.fromGallery && app.shotCount)
		app.browsing = true;
	else
	{
		app.browsing = false;
		camResume();
	}
}

static void saveShot(void)
{
	app.saving = true;
	bottomDirty = true;
	render();

	storageInit();
	bool isNew = app.shotNum < 0;
	if (isNew)
		app.shotNum = storageNextNumber();
	ShotInfo info = {app.pivotX, app.pivotY, app.offset.dx, app.offset.dy, app.delayMs, 0};
	char path[64];
	storageGifPath(app.shotNum, path, sizeof(path));
	Crop c = wiggleCrop(app.offset);
	GifFrame frames[2] = {
		{shotL, CAM_W, c.lx, c.ly},
		{shotR, CAM_W, c.rx, c.ry},
	};
	if (storageSave(app.shotNum, shotL, shotR, &info) && gifWrite(path, frames, 2, c.w, c.h, app.delayMs))
		showToast(false, "Saved · WIG_%04d.gif", app.shotNum);
	else
		showToast(true, "Saving failed");
	app.saving = false;
	if (isNew)
		app.sel = 0; // the newest shot comes first
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
	if ((down & (KEY_L | KEY_LEFT)) && app.offset.dx > -MAX_DX)
		app.offset.dx--;
	if ((down & (KEY_R | KEY_RIGHT)) && app.offset.dx < MAX_DX)
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

// ---- home ----

static void deleteSelected(void)
{
	int num = app.shots[app.sel];
	storageDelete(num);
	refreshGallery();
	showToast(false, "WIG_%04d deleted", num);
	if (!app.shotCount)
		stopBrowsing();
	selChangedAt = osGetTime();
}

static void updateHome(u32 down)
{
	if (!app.browsing && app.camOk && camPoll())
		topDirty = true;

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

	if (!app.browsing)
	{
		if (down & (KEY_A | KEY_L | KEY_R))
		{
			capture();
			return;
		}
		if (down & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT))
		{
			startBrowsing();
			return;
		}
	}

	if (down & KEY_TOUCH)
	{
		int cell = uiCellAt(touchX, touchY);
		int idx = (app.sel / GRID_PAGE) * GRID_PAGE + cell;
		if (cell >= 0 && idx < app.shotCount)
		{
			// Tapping the selected shot opens it for editing.
			if (app.browsing && idx == app.sel)
			{
				openShot(app.shots[idx]);
				return;
			}
			startBrowsing();
			if (idx != app.sel)
			{
				app.sel = idx;
				app.playing = false;
				selChangedAt = osGetTime();
			}
		}
	}
	if (!app.browsing)
		return;

	int prev = app.sel;
	if (down & KEY_RIGHT) app.sel++;
	if (down & KEY_LEFT) app.sel--;
	if (down & KEY_DOWN) app.sel += GRID_COLS;
	if (down & KEY_UP) app.sel -= GRID_COLS;
	if (app.sel < 0) app.sel = 0;
	if (app.sel >= app.shotCount) app.sel = app.shotCount - 1;
	if (app.sel != prev)
	{
		app.playing = false;
		selChangedAt = osGetTime();
		loadPage();
	}

	if (down & KEY_A)
		openShot(app.shots[app.sel]);
	else if (down & KEY_Y)
		app.playing = !app.playing;
	else if (down & KEY_X)
		app.confirmDelete = true;
	else if (down & KEY_B)
		stopBrowsing();
	else if (previewTried != app.shots[app.sel] && osGetTime() - selChangedAt > PREVIEW_DELAY_MS)
	{
		previewTried = app.shots[app.sel];
		app.previewNum = storageLoadPreview(previewTried, app.previewL, app.previewR) ? previewTried : -1;
		topDirty = true;
	}
}

int main(void)
{
	gfxInitDefault();
	gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
	gfxSetScreenFormat(GFX_BOTTOM, GSP_RGB565_OES);
	gfxSetDoubleBuffering(GFX_TOP, true);
	gfxSetDoubleBuffering(GFX_BOTTOM, true);

	storageInit();
	app.shotL = shotL;
	app.shotR = shotR;
	app.shotNum = -1;
	app.delayMs = 120;
	refreshGallery();

	app.camOk = camOpen();
	if (app.camOk)
	{
		camResume();
		app.liveL = camLeft();
		app.liveR = camRight();
	}
	setMode(MODE_HOME);

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
		if (down || nowTouching || released)
			topDirty = bottomDirty = true;

		if (app.mode == MODE_HOME)
			updateHome(down);
		else
			updateReview(down);
		render();
	}

	camClose();
	gfxExit();
	return 0;
}
