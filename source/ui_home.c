// Home: the mode switches, the filter card, shutter, last shot and phone;
// the capture defaults (shown in settings); and the filter menu.
#include "ui_priv.h"

// ---- bottom screen: home ----

#define HEADER_CY (HEADER_BTN_Y + HEADER_BTN_H / 2)

// cos/sin of the gear's 8 teeth.
static const float cosTable[8] = {1, 0.7071f, 0, -0.7071f, -1, -0.7071f, 0, 0.7071f};
static const float sinTable[8] = {0, 0.7071f, 1, 0.7071f, 0, -0.7071f, -1, -0.7071f};

// Header pills, right to left: the settings gear, then night, selfie,
// double exposure and wiggle preview, each with a word under its icon.
static int headerW(int i) { return i == HB_SETTINGS ? HEADER_GEAR_W : HEADER_PILL_W; }
static int headerX(int i)
{
	return 308 - HEADER_GEAR_W - (i == HB_SETTINGS ? 0 : (HB_SETTINGS - i) * (HEADER_PILL_W + HEADER_PILL_GAP));
}

int uiHeaderAt(int x, int y)
{
	if (y < HEADER_BTN_Y - 5 || y >= HEADER_BTN_Y + HEADER_BTN_H + 5)
		return -1;
	for (int i = 0; i < HB_COUNT; i++)
		if (x >= headerX(i) - HEADER_PILL_GAP / 2 && x < headerX(i) + headerW(i) + HEADER_PILL_GAP / 2)
			return i;
	return -1;
}

// Filter menu layout, in content coordinates (0 = the top of the list):
// each group is a heading and its buttons, three to a row.
// The menu's groups: all filter groups, or on the favorites tab just those.
static u8 favList[FILTER_COUNT];
static int favCount;
static u32 favBits[(FILTER_COUNT + 31) / 32];
static bool favTab;

void uiSetFavorites(const u32 *bits)
{
	memcpy(favBits, bits, sizeof(favBits));
	favCount = 0;
	for (int g = 0; g < filterGroupCount(); g++)
		for (int i = 0; i < filterGroupSize(g); i++)
		{
			int f = filterGroupItem(g, i);
			if (favBits[f / 32] >> (f % 32) & 1)
				favList[favCount++] = f;
		}
}

void uiMenuSetFavs(bool on) { favTab = on; }

static bool isFavorite(int f) { return favBits[f / 32] >> (f % 32) & 1; }
static int mFav(void) { return favTab && favCount ? 1 : 0; }
static int mGroups(void) { return favTab ? mFav() : filterGroupCount(); }

int uiMenuHeaderAt(int x, int y)
{
	if (y < MENU_TAB_Y - 5 || y >= MENU_TAB_Y + MENU_TAB_H + 5)
		return -1;
	if (x >= MENU_TAB_X - 4 && x < MENU_TAB_X + 2 * MENU_TAB_W + 6)
		return x < MENU_TAB_X + MENU_TAB_W ? MH_ALL : MH_FAVS;
	if (x >= MENU_ORIG_X - 6 && x < MENU_ORIG_X + MENU_ORIG_W + 4)
		return MH_ORIGINAL;
	if (x >= MENU_DICE_X - 4 && x < MENU_DICE_X + MENU_DICE_W + 4)
		return MH_RANDOM;
	return -1;
}
static const char *mName(int g) { return g < mFav() ? "Favorites" : filterGroupName(g - mFav()); }
static int mSize(int g) { return g < mFav() ? favCount : filterGroupSize(g - mFav()); }
static int mItem(int g, int i) { return g < mFav() ? favList[i] : filterGroupItem(g - mFav(), i); }

int uiMenuFirst(void) { return mGroups() ? mItem(0, 0) : -1; }

static int groupTop(int g)
{
	int y = 0;
	for (int k = 0; k < g; k++)
		y += MENU_HEAD + (mSize(k) + MENU_COLS - 1) / MENU_COLS * MENU_ROW + MENU_GROUP_GAP;
	return y;
}

static int menuHeight(void) { return mGroups() ? groupTop(mGroups()) - MENU_GROUP_GAP : 0; }

// Group and place of a filter in the menu.
static void menuPlace(int filter, int *group, int *index)
{
	for (int g = 0; g < mGroups(); g++)
		for (int i = 0; i < mSize(g); i++)
			if (mItem(g, i) == filter)
			{
				*group = g;
				*index = i;
				return;
			}
	*group = *index = 0;
}

