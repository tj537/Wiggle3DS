#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include "camera.h"
#include "draw.h"
#include "gif.h"
#include "storage.h"
#include "wiggle.h"

// Bottom screen review layout: 640x480 shown as 256x192, text below.
#define PREVIEW_X 32
#define PREVIEW_W 256
#define PREVIEW_H 192
#define MAX_DX 200

// Gallery grid: 4x3 thumbnails of 80x60 on the bottom screen.
#define GRID_COLS 4
#define GRID_ROWS 3
#define PAGE (GRID_COLS * GRID_ROWS)
#define CELL_W 80
#define CELL_H 60
#define MAX_SHOTS 1000

#define WHITE COL565(255, 255, 255)
#define GREY COL565(150, 150, 160)
#define ACCENT COL565(255, 80, 200)
#define BG COL565(16, 12, 28)

typedef enum
{
	MODE_CAMERA,
	MODE_REVIEW,
	MODE_GALLERY,
} Mode;

static Mode mode = MODE_CAMERA, reviewReturn = MODE_CAMERA;
static char status[64];

// Shot being reviewed. shotNum is -1 until it has been saved once.
static u16 shotL[CAM_W * CAM_H], shotR[CAM_W * CAM_H];
static int shotNum = -1;
static int pivotX = CAM_W / 2, pivotY = CAM_H / 2;
static Offset offset;
static int delayMs = 120;

// Gallery state; thumbnails are loaded for the visible page only.
static int shots[MAX_SHOTS], shotCount, sel, loadedPage = -1;
static u16 thumbL[PAGE][THUMB_W * THUMB_H], thumbR[PAGE][THUMB_W * THUMB_H];
static bool thumbOk[PAGE], confirmDelete;

static bool wigglePhase(void) { return (osGetTime() / delayMs) & 1; }

static void swap(void)
{
	gfxFlushBuffers();
	gfxSwapBuffers();
	gspWaitForVBlank();
}

// ---- camera ----

static void drawCamera(void)
{
	bool stereo = osGet3DSliderState() > 0.0f;
	gfxSet3D(stereo);
	for (int eye = 0; eye <= (stereo ? 1 : 0); eye++)
	{
		u16 *fb = drawFramebuffer(1, eye);
		drawClear(fb, 400, 0);
		drawImageScaled(fb, 400, 40, 0, 320, 240, eye ? camRight() : camLeft(), CAM_W, 0, 0, CAM_W, CAM_H);
	}

	u16 *fb = drawFramebuffer(0, 0);
	drawClear(fb, 320, BG);
	drawText(fb, 320, 16, 24, ACCENT, "WIGGLE3DS");
	drawText(fb, 320, 16, 56, WHITE, "A / L / R   Foto aufnehmen");
	drawText(fb, 320, 16, 72, WHITE, "Y           Galerie");
	drawText(fb, 320, 16, 88, WHITE, "3D-Regler   3D-Vorschau");
	drawText(fb, 320, 16, 104, WHITE, "START       Beenden");
	drawText(fb, 320, 16, 136, GREY, "Tipp: Motiv 0,5-2 m entfernt,");
	drawText(fb, 320, 16, 148, GREY, "mit Hintergrund dahinter.");
	if (status[0])
		drawText(fb, 320, 16, 216, ACCENT, "%s", status);
}

static void openGallery(void);

static void updateCamera(u32 down)
{
	camPoll();
	if (down & (KEY_A | KEY_L | KEY_R))
	{
		CAMU_PlayShutterSound(SHUTTER_SOUND_TYPE_NORMAL);
		memcpy(shotL, camLeft(), sizeof(shotL));
		memcpy(shotR, camRight(), sizeof(shotR));
		camPause();
		wiggleSetImages(shotL, shotR);
		shotNum = -1;
		pivotX = CAM_W / 2;
		pivotY = CAM_H / 2;
		offset = wiggleMatch(pivotX, pivotY);
		status[0] = 0;
		reviewReturn = MODE_CAMERA;
		mode = MODE_REVIEW;
	}
	else if (down & KEY_Y)
	{
		camPause();
		openGallery();
	}
}

// ---- review ----

