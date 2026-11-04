#pragma once
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "draw.h"
#include "export.h"
#include "filter.h"
#include "icons.h"

// Shared by the parts of the interface: ui.c (themes, drawing helpers, top
// screen, toasts), ui_home.c (home and the filter menu), ui_gallery.c
// (gallery and review), ui_settings.c (settings and share).

extern u16 uiPalette[9];
#define C_BG_TOP uiPalette[0]
#define C_BG_BOTTOM uiPalette[1]
#define C_SURFACE uiPalette[2]
#define C_SURFACE2 uiPalette[3]
#define C_STROKE uiPalette[4]
#define C_TEXT uiPalette[5]
#define C_DIM uiPalette[6]
#define C_ACC0 uiPalette[7]
#define C_ACC1 uiPalette[8]
#define C_RED COL565(255, 76, 92)
#define C_STAR COL565(255, 200, 70)
#define C_BLACK 0
#define C_WHITE 0xFFFF

#define FILTER_PILL_X (CARD_X + 8)
#define FILTER_PILL_Y (CARD_Y + CARD_H - 28)

#define FADE_MS 160
#define FLASH_MS 220
#define TOAST_MS 2600
#define UNDO_MS 5000 // a toast with an Undo button
#define SLIDE_PX 14

#define LIVE_GLITCH_MS 140 // a moving glitch on the viewfinder changes this often
#define HEADER_CY (HEADER_BTN_Y + HEADER_BTN_H / 2)

int lineH(const Font *f);
u64 since(const App *a, u64 t);
// The bottom screen's background, all of it or some rows.
void background(u16 *fb);
void backgroundRows(u16 *fb, int y0, int y1);
// Dark pills with text over a picture: centered on cx, or from x leftward / rightward.
void overlayPill(u16 *fb, int sw, int cx, int y, const char *text);
void overlayPillLeft(u16 *fb, int sw, int x, int y, const char *text);
void overlayPillRight(u16 *fb, int sw, int x, int y, const char *text);
void button(u16 *fb, int x, int y, int w, const char *btn, const char *label, bool primary, u16 fill);
void centeredHints(u16 *fb, int y, int n, const char *const *btns, const char *const *labels);
void dialog(u16 *fb, int x, int y, int w, int h);
// A region of an image, scaled to dw*dh at (dx, dy), with a filter.
void drawFiltered(u16 *fb, int sw, int dx, int dy, int dw, int dh, const u16 *img, int stride, int sx, int sy, int w, int h,
                  int filter, int radius, int frame, int strength);
bool photoMode(const App *a);
const char *filterLabel(int filter, int strength);
const char *smoothLabel(int smooth);

// The screens' bottom halves, and the share screen's top.
void homeBottom(App *a, u16 *fb);
void galleryBottom(App *a, u16 *fb);
void reviewBottom(App *a, u16 *fb);
void settingsBottom(App *a, u16 *fb);
void shareBottom(App *a, u16 *fb);
void shareTop(App *a, u16 *fb);
// The defaults for new shots as tiles, and the touch spots of them.
void defaultsPanel(App *a, u16 *fb);
// A shot's thumbnail, with its badge while its GIF is made.
void thumb(App *a, u16 *fb, int idx, int x, int y, int w, int h);
