#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "camera.h"
#include "storage.h"
#include "wiggle.h"

#define MAGIC "W3D2"
#define THUMB_PIXELS (THUMB_W * THUMB_H)
#define PREVIEW_PIXELS (PREVIEW_W * PREVIEW_H)
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
	rename(DIR_PHOTO_OLD, DIR_PHOTO); // fails harmlessly if there is nothing to move
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

// Area-averaging downscale of the w*h region at (sx, sy) to dw*dh.
static void downscale(u16 *dst, int dw, int dh, const u16 *img, int sx, int sy, int w, int h)
{
	for (int y = 0; y < dh; y++)
	{
		int y0 = sy + y * h / dh, y1 = sy + (y + 1) * h / dh;
		for (int x = 0; x < dw; x++)
		{
			int x0 = sx + x * w / dw, x1 = sx + (x + 1) * w / dw;
			u32 r = 0, g = 0, b = 0, n = 0;
			for (int yy = y0; yy < y1; yy++)
				for (int xx = x0; xx < x1; xx++)
				{
					u16 p = img[yy * CAM_W + xx];
					r += p >> 11;
					g += (p >> 5) & 63;
					b += p & 31;
					n++;
				}
			dst[y * dw + x] = ((r / n) << 11) | ((g / n) << 5) | (b / n);
		}
	}
}

bool storageSave(int num, const u16 *left, const u16 *right, const ShotInfo *info)
{
	static u16 thumb[THUMB_PIXELS], previewL[PREVIEW_PIXELS], previewR[PREVIEW_PIXELS];
	Crop c = wiggleCrop((Offset){info->dx, info->dy});
	downscale(thumb, THUMB_W, THUMB_H, left, c.lx, c.ly, c.w, c.h);
	downscale(previewL, PREVIEW_W, PREVIEW_H, left, c.lx, c.ly, c.w, c.h);
	downscale(previewR, PREVIEW_W, PREVIEW_H, right, c.rx, c.ry, c.w, c.h);

	char path[64];
	photoPath(num, path, sizeof(path));
	FILE *f = fopen(path, "wb");
	if (!f)
		return false;
	setvbuf(f, NULL, _IOFBF, 64 * 1024);
	Header h = {MAGIC, *info};
	fwrite(&h, sizeof(h), 1, f);
	fwrite(thumb, 2, THUMB_PIXELS, f);
	fwrite(previewL, 2, PREVIEW_PIXELS, f);
	fwrite(previewR, 2, PREVIEW_PIXELS, f);
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

static bool readAt(FILE *f, long offset, u16 *dst, size_t pixels)
{
	return fseek(f, sizeof(Header) + offset * 2, SEEK_SET) == 0 && fread(dst, 2, pixels, f) == pixels;
}

bool storageLoad(int num, u16 *left, u16 *right, ShotInfo *info)
{
	FILE *f = openPhoto(num, info);
	if (!f)
		return false;
	long base = THUMB_PIXELS + 2 * PREVIEW_PIXELS;
	bool ok = readAt(f, base, left, IMAGE_PIXELS) && readAt(f, base + IMAGE_PIXELS, right, IMAGE_PIXELS);
	fclose(f);
	return ok;
}

bool storageLoadThumb(int num, u16 *thumb, ShotInfo *info)
{
	FILE *f = openPhoto(num, info);
	if (!f)
		return false;
	bool ok = readAt(f, 0, thumb, THUMB_PIXELS);
	fclose(f);
	return ok;
}

bool storageLoadPreview(int num, u16 *previewL, u16 *previewR)
{
	FILE *f = openPhoto(num, NULL);
	if (!f)
		return false;
	bool ok = readAt(f, THUMB_PIXELS, previewL, PREVIEW_PIXELS) && readAt(f, THUMB_PIXELS + PREVIEW_PIXELS, previewR, PREVIEW_PIXELS);
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
