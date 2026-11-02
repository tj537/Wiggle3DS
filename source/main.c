#include <3ds.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "draw.h"
#include "filter.h"
#include "gif.h"
#include "qrcodegen.h"
#include "share.h"

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
		ph = (int)(app.now / delay);
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

// ---- settings ----

#define SETTINGS_PATH DIR_ROOT "/settings.txt"

static void loadSettings(void)
{
	Settings *st = &app.settings;
	*st = (Settings){.theme = 0, .smooth = 0, .delayMs = 120, .gifHalf = false, .fps = 20, .flash = true};
	FILE *f = fopen(SETTINGS_PATH, "r");
	if (!f)
		return;
	char key[32];
	int v;
	while (fscanf(f, " %31[^=]=%d", key, &v) == 2)
	{
		if (!strcmp(key, "theme") && v >= 0 && v < THEME_COUNT) st->theme = v;
		else if (!strcmp(key, "smooth") && v >= 0 && v < SMOOTH_LEVELS) st->smooth = v;
		else if (!strcmp(key, "speed") && v >= DELAY_MIN && v <= DELAY_MAX) st->delayMs = v;
		else if (!strcmp(key, "gif_half")) st->gifHalf = v;
		else if (!strcmp(key, "fps") && (v == 15 || v == 20 || v == 30)) st->fps = v;
		else if (!strcmp(key, "flash")) st->flash = v;
	}
	fclose(f);
}

static void saveSettings(void)
{
	const Settings *st = &app.settings;
	FILE *f = fopen(SETTINGS_PATH, "w");
	if (!f)
		return;
	fprintf(f, "theme=%d\nsmooth=%d\nspeed=%d\ngif_half=%d\nfps=%d\nflash=%d\n",
	        st->theme, st->smooth, st->delayMs, st->gifHalf, st->fps, st->flash);
	fclose(f);
}

static void applyTheme(void)
{
	int t = app.settings.theme;
	uiSetTheme(t);
	shareSetColors(uiThemeRgb(t, 1), uiThemeRgb(t, 3), uiThemeRgb(t, 6), uiThemeRgb(t, 7), uiThemeRgb(t, 8));
}

static void changeSetting(int row, int step)
{
	Settings *st = &app.settings;
	static const int fpsSteps[3] = {15, 20, 30};
	int i;
	switch (row)
	{
	case SET_THEME:
		st->theme = (st->theme + step + THEME_COUNT) % THEME_COUNT;
		applyTheme();
		break;
	case SET_SMOOTH:
		st->smooth = (st->smooth + step + SMOOTH_LEVELS) % SMOOTH_LEVELS;
		break;
	case SET_SPEED:
		st->delayMs += step * DELAY_STEP;
		st->delayMs = st->delayMs < DELAY_MIN ? DELAY_MIN : st->delayMs > DELAY_MAX ? DELAY_MAX : st->delayMs;
		break;
	case SET_GIF_SIZE:
		st->gifHalf = !st->gifHalf;
		break;
	case SET_FPS:
		for (i = 0; i < 2 && fpsSteps[i] != st->fps; i++)
			;
		i = (i + step + 3) % 3;
		st->fps = fpsSteps[i];
		if (app.camOk)
			camSetFps(st->fps);
		break;
	case SET_FLASH:
		st->flash = !st->flash;
		break;
	}
	topDirty = true;
}