static void menuItemPos(int g, int i, int *x, int *y)
{
	*x = MENU_X + (i % MENU_COLS) * (MENU_BTN_W + MENU_GAP);
	*y = groupTop(g) + MENU_HEAD + i / MENU_COLS * MENU_ROW;
}

int uiMenuTileAt(const App *a, int x, int y)
{
	if (y < MENU_Y || y >= MENU_Y + MENU_VIEW_H)
		return -1;
	for (int g = 0; g < mGroups(); g++)
		for (int i = 0; i < mSize(g); i++)
		{
			int bx, by;
			menuItemPos(g, i, &bx, &by);
			by += MENU_Y - a->menuScroll;
			if (x >= bx - MENU_GAP / 2 && x < bx + MENU_BTN_W + MENU_GAP / 2 && y >= by - MENU_GAP / 2 &&
			    y < by + MENU_BTN_H + MENU_GAP / 2)
				return mItem(g, i);
		}
	return -1;
}

int uiMenuMove(int filter, int dx, int dy)
{
	if (!mGroups())
		return filter;
	int g, i;
	menuPlace(filter, &g, &i);
	if (dx)
	{
		// Through the menu in order, across groups.
		i += dx;
		if (i < 0)
		{
			g = (g - 1 + mGroups()) % mGroups();
			i = mSize(g) - 1;
		}
		else if (i >= mSize(g))
		{
			g = (g + 1) % mGroups();
			i = 0;
		}
		return mItem(g, i);
	}
	int row = i / MENU_COLS + dy, col = i % MENU_COLS, rows = (mSize(g) + MENU_COLS - 1) / MENU_COLS;
	if (row < 0) // the last row of the group above
	{
		if (--g < 0)
			return filter;
		rows = (mSize(g) + MENU_COLS - 1) / MENU_COLS;
		row = rows - 1;
	}
	else if (row >= rows) // the first row of the group below
	{
		if (++g >= mGroups())
			return filter;
		row = 0;
	}
	int k = row * MENU_COLS + col, n = mSize(g);
	return mItem(g, k < n ? k : n - 1);
}

int uiMenuMaxScroll(void) { return menuHeight() > MENU_VIEW_H ? menuHeight() - MENU_VIEW_H : 0; }

void uiMenuReveal(App *a)
{
	int g, i, x, y;
	menuPlace(a->menuSel, &g, &i);
	menuItemPos(g, i, &x, &y);
	int top = i < MENU_COLS ? groupTop(g) : y; // a first row shows its heading too
	if (top < a->menuScroll)
		a->menuScroll = top;
	if (y + MENU_BTN_H > a->menuScroll + MENU_VIEW_H)
		a->menuScroll = y + MENU_BTN_H - MENU_VIEW_H;
	int max = uiMenuMaxScroll();
	a->menuScroll = a->menuScroll < 0 ? 0 : a->menuScroll > max ? max : a->menuScroll;
}

static void tileRect(int i, int *x, int *y)
{
	*x = TILE_X + (i % 2) * (TILE_W + TILE_GAP);
	*y = TILE_Y + (i / 2) * (TILE_H + TILE_GAP);
}

// Speed tile: big ◀ and ▶ buttons at its right end, for fingers.
#define SPEED_R 14
#define SPEED_PLUS_X (TILE_W - 18)
#define SPEED_MINUS_X (TILE_W - 50)

int uiTileAt(int x, int y, int *step)
{
	*step = 0;
	for (int i = 0; i < TILE_COUNT; i++)
	{
		int tx, ty;
		tileRect(i, &tx, &ty);
		if (x >= tx - TILE_GAP / 2 && x < tx + TILE_W + TILE_GAP / 2 && y >= ty - TILE_GAP / 2 && y < ty + TILE_H + TILE_GAP / 2)
		{
			int rx = x - tx;
			// Each button takes half of the space next to the value.
			*step = rx >= (SPEED_MINUS_X + SPEED_PLUS_X) / 2 ? 1 : rx >= SPEED_MINUS_X - SPEED_R - 6 ? -1 : 0;
			return i;
		}
	}
	return -1;
}