static void drawReview(void)
{
	gfxSet3D(false);
	Crop c = wiggleCrop(offset);
	int dw = c.w * 240 / c.h, dh = 240;
	if (dw > 400)
	{
		dw = 400;
		dh = c.h * 400 / c.w;
	}
	bool second = wigglePhase();
	u16 *fb = drawFramebuffer(1, 0);
	drawClear(fb, 400, 0);
	drawImageScaled(fb, 400, (400 - dw) / 2, (240 - dh) / 2, dw, dh,
	                second ? shotR : shotL, CAM_W, second ? c.rx : c.lx, second ? c.ry : c.ly, c.w, c.h);

	fb = drawFramebuffer(0, 0);
	drawClear(fb, 320, BG);
	drawImageScaled(fb, 320, PREVIEW_X, 0, PREVIEW_W, PREVIEW_H, shotL, CAM_W, 0, 0, CAM_W, CAM_H);
	drawCross(fb, 320, PREVIEW_X + pivotX * PREVIEW_W / CAM_W, pivotY * PREVIEW_H / CAM_H, 8, ACCENT);
	drawText(fb, 320, 4, 196, WHITE, "Tippen: Drehpunkt  </>: fein (%d)", offset.dx);
	drawText(fb, 320, 4, 208, WHITE, "Hoch/Runter: Tempo %d ms", delayMs);
	drawText(fb, 320, 4, 220, WHITE, "A: GIF speichern   B: Zurueck");
	if (status[0])
		drawText(fb, 320, 4, 231, ACCENT, "%s", status);
}

static void saveShot(void)
{
	snprintf(status, sizeof(status), "Speichere...");
	drawReview();
	swap();

	storageInit();
	if (shotNum < 0)
		shotNum = storageNextNumber();
	ShotInfo info = {pivotX, pivotY, offset.dx, offset.dy, delayMs};
	char path[64];
	storageGifPath(shotNum, path, sizeof(path));
	Crop c = wiggleCrop(offset);
	GifFrame frames[2] = {
		{shotL, CAM_W, c.lx, c.ly},
		{shotR, CAM_W, c.rx, c.ry},
	};
	if (storageSave(shotNum, shotL, shotR, &info) && gifWrite(path, frames, 2, c.w, c.h, delayMs))
		snprintf(status, sizeof(status), "Gespeichert: GIF/WIG_%04d.gif", shotNum);
	else
		snprintf(status, sizeof(status), "Fehler beim Speichern!");
	loadedPage = -1; // gallery thumbnails may have changed
}

static void updateReview(u32 down, u32 held)
{
	if (held & KEY_TOUCH)
	{
		touchPosition t;
		hidTouchRead(&t);
		int tx = t.px - PREVIEW_X;
		if (tx >= 0 && tx < PREVIEW_W && t.py < PREVIEW_H)
		{
			int x = tx * CAM_W / PREVIEW_W, y = t.py * CAM_H / PREVIEW_H;
			if (x != pivotX || y != pivotY)
			{
				pivotX = x;
				pivotY = y;
				offset = wiggleMatch(pivotX, pivotY);
			}
		}
	}
	if ((down & KEY_LEFT) && offset.dx > -MAX_DX)
		offset.dx--;
	if ((down & KEY_RIGHT) && offset.dx < MAX_DX)
		offset.dx++;
	if ((down & KEY_UP) && delayMs > 40)
		delayMs -= 20;
	if ((down & KEY_DOWN) && delayMs < 400)
		delayMs += 20;
	if (down & KEY_A)
		saveShot();
	if (down & KEY_B)
	{
		status[0] = 0;
		if (reviewReturn == MODE_GALLERY)
			openGallery();
		else
		{
			camResume();
			mode = MODE_CAMERA;
		}
	}
}

// ---- gallery ----

static void openGallery(void)
{
	shotCount = storageList(shots, MAX_SHOTS);
	if (sel >= shotCount)
		sel = shotCount ? shotCount - 1 : 0;
	loadedPage = -1;
	confirmDelete = false;
	status[0] = 0;
	mode = MODE_GALLERY;
}

static void loadPage(void)
{
	int page = sel / PAGE;
	if (page == loadedPage)
		return;
	for (int i = 0; i < PAGE; i++)
	{
		int idx = page * PAGE + i;
		thumbOk[i] = idx < shotCount && storageLoadThumbs(shots[idx], thumbL[i], thumbR[i]);
	}
	loadedPage = page;
}

