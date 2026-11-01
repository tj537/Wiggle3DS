// Renders the app's screens on the Mac into BMP files, for checking the UI
// without a 3DS. Input: a raw RGB565 640x480 stereo pair (left, then right).
// usage: preview <pair.raw> <outdir>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../source/app.h"
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
	uiDraw(&app, top[0], top[1], bottom, false);
	snprintf(path, sizeof(path), "%s/%s.bmp", dir, name);
	writeBmp(path);
}

int main(int argc, char **argv)
{
	FILE *f = fopen(argv[1], "rb");
	fread(pair, 2, CAM_W * CAM_H * 2, f);
	fclose(f);
	filterInit();
	const char *dir = argv[2];

	app.now = 100000;
	app.modeSince = 0;
	app.camOk = true;
	app.liveL = pair[0];
	app.liveR = pair[1];
	app.chipScroll = 12;

	// Camera, with the carousel settled on a filter.
	app.mode = MODE_CAMERA;
	app.filter = 4;
	for (int i = 0; i < 30; i++)
		uiDraw(&app, top[0], top[1], bottom, false);
	render(dir, "camera");

	app.filterChangedAt = app.now - 200;
	app.lastThumbOk = true;
	downscale(app.lastThumb, THUMB_W, THUMB_H, pair[0], 0, 0, CAM_W, CAM_H);
	render(dir, "camera_switch");
	app.filterChangedAt = 0;

	// Review right after the shot, then with a toast.
	app.mode = MODE_REVIEW;
	app.modeSince = app.now - 1000;
	app.shotL = pair[0];
	app.shotR = pair[1];
	app.shotNum = -1;
	app.pivotX = 300;
	app.pivotY = 200;
	app.offset = (Offset){12, 2};
	app.delayMs = 120;
	app.filter = 0;
	render(dir, "review");
	app.toastAt = app.now - 600;
	strcpy(app.toast, "Gespeichert · WIG_0003.gif");
	app.shotNum = 3;
	app.filter = 5;
	render(dir, "review_toast");
	app.toast[0] = 0;
	app.saving = true;
	render(dir, "review_saving");
	app.saving = false;

	// Gallery with a few shots.
	app.mode = MODE_GALLERY;
	app.shotCount = 7;
	for (int i = 0; i < app.shotCount; i++)
	{
		app.shots[i] = 7 - i;
		app.gridOk[i] = i != 5;
		app.gridInfo[i] = (ShotInfo){0, 0, 12, 2, 120, i % FILTER_COUNT};
		downscale(app.grid[i], THUMB_W, THUMB_H, pair[0], i * 20, i * 10, CAM_W - 140, CAM_H - 70);
	}
	app.sel = 1;
	downscale(app.previewL, PREVIEW_W, PREVIEW_H, pair[0], 0, 0, CAM_W, CAM_H);
	downscale(app.previewR, PREVIEW_W, PREVIEW_H, pair[1], 0, 0, CAM_W, CAM_H);
	app.previewNum = app.shots[1];
	render(dir, "gallery");
	app.confirmDelete = true;
	render(dir, "gallery_delete");
	app.confirmDelete = false;
	app.shotCount = 0;
	render(dir, "gallery_empty");
	return 0;
}
