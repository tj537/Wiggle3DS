#pragma once
#include <3ds/types.h>

// Where the left-eye pixel (x, y) shows up in the right-eye image:
// left(x, y) ~ right(x + dx, y + dy).
typedef struct
{
	int dx, dy;
} Offset;

// Crop of both eye images so that the pivot point stays put.
typedef struct
{
	int lx, ly, rx, ry; // top-left corner in the left / right image
	int w, h;
} Crop;

void wiggleSetImages(const u16 *left, const u16 *right);
// Block-matches the patch around (px, py) of the left image in the right one.
Offset wiggleMatch(int px, int py);
Crop wiggleCrop(Offset o);
// For the viewfinder: the offset of the middle of two camera frames, from
// small copies, fast enough to run a few times a second. *near: how much
// nearer the middle is than the background (the edges), in pixels of
// offset; -1 when either has too little texture to tell. Measured against
// the background, so it doesn't matter how the two cameras sit.
Offset wiggleLiveOffset(const u16 *left, const u16 *right, int *near);

// Smoothing levels: 0 cuts hard between the eyes, 1 adds a 50/50 blend
// between them (L, mix, R, mix), 2 adds two blends (thirds).
#define SMOOTH_LEVELS 3
// Transition step after the smoothing levels: no wiggle, a plain photo.
#define SMOOTH_OFF SMOOTH_LEVELS
#define MAX_WIGGLE_FRAMES 6
// Fills `mix` with each loop frame's share of the right eye (0..255);
// returns the frame count.
int wiggleFrames(int smooth, int *mix);
const char *wiggleSmoothName(int smooth);