// Text cut to a width, with "…" where it was cut.
static const char *fitText(const Font *f, const char *s, int w)
{
	if (textWidth(f, s) <= w)
		return s;
	static char buf[64];
	int n = snprintf(buf, sizeof(buf) - 4, "%s", s);
	n = n > (int)sizeof(buf) - 5 ? (int)sizeof(buf) - 5 : n;
	while (n > 0)
	{
		while (n > 0 && (buf[n - 1] & 0xC0) == 0x80) // whole UTF-8 characters
			n--;
		n--;
		while (n > 0 && buf[n - 1] == ' ')
			n--;
		strcpy(buf + n, "…");
		if (textWidth(f, buf) <= w)
			break;
	}
	return buf;
}

static void headerButtons(App *a, u16 *fb)
{
	static const char *const words[HB_COUNT] = {"Preview", "Double", "Selfie", "Night", ""};
	for (int i = 0; i < HB_COUNT; i++)
	{
		int x = headerX(i), w = headerW(i);
		bool on = (i == HB_NIGHT && a->settings.night) || (i == HB_SELFIE && a->selfie) || (i == HB_DOUBLE && a->doubleExp) ||
		          (i == HB_WIGGLE && a->wigglePreview && !photoMode(a));
		drawRoundRect(fb, 320, x, HEADER_BTN_Y, w, HEADER_BTN_H, HEADER_BTN_H / 2, C_SURFACE2, 255);
		if (on)
		{
			// A switch that just went on fades its color in.
			u64 t = i == a->switchIdx ? since(a, a->switchAt) : 1000;
			int alpha = t < 180 ? (int)(t * 255 / 180) : 255;
			drawRoundRectGrad(fb, 320, x, HEADER_BTN_Y, w, HEADER_BTN_H, HEADER_BTN_H / 2, C_ACC0, C_ACC1, alpha);
		}
		u16 ink = on ? C_WHITE : C_TEXT;
		float cx = x + w / 2.0f, cy = i == HB_SETTINGS ? HEADER_CY : HEADER_BTN_Y + 12;
		switch (i)
		{
		case HB_WIGGLE: // the live view wiggling: a wave, dim when shots are plain photos
			drawIcon(fb, 320, ICON_WAVE, cx, cy, 14, photoMode(a) ? C_DIM : ink, 255);
			break;
		case HB_DOUBLE: // two exposures overlapping, the second filled once the first is taken
			drawRing(fb, 320, cx - 3, cy, 5, 1.3f, ink, 255);
			if (a->haveFirst)
				drawCircle(fb, 320, cx + 3, cy, 5, ink, 200);
			else
				drawRing(fb, 320, cx + 3, cy, 5, 1.3f, ink, 255);
			break;
		case HB_SELFIE:
			drawIcon(fb, 320, ICON_SELFIE, cx, cy, 14, ink, 255);
			break;
		case HB_NIGHT:
			drawIcon(fb, 320, ICON_MOON, cx, cy, 13, ink, 255);
			break;
		default: // gear
			for (int t = 0; t < 8; t++)
				drawCircle(fb, 320, cx + 6.6f * cosTable[t], cy + 6.6f * sinTable[t], 1.9f, ink, 255);
			drawRing(fb, 320, cx, cy, 5.6f, 2.4f, ink, 255);
		}
		if (words[i][0])
			drawTextCentered(fb, 320, &fontSmall, (int)cx, HEADER_BTN_Y + 19, i == HB_WIGGLE && photoMode(a) ? C_DIM : ink, 255, words[i]);
	}
}

static void tile(u16 *fb, int i, const char *label, const char *value)
{
	int x, y;
	tileRect(i, &x, &y);
	drawRoundRect(fb, 320, x, y, TILE_W, TILE_H, 10, C_SURFACE2, 255);
	drawText(fb, 320, &fontSmall, x + 10, y + 4, C_DIM, 255, label);
	drawText(fb, 320, &fontBody, x + 10, y + 17, C_TEXT, 255, value);
}

static void roundButton(u16 *fb, float cx, float cy, float r, const char *glyph, bool on)
{
	drawCircle(fb, 320, cx, cy, r, on ? C_STROKE : C_SURFACE, 255);
	drawTextCentered(fb, 320, &fontSmall, (int)cx, (int)cy - lineH(&fontSmall) / 2, on ? C_TEXT : C_DIM, 255, glyph);
}

