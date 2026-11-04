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

#define APP_VERSION "v0.2.0"
#define THEME_COUNT 6

// Saved to sdmc:/Wiggle3DS/settings.txt.
typedef struct
{
	int theme;
	int filter, smooth, delayMs; // defaults for new photos; smooth SMOOTH_OFF: plain photos (JPG)
	int strength;                // filter strength for new photos, 0..100 %
	u32 favorites[4];            // filters marked as favorites, one bit each
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
	int stereoFile;       // also save a 3D file: STEREO_OFF, STEREO_MPO, STEREO_SBS
	bool mp4;             // also save the wiggle as an MP4 video
	bool dateStamp;       // the date in orange in the corner, like an old film camera
} Settings;

typedef enum { STEREO_OFF, STEREO_MPO, STEREO_SBS, STEREO_COUNT } StereoFile;

typedef enum
{
	SET_THEME, SET_TIMER, SET_GRID, SET_SOUND, SET_FLASH, SET_FPS, SET_PHOTO_EYE,
	SET_AUTOPLAY, SET_GIF_SIZE, SET_GIF_SHAPE, SET_STEREO, SET_MP4, SET_RESET, SET_COUNT
} SettingRow;

#define MAX_SHOTS 1000
#define LOW_FREE (20ULL << 20) // SD card warnings
#define MIN_FREE (3ULL << 20)  // a photo is about 1.9 MB, plus its GIF
#define DELAY_MIN 40
#define DELAY_MAX 400
#define DELAY_STEP 20
#define DELAY_DEFAULT 200 // calm enough when nobody changes it
#define STRENGTH_STEP 10

// ---- bottom screen layout, used for drawing and touch ----

// Header (home and gallery): title left, buttons right. On home, the
// mode switches with an icon and a word each, then the settings gear.
#define HEADER_BTN_Y 6
#define HEADER_BTN_H 36 // 32 px and more for every touch target: fingers, not styluses
#define HEADER_PILL_W 56
#define HEADER_GEAR_W 34
#define HEADER_PILL_GAP 6
typedef enum { HB_WIGGLE, HB_DOUBLE, HB_SELFIE, HB_NIGHT, HB_SETTINGS, HB_COUNT } HeaderButton;

// Home: the filter card with Shuffle and Original beside it (what ▲ Y ▼
// do), then the last shot, the shutter and the way to the phone.
#define CARD_FX 12
#define CARD_FY 54
#define CARD_FW 210
#define CARD_FH 72
#define SIDE_X 228
#define SIDE_W 80
#define SIDE_H 34 // two buttons, one over the other, filling the card's height
#define SHUTTER_CX 160
#define SHUTTER_CY 182
#define SHUTTER_R 36
#define LAST_X 18
#define LAST_Y 154
#define LAST_S 58
#define QR_CX 278
#define QR_CY 176
#define QR_R 24
typedef enum { HH_FILTER, HH_SHUFFLE, HH_ORIGINAL, HH_SHUTTER, HH_GALLERY, HH_SHARE, HH_COUNT } HomeHit;

// Settings: the defaults for new shots on top (2x2 tiles), then the list
#define TILE_X 18
#define TILE_Y 44
#define TILE_W 139
#define TILE_H 34
#define TILE_GAP 6
#define PANEL_X 12
#define PANEL_Y 38
#define PANEL_W 296
#define PANEL_H (2 * TILE_H + 3 * TILE_GAP)
typedef enum { TILE_SMOOTH, TILE_SPEED, TILE_QUICK, TILE_DATE, TILE_COUNT } Tile;

// Gallery: three rows that scroll sideways, filled column by column
#define GRID_ROWS 3
#define GRID_X 11
#define GRID_Y 46
#define CELL_W 70
#define CELL_H 52
#define CELL_GAP 6
#define COL_W (CELL_W + CELL_GAP)
#define VISIBLE_COLS 4
#define THUMB_CACHE 24 // loaded thumbnails, slot = index % THUMB_CACHE
#define BACK_BTN_X 12
#define BACK_BTN_W 64
#define SHARE_BTN_W 78 // at the right of the gallery header
#define SHARE_BTN_X (308 - SHARE_BTN_W)

// Settings rows
#define SET_Y (PANEL_Y + PANEL_H + 8) // under the defaults for new shots
#define SET_ROW 29
#define SET_ROW_H 26
#define SET_VISIBLE 3 // rows on screen; the list scrolls

