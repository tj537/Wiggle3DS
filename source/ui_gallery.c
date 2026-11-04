// Gallery and review.
#include "ui_priv.h"

static int backButtonWidth(void) { return hintWidth("B", "Back") + 20; }

bool uiGalleryShareHit(int x, int y)
{
	return x >= SHARE_BTN_X - 4 && x < 312 && y >= HEADER_BTN_Y - 4 && y < HEADER_BTN_Y + HEADER_BTN_H + 4;
}

bool uiGalleryBackHit(int x, int y)
{
	return x >= BACK_BTN_X - 4 && x < BACK_BTN_X + backButtonWidth() + 4 && y >= HEADER_BTN_Y - 4 &&
	       y < HEADER_BTN_Y + HEADER_BTN_H + 4;
}

int uiMaxScroll(const App *a)
{
	int cols = (a->shotCount + GRID_ROWS - 1) / GRID_ROWS, max = cols * COL_W - CELL_GAP - (320 - 2 * GRID_X);
	return max > 0 ? max : 0;
}

static void cellPos(const App *a, int idx, int *x, int *y)
{
	*x = GRID_X + (idx / GRID_ROWS) * COL_W - (int)a->scrollX;
	*y = GRID_Y + (idx % GRID_ROWS) * (CELL_H + CELL_GAP);
}

int uiCellAt(const App *a, int x, int y)
{
	int col = (x - GRID_X + (int)a->scrollX + CELL_GAP / 2) / COL_W, row = (y - GRID_Y + CELL_GAP / 2) / (CELL_H + CELL_GAP);
	int idx = col * GRID_ROWS + row;
	if (x < GRID_X - CELL_GAP / 2 || y < GRID_Y - CELL_GAP / 2 || row >= GRID_ROWS || idx >= a->shotCount)
		return -1;
	return idx;
}

