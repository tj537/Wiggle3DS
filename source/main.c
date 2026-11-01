#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "camera.h"
#include "draw.h"
#include "gif.h"
#include "wiggle.h"

#define SAVE_DIR "sdmc:/Wiggle3DS"
#define PREVIEW_H 192 // bottom screen: image area, text below
#define WHITE COL565(255, 255, 255)
#define GREY COL565(150, 150, 160)
#define ACCENT COL565(255, 80, 200)
#define BG COL565(16, 12, 28)

typedef enum
{
	MODE_CAMERA,
	MODE_REVIEW,
} Mode;

static u16 shotL[CAM_W * CAM_H], shotR[CAM_W * CAM_H];
static int pivotX = CAM_W / 2, pivotY = CAM_H / 2;
static Offset offset;
static int delayMs = 120;
static char status[64];

static void drawTop3D(void)
{
	bool stereo = osGet3DSliderState() > 0.0f;
	gfxSet3D(stereo);
	drawImage(drawFramebuffer(1, 0), 400, 0, 0, camLeft(), CAM_W, 0, 0, CAM_W, CAM_H);
	if (stereo)
		drawImage(drawFramebuffer(1, 1), 400, 0, 0, camRight(), CAM_W, 0, 0, CAM_W, CAM_H);
}

static void drawCameraHelp(void)
{
	u16 *fb = drawFramebuffer(0, 0);
	drawClear(fb, 320, BG);
	drawText(fb, 320, 16, 24, ACCENT, "WIGGLE3DS");
	drawText(fb, 320, 16, 56, WHITE, "A / L / R   Foto aufnehmen");
	drawText(fb, 320, 16, 72, WHITE, "3D-Regler   3D-Vorschau");
	drawText(fb, 320, 16, 88, WHITE, "START       Beenden");
	drawText(fb, 320, 16, 120, GREY, "Tipp: Motiv 0,5-2 m entfernt,");
	drawText(fb, 320, 16, 132, GREY, "mit Hintergrund dahinter.");
	if (status[0])
		drawText(fb, 320, 16, 216, ACCENT, "%s", status);
}

static void drawWiggle(void)
{
	gfxSet3D(false);
	Crop c = wiggleCrop(offset);
	bool second = (osGetTime() / delayMs) & 1;
	u16 *fb = drawFramebuffer(1, 0);
	drawClear(fb, 400, 0);
	drawImage(fb, 400, (400 - c.w) / 2, (240 - c.h) / 2,
	          second ? shotR : shotL, CAM_W, second ? c.rx : c.lx, second ? c.ry : c.ly, c.w, c.h);
}

static void drawReviewHelp(void)
{
	u16 *fb = drawFramebuffer(0, 0);
	drawClear(fb, 320, BG);
	drawImageScaled(fb, 320, 0, 0, 320, PREVIEW_H, shotL, CAM_W, CAM_H);
	drawCross(fb, 320, pivotX * 320 / CAM_W, pivotY * PREVIEW_H / CAM_H, 8, ACCENT);
	drawText(fb, 320, 4, 196, WHITE, "Tippen: Drehpunkt  </>: fein (%d)", offset.dx);
	drawText(fb, 320, 4, 208, WHITE, "Hoch/Runter: Tempo %d ms", delayMs);
	drawText(fb, 320, 4, 220, WHITE, "A: GIF speichern   B: Neues Foto");
	if (status[0])
		drawText(fb, 320, 4, 231, ACCENT, "%s", status);
}

static void swap(void)
{
	gfxFlushBuffers();
	gfxSwapBuffers();
	gspWaitForVBlank();
}

static void saveGif(void)
{
	char path[64];
	mkdir(SAVE_DIR, 0777);
	for (int n = 1; n < 10000; n++)
	{
		struct stat st;
		snprintf(path, sizeof(path), SAVE_DIR "/WIG_%04d.gif", n);
		if (stat(path, &st) != 0)
			break;
	}

	snprintf(status, sizeof(status), "Speichere...");
	drawReviewHelp();
	swap();

	Crop c = wiggleCrop(offset);
	GifFrame frames[2] = {
		{shotL, CAM_W, c.lx, c.ly},
		{shotR, CAM_W, c.rx, c.ry},
	};
	if (gifWrite(path, frames, 2, c.w, c.h, delayMs))
		snprintf(status, sizeof(status), "Gespeichert: %s", path + sizeof(SAVE_DIR));
	else
		snprintf(status, sizeof(status), "Fehler beim Speichern!");
}

int main(void)
{
	osSetSpeedupEnable(true);
	gfxInitDefault();
	gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
	gfxSetScreenFormat(GFX_BOTTOM, GSP_RGB565_OES);
	gfxSetDoubleBuffering(GFX_TOP, true);
	gfxSetDoubleBuffering(GFX_BOTTOM, true);

	if (camOpen())
		camResume();
	else
		snprintf(status, sizeof(status), "Kamera-Fehler!");

	Mode mode = MODE_CAMERA;
	while (aptMainLoop())
	{
		hidScanInput();
		u32 down = hidKeysDown(), held = hidKeysHeld();
		if (down & KEY_START)
			break;

		if (mode == MODE_CAMERA)
		{
			camPoll();
			if (down & (KEY_A | KEY_L | KEY_R))
			{
				CAMU_PlayShutterSound(SHUTTER_SOUND_TYPE_NORMAL);
				memcpy(shotL, camLeft(), sizeof(shotL));
				memcpy(shotR, camRight(), sizeof(shotR));
				camPause();
				wiggleSetImages(shotL, shotR);
				pivotX = CAM_W / 2;
				pivotY = CAM_H / 2;
				offset = wiggleMatch(pivotX, pivotY);
				status[0] = 0;
				mode = MODE_REVIEW;
				continue;
			}
			drawTop3D();
			drawCameraHelp();
		}
		else
		{
			if (held & KEY_TOUCH)
			{
				touchPosition t;
				hidTouchRead(&t);
				if (t.py < PREVIEW_H)
				{
					int x = t.px * CAM_W / 320, y = t.py * CAM_H / PREVIEW_H;
					if (x != pivotX || y != pivotY)
					{
						pivotX = x;
						pivotY = y;
						offset = wiggleMatch(pivotX, pivotY);
					}
				}
			}
			if ((down & KEY_LEFT) && offset.dx > -120)
				offset.dx--;
			if ((down & KEY_RIGHT) && offset.dx < 120)
				offset.dx++;
			if ((down & KEY_UP) && delayMs > 40)
				delayMs -= 20;
			if ((down & KEY_DOWN) && delayMs < 400)
				delayMs += 20;
			if (down & KEY_A)
				saveGif();
			if (down & KEY_B)
			{
				status[0] = 0;
				camResume();
				mode = MODE_CAMERA;
				continue;
			}
			drawWiggle();
			drawReviewHelp();
		}
		swap();
	}

	camClose();
	gfxExit();
	return 0;
}
