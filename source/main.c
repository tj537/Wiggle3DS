#include <3ds.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "app.h"
#include "draw.h"
#include "export.h"
#include "filter.h"
#include "gif.h"
#include "qrcodegen.h"
#include "share.h"
#include "sound.h"
#include "develop.h"

#define PREVIEW_DELAY_MS 120 // wait before loading the selected shot's preview
#define ALIGN_HOLD_MS 180     // a finger resting this long on the photo aligns it
#define SWIPE_MIN 40          // gallery: horizontal travel that flips the page

static App app;
static u16 shotL[CAM_W * CAM_H], shotR[CAM_W * CAM_H];
static u16 firstL[CAM_W * CAM_H], firstR[CAM_W * CAM_H]; // double exposure

// Touch: where the current touch started and where it was last seen.
static bool touching, released;
static int touchX, touchY, startX, startY;

static u64 selChangedAt, touchMovedAt, touchDownAt;
#define LONG_PRESS_MS 450
static int previewTried = -1; // shot whose preview load was last attempted

// Screens are only redrawn when something on them changed.
static bool topDirty = true, bottomDirty = true;

static bool inRect(int x, int y, int rx, int ry, int rw, int rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }

static void setMode(Mode m)
{
	app.mode = m;
	app.modeSince = osGetTime();
}

// The faster New 3DS clock is only needed for matching, saving and making GIFs.
static int speedupOn = -1;
static void updateSpeedup(void)
{
	int liveFilter = app.filterMenu ? app.menuSel : app.settings.filter;
	int want = app.mode == MODE_REVIEW || app.mode == MODE_GALLERY || app.exporting > 0 ||
	           (app.mode == MODE_HOME && filterHasGlitch(liveFilter)); // glitches on the live view
	if (want != speedupOn)
		osSetSpeedupEnable(want);
	speedupOn = want;
}

static void showToast(bool error, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(app.toast, sizeof(app.toast), fmt, ap);
	va_end(ap);
	app.toastError = error;
	app.toastUndo = false;
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

// Free space on the SD card and the battery, checked every few seconds.
static void checkStatus(void)
{
	static u64 last;
	if (last && osGetTime() - last < 5000)
		return;
	last = osGetTime();
	u64 freeBytes = storageFreeBytes();
	u8 level = 5, charging = 0, percent = 0;
	PTMU_GetBatteryLevel(&level);
	PTMU_GetBatteryChargeState(&charging);
	// The exact percentage from the power chip, else the 0-5 level.
	int battery = R_SUCCEEDED(MCUHWC_GetBatteryLevel(&percent)) ? percent : level * 20;
	bool low = battery <= 10 && !charging;
	if (battery != app.battery || charging != app.charging)
		bottomDirty = true;
	app.battery = battery;
	app.charging = charging;
	if (low != app.lowBattery || (freeBytes < LOW_FREE) != (app.freeBytes < LOW_FREE) ||
	    freeBytes / (100 << 20) != app.freeBytes / (100 << 20))
		bottomDirty = true;
	app.freeBytes = freeBytes;
	app.lowBattery = low;
}

// ---- settings ----

#define SETTINGS_PATH DIR_ROOT "/settings.txt"

static void loadSettings(void)
{
	Settings *st = &app.settings;
	*st = (Settings){.theme = 0, .filter = 0, .smooth = 0, .delayMs = 120, .quick = false, .timer = 0, .grid = false, .autoplay = true, .gifHalf = false, .fps = 20, .flash = true, .sound = true, .photoEye = 1, .strength = 100};
	FILE *f = fopen(SETTINGS_PATH, "r");
	if (!f)
		return;
	char key[32];
	int v;
	while (fscanf(f, " %31[^=]=%d", key, &v) == 2)
	{
		if (!strcmp(key, "theme") && v >= 0 && v < THEME_COUNT) st->theme = v;
		else if (!strcmp(key, "filter") && v >= 0 && v < FILTER_COUNT) st->filter = v;
		else if (!strcmp(key, "quick")) st->quick = v;
		else if (!strcmp(key, "timer") && (v == 0 || v == 3 || v == 10)) st->timer = v;
		else if (!strcmp(key, "grid")) st->grid = v;
		else if (!strcmp(key, "autoplay")) st->autoplay = v;
		else if (!strcmp(key, "smooth") && v >= 0 && v <= SMOOTH_OFF) st->smooth = v;
		else if (!strcmp(key, "speed") && v >= DELAY_MIN && v <= DELAY_MAX) st->delayMs = v;
		else if (!strcmp(key, "gif_half")) st->gifHalf = v;
		else if (!strcmp(key, "fps") && (v == 15 || v == 20 || v == 30)) st->fps = v;
		else if (!strcmp(key, "flash")) st->flash = v;
		else if (!strcmp(key, "night")) st->night = v;
		else if (!strcmp(key, "wiggle") && !v) st->smooth = SMOOTH_OFF; // from the earlier switch
		else if (!strcmp(key, "photo_eye")) st->photoEye = v ? 1 : 0;
		else if (!strcmp(key, "strength") && v >= 0 && v <= 100) st->strength = v;
		else if (!strncmp(key, "fav", 3) && key[3] >= '0' && key[3] <= '2' && !key[4]) st->favorites[key[3] - '0'] = (u32)v;
		else if (!strcmp(key, "sound")) st->sound = v;
		else if (!strcmp(key, "gif_square")) st->gifSquare = v;
		else if (!strcmp(key, "tip_done")) st->tipDone = v;
	}
	fclose(f);
}

static void saveSettings(void)
{
	const Settings *st = &app.settings;
	FILE *f = fopen(SETTINGS_PATH, "w");
	if (!f)
		return;
	fprintf(f, "theme=%d\nfilter=%d\nsmooth=%d\nspeed=%d\nquick=%d\ntimer=%d\ngrid=%d\nautoplay=%d\ngif_half=%d\nfps=%d\nflash=%d\n",
	        st->theme, st->filter, st->smooth, st->delayMs, st->quick, st->timer, st->grid, st->autoplay, st->gifHalf, st->fps, st->flash);
	fprintf(f, "photo_eye=%d\nstrength=%d\n", st->photoEye, st->strength);
	for (int i = 0; i < 3; i++)
		fprintf(f, "fav%d=%d\n", i, (int)st->favorites[i]);
	fprintf(f, "night=%d\nsound=%d\ngif_square=%d\ntip_done=%d\n", st->night, st->sound, st->gifSquare, st->tipDone);
	fclose(f);
}

static void applyTheme(void)
{
	int t = app.settings.theme;
	uiSetTheme(t);
	shareSetColors(uiThemeRgb(t, 1), uiThemeRgb(t, 3), uiThemeRgb(t, 6), uiThemeRgb(t, 7), uiThemeRgb(t, 8));
}

static void resetDefaults(void);

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
	case SET_TIMER:
		st->timer = step > 0 ? (st->timer == 0 ? 3 : st->timer == 3 ? 10 : 0) : (st->timer == 0 ? 10 : st->timer == 10 ? 3 : 0);
		break;
	case SET_GRID:
		st->grid = !st->grid;
		break;
	case SET_AUTOPLAY:
		st->autoplay = !st->autoplay;
		break;
	case SET_GIF_SIZE:
		st->gifHalf = !st->gifHalf;
		exportSetHalf(st->gifHalf);
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
	case SET_PHOTO_EYE:
		st->photoEye = !st->photoEye;
		exportSetPhotoEye(st->photoEye);
		break;
	case SET_SOUND:
		st->sound = !st->sound;
		if (st->sound)
			soundShutter();
		break;
	case SET_RESET:
		resetDefaults();
		break;
	case SET_GIF_SHAPE:
		st->gifSquare = !st->gifSquare;
		exportSetSquare(st->gifSquare);
		break;
	}
	topDirty = true;
}