// Filter menu: a grid of buttons
#define MENU_COLS 3
#define MENU_X 12
#define MENU_Y 44
#define MENU_BTN_W 90
#define MENU_BTN_H 54
#define MENU_IMG_H 32 // the live preview in each tile
#define MENU_GAP 8
#define MENU_ROW (MENU_BTN_H + MENU_GAP)
#define MENU_HEAD 20      // a group's heading
#define MENU_GROUP_GAP 4
#define MENU_VIEW_H 168   // visible height; the menu scrolls
// Its header: a big way back to Original, a random filter, the All /
// Favorites tabs.
#define MENU_TAB_Y 6
#define MENU_TAB_H 30
#define MENU_ORIG_X 12
#define MENU_ORIG_W 132
#define MENU_DICE_X 152
#define MENU_DICE_W 44
#define MENU_TAB_X 204
#define MENU_TAB_W 52
typedef enum { MH_ALL, MH_FAVS, MH_ORIGINAL, MH_RANDOM } MenuHeader;

// Review
// Review: the shot on a card, under it big controls: speed (turtle to
// hare), the transition in four steps, the filter strength; then the buttons.
#define CARD_X 88
#define CARD_Y 4
#define CARD_W 224
#define CARD_H 168
#define SEG_X 8 // left of the card, four steps over each other: photo, hard, soft, softer
#define SEG_Y 4
#define SEG_W 72
#define SEG_H 38
#define SEG_GAP 5
#define SPEED_Y 190 // under the card: the speed slider, then the strength one
#define SPEED_X0 32
#define SPEED_X1 164
#define STR_X0 232
#define STR_X1 268
#define BTN_Y 208
#define BTN_H 28
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
	// Little animations: when the filter last changed (and which way), when
	// the shutter was pressed, when a mode switch changed.
	u64 filterAt, shutterAt, switchAt;
	int filterDir, switchIdx;
	int keyHit; u64 keyHitAt; // a key's button on home, shown pressed for a moment (HomeHit)
	bool touched;        // a finger on the screen now, at touchX, touchY: for pressed looks
	int touchX, touchY;
	int menuSel, menuScroll; // menuScroll: pixels
	bool camOk, selfie; // selfie: the inner camera, plain photos
	// Double exposure: on, and the first exposure while it waits for the second.
	bool doubleExp, haveFirst;
	const u16 *firstL, *firstR;
	bool menuFavs;      // the filter menu shows the favorites tab

	bool camStarting;   // at start, before the camera is up
	// Viewfinder helpers: the live view wiggling, the offset of its middle
	// between the eyes (for that and for "too close"), and how steady the
	// console is (0 still .. 255 shaking), from the accelerometer.
	bool wigglePreview, tooClose;
	Offset liveOffset;
	int shake;
	bool waitSteady; // night mode: waiting for the console to be still
	float depth;     // the 3D slider, 0..1, for the top screen's layers
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
	char stampText[16]; // review: the date stamp of the shot, like '26 11 04
	int exporting; // GIFs still being made

	// Review: shotNum is -1 until the shot has been saved once; savedInfo is
	// what is on the card, to tell whether there are changes.
	u16 *shotL, *shotR;
	int shotNum, pivotX, pivotY, delayMs, shotFilter, shotSmooth, shotStrength;
	Offset offset;
	ShotInfo savedInfo;
	bool saving, fromGallery, aligning;
	bool confirmDiscard; // B with unsaved changes: asks first
	int stacking, stackFrames; // night mode: frames taken so far (0 if not), of how many
	bool compare; // Y held: the shot without its filter

	// Share: page address and its QR code (qrcodegen format, up to version 10).
	bool shareOk;
	char shareUrl[48];
	u8 qr[(57 * 57 + 7) / 8 + 1];
	int sharedCount;
	int shareOnly; // shots picked in the gallery that the page shows; 0: all

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
int uiHomeAt(int x, int y); // home: a HomeHit at a point, -1 if none
// Gallery
bool uiGalleryBackHit(int x, int y);
bool uiGalleryShareHit(int x, int y);
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
int uiMenuTileAt(const App *a, int x, int y); // the filter at a point, -1 if none
int uiMenuMove(int filter, int dx, int dy);    // the filter beside / above / below
int uiMenuMaxScroll(void);
void uiMenuSetFavs(bool on);                   // the favorites tab, or all groups
int uiMenuFirst(void);                         // the first filter listed, -1 if none
int uiMenuHeaderAt(int x, int y);              // a MenuHeader at a point, -1 if none

void uiMenuReveal(App *a);                     // scrolls the selection into view
// Settings: the row at a point, and whether the point is on its ◀ (-1) or ▶ (+1).
int uiSettingsRowAt(const App *a, int x, int y, int *step);
