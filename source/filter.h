#pragma once
#include <3ds/types.h>

// Color filters: an RGB565 -> RGB565 lookup table each, plus an optional
// vignette (darkened corners).
#define FILTER_COUNT 11

void filterInit(void);
const char *filterName(int filter);
// NULL for the original image.
const u16 *filterLut(int filter);
// Vignette strength 0..255.
int filterVignette(int filter);

// Writes the w*h region at (x, y) of a row-major image to `dst` (w*h,
// row-major) with the filter applied, vignette included.
void filterApply(u16 *dst, const u16 *img, int stride, int x, int y, int w, int h, int filter);
