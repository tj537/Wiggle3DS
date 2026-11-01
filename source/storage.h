#pragma once
#include <stddef.h>
#include <3ds/types.h>

// sdmc:/Wiggle3DS/GIF/WIG_0001.gif    finished wigglegram
// sdmc:/Wiggle3DS/Fotos/WIG_0001.w3d  stereo pair + settings, for the gallery
#define DIR_ROOT "sdmc:/Wiggle3DS"
#define DIR_GIF DIR_ROOT "/GIF"
#define DIR_PHOTO DIR_ROOT "/Fotos"

#define THUMB_W 160
#define THUMB_H 120

typedef struct
{
	s32 pivotX, pivotY, dx, dy, delayMs;
} ShotInfo;

void storageInit(void);
// Saved shot numbers, newest first.
int storageList(int *nums, int max);
int storageNextNumber(void);
void storageGifPath(int num, char *buf, size_t size);

// Also stores two aligned thumbnails, so the gallery can wiggle without
// loading the full images.
bool storageSave(int num, const u16 *left, const u16 *right, const ShotInfo *info);
bool storageLoad(int num, u16 *left, u16 *right, ShotInfo *info);
bool storageLoadThumbs(int num, u16 *thumbL, u16 *thumbR);
void storageDelete(int num);