// The defaults for new shots, as four tiles (on the settings screen).
void defaultsPanel(App *a, u16 *fb)
{
	const Settings *st = &a->settings;
	drawRoundRect(fb, 320, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 14, C_SURFACE, 255);
	drawRoundRectOutline(fb, 320, PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 14, 1, C_STROKE, 255);
	int x, y;

	// Off (a plain photo), then the smoothing levels: one dot more each.
	bool off = st->smooth == SMOOTH_OFF;
	tile(fb, TILE_SMOOTH, "Transition", off ? "Off · photo" : wiggleSmoothName(st->smooth));
	tileRect(TILE_SMOOTH, &x, &y);
	for (int k = 0; k <= SMOOTH_LEVELS; k++)
	{
		float cx = x + TILE_W - 38 + k * 9, cy = y + TILE_H / 2.0f;
		if (!off && k <= st->smooth + 1)
			drawCircleGrad(fb, 320, cx, cy, 3.5f, C_ACC0, C_ACC1, 255);
		else
			drawCircle(fb, 320, cx, cy, 3.5f, C_STROKE, 255);
	}

	tile(fb, TILE_SPEED, "Speed", tfmt("%d ms", st->delayMs));
	tileRect(TILE_SPEED, &x, &y);
	roundButton(fb, x + SPEED_MINUS_X, y + TILE_H / 2.0f, SPEED_R, "◀", st->delayMs > DELAY_MIN);
	roundButton(fb, x + SPEED_PLUS_X, y + TILE_H / 2.0f, SPEED_R, "▶", st->delayMs < DELAY_MAX);

	tile(fb, TILE_QUICK, "Quick capture", st->quick ? "On" : "Off");
	tileRect(TILE_QUICK, &x, &y);
	int sx = x + TILE_W - 44, sy = y + (TILE_H - 18) / 2;
	if (st->quick)
		drawRoundRectGrad(fb, 320, sx, sy, 34, 18, 9, C_ACC0, C_ACC1, 255);
	else
		drawRoundRect(fb, 320, sx, sy, 34, 18, 9, C_STROKE, 255);
	drawCircle(fb, 320, st->quick ? sx + 25 : sx + 9, sy + 9, 7, C_TEXT, 255);

	tile(fb, TILE_DATE, "Date stamp", st->dateStamp ? "On" : "Off");
	tileRect(TILE_DATE, &x, &y);
	if (st->dateStamp)
		drawText(fb, 320, &fontSmall, x + TILE_W - 12 - textWidth(&fontSmall, "'26 11 04"), y + (TILE_H - lineH(&fontSmall)) / 2,
		         COL565(255, 160, 50), 255, "'26 11 04");
}

// A thumbnail with the shot's filter; a small badge while its GIF is made.
void thumb(App *a, u16 *fb, int idx, int x, int y, int w, int h)
{
	int i = uiThumbSlot(a, idx);
	if (i < 0)
		drawRoundRect(fb, 320, x, y, w, h, 6, C_SURFACE, 255); // still loading
	else if (a->gridOk[i])
		drawImage(fb, 320, x, y, w, h, a->grid[i], THUMB_W, 0, 0, THUMB_W, THUMB_H, NULL, 6); // filtered on load
	else
	{
		drawRoundRect(fb, 320, x, y, w, h, 6, C_SURFACE2, 255);
		drawTextCentered(fb, 320, &fontSmall, x + w / 2, y + h / 2 - 6, C_DIM, 255, "No preview");
	}
	if (a->exporting && exportIsPending(a->shots[idx]))
	{
		drawRoundRect(fb, 320, x + w - 26, y + h - 17, 22, 13, 6, C_BLACK, 170);
		drawTextCentered(fb, 320, &fontSmall, x + w - 15, y + h - 17 + (13 - lineH(&fontSmall)) / 2 - 2, C_TEXT, 255, "…");
	}
}

// The menu's tiles all show the same part of the live view: it is made
// small once per frame (2x2 samples averaged), and each tile only colors
// that, instead of scaling the whole camera frame again.
#define MENU_TILE_W (MENU_BTN_W - 6)
static u16 menuLive[MENU_TILE_W * MENU_IMG_H];