void galleryBottom(App *a, u16 *fb)
{
	background(fb);

	// Back on the left, share on the right (while picking: the picked ones,
	// if any), where we are in between.
	int bw = backButtonWidth(), ty = HEADER_CY - lineH(&fontSmall) / 2;
	drawRoundRect(fb, 320, BACK_BTN_X, HEADER_BTN_Y, bw, HEADER_BTN_H, HEADER_BTN_H / 2, C_SURFACE2, 255);
	drawHint(fb, 320, BACK_BTN_X + 10, ty, "B", "Back", C_TEXT, 255);
	int right = 308;
	if (!a->picking || a->pickedCount)
	{
		drawRoundRectGrad(fb, 320, SHARE_BTN_X, HEADER_BTN_Y, SHARE_BTN_W, HEADER_BTN_H, HEADER_BTN_H / 2, C_ACC0, C_ACC1, 255);
		const char *label = a->picking ? tfmt(I_SHARE " Share %d", a->pickedCount) : I_SHARE " Share";
		drawText(fb, 320, &fontSmall, SHARE_BTN_X + (SHARE_BTN_W - textWidth(&fontSmall, label)) / 2, ty, C_WHITE, 255, label);
		right = SHARE_BTN_X - 10;
	}
	const char *pos = a->picking ? tfmt("%d selected", a->pickedCount)
	                  : a->selDate[0] ? tfmt("%s · %d / %d", a->selDate, a->sel + 1, a->shotCount)
	                                  : tfmt("%d / %d", a->sel + 1, a->shotCount);
	drawText(fb, 320, &fontSmall, right - textWidth(&fontSmall, pos), ty, C_DIM, 255, pos);

	int first = (int)a->scrollX / COL_W * GRID_ROWS;
	for (int idx = first; idx < a->shotCount && idx < first + (VISIBLE_COLS + 2) * GRID_ROWS; idx++)
	{
		int x, y;
		cellPos(a, idx, &x, &y);
		if (x >= 320)
			break;
		if (idx == a->sel)
			drawRoundRectGrad(fb, 320, x - 3, y - 3, CELL_W + 6, CELL_H + 6, 9, C_ACC0, C_ACC1, 255);
		thumb(a, fb, idx, x, y, CELL_W, CELL_H);
		if (a->picking)
		{
			// A check mark in the corner, empty until picked.
			float cx = x + CELL_W - 10, cy = y + 10;
			if (a->picked[idx])
			{
				drawFill(fb, 320, x, y, CELL_W, CELL_H, C_BLACK, 70);
				drawCircleGrad(fb, 320, cx, cy, 7, C_ACC0, C_ACC1, 255);
				drawTextCentered(fb, 320, &fontSmall, cx, cy - lineH(&fontSmall) / 2, C_WHITE, 255, "✓");
			}
			else
			{
				drawCircle(fb, 320, cx, cy, 7, C_BLACK, 110);
				drawRing(fb, 320, cx, cy, 7, 1.5f, C_WHITE, 220);
			}
		}
	}

	// Scroll bar when the strip is wider than the screen.
	int max = uiMaxScroll(a);
	if (max > 0)
	{
		int track = 296, content = max + 320 - 2 * GRID_X, w = track * (320 - 2 * GRID_X) / content;
		w = w < 24 ? 24 : w;
		int x = 12 + (int)((track - w) * a->scrollX / max);
		drawRoundRect(fb, 320, 12, 214, track, 3, 1, C_SURFACE, 255);
		drawRoundRectGrad(fb, 320, x, 214, w, 3, 1, C_ACC0, C_ACC1, 255);
	}

	if (a->picking)
	{
		static const char *const btns[] = {"A", "X", "B"};
		const char *const labels[] = {"Pick", "Delete", "Done"};
		centeredHints(fb, 222, 3, btns, labels);
	}
	else
	{
		// Home has its button at the top; the hint shows how to pick several.
		static const char *const btns[] = {"A", "Y", "X", "Hold"};
		const char *const labels[] = {"Edit", a->playing ? "Pause" : "Play", "Delete", "Select"};
		centeredHints(fb, 222, 4, btns, labels);
	}

	if (a->confirmDelete)
	{
		dialog(fb, 40, 66, 240, 104);
		if (a->picking)
			drawTextCentered(fb, 320, &fontBody, 160, 82, C_TEXT, 255, a->pickedCount == 1 ? "Delete 1 shot?" : tfmt("Delete %d shots?", a->pickedCount));
		else
			drawTextCentered(fb, 320, &fontBody, 160, 82, C_TEXT, 255, tfmt("Delete WIG_%04d?", a->shots[a->sel]));
		drawTextCentered(fb, 320, &fontSmall, 160, 104, C_DIM, 255, "Photos and GIFs go · you can undo");
		button(fb, 56, 132, 100, "B", "Cancel", false, C_SURFACE2);
		button(fb, 164, 132, 100, "X", "Delete", false, C_RED);
	}
}

// ---- bottom screen: review ----

bool uiReviewChanged(const App *a)
{
	ShotInfo now = {a->pivotX, a->pivotY, a->offset.dx, a->offset.dy, a->delayMs,
	                SHOT_STYLE(a->shotFilter, a->shotSmooth, a->shotStrength)};
	return a->shotNum < 0 || memcmp(&now, &a->savedInfo, sizeof(now)) != 0;
}

