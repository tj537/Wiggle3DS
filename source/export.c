#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "camera.h"
#include "draw.h"
#include "export.h"
#include "filter.h"
#include "gif.h"
#include "jpeg.h"
#include "mp4.h"
#include "stereo.h"
#include "storage.h"
#include "wiggle.h"

#define MAX_JOBS 1000
#define MAX_GIF_FRAMES 16

static Thread thread;
static LightLock lock;
static LightEvent wake;
static volatile bool quit;
static volatile bool half, square, photoRight = true, mp4On;
static volatile int stereoFile;
static volatile bool dateStamp;

// Guarded by `lock`. jobs[0] is the one in progress while `working`.
static int jobs[MAX_JOBS], jobCount;
static bool working;
static volatile u32 generation;
static volatile int lastError;

static u16 imgL[CAM_W * CAM_H], imgR[CAM_W * CAM_H];

// The GIF gets the filtered crops and the blended frames in between; the
// stored photo stays unfiltered. One buffer per distinct mix.
// Written next to the final name, so the share page never sends half a file.
// `other` is the shot's file in the other format, left from before its
// transition was switched on or off.
static bool finish(bool ok, const char *temp, const char *path, const char *other, int num)
{
	unlink(path);
	if (ok)
		unlink(other);
	ok = ok && rename(temp, path) == 0;
	unlink(temp);
	if (!storageExists(num)) // deleted meanwhile
		unlink(path);
	return ok;
}

void exportStampText(u64 when, char *out, int size)
{
	time_t t = (time_t)when;
	strftime(out, size, "'%y %m %d", gmtime(&t));
}

// The date stamp: orange digits with a reddish glow, in the bottom right
// corner, sized to the image.
static void stamp(u16 *img, int w, int h, int num)
{
	char text[16];
	exportStampText(storageShotTime(num), text, sizeof(text));
	int scale = w >= 400 ? 2 : 1, tw = textWidth(&fontBody, text) * scale, th = (fontBody.ascent + fontBody.descent) * scale;
	int x = w - tw - w / 24, y = h - th - h / 24;
	for (int dy = -1; dy <= 1; dy++)
		for (int dx = -1; dx <= 1; dx++)
			if (dx || dy)
				drawTextImage(img, w, h, &fontBody, x + dx * scale, y + dy * scale, scale, COL565(200, 40, 10), 90, text);
	drawTextImage(img, w, h, &fontBody, x, y, scale, COL565(255, 160, 50), 255, text);
}

// The extras of a shot, if set, through a temp file each like the GIF; an
// extra that is off is removed, so none is left over from before.
static void makeExtra(int num, int extra, bool on, bool (*write)(const char *, void *), void *arg)
{
	char path[64], temp[64];
	storageExtraPath(num, extra, path, sizeof(path));
	if (!on)
	{
		unlink(path);
		return;
	}
	snprintf(temp, sizeof(temp), "%.59s.tmp", path);
	finish(write(temp, arg), temp, path, path, num);
}

typedef struct
{
	const u16 *left, *right;
	int stride, w, h;
	const GifFrame *frames;
	int count, delayMs;
} ExtraArgs;

static bool writeMpo(const char *path, void *arg)
{
	const ExtraArgs *e = arg;
	return mpoWrite(path, e->left, 0, 0, e->right, 0, 0, e->stride, e->w, e->h);
}

static bool writeSbs(const char *path, void *arg)
{
	const ExtraArgs *e = arg;
	return sbsWrite(path, e->left, 0, 0, e->right, 0, 0, e->stride, e->w, e->h);
}

static bool writeMp4(const char *path, void *arg)
{
	const ExtraArgs *e = arg;
	return mp4Write(path, e->frames, e->count, e->w, e->h, e->delayMs);
}

// mode: 0 none, 1 MPO, 2 side by side.
static void makeStereo(int num, ExtraArgs *e, int mode)
{
	makeExtra(num, EXTRA_MPO, mode == 1, writeMpo, e);
	makeExtra(num, EXTRA_SBS, mode == 2, writeSbs, e);
}

// A selfie has the same image in both eyes: no 3D file for it.
static int stereoFor(void) { return memcmp(imgL, imgR, sizeof(imgL)) ? stereoFile : 0; }

