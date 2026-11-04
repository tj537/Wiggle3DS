#pragma once
#include <3ds/types.h>

// Color filters: an RGB565 -> RGB565 lookup table each, plus an optional
// vignette (darkened corners) and for some a glitch.
// Filter ids are stored in saved shots and settings: only ever add new ones
// at the end, never reorder or remove.
typedef enum
{
	F_ORIGINAL = 0, F_VIVID, F_WARM, F_COOL, F_DREAM, F_CINEMA, F_VINTAGE, F_LOMO, F_SEPIA, F_MONO, F_NOIR,
	F_CHROME, F_BLEACH, F_CROSS, F_PASTEL, F_DUOTONE, F_INFRARED, F_THERMAL, F_ACID, F_SOLAR, F_GAME_BOY, F_BENT,
	F_RGB_SPLIT, F_VHS, F_DATAMOSH, F_PIXEL_SORT, F_TEAR, F_MOSAIC, F_CRT, F_BIT_ROT, F_ECHO, F_NIGHT_VISION,
	F_MELT, F_INTERLACE, F_CIRCUIT, F_PRISM, F_GHOST, F_TRAILS, F_BLEED, F_WOBBLE, F_SIGNAL_LOSS, F_HOLOGRAM,
	F_BROADCAST, F_VHS_WORN, F_ANAGLYPH, F_SMEAR, F_SWIRL, F_COMPRESS, F_DRIFT, F_KEYFRAME, F_GRAY_CODE,
	F_OVERFLOW, F_REVERSE, F_CROSSTALK, F_STUCK_BITS, F_SYNC_LOSS, F_CLOCK, F_XOR, F_CYANOTYPE, F_MATTE,
	F_GOLDEN, F_POP, F_CGA, F_HALFTONE, F_RISO, F_ONE_BIT, F_EDGES, F_FEEDBACK, F_TOXIC, F_HELLFIRE, F_X_RAY,
	F_NEGATIVE, F_VAPORWAVE, F_DEEP_FRIED, F_KALEIDO, F_MIRROR, F_SLIT_SCAN, F_SHATTER, F_DISCO, F_DROSTE,
	F_SCRAMBLE, F_BUBBLEGUM, F_BLOOD_MOON, F_SYNTHWAVE, F_CURSED, F_PSYCHEDELIC, F_ECTOPLASM, F_EIGHT_COLORS, F_NEON,
	F_PIXEL_RAIN, F_LED_WALL, F_DOUBLE_VISION, F_ZOOM_BLUR, F_LIQUID, F_RIPPLE, F_FISHEYE,
	FILTER_COUNT

} FilterId;

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