static void shrinkLive(const u16 *img)
{
	int ch = CAM_W * MENU_IMG_H / MENU_TILE_W, y0 = (CAM_H - ch) / 2;
	for (int y = 0; y < MENU_IMG_H; y++)
	{
		const u16 *r0 = img + (y0 + (2 * y) * ch / (2 * MENU_IMG_H)) * CAM_W,
		          *r1 = img + (y0 + (2 * y + 1) * ch / (2 * MENU_IMG_H)) * CAM_W;
		for (int x = 0; x < MENU_TILE_W; x++)
		{
			int x0 = (2 * x) * CAM_W / (2 * MENU_TILE_W), x1 = (2 * x + 1) * CAM_W / (2 * MENU_TILE_W);
			u16 a = r0[x0], b = r0[x1], c = r1[x0], d = r1[x1];
			int r = ((a >> 11) + (b >> 11) + (c >> 11) + (d >> 11) + 2) >> 2;
			int g = ((a >> 5 & 63) + (b >> 5 & 63) + (c >> 5 & 63) + (d >> 5 & 63) + 2) >> 2;
			int bl = ((a & 31) + (b & 31) + (c & 31) + (d & 31) + 2) >> 2;
			menuLive[y * MENU_TILE_W + x] = r << 11 | g << 5 | bl;
		}
	}
}

