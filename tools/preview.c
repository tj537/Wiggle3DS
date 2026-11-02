// Renders the app's screens on the Mac into BMP files, for checking the UI
// without a 3DS. Input: a raw RGB565 640x480 stereo pair (left, then right).
// usage: preview <pair.raw> <outdir>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../source/app.h"
#include "../source/qrcodegen.h"
#include "../source/filter.h"

static App app;
static u16 top[2][400 * 240], bottom[320 * 240];
static u16 pair[2][CAM_W * CAM_H];

static void writeBmp(const char *path)
{
	// Both screens like on the device: top 400x240, bottom 320x240 centered below.
	int W = 400, H = 480;
	FILE *f = fopen(path, "wb");
	int rowBytes = W * 3, size = 54 + rowBytes * H;
	u8 hdr[54] = {'B', 'M'};
	*(u32 *)(hdr + 2) = size;
	*(u32 *)(hdr + 10) = 54;
	*(u32 *)(hdr + 14) = 40;
	*(s32 *)(hdr + 18) = W;
	*(s32 *)(hdr + 22) = -H;
	*(u16 *)(hdr + 26) = 1;
	*(u16 *)(hdr + 28) = 24;
	fwrite(hdr, 1, 54, f);
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
		{
			u16 p = 0x18E3;
			if (y < 240)
				p = top[0][x * 240 + (239 - y)];
			else if (x >= 40 && x < 360)
				p = bottom[(x - 40) * 240 + (239 - (y - 240))];
			u8 bgr[3] = {(p & 31) << 3, ((p >> 5) & 63) << 2, (p >> 11) << 3};
			fwrite(bgr, 1, 3, f);
		}
	fclose(f);
}

static void downscale(u16 *dst, int dw, int dh, const u16 *img, int sx, int sy, int w, int h)
{
	for (int y = 0; y < dh; y++)
		for (int x = 0; x < dw; x++)
			dst[y * dw + x] = img[(sy + y * h / dh) * CAM_W + sx + x * w / dw];
}

static void render(const char *dir, const char *name)
{
	char path[1024];
	uiDrawTop(&app, top[0], top[1], false);
	uiDrawBottom(&app, bottom);
	snprintf(path, sizeof(path), "%s/%s.bmp", dir, name);
	writeBmp(path);
}

// Export runs on the 3DS only; pretend one GIF is still being made.
bool exportIsPending(int num) { return num == 30; }

int main(int argc, char **argv)
{
	FILE *f = fopen(argv[1], "rb");
	fread(pair, 2, CAM_W * CAM_H * 2, f);
	fclose(f);
	const char *dir = argv[2];
	filterInit();
	uiSetTheme(0);
	app.settings = (Settings){.filter = 4, .delayMs = 120, .fps = 20, .flash = true, .smooth = 1};
	app.freeBytes = 3ULL << 30;
	app.battery = 76;
	app.charging = true;

	app.now = 100000;
	app.camOk = true;
	app.liveL = pair[0];
	app.liveR = pair[1];
	app.shotL = pair[0];
	app.shotR = pair[1];
	app.previewNum = -1;

	// Home, empty gallery, live camera.
	app.mode = MODE_HOME;
	render(dir, "home_empty");

	// Home with shots: live, then browsing a selection.
	app.shotCount = 30;
	app.exporting = 1;
	app.scrollX = 40;
	for (int i = 0; i < THUMB_CACHE; i++)
	{
		app.shots[i] = 30 - i;
		app.gridIdx[i] = i;
		app.gridOk[i] = i != 5;
		app.gridInfo[i] = (ShotInfo){0, 0, 12, 2, 120, 0};
		downscale(app.grid[i], THUMB_W, THUMB_H, pair[0], i % 7 * 20, i % 7 * 10, CAM_W - 140, CAM_H - 70);
	}
	for (int i = 0; i < THUMB_CACHE; i++)
		app.gridInfo[i].style = SHOT_STYLE(i % FILTER_COUNT, 1);
	app.settings.night = true;
	app.settings.smooth = SMOOTH_OFF;
	app.stacking = 2;
	app.stackFrames = 4;
	render(dir, "home");
	app.stacking = 0;
	app.settings.night = false;
	app.settings.smooth = 1;
	render(dir, "home_wiggle");
	app.filterMenu = true;
	app.menuSel = 45;
	app.menuScroll = 0;
	uiMenuReveal(&app);
	render(dir, "filter_menu");
	for (int f = 11; f < FILTER_COUNT; f++)
	{
		app.menuSel = f;
		char name[32];
		snprintf(name, sizeof(name), "f%02d", f);
		render(dir, name);
	}
	app.menuScroll = 0;
	app.filterMenu = false;
	app.mode = MODE_GALLERY;
	app.playing = true;
	app.sel = 4;
	downscale(app.previewL, PREVIEW_W, PREVIEW_H, pair[0], 0, 0, CAM_W, CAM_H);
	downscale(app.previewR, PREVIEW_W, PREVIEW_H, pair[1], 0, 0, CAM_W, CAM_H);
	app.previewNum = app.shots[1];
	render(dir, "home_browse");
	strcpy(app.selDate, "Yesterday");
	render(dir, "home_browse");
	app.picking = true;
	app.picked[4] = app.picked[6] = true;
	app.pickedCount = 2;
	render(dir, "gallery_pick");
	app.confirmDelete = true;
	render(dir, "home_delete");
	app.confirmDelete = false;
	app.picking = false;
	app.toastAt = app.now - 600;
	app.toastUndo = true;
	strcpy(app.toast, "2 shots deleted");
	render(dir, "gallery_undo");
	app.toast[0] = 0;
	app.toastUndo = false;

	// Review, with a toast and while saving.
	app.mode = MODE_REVIEW;
	app.shotNum = -1;
	app.pivotX = 300;
	app.pivotY = 200;
	app.offset = (Offset){12, 2};
	app.delayMs = 120;
	app.shotFilter = 7;
	render(dir, "review_tip");
	app.settings.tipDone = true;
	render(dir, "review");
	app.shotSmooth = SMOOTH_OFF;
	render(dir, "review_photo");
	app.shotSmooth = 0;
	app.toastAt = app.now - 600;
	strcpy(app.toast, "Saved · WIG_0003.gif");
	app.shotNum = 3;
	render(dir, "review_toast");
	app.toast[0] = 0;
	app.saving = true;
	render(dir, "review_saving");
	app.saving = false;

	// Settings, then the home screen in another theme.
	app.mode = MODE_SETTINGS;
	app.settingsSel = 0;
	render(dir, "settings");
	app.settingsTop = SET_COUNT - SET_VISIBLE;
	app.settingsSel = SET_COUNT - 1;
	render(dir, "settings_end");
	app.lowBattery = true;
	uiSetTheme(2);
	app.settings.theme = 2;
	app.mode = MODE_HOME;
	render(dir, "home_ocean");
	uiSetTheme(0);
	app.settings.theme = 0;

	// Share screen with a QR code for an example address.
	app.mode = MODE_SHARE;
	app.shareOk = true;
	app.sharedCount = 2;
	strcpy(app.shareUrl, "http://192.168.68.42:8080/");
	static u8 temp[sizeof(app.qr)];
	qrcodegen_encodeText(app.shareUrl, temp, app.qr, qrcodegen_Ecc_MEDIUM, 1, 10, qrcodegen_Mask_AUTO, true);
	render(dir, "share");
	app.shareOk = false;
	render(dir, "share_nowifi");
	return 0;
}