static void updateSettings(u32 down)
{
	if (down & KEY_DOWN) app.settingsSel = (app.settingsSel + 1) % SET_COUNT;
	if (down & KEY_UP) app.settingsSel = (app.settingsSel + SET_COUNT - 1) % SET_COUNT;
	if (down & KEY_LEFT) changeSetting(app.settingsSel, -1);
	if (down & (KEY_RIGHT | KEY_A)) changeSetting(app.settingsSel, 1);
	if (down & KEY_TOUCH)
	{
		int step, row = uiSettingsRowAt(touchX, touchY, &step);
		if (row >= 0)
		{
			// Tapping a row picks it; tapping it again (or an arrow) changes it.
			if (step || row == app.settingsSel)
				changeSetting(row, step ? step : 1);
			app.settingsSel = row;
		}
	}
	if (down & (KEY_B | KEY_START))
	{
		saveSettings();
		setMode(MODE_HOME);
	}
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
	if (app.settings.flash)
		app.flashAt = osGetTime();
	wiggleSetImages(shotL, shotR);
	app.shotNum = -1;
	app.pivotX = CAM_W / 2;
	app.pivotY = CAM_H / 2;
	app.offset = wiggleMatch(app.pivotX, app.pivotY);
	app.delayMs = app.settings.delayMs;
	app.shotFilter = app.filter;
	app.shotSmooth = app.settings.smooth;
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
	app.shotFilter = SHOT_FILTER(&info) < FILTER_COUNT ? SHOT_FILTER(&info) : 0;
	app.shotSmooth = SHOT_SMOOTH(&info);
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
	ShotInfo info = {app.pivotX, app.pivotY, app.offset.dx, app.offset.dy, app.delayMs, SHOT_STYLE(app.shotFilter, app.shotSmooth)};
	char path[64];
	storageGifPath(app.shotNum, path, sizeof(path));

	// The GIF gets the filtered crops and the blended frames in between;
	// the stored photo stays unfiltered. One buffer per distinct mix.
	Crop c = wiggleCrop(app.offset);
	int pixels = c.w * c.h, mix[MAX_WIGGLE_FRAMES], n = wiggleFrames(app.shotSmooth, mix);
	u16 *bufs[MAX_WIGGLE_FRAMES] = {0};
	GifFrame frames[MAX_WIGGLE_FRAMES];
	bool ok = true;
	for (int i = 0; i < n && ok; i++)
	{
		int same = -1;
		for (int j = 0; j < i; j++)
			if (mix[j] == mix[i])
				same = j;
		if (same < 0 && !(bufs[i] = malloc(pixels * 2)))
			ok = false;
		frames[i] = (GifFrame){same < 0 ? bufs[i] : frames[same].img, c.w, 0, 0};
	}
	if (ok)
	{
		filterApply(bufs[0], shotL, CAM_W, c.lx, c.ly, c.w, c.h, app.shotFilter);
		int right = n / 2; // the frame that is all right eye
		filterApply(bufs[right], shotR, CAM_W, c.rx, c.ry, c.w, c.h, app.shotFilter);
		for (int i = 1; i < right; i++)
			blendRows(bufs[i], bufs[0], bufs[right], pixels, mix[i]);
		int gw = c.w, gh = c.h;
		if (app.settings.gifHalf)
		{
			// Halve in place: each frame's buffer has room for the bigger image.
			for (int i = 0; i <= right; i++)
				halveRows(bufs[i], bufs[i], c.w, c.h);
			gw = c.w / 2;
			gh = c.h / 2;
			for (int i = 0; i < n; i++)
				frames[i].stride = gw;
		}
		ok = storageSave(app.shotNum, shotL, shotR, &info) && gifWrite(path, frames, n, gw, gh, app.delayMs);
	}
	for (int i = 0; i < n; i++)
		free(bufs[i]);
	if (ok)
		showToast(false, "Saved · WIG_%04d.gif", app.shotNum);
	else
		showToast(true, "Saving failed");
	app.saving = false;
	if (isNew)
		app.sel = 0; // the newest shot comes first
}

static void updateReview(u32 down)
{
	if (touching && inRect(startX, startY, CARD_X, CARD_Y, CARD_W, CARD_H) && !uiFilterPillHit(startX, startY) &&
	    !uiSmoothPillHit(startX, startY))
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
	if ((down & KEY_L) && app.offset.dx > -MAX_DX)
		app.offset.dx--;
	if ((down & KEY_R) && app.offset.dx < MAX_DX)
		app.offset.dx++;
	if ((down & KEY_LEFT) && app.delayMs > DELAY_MIN)
		app.delayMs -= DELAY_STEP;
	if ((down & KEY_RIGHT) && app.delayMs < DELAY_MAX)
		app.delayMs += DELAY_STEP;
	if (down & KEY_UP)
		app.shotFilter = (app.shotFilter + FILTER_COUNT - 1) % FILTER_COUNT;
	if ((down & KEY_DOWN) || (released && uiFilterPillHit(startX, startY) && uiFilterPillHit(touchX, touchY)))
		app.shotFilter = (app.shotFilter + 1) % FILTER_COUNT;
	if ((down & KEY_X) || (released && uiSmoothPillHit(startX, startY) && uiSmoothPillHit(touchX, touchY)))
		app.shotSmooth = (app.shotSmooth + 1) % SMOOTH_LEVELS;
	if ((down & KEY_A) || hit == HIT_SAVE)
		saveShot();
	else if ((down & KEY_B) || hit == HIT_BACK)
		backFromReview();
}