// Filter picker over the whole bottom screen: plain buttons; the live view
// on top previews the selected one.
static void filterMenu(App *a, u16 *fb)
{
	background(fb);
	bool live = a->camOk && a->liveL;
	if (live)
		shrinkLive(a->liveL);
	int off = MENU_Y - a->menuScroll;
	for (int g = 0; g < mGroups(); g++)
	{
		int hy = off + groupTop(g);
		if (hy > MENU_Y + MENU_VIEW_H)
			break;
		int nx = MENU_X + 4;
		if (g < mFav())
		{
			drawStar(fb, 320, nx + 5, hy + MENU_HEAD / 2.0f - 2, 5, C_STAR, 255);
			nx += 14;
		}
		drawText(fb, 320, &fontSmall, nx, hy + (MENU_HEAD - lineH(&fontSmall)) / 2 - 2, C_DIM, 255, g < mFav() ? "" : mName(g));
		for (int i = 0; i < mSize(g); i++)
		{
			int f = mItem(g, i), x, y;
			menuItemPos(g, i, &x, &y);
			y += off;
			if (y + MENU_BTN_H < MENU_Y - 8 || y > MENU_Y + MENU_VIEW_H)
				continue;
			// A tile: the live view with the filter on top, the name below.
			bool sel = f == a->menuSel, current = f == a->settings.filter;
			if (sel)
				drawRoundRectGrad(fb, 320, x, y, MENU_BTN_W, MENU_BTN_H, 10, C_ACC0, C_ACC1, 255);
			else
				drawRoundRect(fb, 320, x, y, MENU_BTN_W, MENU_BTN_H, 10, C_SURFACE2, 255);
			if (current && !sel)
				drawRoundRectOutline(fb, 320, x, y, MENU_BTN_W, MENU_BTN_H, 10, 1, C_ACC1, 255);
			if (live)
				drawFiltered(fb, 320, x + 3, y + 3, MENU_TILE_W, MENU_IMG_H, menuLive, MENU_TILE_W, 0, 0, MENU_TILE_W, MENU_IMG_H,
				             f, 7, photoMode(a) ? 0 : (int)(a->now / LIVE_GLITCH_MS), 100);
			else
				drawRoundRect(fb, 320, x + 3, y + 3, MENU_TILE_W, MENU_IMG_H, 7, C_SURFACE, 255);
			if (isFavorite(f))
			{
				drawCircle(fb, 320, x + MENU_BTN_W - 11, y + 11, 7, C_BLACK, 140);
				drawStar(fb, 320, x + MENU_BTN_W - 11, y + 11, 5, C_STAR, 255);
			}
			drawTextCentered(fb, 320, &fontSmall, x + MENU_BTN_W / 2, y + 3 + MENU_IMG_H + (MENU_BTN_H - 3 - MENU_IMG_H - lineH(&fontSmall)) / 2,
			                 sel ? C_WHITE : current ? C_TEXT : C_DIM, 255, filterName(f));
		}
	}

	if (favTab && !favCount)
	{
		drawStar(fb, 320, 160, MENU_Y + 36, 14, C_STAR, 255);
		drawTextCentered(fb, 320, &fontBody, 160, MENU_Y + 58, C_TEXT, 255, "No favorites yet");
		drawTextCentered(fb, 320, &fontSmall, 160, MENU_Y + 80, C_DIM, 255, "In All, X marks a filter as a favorite");
	}

	// Header and hints over what scrolls under them.
	backgroundRows(fb, 0, MENU_Y - 4);
	backgroundRows(fb, MENU_Y + MENU_VIEW_H + 2, 240);
	float my = MENU_TAB_Y + MENU_TAB_H / 2.0f;
	// Original, big: one tap from anywhere in the list.
	bool orig = a->settings.filter == 0;
	drawRoundRect(fb, 320, MENU_ORIG_X, MENU_TAB_Y, MENU_ORIG_W, MENU_TAB_H, MENU_TAB_H / 2, C_SURFACE2, 255);
	if (orig)
		drawRoundRectOutline(fb, 320, MENU_ORIG_X, MENU_TAB_Y, MENU_ORIG_W, MENU_TAB_H, MENU_TAB_H / 2, 2, C_ACC1, 255);
	drawIcon(fb, 320, ICON_ORIGINAL, MENU_ORIG_X + 20, my, 16, C_TEXT, 255);
	drawText(fb, 320, &fontBody, MENU_ORIG_X + 34, (int)my - lineH(&fontBody) / 2, C_TEXT, 255, filterName(0));
	// A random filter.
	drawRoundRect(fb, 320, MENU_DICE_X, MENU_TAB_Y, MENU_DICE_W, MENU_TAB_H, MENU_TAB_H / 2, C_SURFACE2, 255);
	drawIcon(fb, 320, ICON_DICE, MENU_DICE_X + MENU_DICE_W / 2.0f, my, 17, C_TEXT, 255);
	// All or the favorites.
	drawRoundRect(fb, 320, MENU_TAB_X, MENU_TAB_Y, 2 * MENU_TAB_W, MENU_TAB_H, MENU_TAB_H / 2, C_SURFACE2, 255);
	int tx = favTab ? MENU_TAB_X + MENU_TAB_W : MENU_TAB_X;
	drawRoundRectGrad(fb, 320, tx + 2, MENU_TAB_Y + 2, MENU_TAB_W - 4, MENU_TAB_H - 4, MENU_TAB_H / 2 - 2, C_ACC0, C_ACC1, 255);
	drawTextCentered(fb, 320, &fontSmall, MENU_TAB_X + MENU_TAB_W / 2, (int)my - lineH(&fontSmall) / 2, favTab ? C_DIM : C_WHITE, 255, "All");
	drawStar(fb, 320, MENU_TAB_X + MENU_TAB_W * 1.5f, my, 7.5f, favTab ? C_WHITE : C_STAR, 255);

	int max = uiMenuMaxScroll();
	if (max > 0)
	{
		int track = MENU_VIEW_H, bh = track * MENU_VIEW_H / menuHeight(), by = MENU_Y + (track - bh) * a->menuScroll / max;
		drawRoundRect(fb, 320, 310, MENU_Y, 3, track, 1, C_SURFACE, 255);
		drawRoundRectGrad(fb, 320, 310, by, 3, bh, 1, C_ACC0, C_ACC1, 255);
	}
	static const char *const btns[] = {"A", "X", "L R", "B"};
	const char *const labels[] = {"Apply", isFavorite(a->menuSel) ? "Unfavorite" : "Favorite",
	                              tfmt("%d%%", a->settings.strength), "Back"};
	centeredHints(fb, 218, 4, btns, labels);
}

int uiHomeAt(int x, int y)
{
	if (x >= CARD_FX - 4 && x < CARD_FX + CARD_FW + 3 && y >= CARD_FY - 4 && y < CARD_FY + CARD_FH + 4)
		return HH_FILTER;
	if (x >= SIDE_X - 3 && x < SIDE_X + SIDE_W + 4 && y >= CARD_FY - 4 && y < CARD_FY + CARD_FH + 4)
		return y < CARD_FY + CARD_FH / 2 ? HH_SHUFFLE : HH_ORIGINAL;
	int dx = x - SHUTTER_CX, dy = y - SHUTTER_CY;
	if (dx * dx + dy * dy <= (SHUTTER_R + 8) * (SHUTTER_R + 8))
		return HH_SHUTTER;
	if (x >= LAST_X - 6 && x < LAST_X + LAST_S + 6 && y >= LAST_Y - 6 && y < LAST_Y + LAST_S + 16)
		return HH_GALLERY;
	dx = x - QR_CX;
	dy = y - QR_CY;
	if (dx * dx + dy * dy <= (QR_R + 10) * (QR_R + 10) || (dx > -QR_R - 10 && dx < QR_R + 10 && dy > 0 && dy < QR_R + 22))
		return HH_SHARE;
	return -1;
}