// Back and save, and the saving dialog.
static void reviewButtons(App *a, u16 *fb)
{
	button(fb, BACK_X, BTN_Y, BACK_W, "B", "Back", false, C_SURFACE2);
	if (uiReviewChanged(a))
		button(fb, SAVE_X, BTN_Y, SAVE_W, "A", a->shotNum < 0 ? (uiShotIsPhoto(a) ? "Save photo" : "Save GIF") : "Save changes", true, 0);
	else
	{
		drawRoundRect(fb, 320, SAVE_X, BTN_Y, SAVE_W, BTN_H, BTN_H / 2, C_SURFACE, 255);
		drawRoundRectOutline(fb, 320, SAVE_X, BTN_Y, SAVE_W, BTN_H, BTN_H / 2, 1, C_STROKE, 255);
		drawTextCentered(fb, 320, &fontSmall, SAVE_X + SAVE_W / 2, BTN_Y + (BTN_H - lineH(&fontSmall)) / 2, C_DIM, 255,
		                 tfmt("✓ Saved as WIG_%04d", a->shotNum));
	}

	if (a->confirmDiscard)
	{
		bool fresh = a->shotNum < 0;
		dialog(fb, 40, 66, 240, 104);
		drawTextCentered(fb, 320, &fontBody, 160, 82, C_TEXT, 255, fresh ? "Discard this shot?" : "Discard your changes?");
		drawTextCentered(fb, 320, &fontSmall, 160, 104, C_DIM, 255, fresh ? "It hasn't been saved yet." : "The saved version stays.");
		button(fb, 56, 132, 100, "B", "Keep", false, C_SURFACE2);
		button(fb, 164, 132, 100, "A", "Discard", false, C_RED);
	}
	if (a->saving)
	{
		dialog(fb, 60, 84, 200, 72);
		drawTextCentered(fb, 320, &fontBody, 160, 100, C_TEXT, 255, "Saving …");
		drawTextCentered(fb, 320, &fontSmall, 160, 122, C_DIM, 255, "One moment");
		drawRoundRectGrad(fb, 320, 100, 140, 120, 3, 1, C_ACC0, C_ACC1, 255);
	}
}

// A slider: track, filled part, knob; dim when it does nothing.
static void slider(u16 *fb, int x0, int x1, int cy, float k, bool on)
{
	int knob = x0 + (int)(k * (x1 - x0));
	drawRoundRect(fb, 320, x0, cy - 3, x1 - x0, 6, 3, C_SURFACE2, 255);
	if (on)
		drawRoundRectGrad(fb, 320, x0, cy - 3, knob - x0 + 3, 6, 3, C_ACC0, C_ACC1, 255);
	drawCircle(fb, 320, knob, cy + 1.5f, 11, C_BLACK, 70);
	drawCircle(fb, 320, knob, cy, 10, on ? C_TEXT : C_STROKE, 255);
}

