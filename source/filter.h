#pragma once
#include <3ds/types.h>

// Color filters as RGB565 -> RGB565 lookup tables (65536 entries).
#define FILTER_COUNT 7

void filterInit(void);
const char *filterName(int filter);
// NULL for the original image.
const u16 *filterLut(int filter);
