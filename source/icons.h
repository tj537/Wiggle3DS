#pragma once
#include <3ds/types.h>

// Pictograms instead of words, so the app reads the same in any language.
// Each icon is a character of its own (Unicode private use, U+E000 + id):
// put one in any text and drawText, textWidth, hints and buttons show it,
// sized and colored like the text around it.
typedef enum
{
	ICON_CAMERA, ICON_FILTER, ICON_WAVE, ICON_SPEED, ICON_BOLT, ICON_TIMER, ICON_GRID, ICON_SOUND,
	ICON_FLASH, ICON_MOON, ICON_EYE, ICON_PLAY, ICON_PAUSE, ICON_STAR, ICON_DICE, ICON_SUN,
	ICON_TRASH, ICON_CHECK, ICON_CROSS, ICON_BACK, ICON_HOME, ICON_GALLERY, ICON_SAVE, ICON_STRENGTH,
	ICON_PALETTE, ICON_FILM, ICON_3D, ICON_SQUARE, ICON_HALF, ICON_RESET, ICON_SHARE, ICON_GEAR,
	ICON_POWER, ICON_TOUCH, ICON_PHOTO, ICON_WAIT, ICON_SD, ICON_COMPARE, ICON_UNDO, ICON_LIST,
	ICON_EDIT, ICON_ORIGINAL, ICON_SELFIE, ICON_TURTLE, ICON_HARE, ICON_QR, ICON_SPARKLE, ICON_COUNT
} Icon;

#define ICON_CODE 0xE000

// UTF-8 of the icon characters (U+E000 + id is EE 80 80+id, for id < 64).
#define I_CAMERA "\xEE\x80\x80"
#define I_FILTER "\xEE\x80\x81"
#define I_WAVE "\xEE\x80\x82"
#define I_SPEED "\xEE\x80\x83"
#define I_BOLT "\xEE\x80\x84"
#define I_TIMER "\xEE\x80\x85"
#define I_GRID "\xEE\x80\x86"
#define I_SOUND "\xEE\x80\x87"
#define I_FLASH "\xEE\x80\x88"
#define I_MOON "\xEE\x80\x89"
#define I_EYE "\xEE\x80\x8A"
#define I_PLAY "\xEE\x80\x8B"
#define I_PAUSE "\xEE\x80\x8C"
#define I_STAR "\xEE\x80\x8D"
#define I_DICE "\xEE\x80\x8E"
#define I_SUN "\xEE\x80\x8F"
#define I_TRASH "\xEE\x80\x90"
#define I_CHECK "\xEE\x80\x91"
#define I_CROSS "\xEE\x80\x92"
#define I_BACK "\xEE\x80\x93"
#define I_HOME "\xEE\x80\x94"
#define I_GALLERY "\xEE\x80\x95"
#define I_SAVE "\xEE\x80\x96"
#define I_STRENGTH "\xEE\x80\x97"
#define I_PALETTE "\xEE\x80\x98"
#define I_FILM "\xEE\x80\x99"
#define I_3D "\xEE\x80\x9A"
#define I_SQUARE "\xEE\x80\x9B"
#define I_HALF "\xEE\x80\x9C"
#define I_RESET "\xEE\x80\x9D"
#define I_SHARE "\xEE\x80\x9E"
#define I_GEAR "\xEE\x80\x9F"
#define I_POWER "\xEE\x80\xA0"
#define I_TOUCH "\xEE\x80\xA1"
#define I_PHOTO "\xEE\x80\xA2"
#define I_WAIT "\xEE\x80\xA3"
#define I_SD "\xEE\x80\xA4"
#define I_COMPARE "\xEE\x80\xA5"
#define I_UNDO "\xEE\x80\xA6"
#define I_LIST "\xEE\x80\xA7"
#define I_EDIT "\xEE\x80\xA8"
#define I_ORIGINAL "\xEE\x80\xA9"
#define I_SELFIE "\xEE\x80\xAA"

// Draws an icon into a box of `size` px centered at (cx, cy).
void drawIcon(u16 *fb, int sw, int icon, float cx, float cy, float size, u16 color, int alpha);
