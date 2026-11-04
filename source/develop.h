#pragma once
#include <3ds/types.h>

// Processing of a freshly captured stereo pair (both CAM_W x CAM_H RGB565).

// Night mode: several pairs averaged into one, which cuts the sensor noise.
// Pixels that moved (hand shake, people walking) keep the first frame, so
// nothing ghosts. stackBegin takes the first pair, which stackFinish
// overwrites with the result; false if there is no memory for it.
bool stackBegin(const u16 *left, const u16 *right);
void stackAdd(const u16 *left, const u16 *right);
void stackFinish(u16 *left, u16 *right);

// The two sensors meter on their own, so one eye often comes out a bit
// brighter or warmer, and the wiggle flickers. Scales each channel of the
// right eye to match the left one.
void matchEyes(const u16 *left, u16 *right);

// Light meter for the live view: mean brightness 0..255 from a sparse grid.
int meterLuma(const u16 *img);

// Auto develop, after matchEyes: one white balance and tone curve for both
// eyes, from the left image. White balance from the near-neutral pixels,
// pulled most of the way to gray so warm light stays a little warm;
// exposure lifts the median toward middle gray, with a soft shoulder that
// keeps lamps and sky from clipping. night: allows more gain.
void autoDevelop(u16 *left, u16 *right, bool night);
