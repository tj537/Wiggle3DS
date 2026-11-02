#pragma once
#include <3ds/types.h>

// Baseline JPEG (JFIF, 4:2:0) from an RGB565 image; quality 1..100.
bool jpegWrite(const char *path, const u16 *img, int stride, int w, int h, int quality);
