#pragma once
#include <3ds/types.h>
#include "camera.h"
#include "storage.h"
#include "wiggle.h"

// App state shared by the input handling (main.c) and drawing (ui.c).

typedef enum
{
	MODE_HOME,    // top: live view, bottom: capture defaults and recent shots
	MODE_GALLERY, // top: the selected shot, bottom: all shots, page by page
	MODE_REVIEW,  // edit pivot and speed, save
	MODE_SHARE,  // web server for the phone, QR code on top
	MODE_SETTINGS,
} Mode;

#define APP_VERSION "v0.0.1"
#define THEME_COUNT 6

// Saved to sdmc:/Wiggle3DS/settings.txt.
typedef struct
{
	int theme;
	int filter, smooth, delayMs; // defaults for new photos; smooth SMOOTH_OFF: plain photos (JPG)
	int strength;                // filter strength for new photos, 0..100 %
	u32 favorites[3];            // filters marked as favorites, one bit each
	bool quick;                  // quick capture: save every shot right away
	int timer;                   // self-timer in seconds, 0 = off
	bool grid;                   // rule-of-thirds lines on the viewfinder
	bool autoplay;               // gallery: play the selected shot right away
	bool gifHalf;                // export at half size, for messengers
	int fps;             // viewfinder: 15, 20 or 30
	bool flash;          // white flash on capture
	int photoEye;         // transition off: the camera the photo is taken from, 0 left, 1 right
	bool night;           // night mode: longer exposures, several frames stacked
	bool sound;           // shutter sound
	bool gifSquare;       // export a 1:1 crop
	bool tipDone;         // the pivot tip was shown and used once
} Settings;

typedef enum
{
	SET_THEME, SET_TIMER, SET_GRID, SET_SOUND, SET_FLASH, SET_FPS, SET_PHOTO_EYE,
	SET_AUTOPLAY, SET_GIF_SIZE, SET_GIF_SHAPE, SET_RESET, SET_COUNT
} SettingRow;

#define MAX_SHOTS 1000
#define LOW_FREE (20ULL << 20) // SD card warnings
#define MIN_FREE (3ULL << 20)  // a photo is about 1.9 MB, plus its GIF
#define DELAY_MIN 40
#define DELAY_MAX 400
#define DELAY_STEP 20
#define STRENGTH_STEP 10

// ---- bottom screen layout, used for drawing and touch ----

// Header (home and gallery): title left, buttons right
#define HEADER_BTN_Y 8
#define HEADER_BTN_H 26
#define HEADER_PILL_W 40
#define HEADER_PILL_GAP 6
typedef enum { HB_DOUBLE, HB_SELFIE, HB_NIGHT, HB_SHARE, HB_SETTINGS, HB_COUNT } HeaderButton;

// Home: a card of capture defaults (2x2 tiles), then the newest shots
#define TILE_X 18
#define TILE_Y 46
#define TILE_W 139
#define TILE_H 34
#define TILE_GAP 6
#define PANEL_X 12
#define PANEL_Y 40
#define PANEL_W 296
#define PANEL_H (2 * TILE_H + 3 * TILE_GAP)
typedef enum { TILE_FILTER, TILE_SMOOTH, TILE_SPEED, TILE_QUICK, TILE_COUNT } Tile;
#define RECENT_COUNT 4
#define RECENT_LABEL_Y 142
#define RECENT_Y 165
#define RECENT_W 68
#define RECENT_H 51
#define RECENT_GAP 8
#define ALL_BTN_W 84
#define ALL_BTN_H 24

// Gallery: three rows that scroll sideways, filled column by column
#define GRID_ROWS 3
#define GRID_X 11
#define GRID_Y 42
#define CELL_W 70
#define CELL_H 52
#define CELL_GAP 6
#define COL_W (CELL_W + CELL_GAP)
#define VISIBLE_COLS 4
#define THUMB_CACHE 24 // loaded thumbnails, slot = index % THUMB_CACHE
#define BACK_BTN_X 12
#define BACK_BTN_W 64

// Settings rows
#define SET_Y 36
#define SET_ROW 25
#define SET_ROW_H 22
#define SET_VISIBLE 7 // rows on screen; the list scrolls

