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
