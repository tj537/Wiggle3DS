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
