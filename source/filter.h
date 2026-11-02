#pragma once
#include <3ds/types.h>

// Color filters: an RGB565 -> RGB565 lookup table each, plus an optional
// vignette (darkened corners) and for some a glitch.
#define FILTER_COUNT 50
#define FILTER_FIRST_GLITCH 22 // the glitches come after the colors

void filterInit(void);
// Builds every table now; the export thread calls it once it is idle.
void filterPrebuild(void);
const char *filterName(int filter);
// NULL for the original image.
const u16 *filterLut(int filter);
// Vignette strength 0..255.
int filterVignette(int filter);
// Glitches change the image itself (shifts, blocks, sorting), not just its
// colors: filterGlitch applies one to a w*h row-major image (after the
// colors, before the vignette).
bool filterHasGlitch(int filter);
// frame: the step of the loop, for glitches that move (0 for a still image).
void filterGlitch(u16 *img, int w, int h, int filter, int frame);
bool filterMoves(int filter);
// Steps of a loop of n wiggle frames with this filter: two loops for one that moves.
int filterLoopFrames(int filter, int n);

// The filter menu: groups of filters, and stepping through them in that order.
int filterGroupCount(void);
const char *filterGroupName(int group);
int filterGroupSize(int group);
int filterGroupItem(int group, int i);
int filterNext(int filter, int step);

// Writes the w*h region at (x, y) of a row-major image to `dst` (w*h,
// row-major) with the filter applied, vignette included.
// strength: 0..100 %, mixed with the original below 100.
void filterApply(u16 *dst, const u16 *img, int stride, int x, int y, int w, int h, int filter, int frame, int strength);