static void drawGallery(void)
{
	gfxSet3D(false);
	u16 *top = drawFramebuffer(1, 0), *fb = drawFramebuffer(0, 0);
	drawClear(top, 400, 0);
	drawClear(fb, 320, BG);

	if (!shotCount)
	{
		drawText(top, 400, 108, 116, GREY, "Noch keine Wigglegrams.");
		drawText(fb, 320, 16, 80, WHITE, "Mach ein Foto und speichere");
		drawText(fb, 320, 16, 92, WHITE, "es mit A, dann landet es hier.");
		drawText(fb, 320, 16, 200, WHITE, "B: Zurueck zur Kamera");
		return;
	}

	loadPage();
	int page = sel / PAGE, cur = sel % PAGE;
	// The selected shot wiggles big on the top screen.
	if (thumbOk[cur])
		drawImageScaled(top, 400, 40, 0, 320, 240, wigglePhase() ? thumbR[cur] : thumbL[cur], THUMB_W, 0, 0, THUMB_W, THUMB_H);

	for (int i = 0; i < PAGE && page * PAGE + i < shotCount; i++)
	{
		int x = (i % GRID_COLS) * CELL_W, y = (i / GRID_COLS) * CELL_H;
		if (i == cur)
			drawRect(fb, 320, x, y, CELL_W, CELL_H, ACCENT);
		if (thumbOk[i])
			drawImageScaled(fb, 320, x + 3, y + 3, CELL_W - 6, CELL_H - 6, thumbL[i], THUMB_W, 0, 0, THUMB_W, THUMB_H);
		else
			drawRect(fb, 320, x + 3, y + 3, CELL_W - 6, CELL_H - 6, GREY);
	}

	drawText(fb, 320, 4, 186, ACCENT, "GALERIE  WIG_%04d  (%d/%d)", shots[sel], sel + 1, shotCount);
	drawText(fb, 320, 4, 200, WHITE, "A: Oeffnen  X: Loeschen  B: Kamera");
	if (confirmDelete)
		drawText(fb, 320, 4, 216, ACCENT, "Nochmal X druecken zum Loeschen");
	else if (status[0])
		drawText(fb, 320, 4, 216, ACCENT, "%s", status);
}

static void openShot(int num)
{
	ShotInfo info;
	if (!storageLoad(num, shotL, shotR, &info))
	{
		snprintf(status, sizeof(status), "Konnte WIG_%04d nicht laden", num);
		return;
	}
	wiggleSetImages(shotL, shotR);
	shotNum = num;
	pivotX = info.pivotX;
	pivotY = info.pivotY;
	offset = (Offset){info.dx, info.dy};
	delayMs = info.delayMs;
	status[0] = 0;
	reviewReturn = MODE_GALLERY;
	mode = MODE_REVIEW;
}

static void deleteSelected(void)
{
	int num = shots[sel], keep = sel;
	storageDelete(num);
	openGallery();
	sel = keep < shotCount ? keep : (shotCount ? shotCount - 1 : 0);
	snprintf(status, sizeof(status), "WIG_%04d geloescht", num);
}

static void updateGallery(u32 down)
{
	if (down & KEY_B)
	{
		status[0] = 0;
		camResume();
		mode = MODE_CAMERA;
		return;
	}
	if (!shotCount || !down)
		return;

	bool wasConfirm = confirmDelete;
	confirmDelete = false;

	int prev = sel;
	if (down & KEY_RIGHT) sel++;
	if (down & KEY_LEFT) sel--;
	if (down & KEY_DOWN) sel += GRID_COLS;
	if (down & KEY_UP) sel -= GRID_COLS;
	if (down & KEY_TOUCH)
	{
		touchPosition t;
		hidTouchRead(&t);
		if (t.py < GRID_ROWS * CELL_H)
			sel = (sel / PAGE) * PAGE + (t.py / CELL_H) * GRID_COLS + t.px / CELL_W;
	}
	if (sel < 0) sel = 0;
	if (sel >= shotCount) sel = shotCount - 1;
	if (sel != prev)
		status[0] = 0;

	if (down & KEY_A)
		openShot(shots[sel]);
	else if (down & KEY_X)
	{
		if (wasConfirm)
			deleteSelected();
		else
			confirmDelete = true;
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
	storageInit();

	if (camOpen())
		camResume();
	else
		snprintf(status, sizeof(status), "Kamera-Fehler!");

	while (aptMainLoop())
	{
		hidScanInput();
		u32 down = hidKeysDown(), held = hidKeysHeld();
		if (down & KEY_START)
			break;

		Mode before = mode;
		switch (mode)
		{
		case MODE_CAMERA: updateCamera(down); break;
		case MODE_REVIEW: updateReview(down, held); break;
		case MODE_GALLERY: updateGallery(down); break;
		}
		if (mode != before)
			continue;

		switch (mode)
		{
		case MODE_CAMERA: drawCamera(); break;
		case MODE_REVIEW: drawReview(); break;
		case MODE_GALLERY: drawGallery(); break;
		}
		swap();
	}

	camClose();
	gfxExit();
	return 0;
}
