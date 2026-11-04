// Settings and share.
#include "qrcodegen.h"
#include "ui_priv.h"

// ---- settings ----

// Each row: an icon first, so it reads without the words.
static const char *const settingLabels[SET_COUNT] = {
	I_PALETTE " Theme", I_TIMER " Self-timer", I_GRID " Grid lines", I_SOUND " Sounds", I_FLASH " Capture flash",
	I_EYE " Viewfinder", I_PHOTO " Photo camera", I_PLAY " Gallery autoplay", I_HALF " GIF size",
	I_SQUARE " GIF shape", I_3D " 3D file", I_FILM " Video", I_RESET " Capture defaults",
};

const char *uiSettingValue(const Settings *st, int row)
{
	switch (row)
	{
	case SET_THEME: return uiThemeName(st->theme);
	case SET_TIMER: return st->timer ? tfmt("%d s", st->timer) : "Off";
	case SET_GRID: return st->grid ? "On" : "Off";
	case SET_SOUND: return st->sound ? "On" : "Off";
	case SET_FLASH: return st->flash ? "On" : "Off";
	case SET_FPS: return tfmt("%d fps", st->fps);
	case SET_PHOTO_EYE: return st->photoEye ? "Right" : "Left";
	case SET_AUTOPLAY: return st->autoplay ? "On" : "Off";
	case SET_GIF_SIZE: return st->gifHalf ? "50%" : "100%";
	case SET_GIF_SHAPE: return st->gifSquare ? "1:1" : "4:3";
	case SET_STEREO: return st->stereoFile == STEREO_MPO ? "MPO" : st->stereoFile == STEREO_SBS ? "Side by side" : "Off";
	case SET_MP4: return st->mp4 ? "MP4" : "Off";
	}
	return "";
}

#define SET_VALUE_X0 176 // left arrow
#define SET_VALUE_X1 300 // right arrow

int uiSettingsRowAt(const App *a, int x, int y, int *step)
{
	if (y < SET_Y || y >= SET_Y + SET_VISIBLE * SET_ROW)
		return -1;
	int row = a->settingsTop + (y - SET_Y) / SET_ROW;
	if (row >= SET_COUNT)
		return -1;
	*step = x >= SET_VALUE_X0 - 10 && x < SET_VALUE_X0 + 22 ? -1 : x >= SET_VALUE_X1 - 22 ? 1 : 0;
	return row;
}

static const char *sizeText(u64 bytes)
{
	return bytes >= (1ULL << 30) ? tfmt("%.1f GB", bytes / (double)(1ULL << 30)) : tfmt("%d MB", (int)(bytes >> 20));
}

void settingsBottom(App *a, u16 *fb)
{
	background(fb);
	int tw = drawText(fb, 320, &fontTitle, 16, 9, C_TEXT, 255, "Settings");
	const char *info = tfmt("%s · %d shots · %s free", APP_VERSION, a->shotCount, sizeText(a->freeBytes));
	(void)tw;
	drawText(fb, 320, &fontSmall, 308 - textWidth(&fontSmall, info), 9 + fontTitle.ascent - fontSmall.ascent, C_DIM, 255, info);
	defaultsPanel(a, fb); // for new shots, on top

	for (int v = 0; v < SET_VISIBLE; v++)
	{
		int i = a->settingsTop + v;
		if (i >= SET_COUNT)
			break;
		int y = SET_Y + v * SET_ROW, cy = y + SET_ROW_H / 2;
		bool sel = i == a->settingsSel;
		drawRoundRect(fb, 320, 12, y, 290, SET_ROW_H, SET_ROW_H / 2, sel ? C_SURFACE2 : C_SURFACE, 255);
		if (sel)
			drawRoundRectOutline(fb, 320, 12, y, 290, SET_ROW_H, SET_ROW_H / 2, 1, C_ACC1, 255);

		drawText(fb, 320, &fontSmall, 24, cy - lineH(&fontSmall) / 2, C_TEXT, 255, settingLabels[i]);

		if (i == SET_RESET)
		{
			// An action: a button instead of a value.
			const char *label = "Reset";

			int bw = textWidth(&fontSmall, label) + 24, bx = 296 - bw;
			drawRoundRectGrad(fb, 320, bx, y + 3, bw, SET_ROW_H - 6, (SET_ROW_H - 6) / 2, C_ACC0, C_ACC1, 255);
			drawTextCentered(fb, 320, &fontSmall, bx + bw / 2, cy - lineH(&fontSmall) / 2, C_WHITE, 255, label);
			continue;
		}
		u16 arrow = sel ? C_TEXT : C_DIM;
		drawText(fb, 320, &fontSmall, SET_VALUE_X0, cy - lineH(&fontSmall) / 2, arrow, 255, "◀");
		drawText(fb, 320, &fontSmall, SET_VALUE_X1 - 6 - textWidth(&fontSmall, "▶"), cy - lineH(&fontSmall) / 2, arrow, 255, "▶");
		int vx = (SET_VALUE_X0 + SET_VALUE_X1 - 6) / 2;
		const char *value = uiSettingValue(&a->settings, i);
		if (i == SET_THEME)
		{
			// The theme's two accent colors next to its name.
			vx += 8;
			int tx = vx - textWidth(&fontSmall, value) / 2 - 12;
			drawCircleGrad(fb, 320, tx, cy, 5, C_ACC0, C_ACC1, 255);
		}
		drawTextCentered(fb, 320, &fontSmall, vx, cy - lineH(&fontSmall) / 2, C_TEXT, 255, value);
	}

	// Scroll bar for the rows below or above the screen.
	int track = SET_VISIBLE * SET_ROW - 3, h = track * SET_VISIBLE / SET_COUNT;
	int y = SET_Y + (track - h) * a->settingsTop / (SET_COUNT - SET_VISIBLE);
	drawRoundRect(fb, 320, 307, SET_Y, 3, track, 1, C_SURFACE, 255);
	drawRoundRectGrad(fb, 320, 307, y, 3, h, 1, C_ACC0, C_ACC1, 255);

	static const char *const btns[] = {"▲▼", "◀▶", "B"}, *const labels[] = {"Select", "Change", "Done"};
	centeredHints(fb, 218, 3, btns, labels);
}

