#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "camera.h"
#include "storage.h"
#include "wiggle.h"

#define MAGIC "W3D1"
#define THUMB_PIXELS (THUMB_W * THUMB_H)
#define IMAGE_PIXELS (CAM_W * CAM_H)

typedef struct
{
	char magic[4];
	ShotInfo info;
} Header;

static void photoPath(int num, char *buf, size_t size)
{
	snprintf(buf, size, DIR_PHOTO "/WIG_%04d.w3d", num);
}

void storageGifPath(int num, char *buf, size_t size)
{
	snprintf(buf, size, DIR_GIF "/WIG_%04d.gif", num);
}

void storageInit(void)
{
	mkdir(DIR_ROOT, 0777);
	mkdir(DIR_GIF, 0777);
	mkdir(DIR_PHOTO, 0777);
}

static int cmpDesc(const void *a, const void *b) { return *(const int *)b - *(const int *)a; }

int storageList(int *nums, int max)
{
	DIR *dir = opendir(DIR_PHOTO);
	if (!dir)
		return 0;
	int count = 0;
	struct dirent *e;
	while ((e = readdir(dir)) && count < max)
	{
		int num;
		char ext[8];
		if (sscanf(e->d_name, "WIG_%d.%7s", &num, ext) == 2 && !strcmp(ext, "w3d"))
			nums[count++] = num;
	}
	closedir(dir);
	qsort(nums, count, sizeof(int), cmpDesc);
	return count;
}

int storageNextNumber(void)
{
	static int nums[1000];
	int count = storageList(nums, 1000);
	return count ? nums[0] + 1 : 1;
}

static void makeThumb(u16 *dst, const u16 *img, int sx, int sy, int w, int h)
{
	for (int y = 0; y < THUMB_H; y++)
		for (int x = 0; x < THUMB_W; x++)
			dst[y * THUMB_W + x] = img[(sy + y * h / THUMB_H) * CAM_W + sx + x * w / THUMB_W];
}

bool storageSave(int num, const u16 *left, const u16 *right, const ShotInfo *info)
{
	static u16 thumbL[THUMB_PIXELS], thumbR[THUMB_PIXELS];
	Crop c = wiggleCrop((Offset){info->dx, info->dy});
	makeThumb(thumbL, left, c.lx, c.ly, c.w, c.h);
	makeThumb(thumbR, right, c.rx, c.ry, c.w, c.h);

	char path[64];
	photoPath(num, path, sizeof(path));
	FILE *f = fopen(path, "wb");
	if (!f)
		return false;
	setvbuf(f, NULL, _IOFBF, 64 * 1024);
	Header h = {MAGIC, *info};
	fwrite(&h, sizeof(h), 1, f);
	fwrite(thumbL, 2, THUMB_PIXELS, f);
	fwrite(thumbR, 2, THUMB_PIXELS, f);
	fwrite(left, 2, IMAGE_PIXELS, f);
	fwrite(right, 2, IMAGE_PIXELS, f);
	bool ok = !ferror(f);
	fclose(f);
	return ok;
}

static FILE *openPhoto(int num, ShotInfo *info)
{
	char path[64];
	photoPath(num, path, sizeof(path));
	FILE *f = fopen(path, "rb");
	Header h;
	if (f && (fread(&h, sizeof(h), 1, f) != 1 || memcmp(h.magic, MAGIC, 4)))
	{
		fclose(f);
		return NULL;
	}
	if (f && info)
		*info = h.info;
	return f;
}

bool storageLoad(int num, u16 *left, u16 *right, ShotInfo *info)
{
	FILE *f = openPhoto(num, info);
	if (!f)
		return false;
	fseek(f, THUMB_PIXELS * 2 * 2, SEEK_CUR);
	bool ok = fread(left, 2, IMAGE_PIXELS, f) == IMAGE_PIXELS && fread(right, 2, IMAGE_PIXELS, f) == IMAGE_PIXELS;
	fclose(f);
	return ok;
}

bool storageLoadThumbs(int num, u16 *thumbL, u16 *thumbR)
{
	FILE *f = openPhoto(num, NULL);
	if (!f)
		return false;
	bool ok = fread(thumbL, 2, THUMB_PIXELS, f) == THUMB_PIXELS && fread(thumbR, 2, THUMB_PIXELS, f) == THUMB_PIXELS;
	fclose(f);
	return ok;
}

void storageDelete(int num)
{
	char path[64];
	photoPath(num, path, sizeof(path));
	unlink(path);
	storageGifPath(num, path, sizeof(path));
	unlink(path);
}