static void updateSettings(u32 down)
{
	static int dragY, dragTop;
	static bool dragged;
	if (down & KEY_DOWN) app.settingsSel = (app.settingsSel + 1) % SET_COUNT;
	if (down & KEY_UP) app.settingsSel = (app.settingsSel + SET_COUNT - 1) % SET_COUNT;
	if (down & KEY_LEFT) changeSetting(app.settingsSel, -1);
	if (down & (KEY_RIGHT | KEY_A)) changeSetting(app.settingsSel, 1);
	if (down & (KEY_UP | KEY_DOWN))
	{
		// Keep the selection on screen.
		if (app.settingsSel < app.settingsTop)
			app.settingsTop = app.settingsSel;
		if (app.settingsSel >= app.settingsTop + SET_VISIBLE)
			app.settingsTop = app.settingsSel - SET_VISIBLE + 1;
	}
	// Dragging scrolls the list a row at a time; a tap picks a row.
	if (down & KEY_TOUCH)
	{
		dragY = touchY;
		dragTop = app.settingsTop;
		dragged = false;
	}
	if (touching && abs(touchY - dragY) > 10)
	{
		int top = dragTop - (touchY - dragY) / SET_ROW;
		top = top < 0 ? 0 : top > SET_COUNT - SET_VISIBLE ? SET_COUNT - SET_VISIBLE : top;
		dragged = true;
		if (top != app.settingsTop)
		{
			app.settingsTop = top;
			bottomDirty = true;
		}
	}
	if (released && !dragged)
	{
		int step, row = uiSettingsRowAt(&app, touchX, touchY, &step);
		if (row >= 0)
		{
			// Tapping a row picks it; tapping it again (or an arrow) changes it.
			if (step || row == app.settingsSel || row == SET_RESET)
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

// Loads missing thumbnails for the visible columns (plus one either side),
// at most `budget` per call so scrolling stays smooth; true if any loaded.
static bool loadThumbs(int budget)
{
	int first = 0, last = RECENT_COUNT - 1;
	if (app.mode == MODE_GALLERY)
	{
		int col = (int)app.scrollX / COL_W - 1;
		first = (col < 0 ? 0 : col) * GRID_ROWS;
		last = first + (VISIBLE_COLS + 3) * GRID_ROWS - 1;
	}
	bool loaded = false;
	for (int idx = first; idx <= last && idx < app.shotCount && budget > 0; idx++)
	{
		int slot = idx % THUMB_CACHE;
		if (app.gridIdx[slot] == idx)
			continue;
		app.gridOk[slot] = storageLoadThumb(app.shots[idx], app.grid[slot], &app.gridInfo[slot]);
		// Filtered once here, so drawing the strip is a plain copy.
		static u16 temp[THUMB_W * THUMB_H];
		int filter = SHOT_FILTER(&app.gridInfo[slot]);
		if (app.gridOk[slot] && filter && filter < FILTER_COUNT)
		{
			filterApply(temp, app.grid[slot], THUMB_W, 0, 0, THUMB_W, THUMB_H, filter, 0, SHOT_STRENGTH(&app.gridInfo[slot]));
			memcpy(app.grid[slot], temp, sizeof(temp));
		}
		app.gridIdx[slot] = idx;
		budget--;
		loaded = true;
	}
	return loaded;
}

static void refreshGallery(void)
{
	app.shotCount = storageList(app.shots, MAX_SHOTS);
	if (app.sel >= app.shotCount)
		app.sel = app.shotCount ? app.shotCount - 1 : 0;
	for (int i = 0; i < THUMB_CACHE; i++)
		app.gridIdx[i] = -1;
	int max = uiMaxScroll(&app);
	if (app.scrollX > max)
		app.scrollX = max;
	app.previewNum = -1;
	previewTried = -1;
	loadThumbs(THUMB_CACHE);
}

// Scrolls (animated) so that a shot's column is fully visible.
static void reveal(int idx)
{
	int x = (idx / GRID_ROWS) * COL_W, target = (int)app.scrollX;
	if (x < target)
		target = x;
	else if (x + CELL_W > target + 320 - 2 * GRID_X)
		target = x + CELL_W - (320 - 2 * GRID_X);
	int max = uiMaxScroll(&app);
	target = target < 0 ? 0 : target > max ? max : target;
	if (target != (int)app.scrollX)
	{
		app.scrollTo = target;
		app.scrollVel = 0;
	}
}

// "Today", "Yesterday" or the day, for the gallery header.
static void updateSelDate(void)
{
	u64 t = app.shotCount ? storageShotTime(app.shots[app.sel]) : 0;
	app.selDate[0] = 0;
	if (!t)
		return;
	time_t now = time(NULL), when = (time_t)t;
	long days = (long)(now / 86400) - (long)(when / 86400);
	if (days == 0)
		strcpy(app.selDate, "Today");
	else if (days == 1)
		strcpy(app.selDate, "Yesterday");
	else
		strftime(app.selDate, sizeof(app.selDate), days < 300 ? "%-d %b" : "%b %Y", gmtime(&when));
}

static void selectShot(int idx)
{
	idx = idx < 0 ? 0 : idx >= app.shotCount ? app.shotCount - 1 : idx;
	if (idx == app.sel)
		return;
	app.sel = idx;
	updateSelDate();
	selChangedAt = osGetTime();
	topDirty = true;
	if (app.mode == MODE_GALLERY)
		reveal(idx);
}

static void openGallery(int idx)
{
	if (!app.shotCount)
		return;
	camPause();
	app.filterMenu = false;
	app.playing = app.settings.autoplay;
	app.sel = idx;
	selChangedAt = osGetTime();
	app.mode = MODE_GALLERY;
	app.scrollTo = -1;
	app.scrollVel = 0;
	app.scrollX = 0;
	reveal(idx);
	if (app.scrollTo >= 0)
		app.scrollX = app.scrollTo; // jump there, no animation on entry
	app.scrollTo = -1;
	loadThumbs(THUMB_CACHE);
	updateSelDate();
	setMode(MODE_GALLERY); // after loading, so the fade isn't spent on it
}

static void closeGallery(void)
{
	app.confirmDelete = false;
	app.picking = false;
	setMode(MODE_HOME);
	loadThumbs(RECENT_COUNT);
	camResume();
}

// ---- review ----

static bool saveShot(void);

// Night mode: averages the next few pairs into the shot while showing
// "Hold still"; gives up waiting after a while in very low light.
#define NIGHT_FRAMES 4
#define NIGHT_MAX_MS 1600
static void stackNight(void)
{
	if (!stackBegin(shotL, shotR))
		return;
	// The live view keeps running underneath, with the progress on top.
	app.stacking = 1;
	app.stackFrames = NIGHT_FRAMES;
	topDirty = true;
	render();
	u64 start = osGetTime();
	while (app.stacking < NIGHT_FRAMES && osGetTime() - start < NIGHT_MAX_MS)
	{
		if (camPoll())
		{
			stackAdd(camLeft(), camRight());
			app.stacking++;
			app.liveL = camLeft();
			app.liveR = camRight();
			topDirty = true;
			render();
		}
		else
			svcSleepThread(2000000LL);
	}
	stackFinish(shotL, shotR);
	app.stacking = 0;
	topDirty = bottomDirty = true;
}

static void capture(void)
{
	if (!app.camOk)
		return;
	if (storageFreeBytes() < MIN_FREE)
	{
		showToast(true, "SD card is full");
		return;
	}
	memcpy(shotL, camLeft(), sizeof(shotL));
	memcpy(shotR, camRight(), sizeof(shotR));
	if (app.settings.sound)
		soundShutter();
	if (app.settings.night)
		stackNight();
	if (app.settings.flash)
		app.flashAt = osGetTime(); // after the extra frames, which want a clear view
	if (!app.selfie) // a selfie has one image in both eyes
		matchEyes(shotL, shotR);
	if (app.doubleExp && !app.haveFirst)
	{
		// The first of two: kept, and shown over the viewfinder until the second.
		memcpy(firstL, shotL, sizeof(shotL));
		memcpy(firstR, shotR, sizeof(shotR));
		app.haveFirst = true;
		showToast(false, "First exposure · now the second");
		topDirty = bottomDirty = true;
		return;
	}
	if (app.haveFirst)
	{
		doubleExpose(shotL, firstL);
		doubleExpose(shotR, firstR);
		app.haveFirst = false;
	}
	wiggleSetImages(shotL, shotR);
	app.shotNum = -1;
	app.pivotX = CAM_W / 2;
	app.pivotY = CAM_H / 2;
	app.delayMs = app.settings.delayMs;
	app.shotFilter = app.settings.filter;
	app.shotSmooth = app.selfie ? SMOOTH_OFF : app.settings.smooth;
	app.shotStrength = app.settings.strength;
	app.fromGallery = false;
	app.aligning = false;
	if (app.settings.quick || app.shotSmooth == SMOOTH_OFF)
	{
		// Saved as is, aligned on the center (ready for when the photo is
		// made a wiggle later); the camera keeps running.
		app.offset = app.selfie ? (Offset){0, 0} : wiggleMatch(app.pivotX, app.pivotY);
		if (saveShot())
		{
			refreshGallery();
			showToast(false, "Saved · WIG_%04d", app.shotNum);
		}
		return;
	}
	camPause();
	app.offset = wiggleMatch(app.pivotX, app.pivotY);
	setMode(MODE_REVIEW);
	app.savedInfo = (ShotInfo){0};
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
	app.shotSmooth = SHOT_IS_PHOTO(&info) ? SMOOTH_OFF : SHOT_SMOOTH(&info);
	// Photos of 0.0.1 had a flag of their own; boomerangs of 0.0.1 keep their first frame.
	app.shotStrength = SHOT_STRENGTH(&info);
	info.style = SHOT_STYLE(app.shotFilter, app.shotSmooth, app.shotStrength);
	app.savedInfo = info;
	app.fromGallery = true;
	app.aligning = false;
}

static void backFromReview(void)
{
	int num = app.shotNum;
	if (app.fromGallery)
		setMode(MODE_GALLERY);
	else
		setMode(MODE_HOME);
	refreshGallery();
	if (app.mode == MODE_GALLERY && app.shotCount)
	{
		for (int i = 0; i < app.shotCount; i++)
			if (app.shots[i] == num)
				selectShot(i);
		app.playing = app.settings.autoplay;
	}
	else
	{
		setMode(MODE_HOME);
		camResume();
	}
}

static ShotInfo currentInfo(void)
{
	return (ShotInfo){app.pivotX, app.pivotY, app.offset.dx, app.offset.dy, app.delayMs,
	                  SHOT_STYLE(app.shotFilter, app.shotSmooth, app.shotStrength)};
}

// Stores the photo with its settings (over the earlier version, if any);
// the GIF is made in the background.
static bool saveShot(void)
{
	storageInit();
	if (app.shotNum < 0)
		app.shotNum = storageNextNumber();
	ShotInfo info = currentInfo();
	if (!storageSave(app.shotNum, shotL, shotR, &info))
	{
		showToast(true, "Saving failed");
		return false;
	}
	app.savedInfo = info;
	exportQueue(app.shotNum);
	app.exporting = exportPending();
	app.sel = 0; // shows the newest shot first
	return true;
}

static void saveFromReview(void)
{
	if (!uiReviewChanged(&app))
		return;
	app.saving = true;
	bottomDirty = true;
	render();
	bool isNew = app.shotNum < 0;
	if (saveShot())
		showToast(false, isNew ? "Saved · WIG_%04d" : "Updated · WIG_%04d", app.shotNum);
	app.saving = false;
}

static void updateReview(u32 down)
{
	if (!uiShotIsPhoto(&app) && touching && inRect(startX, startY, CARD_X, CARD_Y, CARD_W, CARD_H) && !uiFilterPillHit(startX, startY) &&
	    !uiSmoothPillHit(startX, startY))
	{
		int tx = touchX < CARD_X ? CARD_X : touchX >= CARD_X + CARD_W ? CARD_X + CARD_W - 1 : touchX;
		int ty = touchY < CARD_Y ? CARD_Y : touchY >= CARD_Y + CARD_H ? CARD_Y + CARD_H - 1 : touchY;
		int x = (tx - CARD_X) * CAM_W / CARD_W, y = (ty - CARD_Y) * CAM_H / CARD_H;
		// Only the marker follows the finger; matching runs once it rests
		// or lets go, so dragging stays smooth.
		if (abs(x - app.pivotX) > 2 || abs(y - app.pivotY) > 2 || (down & KEY_TOUCH))
		{
			app.pivotX = x;
			app.pivotY = y;
			app.aligning = true;
			touchMovedAt = osGetTime();
			if (!app.settings.tipDone)
			{
				app.settings.tipDone = true;
				saveSettings();
			}
		}
	}
	if (app.aligning && (!touching || osGetTime() - touchMovedAt > ALIGN_HOLD_MS))
	{
		app.offset = wiggleMatch(app.pivotX, app.pivotY);
		app.aligning = false;
		topDirty = bottomDirty = true;
	}
	if (!uiShotIsPhoto(&app) && touching && inRect(startX, startY, SLIDER_X0 - 20, SLIDER_Y - 12, SLIDER_X1 - SLIDER_X0 + 40, 24))
	{
		int x = touchX < SLIDER_X0 ? SLIDER_X0 : touchX > SLIDER_X1 ? SLIDER_X1 : touchX;
		int v = DELAY_MIN + (x - SLIDER_X0) * (DELAY_MAX - DELAY_MIN) / (SLIDER_X1 - SLIDER_X0);
		app.delayMs = (v + DELAY_STEP / 2) / DELAY_STEP * DELAY_STEP;
	}

	Hit hit = released && uiReviewHit(startX, startY) == uiReviewHit(touchX, touchY) ? uiReviewHit(touchX, touchY) : HIT_NONE;
	if ((down & KEY_LEFT) && app.delayMs > DELAY_MIN && !uiShotIsPhoto(&app))
		app.delayMs -= DELAY_STEP;
	if ((down & KEY_RIGHT) && app.delayMs < DELAY_MAX && !uiShotIsPhoto(&app))
		app.delayMs += DELAY_STEP;
	// L R: filter strength.
	if ((down & KEY_L) && app.shotStrength > 0)
		app.shotStrength -= STRENGTH_STEP;
	if ((down & KEY_R) && app.shotStrength < 100)
		app.shotStrength += STRENGTH_STEP;
	if (down & KEY_UP)
		app.shotFilter = filterNext(app.shotFilter, -1);
	if ((down & KEY_DOWN) || (released && uiFilterPillHit(startX, startY) && uiFilterPillHit(touchX, touchY)))
		app.shotFilter = filterNext(app.shotFilter, 1);
	if ((down & KEY_X) || (released && uiSmoothPillHit(startX, startY) && uiSmoothPillHit(touchX, touchY)))
		app.shotSmooth = (app.shotSmooth + 1) % (SMOOTH_OFF + 1);
	if ((down & KEY_A) || hit == HIT_SAVE)
		saveFromReview();
	else if ((down & KEY_B) || hit == HIT_BACK)
		backFromReview();
}

// ---- share ----

static void openShare(void)
{
	camPause();
	app.shareFrom = app.mode;
	app.filterMenu = false;
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
	setMode(app.shareFrom == MODE_GALLERY && app.shotCount ? MODE_GALLERY : MODE_HOME);
	refreshGallery();
	if (app.mode == MODE_HOME)
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

// ---- gallery ----

// Deleted shots stay in the trash while the undo toast is up.
static void purgeTrash(void)
{
	for (int i = 0; i < app.trashedCount; i++)
		storagePurge(app.trashed[i]);
	app.trashedCount = 0;
}

static void togglePick(int idx)
{
	app.picked[idx] = !app.picked[idx];
	app.pickedCount += app.picked[idx] ? 1 : -1;
	if (!app.pickedCount)
		app.picking = false;
	bottomDirty = true;
}

static void endPicking(void)
{
	app.picking = false;
	app.pickedCount = 0;
	memset(app.picked, 0, sizeof(app.picked));
	bottomDirty = true;
}

static void deleteShots(void)
{
	purgeTrash();
	for (int i = 0; i < app.shotCount; i++)
		if (app.picking ? app.picked[i] : i == app.sel)
		{
			storageTrash(app.shots[i]);
			app.trashed[app.trashedCount++] = app.shots[i];
		}
	endPicking();
	refreshGallery();
	if (app.trashedCount == 1)
		showToast(false, "WIG_%04d deleted", app.trashed[0]);
	else
		showToast(false, "%d shots deleted", app.trashedCount);
	app.toastUndo = true;
	if (!app.shotCount)
		closeGallery();
	selChangedAt = osGetTime();
}

static void undoDelete(void)
{
	int first = app.trashed[0], n = app.trashedCount;
	for (int i = 0; i < n; i++)
		storageRestore(app.trashed[i]);
	app.trashedCount = 0;
	refreshGallery();
	exportQueueMissing(app.shots, app.shotCount);
	for (int i = 0; i < app.shotCount; i++)
		if (app.shots[i] == first && app.mode == MODE_GALLERY)
			selectShot(i);
	showToast(false, n == 1 ? "Restored" : "%d shots restored", n);
	bottomDirty = topDirty = true;
}

// Y while the undo message is up.
static bool checkUndo(u32 down)
{
	if (!app.trashedCount || !app.toastUndo || !uiToastVisible(&app))
		return false;
	if (down & KEY_Y)
	{
		undoDelete();
		return true;
	}
	return false;
}

// Finger drags and flicks scroll the strip; the arrows / L R move the selection.
static void updateScroll(void)
{
	static bool dragging;
	static int lastX;
	int max = uiMaxScroll(&app);
	if (touching && startY >= GRID_Y - 4 && startY < GRID_Y + GRID_ROWS * (CELL_H + CELL_GAP))
	{
		if (!dragging && abs(touchX - startX) > 8)
		{
			dragging = true;
			lastX = touchX;
			app.scrollTo = -1;
		}
		if (dragging)
		{
			float d = touchX - lastX;
			app.scrollX -= d;
			app.scrollVel = app.scrollVel * 0.4f - d * 0.6f;
			lastX = touchX;
		}
	}
	else
		dragging = false;

	if (app.scrollTo >= 0)
	{
		float d = app.scrollTo - app.scrollX;
		app.scrollX += d * 0.3f;
		if (d > -1 && d < 1)
		{
			app.scrollX = app.scrollTo;
			app.scrollTo = -1;
		}
		bottomDirty = true;
	}
	else if (!dragging && (app.scrollVel > 0.5f || app.scrollVel < -0.5f))
	{
		app.scrollX += app.scrollVel;
		app.scrollVel *= 0.92f;
		bottomDirty = true;
	}
	else if (!dragging)
		app.scrollVel = 0;
	if (app.scrollX < 0 || app.scrollX > max)
	{
		app.scrollX = app.scrollX < 0 ? 0 : max;
		app.scrollVel = 0;
	}
	// Reading from the SD card stalls a frame, so thumbnails load one at a
	// time while the strip moves and in a burst once it rests.
	float speed = app.scrollVel < 0 ? -app.scrollVel : app.scrollVel;
	bool moving = dragging || app.scrollTo >= 0 || speed > 0.5f;
	if ((!moving || speed < 6) && loadThumbs(moving ? 1 : 3))
		bottomDirty = true;
}

static void updateGallery(u32 down)
{
	if (app.confirmDelete)
	{
		Hit hit = released ? uiDeleteDialogHit(touchX, touchY) : HIT_NONE;
		if ((down & KEY_X) || hit == HIT_DELETE)
		{
			app.confirmDelete = false;
			deleteShots();
		}
		else if ((down & KEY_B) || hit == HIT_CANCEL)
			app.confirmDelete = false;
		return;
	}

	if (checkUndo(down))
		return;
	if ((down & KEY_B) || (released && uiGalleryBackHit(startX, startY) && uiGalleryBackHit(touchX, touchY)))
	{
		if (app.picking)
			endPicking();
		else
			closeGallery();
		return;
	}
	if ((down & KEY_SELECT) && !app.picking)
	{
		openShare();
		return;
	}

	updateScroll();

	// Holding a shot starts picking; then taps (or A) pick shots.
	static bool held;
	bool still = abs(touchX - startX) < 8 && abs(touchY - startY) < 12;
	if (down & KEY_TOUCH)
		held = false;
	if (touching && still && !held && osGetTime() - touchDownAt > LONG_PRESS_MS)
	{
		int idx = uiCellAt(&app, startX, startY);
		if (idx >= 0)
		{
			held = true;
			if (!app.picking)
				endPicking();
			app.picking = true;
			togglePick(idx);
			selectShot(idx);
		}
	}
	if (released && still && !held)
	{
		int idx = uiCellAt(&app, touchX, touchY);
		if (idx >= 0)
		{
			if (app.picking)
				togglePick(idx);
			else if (idx == app.sel)
			{
				openShot(app.shots[idx]);
				return;
			}
			selectShot(idx);
		}
	}

	int row = app.sel % GRID_ROWS;
	if (down & KEY_RIGHT) selectShot(app.sel + GRID_ROWS);
	if (down & KEY_LEFT) selectShot(app.sel - GRID_ROWS);
	if ((down & KEY_DOWN) && row < GRID_ROWS - 1) selectShot(app.sel + 1);
	if ((down & KEY_UP) && row > 0) selectShot(app.sel - 1);
	if (down & KEY_R) selectShot(app.sel + VISIBLE_COLS * GRID_ROWS);
	if (down & KEY_L) selectShot(app.sel - VISIBLE_COLS * GRID_ROWS);

	if ((down & KEY_A) && app.picking)
		togglePick(app.sel);
	else if (down & KEY_A)
		openShot(app.shots[app.sel]);
	else if (down & KEY_Y)
		app.playing = !app.playing;
	else if ((down & KEY_X) && (!app.picking || app.pickedCount))
		app.confirmDelete = true;
	else if (previewTried != app.shots[app.sel] && osGetTime() - selChangedAt > PREVIEW_DELAY_MS)
	{
		previewTried = app.shots[app.sel];
		app.previewNum = storageLoadPreview(previewTried, app.previewL, app.previewR) ? previewTried : -1;
		topDirty = true;
	}
}

// ---- home ----

static void openFilterMenu(void)
{
	app.filterMenu = true;
	app.menuSel = app.settings.filter;
	app.menuScroll = 0;
	uiMenuReveal(&app);
}

static void updateFilterMenu(u32 down)
{
	static int dragY, dragScroll;
	static bool dragged;
	int sel = app.menuSel;
	if (down & KEY_X)
	{
		// Favorite on or off; favorites are listed first.
		app.settings.favorites[sel / 32] ^= 1u << (sel % 32);
		uiSetFavorites(app.settings.favorites);
		saveSettings();
		bool on = app.settings.favorites[sel / 32] >> (sel % 32) & 1;
		showToast(false, on ? "%s · favorite" : "%s · no longer a favorite", filterName(sel));
	}
	if (down & KEY_RIGHT) sel = uiMenuMove(sel, 1, 0);
	if (down & KEY_LEFT) sel = uiMenuMove(sel, -1, 0);
	if (down & KEY_DOWN) sel = uiMenuMove(sel, 0, 1);
	if (down & KEY_UP) sel = uiMenuMove(sel, 0, -1);
	bool moved = sel != app.menuSel;
	// L R: strength of the filter for new photos.
	if ((down & KEY_L) && app.settings.strength > 0)
		app.settings.strength -= STRENGTH_STEP;
	if ((down & KEY_R) && app.settings.strength < 100)
		app.settings.strength += STRENGTH_STEP;

	// Dragging scrolls; touching a button previews it, tapping the previewed
	// one again applies it.
	bool apply = down & KEY_A;
	if (down & KEY_TOUCH)
	{
		dragY = touchY;
		dragScroll = app.menuScroll;
		dragged = false;
	}
	if (touching && (dragged || abs(touchY - dragY) > 10))
	{
		int max = uiMenuMaxScroll(), v = dragScroll - (touchY - dragY);
		app.menuScroll = v < 0 ? 0 : v > max ? max : v;
		dragged = true;
	}
	if (released && !dragged)
	{
		int tile = uiMenuTileAt(&app, touchX, touchY);
		if (tile >= 0)
		{
			apply = tile == app.menuSel;
			sel = tile;
		}
	}
	app.menuSel = sel;
	if (moved)
		uiMenuReveal(&app);

	if (apply)
	{
		app.settings.filter = app.menuSel;
		app.filterMenu = false;
		saveSettings();
	}
	else if (down & (KEY_B | KEY_Y))
		app.filterMenu = false;
}

static bool tapped(bool (*hit)(int, int)) { return released && hit(startX, startY) && hit(touchX, touchY); }

// Capture defaults and modes back to how the app starts the first time.
static void resetDefaults(void)
{
	Settings *st = &app.settings;
	st->filter = 0;
	st->strength = 100;
	st->smooth = 0;
	st->delayMs = 120;
	st->quick = false;
	st->night = false;
	camSetNight(false);
	if (app.selfie)
	{
		app.selfie = false;
		camSetSelfie(false);
		app.liveL = camLeft();
		app.liveR = camRight();
	}
	app.doubleExp = app.haveFirst = false;
	saveSettings();
	showToast(false, "Back to the defaults");
	topDirty = bottomDirty = true;
}

// The recent shots by D-pad: left and right choose, A opens, B or up leaves.
// True if the keys were used here.
static bool updateRecentFocus(u32 down)
{
	int n = app.shotCount < RECENT_COUNT ? app.shotCount : RECENT_COUNT;
	if (down & KEY_RIGHT && app.homeFocus < n)
		app.homeFocus = app.homeFocus == n - 1 ? RECENT_COUNT : app.homeFocus + 1;
	else if (down & KEY_LEFT)
		app.homeFocus = app.homeFocus == RECENT_COUNT ? n - 1 : app.homeFocus - 1;
	else if (down & (KEY_B | KEY_UP))
		app.homeFocus = -1;
	else if (down & KEY_A)
	{
		int f = app.homeFocus;
		app.homeFocus = -1;
		openGallery(f == RECENT_COUNT ? 0 : f);
	}
	else
		return down & (KEY_DOWN | KEY_RIGHT); // nothing else for the D-pad while here
	return true;
}

static void updateHome(u32 down)
{
	if (checkUndo(down))
		return;
	if (app.camOk && camPoll())
	{
		app.liveL = camLeft();
		app.liveR = camRight();
		topDirty = true;
	}

	// Self-timer: counting down; A or B (or a tap) stops it.
	if (app.timerAt)
	{
		if (down & (KEY_A | KEY_B | KEY_TOUCH))
			app.timerAt = 0;
		else if (osGetTime() >= app.timerAt)
		{
			app.timerAt = 0;
			topDirty = bottomDirty = true;
			capture();
		}
		return;
	}
	if (app.filterMenu)
	{
		updateFilterMenu(down);
		return;
	}
	if (app.homeFocus >= 0 && updateRecentFocus(down))
		return;
	if ((down & KEY_RIGHT) && app.shotCount)
	{
		app.homeFocus = 0; // into the recent shots
		return;
	}
	if (down & KEY_DOWN)
	{
		resetDefaults();
		return;
	}
	int header = released && uiHeaderAt(startX, startY) == uiHeaderAt(touchX, touchY) ? uiHeaderAt(touchX, touchY) : -1;
	if (header == HB_SETTINGS)
	{
		app.settingsSel = 0;
		setMode(MODE_SETTINGS);
		return;
	}
	if ((down & KEY_SELECT) || header == HB_SHARE)
	{
		openShare();
		return;
	}
	if (app.haveFirst && (down & KEY_B))
	{
		app.haveFirst = false;
		showToast(false, "First exposure dropped");
		return;
	}
	if ((down & KEY_B) || tapped(uiAllButtonHit))
	{
		openGallery(0);
		return;
	}
	if (down & (KEY_A | KEY_L | KEY_R | KEY_ZL | KEY_ZR))
	{
		if (app.settings.timer && app.camOk)
			app.timerAt = osGetTime() + app.settings.timer * 1000;
		else
			capture();
		return;
	}
	if (down & KEY_Y)
	{
		openFilterMenu();
		return;
	}
	if (header == HB_DOUBLE)
	{
		app.doubleExp = !app.doubleExp;
		app.haveFirst = false;
		showToast(false, app.doubleExp ? "Double exposure · two shots in one" : "Double exposure off");
		return;
	}
	if (header == HB_SELFIE)
	{
		app.selfie = !app.selfie;
		camSetSelfie(app.selfie);
		app.liveL = camLeft();
		app.liveR = camRight();
		showToast(false, app.selfie ? "Selfie · plain photos" : "Back to 3D");
		return;
	}
	if ((down & KEY_X) || header == HB_NIGHT)
	{
		app.settings.night = !app.settings.night;
		camSetNight(app.settings.night);
		saveSettings();
		showToast(false, app.settings.night ? "Night mode · hold still" : "Night mode off");
		return;
	}

	if (released)
	{
		int recent = uiRecentAt(touchX, touchY);
		if (recent >= 0 && recent == uiRecentAt(startX, startY) && recent < app.shotCount)
		{
			openGallery(recent);
			return;
		}
		int step, tile = uiTileAt(touchX, touchY, &step), startStep;
		if (tile < 0 || tile != uiTileAt(startX, startY, &startStep))
			return;
		Settings *st = &app.settings;
		if (tile == TILE_FILTER)
		{
			openFilterMenu();
			return;
		}
		if (tile == TILE_SMOOTH)
			st->smooth = (st->smooth + 1) % (SMOOTH_OFF + 1);
		else if (tile == TILE_SPEED)
		{
			st->delayMs += step * DELAY_STEP;
			st->delayMs = st->delayMs < DELAY_MIN ? DELAY_MIN : st->delayMs > DELAY_MAX ? DELAY_MAX : st->delayMs;
		}
		else if (tile == TILE_QUICK)
			st->quick = !st->quick;
		saveSettings();
	}
}

int main(void)
{
	gfxInitDefault();
	gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
	gfxSetScreenFormat(GFX_BOTTOM, GSP_RGB565_OES);
	gfxSetDoubleBuffering(GFX_TOP, true);
	gfxSetDoubleBuffering(GFX_BOTTOM, true);

	// Up quickly: settings and the home screen first, the camera and the
	// rest after the first frame is on screen.
	osSetSpeedupEnable(true);
	filterInit();
	storageInit();
	loadSettings();
	applyTheme();
	uiSetFavorites(app.settings.favorites);
	ptmuInit();
	mcuHwcInit();
	checkStatus();
	app.shotL = shotL;
	app.shotR = shotR;
	app.firstL = firstL;
	app.firstR = firstR;
	app.shotNum = -1;
	app.delayMs = app.settings.delayMs;
	app.scrollTo = -1;
	refreshGallery();
	setMode(MODE_HOME);
	app.modeSince = 0; // no fade on the first frame
	app.camStarting = true;
	app.homeFocus = -1;
	render();

	storagePurgeAll();
	exportSetHalf(app.settings.gifHalf);
	exportSetSquare(app.settings.gifSquare);
	exportSetPhotoEye(app.settings.photoEye);
	exportQueueMissing(app.shots, app.shotCount);
	exportStart();
	soundInit();
	osSetSpeedupEnable(false);

	app.camOk = camOpen();
	if (app.camOk)
	{
		camSetFps(app.settings.fps);
		camSetNight(app.settings.night);
		camResume();
		app.liveL = camLeft();
		app.liveR = camRight();
	}
	app.camStarting = false;
	topDirty = true;

	while (aptMainLoop())
	{
		hidScanInput();
		u32 down = hidKeysDown(), held = hidKeysHeld();
		bool compare = app.mode == MODE_REVIEW && (held & KEY_Y);
		if (compare != app.compare)
			topDirty = true;
		app.compare = compare;
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
				touchDownAt = osGetTime();
			}
			touchX = t.px;
			touchY = t.py;
		}
		touching = nowTouching;
		// The gallery's top screen only changes with the selection, not with
		// every touch, and redrawing it would slow down scrolling.
		if (down || nowTouching || released)
		{
			bottomDirty = true;
			if (down || app.mode != MODE_GALLERY)
				topDirty = true;
		}

		// Background GIFs: note when one is done, so badges and counts update.
		static u32 lastGen;
		static int lastError;
		if (exportGeneration() != lastGen)
		{
			lastGen = exportGeneration();
			app.exporting = exportPending();
			bottomDirty = true;
			if (exportLastError() != lastError)
			{
				lastError = exportLastError();
				showToast(true, "GIF for WIG_%04d failed", lastError);
			}
		}
		updateSpeedup();
		if (app.trashedCount && !uiToastVisible(&app))
			purgeTrash();
		checkStatus();

		if (app.mode == MODE_HOME)
			updateHome(down);
		else if (app.mode == MODE_GALLERY)
			updateGallery(down);
		else if (app.mode == MODE_REVIEW)
			updateReview(down);
		else if (app.mode == MODE_SETTINGS)
			updateSettings(down);
		else
			updateShare(down);
		render();
	}

	purgeTrash();
	shareStop();
	exportStop();
	soundExit();
	ptmuExit();
	mcuHwcExit();
	camClose();
	gfxExit();
	return 0;
}