// A plain photo: the whole left eye with its filter.
static bool makeJpg(int num, int filter, int strength)
{
	char path[64], temp[64], gif[64];
	storagePhotoPath(num, path, sizeof(path));
	storageGifPath(num, gif, sizeof(gif));
	snprintf(temp, sizeof(temp), "%.59s.tmp", path);
	// Into the other eye's buffer, which isn't needed.
	u16 *src = photoRight ? imgR : imgL, *dst = photoRight ? imgL : imgR;
	int stereo = stereoFor();
	if (stereo)
	{
		// Both eyes filtered for the 3D file first, then the photo from them.
		u16 *l = malloc(CAM_W * CAM_H * 2), *r = malloc(CAM_W * CAM_H * 2);
		if (l && r)
		{
			filterApply(l, imgL, CAM_W, 0, 0, CAM_W, CAM_H, filter, 0, strength);
			filterApply(r, imgR, CAM_W, 0, 0, CAM_W, CAM_H, filter, 0, strength);
			makeStereo(num, &(ExtraArgs){l, r, CAM_W, CAM_W, CAM_H}, stereo);
		}
		free(l);
		free(r);
	}
	else
		makeStereo(num, &(ExtraArgs){0}, 0);
	makeExtra(num, EXTRA_MP4, false, writeMp4, NULL); // a photo has no video
	filterApply(dst, src, CAM_W, 0, 0, CAM_W, CAM_H, filter, 0, strength);
	if (dateStamp)
		stamp(dst, CAM_W, CAM_H, num);
	return finish(jpegWrite(temp, dst, CAM_W, CAM_W, CAM_H, 92), temp, path, gif, num);
}

// The part of the images the GIF shows: aligned on the pivot, or for a
// plain photo the whole frame; square if set.
static Crop crop(const ShotInfo *info, bool whole)
{
	Crop c = whole ? (Crop){0, 0, 0, 0, CAM_W, CAM_H} : wiggleCrop((Offset){info->dx, info->dy});
	if (square && c.w > c.h)
	{
		int cut = (c.w - c.h) / 2;
		c.lx += cut;
		c.rx += cut;
		c.w = c.h;
	}
	return c;
}

static bool makeGif(int num)
{
	ShotInfo info;
	if (!storageLoad(num, imgL, imgR, &info))
		return false;
	int filter = SHOT_FILTER(&info) < FILTER_COUNT ? SHOT_FILTER(&info) : 0, strength = SHOT_STRENGTH(&info);
	if (SHOT_IS_PHOTO(&info))
		return makeJpg(num, filter, strength);
	Crop c = crop(&info, false);
	int pixels = c.w * c.h, mix[MAX_WIGGLE_FRAMES], n = wiggleFrames(SHOT_SMOOTH(&info), mix);
	// A moving glitch needs every step of the loop made on its own; a still
	// one only the left and right eye, and blends of them.
	bool moves = filterMoves(filter);
	int total = filterLoopFrames(filter, n);
	u16 *bufs[MAX_GIF_FRAMES] = {0}, *tmp = NULL;
	GifFrame frames[MAX_GIF_FRAMES];
	bool ok = total <= MAX_GIF_FRAMES && (!moves || (tmp = malloc(pixels * 2)));
	for (int i = 0; i < total && ok; i++)
	{
		int same = -1;
		for (int j = 0; j < i && !moves; j++)
			if (mix[j] == mix[i])
				same = j;
		if (same < 0 && !(bufs[i] = malloc(pixels * 2)))
			ok = false;
		frames[i] = (GifFrame){same < 0 ? bufs[i] : frames[same].img, c.w, 0, 0};
	}
	int right = n / 2; // the frame that is all right eye
	if (ok && moves)
		for (int i = 0; i < total; i++)
		{
			int m = mix[i % n];
			if (m < 255)
				filterApply(bufs[i], imgL, CAM_W, c.lx, c.ly, c.w, c.h, filter, i, strength);
			if (m > 0)
			{
				filterApply(m < 255 ? tmp : bufs[i], imgR, CAM_W, c.rx, c.ry, c.w, c.h, filter, i, strength);
				if (m < 255)
					blendRows(bufs[i], bufs[i], tmp, pixels, m);
			}
		}
	else if (ok)
	{
		filterApply(bufs[0], imgL, CAM_W, c.lx, c.ly, c.w, c.h, filter, 0, strength);
		filterApply(bufs[right], imgR, CAM_W, c.rx, c.ry, c.w, c.h, filter, 0, strength);
		for (int i = 1; i < right; i++)
			blendRows(bufs[i], bufs[0], bufs[right], pixels, mix[i]);
	}
	if (ok) // the 3D file at full size, from the two eyes of the loop
		makeStereo(num, &(ExtraArgs){bufs[0], bufs[right], c.w, c.w, c.h}, stereoFor());
	if (ok && dateStamp) // each frame once (repeats share a buffer)
		for (int i = 0; i < total; i++)
			if (bufs[i])
				stamp(bufs[i], c.w, c.h, num);
	if (ok)
	{
		int gw = c.w, gh = c.h;
		if (half)
		{
			// Halve in place: each frame's buffer has room for the bigger image.
			for (int i = 0; i < total; i++)
				if (bufs[i])
					halveRows(bufs[i], bufs[i], c.w, c.h);
			gw = c.w / 2;
			gh = c.h / 2;
			for (int i = 0; i < total; i++)
				frames[i].stride = gw;
		}
		char path[64], temp[64];
		storageGifPath(num, path, sizeof(path));
		snprintf(temp, sizeof(temp), "%.*s.tmp", (int)strlen(path) - 4, path);
		char jpg[64];
		storagePhotoPath(num, jpg, sizeof(jpg));
		ok = finish(gifWrite(temp, frames, total, gw, gh, info.delayMs), temp, path, jpg, num);
		makeExtra(num, EXTRA_MP4, ok && mp4On, writeMp4, &(ExtraArgs){.frames = frames, .count = total, .w = gw, .h = gh, .delayMs = info.delayMs});
	}
	for (int i = 0; i < total && i < MAX_GIF_FRAMES; i++)
		free(bufs[i]);
	free(tmp);
	return ok;
}