void reviewBottom(App *a, u16 *fb)
{
	background(fb);

	drawShadow(fb, 320, CARD_X, CARD_Y, CARD_W, CARD_H, 10, 160);
	const u16 *card = uiShotIsPhoto(a) && a->settings.photoEye ? a->shotR : a->shotL;
	drawFiltered(fb, 320, CARD_X, CARD_Y, CARD_W, CARD_H, card, CAM_W, 0, 0, CAM_W, CAM_H, a->shotFilter, 10, 0, a->shotStrength);
	if (a->settings.dateStamp) // roughly where the export puts it
		drawText(fb, 320, &fontSmall, CARD_X + CARD_W - 8 - textWidth(&fontSmall, a->stampText), CARD_Y + CARD_H - 8 - lineH(&fontSmall),
		         COL565(255, 150, 40), 255, a->stampText);
	const char *fl = filterName(a->shotFilter);
	int fw = textWidth(&fontSmall, fl) + 20;
	drawRoundRect(fb, 320, FILTER_PILL_X, FILTER_PILL_Y, fw, 20, 10, C_BLACK, 150);
	drawText(fb, 320, &fontSmall, FILTER_PILL_X + 10, FILTER_PILL_Y + (20 - lineH(&fontSmall)) / 2, C_TEXT, 255, fl);
	bool photo = uiShotIsPhoto(a);
	if (!photo)
	{
		float px = CARD_X + (a->pivotX + 0.5f) * CARD_W / CAM_W, py = CARD_Y + (a->pivotY + 0.5f) * CARD_H / CAM_H;
		drawCircle(fb, 320, px, py, 12, C_BLACK, 70);
		drawRing(fb, 320, px, py, 10, 2, C_TEXT, a->aligning ? 140 : 240);
		if (!a->aligning)
			drawCircleGrad(fb, 320, px, py, 3.5f, C_ACC0, C_ACC1, 255);
	}
	if (!photo && !a->settings.tipDone)
	{
		// First time: what the point is for, with a pulse on it.
		float pulse = (a->now % 1200) / 1200.0f;
		float cx = CARD_X + (a->pivotX + 0.5f) * CARD_W / CAM_W, cy = CARD_Y + (a->pivotY + 0.5f) * CARD_H / CAM_H;
		drawRing(fb, 320, cx, cy, 10 + pulse * 18, 2, C_WHITE, (int)(200 * (1 - pulse)));
		drawRoundRect(fb, 320, CARD_X + 6, CARD_Y + 6, CARD_W - 12, 42, 12, C_BLACK, 190);
		drawTextCentered(fb, 320, &fontBody, CARD_X + CARD_W / 2, CARD_Y + 10, C_TEXT, 255, "Tap your subject");
		drawTextCentered(fb, 320, &fontSmall, CARD_X + CARD_W / 2, CARD_Y + 28, C_DIM, 255, "It stays still, the rest wiggles");
	}
	else
	{
		const char *hint = a->shotFilter ? "▲ Shuffle · ▼ Original · hold Y" : "▲ Shuffle filter";
		drawRoundRect(fb, 320, CARD_X + 8, CARD_Y + 8, textWidth(&fontSmall, hint) + 20, 20, 10, C_BLACK, 150);
		drawText(fb, 320, &fontSmall, CARD_X + 18, CARD_Y + 8 + (20 - lineH(&fontSmall)) / 2, C_TEXT, 255, hint);
	}

	// Speed: slow on the left, fast on the right (◀ ▶ too).
	u16 slow = photo ? C_STROKE : C_DIM;
	drawIcon(fb, 320, ICON_TURTLE, SPEED_X0 - 18, SPEED_Y, 20, slow, 255);
	drawIcon(fb, 320, ICON_HARE, SPEED_X1 + 20, SPEED_Y, 20, slow, 255);
	slider(fb, SPEED_X0, SPEED_X1, SPEED_Y, (float)(DELAY_MAX - a->delayMs) / (DELAY_MAX - DELAY_MIN), !photo);

	// Transition: four steps, the last one makes a plain photo (X steps too).
	// Photo on top, then the wiggles from hard to softest.
	static const char *const steps[4] = {"Photo", "Hard", "Soft", "Softer"};
	int cur = photo ? 0 : a->shotSmooth + 1;
	for (int i = 0; i < 4; i++)
	{
		int y = SEG_Y + i * (SEG_H + SEG_GAP);
		if (i == cur)
			drawRoundRectGrad(fb, 320, SEG_X, y, SEG_W, SEG_H, 12, C_ACC0, C_ACC1, 255);
		else
			drawRoundRect(fb, 320, SEG_X, y, SEG_W, SEG_H, 12, C_SURFACE, 255);
		drawTextCentered(fb, 320, &fontSmall, SEG_X + SEG_W / 2, y + (SEG_H - lineH(&fontSmall)) / 2, i == cur ? C_WHITE : C_DIM, 255,
		                 steps[i]);
	}

	// Filter strength (L R too).
	bool str = a->shotFilter != 0;
	drawIcon(fb, 320, ICON_STRENGTH, STR_X0 - 20, SPEED_Y, 15, str ? C_DIM : C_STROKE, 255);
	slider(fb, STR_X0, STR_X1, SPEED_Y, a->shotStrength / 100.0f, str);
	drawText(fb, 320, &fontSmall, STR_X1 + 14, SPEED_Y - lineH(&fontSmall) / 2, str ? C_TEXT : C_STROKE, 255,
	         tfmt("%d%%", a->shotStrength));

	reviewButtons(a, fb);
}