// Filter menu: a grid of buttons
#define MENU_COLS 3
#define MENU_X 12
#define MENU_Y 44
#define MENU_BTN_W 90
#define MENU_BTN_H 34
#define MENU_GAP 8
#define MENU_ROW (MENU_BTN_H + MENU_GAP)
#define MENU_HEAD 20      // a group's heading
#define MENU_GROUP_GAP 4
#define MENU_VIEW_H 168   // visible height; the menu scrolls

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
	Mode mode, shareFrom;
	u64 now, modeSince, flashAt;
	u64 timerAt; // self-timer: when the shot is taken, 0 if not running

	// Home: the live view; the filter menu previews its selection on it.
	bool filterMenu;
	int homeFocus; // D-pad on the recent shots: 0..RECENT_COUNT-1, RECENT_COUNT for "All", -1 if not
	int menuSel, menuScroll; // menuScroll: pixels
	bool camOk, selfie; // selfie: the inner camera, plain photos
	bool camStarting;   // at start, before the camera is up
	// Double exposure: on, and the first exposure while it waits for the second.
	bool doubleExp, haveFirst;
	const u16 *firstL, *firstR;
	const u16 *liveL, *liveR;

	// Gallery: thumbnails around the visible columns (the first ones also
	// feed the home screen's recent shots), preview of the selection.
	int shots[MAX_SHOTS], shotCount, sel;
	bool playing;
	u16 grid[THUMB_CACHE][THUMB_W * THUMB_H];
	ShotInfo gridInfo[THUMB_CACHE];
	int gridIdx[THUMB_CACHE]; // shot index in each slot, -1 if empty
	bool gridOk[THUMB_CACHE];
	float scrollX, scrollVel; // pixels; velocity keeps a flick going
	int scrollTo;             // scroll animation target, -1 if none
	u16 previewL[PREVIEW_W * PREVIEW_H], previewR[PREVIEW_W * PREVIEW_H];
	int previewNum; // shot the preview belongs to, -1 if none
	bool confirmDelete;
	// Picking: a long press starts it, taps then pick shots to delete.
	bool picking, picked[MAX_SHOTS];
	int pickedCount;
	// Deleted shots, in the trash until the undo toast is gone.
	int trashed[MAX_SHOTS], trashedCount;
	char selDate[16]; // "Today", "Yesterday" or "3 Nov" for the selected shot
	int exporting; // GIFs still being made

	// Review: shotNum is -1 until the shot has been saved once; savedInfo is
	// what is on the card, to tell whether there are changes.
	u16 *shotL, *shotR;
	int shotNum, pivotX, pivotY, delayMs, shotFilter, shotSmooth, shotStrength;
	Offset offset;
	ShotInfo savedInfo;
	bool saving, fromGallery, aligning;
	int stacking, stackFrames; // night mode: frames taken so far (0 if not), of how many
	bool compare; // Y held: the shot without its filter

	// Share: page address and its QR code (qrcodegen format, up to version 10).
	bool shareOk;
	char shareUrl[48];
	u8 qr[(57 * 57 + 7) / 8 + 1];
	int sharedCount;

	Settings settings;
	int settingsSel, settingsTop; // selected row, first visible row
	u64 freeBytes;                // on the SD card, refreshed now and then
	bool lowBattery, charging;
	int battery; // percent, -1 if unknown

	// Message on the top screen.
	char toast[64];
	bool toastError, toastUndo; // toastUndo: with an Undo button, shown longer
	u64 toastAt;
} App;

void uiSetTheme(int theme);
void uiSetFavorites(const u32 *bits); // the filter menu shows them first
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
bool uiToastVisible(const App *a);
bool uiReviewChanged(const App *a); // unsaved changes in review
int uiHeaderAt(int x, int y); // home: the HeaderButton at a point, -1 if none
// Home: the tile at a point, and for the speed tile which half (-1 / +1).
int uiTileAt(int x, int y, int *step);
int uiRecentAt(int x, int y);
bool uiAllButtonHit(int x, int y);
// Gallery
bool uiGalleryBackHit(int x, int y);
int uiCellAt(const App *a, int x, int y); // shot index at a point, -1 if none
int uiMaxScroll(const App *a);
// Review: transition off makes the shot a plain photo (JPG).
static inline bool uiShotIsPhoto(const App *a) { return a->shotSmooth == SMOOTH_OFF; }
// The slot holding a shot's thumbnail, or -1 when it isn't loaded.
static inline int uiThumbSlot(const App *a, int idx)
{
	int slot = idx % THUMB_CACHE;
	return idx >= 0 && a->gridIdx[slot] == idx ? slot : -1;
}
bool uiFilterPillHit(int x, int y); // review: the filter pill on the photo
bool uiSmoothPillHit(int x, int y); // review: the smoothing pill on the photo
int uiMenuTileAt(const App *a, int x, int y); // the filter at a point, -1 if none
int uiMenuMove(int filter, int dx, int dy);    // the filter beside / above / below
int uiMenuMaxScroll(void);
void uiMenuReveal(App *a);                     // scrolls the selection into view
// Settings: the row at a point, and whether the point is on its ◀ (-1) or ▶ (+1).
int uiSettingsRowAt(const App *a, int x, int y, int *step);