// A side button: a little D-pad with its arm lit (up or down), then what
// it does.
static void sideButton(u16 *fb, int y, bool up, const char *label, bool pressed)
{
	drawRoundRect(fb, 320, SIDE_X, y, SIDE_W, SIDE_H, 12, pressed ? C_STROKE : C_SURFACE2, 255);
	int cx = SIDE_X + 15, cy = y + SIDE_H / 2, a = 5; // arm size
	drawFill(fb, 320, cx - a / 2, cy - a / 2 - a, a, 3 * a, C_DIM, 255);
	drawFill(fb, 320, cx - a / 2 - a, cy - a / 2, 3 * a, a, C_DIM, 255);
	drawFill(fb, 320, cx - a / 2, up ? cy - a / 2 - a : cy + a / 2 + 1, a, a, C_WHITE, 255);
	drawText(fb, 320, &fontSmall, SIDE_X + 28, cy - lineH(&fontSmall) / 2, C_TEXT, 255, label);
}

// The share button's picture: a little QR code, drawn on whole pixels so
// it stays crisp: three finder squares and a fixed pattern in between.
static void qrIcon(u16 *fb, int cx, int cy, u16 color)
{
	const int n = 13, m = 2, x0 = cx - n * m / 2, y0 = cy - n * m / 2;
	for (int y = 0; y < n; y++)
		for (int x = 0; x < n; x++)
		{
			bool on;
			int fx = x < 8 ? x : x - 8, fy = y < 8 ? y : y - 8;
			bool finder = (x < 5 || x >= 8) && (y < 5 || y >= 8) && !(x >= 8 && y >= 8);
			if (finder)
				on = fx == 0 || fx == 4 || fy == 0 || fy == 4 || (fx == 2 && fy == 2);
			else if ((x <= 5 && y <= 5) || (x >= 7 && y <= 5) || (x <= 5 && y >= 7))
				on = false; // the quiet space around a finder
			else
				on = (x * 7 + y * 13 + x * y) % 5 < 2;
			if (on)
				drawFill(fb, 320, x0 + x * m, y0 + y * m, m, m, color, 255);
		}
}