// Shots to check for a missing GIF, handed over by exportQueueMissing.
static int scanNums[MAX_JOBS], scanCount;

static void worker(void *arg)
{
	(void)arg;
	// Work that would slow down the start: checking the SD card for missing
	// GIFs, and the filter tables.
	LightLock_Lock(&lock);
	int count = scanCount;
	scanCount = 0;
	LightLock_Unlock(&lock);
	for (int i = count - 1; i >= 0 && !quit; i--) // oldest first
		if (!storageHasGif(scanNums[i]))
			exportQueue(scanNums[i]);
	filterPrebuild();
	while (!quit)
	{
		LightLock_Lock(&lock);
		working = jobCount > 0;
		int num = working ? jobs[0] : 0;
		LightLock_Unlock(&lock);
		if (!num)
		{
			LightEvent_Wait(&wake);
			continue;
		}

		bool ok = makeGif(num);
		LightLock_Lock(&lock);
		jobCount--;
		memmove(jobs, jobs + 1, jobCount * sizeof(int));
		working = false;
		LightLock_Unlock(&lock);
		if (!ok && storageExists(num)) // not just deleted
			lastError = num;
		generation++;
	}
}

void exportStart(void)
{
	LightLock_Init(&lock);
	LightEvent_Init(&wake, RESET_ONESHOT);
	// Below the main thread; on the New 3DS on a core of its own, so the UI
	// stays smooth while a GIF is encoded.
	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	prio = prio < 0x3F ? prio + 1 : prio;
	bool isNew = false;
	APT_CheckNew3DS(&isNew);
	thread = isNew ? threadCreate(worker, NULL, 0x8000, prio, 2, false) : NULL;
	if (!thread)
		thread = threadCreate(worker, NULL, 0x8000, prio, -2, false);
}

void exportStop(void)
{
	if (!thread)
		return;
	quit = true;
	LightEvent_Signal(&wake);
	threadJoin(thread, U64_MAX);
	threadFree(thread);
	thread = NULL;
}

void exportSetHalf(bool h) { half = h; }
void exportSetSquare(bool s) { square = s; }
void exportSetPhotoEye(int eye) { photoRight = eye != 0; }

void exportSetDateStamp(bool on) { dateStamp = on; }

void exportSetExtras(int stereo, bool mp4)
{
	stereoFile = stereo;
	mp4On = mp4;
}


void exportQueue(int num)
{
	LightLock_Lock(&lock);
	// The job in progress may already have read the old photo, so a shot
	// that is being made is queued again.
	bool found = false;
	for (int i = working ? 1 : 0; i < jobCount && !found; i++)
		found = jobs[i] == num;
	if (!found && jobCount < MAX_JOBS)
		jobs[jobCount++] = num;
	LightLock_Unlock(&lock);
	LightEvent_Signal(&wake);
}

void exportQueueMissing(const int *nums, int count)
{
	if (thread)
	{
		for (int i = count - 1; i >= 0; i--) // oldest first
			if (!storageHasGif(nums[i]))
				exportQueue(nums[i]);
		return;
	}
	// Before the start: the thread checks them once it runs.
	scanCount = count < MAX_JOBS ? count : MAX_JOBS;
	memcpy(scanNums, nums, scanCount * sizeof(int));
}

int exportPending(void)
{
	LightLock_Lock(&lock);
	int n = jobCount;
	LightLock_Unlock(&lock);
	return n;
}

bool exportIsPending(int num)
{
	LightLock_Lock(&lock);
	bool found = false;
	for (int i = 0; i < jobCount && !found; i++)
		found = jobs[i] == num;
	LightLock_Unlock(&lock);
	return found;
}

u32 exportGeneration(void) { return generation; }
int exportLastError(void) { return lastError; }