// ---- share ----

static void openShare(void)
{
	camPause();
	app.sharedCount = 0;
	app.shareOk = shareStart(app.shareUrl, sizeof(app.shareUrl));
	if (app.shareOk)
	{
		static u8 temp[sizeof(app.qr)];
		app.shareOk = qrcodegen_encodeText(app.shareUrl, temp, app.qr, qrcodegen_Ecc_MEDIUM, 1, 10,
		                                   qrcodegen_Mask_AUTO, true);
	}
	setMode(MODE_SHARE);
}

static void closeShare(void)
{
	shareStop();
	setMode(MODE_HOME);
	refreshGallery();
	if (app.browsing && !app.shotCount)
		app.browsing = false;
	if (!app.browsing)
		camResume();
}

static void updateShare(u32 down)
{
	if (app.shareOk && sharePoll())
	{
		app.sharedCount++;
		bottomDirty = true;
	}
	if (down & (KEY_B | KEY_SELECT) || (released && touchY >= 210))
		closeShare();
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

static void openFilterMenu(void)
{
	// The menu sets the camera filter, so show the camera to preview it on.
	if (app.browsing)
		stopBrowsing();
	app.filterMenu = true;
	app.menuSel = app.filter;
}

static void updateFilterMenu(u32 down)
{
	int sel = app.menuSel;
	if (down & KEY_RIGHT && sel < FILTER_COUNT - 1) sel++;
	if (down & KEY_LEFT && sel > 0) sel--;
	if (down & KEY_DOWN && sel + MENU_COLS < FILTER_COUNT) sel += MENU_COLS;
	if (down & KEY_UP && sel - MENU_COLS >= 0) sel -= MENU_COLS;

	// Touching a button previews it; tapping the previewed one again applies it.
	bool apply = down & KEY_A;
	if (down & KEY_TOUCH)
	{
		int tile = uiMenuTileAt(touchX, touchY);
		if (tile >= 0)
		{
			apply = tile == app.menuSel;
			sel = tile;
		}
	}
	app.menuSel = sel;

	if (apply)
	{
		app.filter = app.menuSel;
		app.filterMenu = false;
	}
	else if (down & (KEY_B | KEY_Y))
		app.filterMenu = false;
}

static void updateHome(u32 down)
{
	if (!app.browsing && app.camOk && camPoll())
	{
		app.liveL = camLeft();
		app.liveR = camRight();
		topDirty = true;
	}

	if (app.filterMenu)
	{
		updateFilterMenu(down);
		return;
	}
	if (released && uiFilterButtonHit(&app, startX, startY) && uiFilterButtonHit(&app, touchX, touchY))
	{
		openFilterMenu();
		return;
	}

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

	if (released && uiGearHit(startX, startY) && uiGearHit(touchX, touchY))
	{
		app.settingsSel = 0;
		setMode(MODE_SETTINGS);
		return;
	}
	if ((down & KEY_SELECT) || (released && uiShareButtonHit(startX, startY) && uiShareButtonHit(touchX, touchY)))
	{
		openShare();
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
		if (down & KEY_Y)
		{
			openFilterMenu();
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

	osSetSpeedupEnable(true);
	filterInit();
	osSetSpeedupEnable(false);
	storageInit();
	loadSettings();
	applyTheme();
	app.shotL = shotL;
	app.shotR = shotR;
	app.shotNum = -1;
	app.delayMs = app.settings.delayMs;
	refreshGallery();

	app.camOk = camOpen();
	if (app.camOk)
	{
		camSetFps(app.settings.fps);
		camResume();
		app.liveL = camLeft();
		app.liveR = camRight();
	}
	setMode(MODE_HOME);

	while (aptMainLoop())
	{
		hidScanInput();
		u32 down = hidKeysDown(), held = hidKeysHeld();
		if ((down & KEY_START) && app.mode != MODE_SETTINGS)
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
		else if (app.mode == MODE_REVIEW)
			updateReview(down);
		else if (app.mode == MODE_SETTINGS)
			updateSettings(down);
		else
			updateShare(down);
		render();
	}

	shareStop();
	camClose();
	gfxExit();
	return 0;
}
