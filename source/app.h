#pragma once
#include <3ds/types.h>
#include "camera.h"
#include "storage.h"
#include "wiggle.h"

// App state shared by the input handling (main.c) and drawing (ui.c).

typedef enum
{
	MODE_HOME,   // top: live view or the selected shot, bottom: gallery
	MODE_REVIEW, // edit pivot and speed, save
	MODE_SHARE,  // web server for the phone, QR code on top
	MODE_SETTINGS,
} Mode;

#define THEME_COUNT 6

// Saved to sdmc:/Wiggle3DS/settings.txt.
typedef struct
{
	int theme;
	int smooth, delayMs; // defaults for new photos
	bool gifHalf;        // export at half size, for messengers
	int fps;             // viewfinder: 15, 20 or 30
	bool flash;          // white flash on capture
} Settings;

typedef enum { SET_THEME, SET_SMOOTH, SET_SPEED, SET_GIF_SIZE, SET_FPS, SET_FLASH, SET_COUNT } SettingRow;

#define MAX_SHOTS 1000
#define DELAY_MIN 40
#define DELAY_MAX 400
#define DELAY_STEP 20

// ---- bottom screen layout, used for drawing and touch ----

// Home: filter and share buttons in the header, gallery grid
#define HEADER_BTN_Y 9
#define HEADER_BTN_H 22
#define GRID_COLS 4
#define GRID_ROWS 3
#define GRID_PAGE (GRID_COLS * GRID_ROWS)
#define GRID_X 11
#define GRID_Y 40
#define CELL_W 70
#define CELL_H 52
#define CELL_GAP 6

#define FILTER_BTN_X 12
#define GEAR_R 11

// Settings rows
#define SET_Y 40
#define SET_ROW 28
#define SET_ROW_H 24

// Filter menu: a grid of buttons
#define MENU_COLS 3
#define MENU_X 12
#define MENU_Y 44
#define MENU_BTN_W 92
#define MENU_BTN_H 34
#define MENU_GAP 8
#define MENU_ROW (MENU_BTN_H + MENU_GAP)

// Review
#define CARD_X 48
#define CARD_Y 8
#define CARD_W 224
#define CARD_H 168
#define SLIDER_X0 86
#define SLIDER_X1 232
#define SLIDER_Y 192
#define BTN_Y 210
#define BTN_H 24
#define BACK_X 12
#define BACK_W 100
#define SAVE_X 120
#define SAVE_W 188

typedef struct
{
	Mode mode;
	u64 now, modeSince, flashAt;

	// Home: `browsing` shows the selected shot on top instead of the camera.
	bool browsing, playing;
	int filter; // camera filter, also used for new shots
	bool filterMenu;
	int menuSel; // previewed in the live view while the menu is open
	bool camOk;
	const u16 *liveL, *liveR;

	// Gallery grid for the visible page, preview of the selection.
	int shots[MAX_SHOTS], shotCount, sel, gridPage;
	u16 grid[GRID_PAGE][THUMB_W * THUMB_H];
	ShotInfo gridInfo[GRID_PAGE];
	bool gridOk[GRID_PAGE];
	u16 previewL[PREVIEW_W * PREVIEW_H], previewR[PREVIEW_W * PREVIEW_H];
	int previewNum; // shot the preview belongs to, -1 if none
	bool confirmDelete;

	// Review: shotNum is -1 until the shot has been saved once.
	u16 *shotL, *shotR;
	int shotNum, pivotX, pivotY, delayMs, shotFilter, shotSmooth;
	Offset offset;
	bool saving, fromGallery;

	// Share: page address and its QR code (qrcodegen format, up to version 10).
	bool shareOk;
	char shareUrl[48];
	u8 qr[(57 * 57 + 7) / 8 + 1];
	int sharedCount;

	Settings settings;
	int settingsSel;

	// Toast message at the top of the bottom screen.
	char toast[64];
	bool toastError;
	u64 toastAt;
} App;

void uiSetTheme(int theme);
const char *uiThemeName(int theme);
u32 uiThemeRgb(int theme, int which); // 0xRRGGBB; which: 0 bg, 2 surface, 6 dim, 7/8 accents
const char *uiSettingValue(const Settings *s, int row);

void uiDrawTop(App *a, u16 *left, u16 *right, bool stereo);
void uiDrawBottom(App *a, u16 *fb);
// Whether a screen is mid-animation (fade, flash, toast) and needs redrawing.
bool uiTopAnimating(const App *a);
bool uiBottomAnimating(const App *a);
// Whether the top screen shows a wiggle that changes frame every delay.
bool uiTopWiggles(const App *a, int *delayMs);

typedef enum { HIT_NONE, HIT_BACK, HIT_SAVE, HIT_CANCEL, HIT_DELETE } Hit;
Hit uiReviewHit(int x, int y);
Hit uiDeleteDialogHit(int x, int y);
int uiCellAt(int x, int y);
bool uiShareButtonHit(int x, int y);
bool uiFilterButtonHit(const App *a, int x, int y);
bool uiFilterPillHit(int x, int y); // review: the filter pill on the photo
bool uiSmoothPillHit(int x, int y); // review: the smoothing pill on the photo
int uiMenuTileAt(int x, int y);
bool uiGearHit(int x, int y);
// Settings: the row at a point, and whether the point is on its ◀ (-1) or ▶ (+1).
int uiSettingsRowAt(int x, int y, int *step);
