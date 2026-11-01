#pragma once
#include <3ds/types.h>
#include "camera.h"
#include "storage.h"
#include "wiggle.h"

// App state shared by the input handling (main.c) and drawing (ui.c).

typedef enum
{
	MODE_CAMERA,
	MODE_REVIEW,
	MODE_GALLERY,
} Mode;

#define MAX_SHOTS 1000
#define DELAY_MIN 40
#define DELAY_MAX 400
#define DELAY_STEP 20

// ---- bottom screen layout, used for drawing and touch ----

// Camera
#define SHUTTER_X 160
#define SHUTTER_Y 100
#define SHUTTER_R 34
#define GALLERY_BTN_X 30 // 52x52 rounded square
#define GALLERY_BTN_Y 74
#define GALLERY_BTN_S 52
#define FILTER_BTN_X 264 // circle
#define FILTER_BTN_Y 100
#define FILTER_BTN_R 26
#define CHIPS_Y 168
#define CHIP_H 26
#define CHIP_GAP 6

// Review
#define CARD_X 48
#define CARD_Y 8
#define CARD_W 224
#define CARD_H 168
#define SLIDER_X0 72
#define SLIDER_X1 232
#define SLIDER_Y 192
#define BTN_Y 210
#define BTN_H 24

// Gallery
#define GRID_COLS 4
#define GRID_ROWS 3
#define GRID_PAGE (GRID_COLS * GRID_ROWS)
#define GRID_X 11
#define GRID_Y 40
#define CELL_W 70
#define CELL_H 52
#define CELL_GAP 6

typedef struct
{
	Mode mode, reviewReturn;
	u64 now, modeSince;
	int filter; // current filter in the camera and the shot being reviewed

	// Camera
	bool camOk, shutterHeld;
	const u16 *liveL, *liveR;
	float chipScroll; // animated carousel offset
	u64 filterChangedAt;

	// Review: shotNum is -1 until the shot has been saved once.
	u16 *shotL, *shotR;
	int shotNum, pivotX, pivotY, delayMs;
	Offset offset;
	bool saving;

	// Gallery: grid thumbnails for the visible page, preview of the selection.
	int shots[MAX_SHOTS], shotCount, sel, gridPage;
	u16 grid[GRID_PAGE][THUMB_W * THUMB_H];
	ShotInfo gridInfo[GRID_PAGE];
	bool gridOk[GRID_PAGE];
	u16 previewL[PREVIEW_W * PREVIEW_H], previewR[PREVIEW_W * PREVIEW_H];
	int previewNum; // shot the preview belongs to, -1 if none
	bool confirmDelete;

	// Newest shot, shown on the camera's gallery button.
	u16 lastThumb[THUMB_W * THUMB_H];
	bool lastThumbOk;

	// Toast message at the top of the bottom screen.
	char toast[64];
	bool toastError;
	u64 toastAt;
} App;

void uiDraw(App *a, u16 *topL, u16 *topR, u16 *bottom, bool stereo);
// Touch targets. uiChipAt returns a filter index or -1.
int uiChipAt(const App *a, int x, int y);
typedef enum { HIT_NONE, HIT_BACK, HIT_FILTER_PREV, HIT_FILTER_NEXT, HIT_SAVE, HIT_CANCEL, HIT_DELETE } Hit;
Hit uiReviewHit(int x, int y);
Hit uiDeleteDialogHit(int x, int y);
