#pragma once
#include <stddef.h>
#include <3ds/types.h>

// sdmc:/Wiggle3DS/GIF/WIG_0001.gif     finished wigglegram
// sdmc:/Wiggle3DS/Photos/WIG_0001.w3d  stereo pair + settings, for the gallery
#define DIR_ROOT "sdmc:/Wiggle3DS"
#define DIR_GIF DIR_ROOT "/GIF"
#define DIR_PHOTO DIR_ROOT "/Photos"
#define DIR_PHOTO_OLD DIR_ROOT "/Fotos" // name used by earlier versions

// Aligned crops, downscaled: a small one for the grid, a screen-sized one
// for the top screen preview (both eyes).
#define THUMB_W 80
#define THUMB_H 60
#define PREVIEW_W 320
#define PREVIEW_H 240

typedef struct
{
	s32 pivotX, pivotY, dx, dy, delayMs;
	s32 reserved; // was the color filter
} ShotInfo;

void storageInit(void);
// Saved shot numbers, newest first.
int storageList(int *nums, int max);
int storageNextNumber(void);
void storageGifPath(int num, char *buf, size_t size);

// Also stores the thumbnail and previews, so the gallery can show and
// wiggle shots without loading the full images.
bool storageSave(int num, const u16 *left, const u16 *right, const ShotInfo *info);
bool storageLoad(int num, u16 *left, u16 *right, ShotInfo *info);
bool storageLoadThumb(int num, u16 *thumb, ShotInfo *info);
bool storageLoadPreview(int num, u16 *previewL, u16 *previewR);
void storageDelete(int num);
