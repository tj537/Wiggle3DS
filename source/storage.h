#pragma once
#include <stddef.h>
#include <3ds/types.h>

// sdmc:/Wiggle3DS/GIF/WIG_0001.gif     finished wigglegram (or .jpg: a plain photo)
// sdmc:/Wiggle3DS/Photos/WIG_0001.w3d  stereo pair + settings, for the gallery
#define DIR_ROOT "sdmc:/Wiggle3DS"
#define DIR_GIF DIR_ROOT "/GIF"
#define DIR_PHOTO DIR_ROOT "/Photos"
#define DIR_PHOTO_OLD DIR_ROOT "/Fotos" // name used by earlier versions
#define DIR_CACHE DIR_ROOT "/Cache"      // small JPGs for the share page, made on demand

// Aligned crops, downscaled: a small one for the grid, a screen-sized one
// for the top screen preview (both eyes).
#define THUMB_W 80
#define THUMB_H 60
#define PREVIEW_W 320
#define PREVIEW_H 240

typedef struct
{
	s32 pivotX, pivotY, dx, dy, delayMs;
	s32 style; // filter in bits 0-7, smoothing level in bits 8-9 (3: plain photo), bit 10: photo of version 0.0.1,
	           // bits 11-13: frames of a boomerang of 0.0.1 (only the first is used),
	           // bits 16-22: 100 - filter strength in percent
} ShotInfo;

#define SHOT_FILTER(info) ((info)->style & 0xFF)
#define SHOT_SMOOTH(info) (((info)->style >> 8) & 3)
// Filter strength in bits 16-22, stored as 100 - percent so older shots are 100 %.
#define SHOT_STRENGTH(info) (100 - (((info)->style >> 16) & 127) < 0 ? 0 : 100 - (((info)->style >> 16) & 127))
#define SHOT_STYLE(filter, smooth, strength) ((filter) | (smooth) << 8 | (100 - (strength)) << 16)
#define SHOT_PHOTO_OLD (1 << 10)
#define SHOT_IS_PHOTO(info) (SHOT_SMOOTH(info) == 3 || ((info)->style & SHOT_PHOTO_OLD)) // 3: SMOOTH_OFF

void storageInit(void);
// Saved shot numbers, newest first.
int storageList(int *nums, int max);
int storageNextNumber(void);
void storageGifPath(int num, char *buf, size_t size);
// Plain photos (wiggle off) are JPGs next to the GIFs: GIF/WIG_0001.jpg.
void storagePhotoPath(int num, char *buf, size_t size);
// Files made on top if set: the MP4 video and the 3D files (MPO, side by
// side), next to the GIF: GIF/WIG_0001.mp4, .mpo, _LR.jpg.
typedef enum { EXTRA_MP4, EXTRA_MPO, EXTRA_SBS, EXTRA_COUNT } Extra;
void storageExtraPath(int num, int extra, char *buf, size_t size);
// The share page's small picture of a shot: Cache/WIG_0001.jpg.
void storageCachePath(int num, char *buf, size_t size);


// Also stores the thumbnail and previews, so the gallery can show and
// wiggle shots without loading the full images.
bool storageSave(int num, const u16 *left, const u16 *right, const ShotInfo *info);
bool storageLoad(int num, u16 *left, u16 *right, ShotInfo *info);
bool storageLoadThumb(int num, u16 *thumb, ShotInfo *info);
bool storageLoadPreview(int num, u16 *previewL, u16 *previewR);
bool storageExists(int num);
bool storageHasGif(int num); // or its JPG, for a photo
void storageDelete(int num);
// Deleting goes through a trash, so it can be undone for a few seconds:
// trashed shots are renamed to *.del and left out of storageList.
void storageTrash(int num);
bool storageTrashed(int num);
void storageRestore(int num);
void storagePurge(int num); // gone for good
void storagePurgeAll(void); // at start: deletions left from last time
u64 storageFreeBytes(void);
u64 storageShotTime(int num); // last saved, local time in seconds since 1970; 0 if unknown
u64 storageFileTime(const char *path); // the same for any file
