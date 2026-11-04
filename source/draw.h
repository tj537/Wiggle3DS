#pragma once
#include <3ds/types.h>
#include "font.h"

// Software drawing into RGB565 framebuffers. Screens are 240 px high and
// `sw` px wide (400 top, 320 bottom); x/y are in normal screen orientation.
// Alpha values are 0..255. Shapes are anti-aliased.

#define COL565(r, g, b) (u16)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))

void drawClear(u16 *fb, int sw, u16 color);
void drawFill(u16 *fb, int sw, int x, int y, int w, int h, u16 color, int alpha);
void drawGradientV(u16 *fb, int sw, int x, int y, int w, int h, u16 top, u16 bottom);
void drawRoundRect(u16 *fb, int sw, int x, int y, int w, int h, int r, u16 color, int alpha);
// Horizontal gradient from c0 (left) to c1 (right).
void drawRoundRectGrad(u16 *fb, int sw, int x, int y, int w, int h, int r, u16 c0, u16 c1, int alpha);
void drawRoundRectOutline(u16 *fb, int sw, int x, int y, int w, int h, int r, int thick, u16 color, int alpha);
void drawShadow(u16 *fb, int sw, int x, int y, int w, int h, int r, int alpha);
void drawCircle(u16 *fb, int sw, float cx, float cy, float r, u16 color, int alpha);
void drawCircleGrad(u16 *fb, int sw, float cx, float cy, float r, u16 c0, u16 c1, int alpha);
// A five-pointed star.
void drawStar(u16 *fb, int sw, float cx, float cy, float r, u16 color, int alpha);
// A disc with a second disc (bx, by, br) cut out: nothing is drawn there.
void drawCrescent(u16 *fb, int sw, float cx, float cy, float r, float bx, float by, float br, u16 color, int alpha);
void drawRing(u16 *fb, int sw, float cx, float cy, float r, float thick, u16 color, int alpha);

// Scales the w*h region at (sx, sy) of a row-major image into dw*dh at
// (dx, dy) with 2x2 supersampling. `lut` (65536 entries) recolors pixels,
// `radius` rounds the corners (dw/2 for a circle). Either may be 0.
void drawImage(u16 *fb, int sw, int dx, int dy, int dw, int dh,
               const u16 *img, int stride, int sx, int sy, int w, int h, const u16 *lut, int radius);

// drawImage blended over what is already there (alpha 0..255, no rounding).
void drawImageAlpha(u16 *fb, int sw, int dx, int dy, int dw, int dh,
                    const u16 *img, int stride, int sx, int sy, int w, int h, const u16 *lut, int alpha);
// Halves a w*h row-major image into (w/2)*(h/2) by averaging 2x2 blocks.
void halveRows(u16 *dst, const u16 *src, int w, int h);
// dst = a mixed with b by t (0..255), for row-major images.
void blendRows(u16 *dst, const u16 *a, const u16 *b, int pixels, int t);

// Darkens the corners of a w*h region: on screen at (x, y) (clipped to
// rounded corners of `radius`), or of a row-major image. Strength 0..255.
void drawVignette(u16 *fb, int sw, int x, int y, int w, int h, int strength, int radius);
void vignetteRows(u16 *img, int w, int h, int strength);

// Text: (x, y) is the top-left of the line; returns the advance width.
int textWidth(const Font *font, const char *text);
int drawText(u16 *fb, int sw, const Font *font, int x, int y, u16 color, int alpha, const char *text);
// Text into a w*h row-major image (not a screen), each font pixel `scale`
// times as big; icons are left out.
void drawTextImage(u16 *img, int w, int h, const Font *font, int x, int y, int scale, u16 color, int alpha, const char *text);
int drawTextGrad(u16 *fb, int sw, const Font *font, int x, int y, u16 c0, u16 c1, const char *text);
void drawTextCentered(u16 *fb, int sw, const Font *font, int cx, int y, u16 color, int alpha, const char *text);
// printf into one of a few rotating static buffers.
const char *tfmt(const char *fmt, ...);

// A round button glyph like (A) followed by a label; returns the total width.
int hintWidth(const char *button, const char *label);
int drawHint(u16 *fb, int sw, int x, int y, const char *button, const char *label, u16 color, int alpha);