// ---- share ----

void shareTop(App *a, u16 *fb)
{
	drawClear(fb, 400, COL565(11, 7, 20));
	if (!a->shareOk)
	{
		drawTextCentered(fb, 400, &fontTitle, 200, 96, C_TEXT, 255, "No WiFi connection");
		drawTextCentered(fb, 400, &fontBody, 200, 128, C_DIM, 255, "Turn on WiFi and try again.");
		return;
	}
	// White card with the code and a quiet zone of 3 modules.
	int n = qrcodegen_getSize(a->qr), quiet = 3, scale = 216 / (n + 2 * quiet);
	int card = (n + 2 * quiet) * scale, x0 = 200 - card / 2, y0 = 120 - card / 2;
	drawShadow(fb, 400, x0, y0, card, card, 14, 200);
	drawRoundRect(fb, 400, x0, y0, card, card, 14, C_WHITE, 255);
	for (int y = 0; y < n; y++)
		for (int x = 0; x < n; x++)
			if (qrcodegen_getModule(a->qr, x, y))
				drawFill(fb, 400, x0 + (quiet + x) * scale, y0 + (quiet + y) * scale, scale, scale, COL565(20, 12, 36), 255);
}

static void step(u16 *fb, int y, int num, const char *text)
{
	drawCircleGrad(fb, 320, 28, y + 9, 9, C_ACC0, C_ACC1, 255);
	drawTextCentered(fb, 320, &fontSmall, 28, y + 9 - lineH(&fontSmall) / 2, C_TEXT, 255, tfmt("%d", num));
	drawText(fb, 320, &fontBody, 46, y + 9 - lineH(&fontBody) / 2, C_TEXT, 255, text);
}

void shareBottom(App *a, u16 *fb)
{
	background(fb);
	drawTextGrad(fb, 320, &fontTitle, 16, 9, C_ACC0, C_ACC1, "Share to phone");

	if (a->shareOk)
	{
		step(fb, 48, 1, "Put your phone on the same WiFi");
		step(fb, 78, 2, "Scan the code with its camera");
		step(fb, 108, 3, "Tap a picture, then Save");

		if (a->shareOnly)
			drawTextCentered(fb, 320, &fontSmall, 160, 184, C_DIM, 255,
			                 tfmt(a->shareOnly == 1 ? "Only the shot you picked" : "Only the %d shots you picked", a->shareOnly));
		else if (a->exporting)
			drawTextCentered(fb, 320, &fontSmall, 160, 184, C_DIM, 255,
			                 tfmt(a->exporting == 1 ? "1 GIF is still being made …" : "%d GIFs are still being made …", a->exporting));
		else if (a->sharedCount)
			drawTextCentered(fb, 320, &fontSmall, 160, 184, C_DIM, 255,
			                 tfmt(a->sharedCount == 1 ? "%d GIF sent" : "%d GIFs sent", a->sharedCount));
	}
	else
	{
		drawTextCentered(fb, 320, &fontBody, 160, 90, C_TEXT, 255, "Your 3DS isn't connected to WiFi.");
		drawTextCentered(fb, 320, &fontSmall, 160, 112, C_DIM, 255, "Check the WiFi switch and your connection settings.");
	}

	int hw = hintWidth("B", "Close");
	drawHint(fb, 320, 160 - hw / 2, 218, "B", "Close", C_DIM, 255);
}
