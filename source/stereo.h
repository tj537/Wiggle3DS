#pragma once
#include <3ds/types.h>

// 3D files of a stereo pair (both w*h crops at the given offsets of images
// with `stride`), for 3D TVs, VR viewers and the 3DS itself.
// MPO: two JPEGs in one file with an MP index (CIPA DC-007), like the 3DS
// camera saves them.
bool mpoWrite(const char *path, const u16 *left, int lx, int ly, const u16 *right, int rx, int ry, int stride, int w, int h);
// Side by side: one JPEG, left eye on the left (parallel view).
bool sbsWrite(const char *path, const u16 *left, int lx, int ly, const u16 *right, int rx, int ry, int stride, int w, int h);