void homeBottom(App *a, u16 *fb)
{
	background(fb);
	const Settings *st = &a->settings;
	int pressed = a->touched ? uiHomeAt(a->touchX, a->touchY) : since(a, a->keyHitAt) < 160 ? a->keyHit : -1;

	// Battery: one LED, green, orange or red; a ring around it while charging.
	if (a->battery >= 0)
	{
		u16 led = a->battery > 50 ? COL565(70, 210, 110) : a->battery > 20 ? COL565(255, 160, 50) : C_RED;
		drawCircle(fb, 320, 12, HEADER_CY, 4, led, 255);
		if (a->charging)
			drawRing(fb, 320, 12, HEADER_CY, 6.5f, 1.2f, led, 200);
	}
	headerButtons(a, fb);

	// The filter: a card with its name (it slides in when it changes), and
	// beside it what ▲ and ▼ do. Y or a tap opens all of them.
	drawRoundRect(fb, 320, CARD_FX, CARD_FY, CARD_FW, CARD_FH, 18, pressed == HH_FILTER ? C_SURFACE2 : C_SURFACE, 255);
	float bx = CARD_FX + 32, by = CARD_FY + CARD_FH / 2.0f;
	drawCircleGrad(fb, 320, bx, by, 22, C_ACC0, C_ACC1, 255);
	drawIcon(fb, 320, ICON_SPARKLE, bx, by, 26, C_WHITE, 255);
	u64 t = since(a, a->filterAt);
	int slide = t < 220 ? (int)((220 - t) * (220 - t) * 24 / (220 * 220)) * a->filterDir : 0;
	int alpha = t < 220 ? 80 + (int)(t * 175 / 220) : 255;
	int tx = CARD_FX + 64;
	drawText(fb, 320, &fontSmall, tx, CARD_FY + 9, C_DIM, 255,
	         st->filter && st->strength < 100 ? tfmt("Filter · %d%%", st->strength) : "Filter");
	drawText(fb, 320, &fontTitle, tx + slide, CARD_FY + 22, C_TEXT, alpha, fitText(&fontTitle, filterName(st->filter), CARD_FX + CARD_FW - tx - 10));
	drawHint(fb, 320, tx, CARD_FY + CARD_FH - 20, "Y", "All filters", C_ACC1, 255);
	sideButton(fb, CARD_FY, true, "Shuffle", pressed == HH_SHUFFLE);
	sideButton(fb, CARD_FY + CARD_FH - SIDE_H, false, "Original", pressed == HH_ORIGINAL);

	// The newest shot, leading to all of them; or an empty frame.
	if (a->shotCount)
	{
		thumb(a, fb, 0, LAST_X, LAST_Y, LAST_S, LAST_S);
		drawRoundRectOutline(fb, 320, LAST_X - 2, LAST_Y - 2, LAST_S + 4, LAST_S + 4, 9, 2, pressed == HH_GALLERY ? C_ACC1 : C_SURFACE2, 255);
		const char *n = tfmt("%d", a->shotCount);
		int nw = textWidth(&fontSmall, n) + 10;
		drawRoundRect(fb, 320, LAST_X + LAST_S - nw - 3, LAST_Y + LAST_S - 17, nw, 14, 7, C_BLACK, 170);
		drawTextCentered(fb, 320, &fontSmall, LAST_X + LAST_S - 3 - nw / 2, LAST_Y + LAST_S - 17 + (14 - lineH(&fontSmall)) / 2, C_WHITE, 255, n);
	}
	else
		drawRoundRectOutline(fb, 320, LAST_X, LAST_Y, LAST_S, LAST_S, 9, 2, C_SURFACE2, 255);
	const char *warn = a->freeBytes < MIN_FREE ? "SD full" : a->freeBytes < LOW_FREE ? "SD low" : a->exporting ? "Saving …" : NULL;
	drawTextCentered(fb, 320, &fontSmall, LAST_X + LAST_S / 2, LAST_Y + LAST_S + 4, a->freeBytes < LOW_FREE ? C_RED : C_DIM, 255,
	                 warn ? warn : "Gallery");

	// The shutter: a white disc in a ring; it dips when pressed.
	u64 s = since(a, a->shutterAt);
	float dip = pressed == HH_SHUTTER ? 3 : s < 200 ? 4 * (1 - s / 200.0f) : 0;
	drawCircleGrad(fb, 320, SHUTTER_CX, SHUTTER_CY, SHUTTER_R, C_ACC0, C_ACC1, 255);
	drawCircle(fb, 320, SHUTTER_CX, SHUTTER_CY, SHUTTER_R - 5, C_BG_BOTTOM, 255);
	drawCircle(fb, 320, SHUTTER_CX, SHUTTER_CY, SHUTTER_R - 8 - dip, C_WHITE, 255);
	if (a->timerAt)
		drawTextCentered(fb, 320, &fontTitle, SHUTTER_CX, SHUTTER_CY - lineH(&fontTitle) / 2, C_BG_BOTTOM, 255,
		                 tfmt("%d", a->timerAt > a->now ? (int)((a->timerAt - a->now + 999) / 1000) : 0));
	else if (a->haveFirst) // the second exposure is next
		drawTextCentered(fb, 320, &fontBody, SHUTTER_CX, SHUTTER_CY - lineH(&fontBody) / 2, C_BG_BOTTOM, 255, "2nd");

	// Share to the phone: a QR code, since that is how it works.
	drawCircle(fb, 320, QR_CX, QR_CY, QR_R, pressed == HH_SHARE ? C_SURFACE2 : C_SURFACE, 255);
	drawRing(fb, 320, QR_CX, QR_CY, QR_R, 2, pressed == HH_SHARE ? C_ACC1 : C_STROKE, 255);
	qrIcon(fb, QR_CX, QR_CY, C_TEXT);
	drawTextCentered(fb, 320, &fontSmall, QR_CX, QR_CY + QR_R + 4, C_DIM, 255, "Share");

	if (a->filterMenu)
		filterMenu(a, fb);
}
