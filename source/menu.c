#include "menu.h"
#include "audio.h"
#include "cosmetics.h"
#include "map.h"
#include "ui.h"
#include "weapons.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// ------------------------------------------------------------------ layout
#define LIST_X        10
#define LIST_W        282
#define ROW_H         24
#define ROWS_VISIBLE  6
#define ARROW_X       296
#define VAL_CENTER    222
#define VAL_LEFT_X    158
#define VAL_RIGHT_X   286
#define FOOT_Y        207
#define FOOT_H        28
#define MAX_ITEMS     14
#define TAB_Y         38
#define TAB_H         20

enum { MS_MAIN, MS_LOBBY, MS_WEAPONS, MS_MAPS, MS_SETTINGS, MS_STATS, MS_CREDITS, MS_CUSTOM,
       MS_MP, MS_JOIN, MS_ROOM, MS_PAUSE, MS_POST, MS_COUNT };

typedef enum { IT_BUTTON, IT_CHOICE, IT_NUMBER, IT_TOGGLE } ItemType;
enum { F_TEAMMODE = 1, F_NOTPRACTICE = 2, F_NOTMP = 4 };

enum {
	I_PLAY, I_MULTI, I_LOBBY, I_WEAPONS, I_MAPS, I_SETTINGS, I_STATS, I_CUSTOM, I_CREDITS, I_EXIT,
	I_L_MODE, I_L_MAP, I_L_TEAM, I_L_DIFF, I_L_TOTAL, I_L_RED, I_L_BLUE, I_L_BALANCE,
	I_L_DURATION, I_L_SCORE, I_L_WEAPON, I_L_WEAPON2,
	I_S_SENS, I_S_INVERT, I_S_TOUCH, I_S_SWAP, I_S_MUSIC, I_S_SFX,
	I_C_BASE,              // I_C_BASE + category
	I_W_BASE = 100,        // I_W_BASE + weapon id
	I_M_BASE = 200,        // I_M_BASE + map id
	I_P_RESUME = 300, I_P_RESPAWN, I_P_SETTINGS, I_P_QUIT,
	I_R_REMATCH, I_R_MENU,
	I_MP_HOST, I_MP_JOIN
};

typedef struct {
	const char* label;
	ItemType type;
	int* value;
	int min, max;
	int (*maxFn)(void);
	const char* const* names;
	void (*fmt)(char* out, int n, int v);
	int id;
	int flags;
	int page;              // 0 = always, 1 / 2 = first / second lobby page
	const char* help;
} Item;

typedef struct {
	Item items[MAX_ITEMS];
	int count;
	int focus, scroll;
} Screen;

typedef struct { int kind; int id; } Evt;   // kind: 0 none, 1 changed, 2 activated

// ------------------------------------------------------------------- state
static PlayerProfile* gProf;
static MatchConfig* gCfg;
static Screen scr[MS_COUNT];
static int cur = MS_MAIN;
static int settingsReturn = MS_MAIN;
static int lobbyPage;     // 0 = match rules, 1 = bots
static bool boughtFlag;   // suppress the generic select sound after a purchase
static float menuTime;
static char toast[48];
static float toastT;
static bool heldNow;      // finger on the touch screen (for the pressed-button look)
static int heldX, heldY;

static int cosPrev[COS_CATEGORIES];       // item being previewed in Customize
static Match postMatch;
static int postCoins;
static bool postWon;
static ScoreRow postRows[16];
static int postRowCount;

// ---- multiplayer
static Np* gNp;
static NpRoomInfo roomList[8];
static int roomCount, roomSel;
static bool roomsSearching;

static Map previewMap;
static int previewId = -1;

// --------------------------------------------------------------- name lists
static const char* const modeShort[MODE_COUNT] = { "FFA", "TDM", "CTF", "KotP", "Elim", "Practice" };
static const char* const mapShort[MAP_COUNT]   = { "Eggworks", "Sunny Side", "Canyon", "Pan Palace", "Fridge", "Diner", "Henhouse" };
static const char* const teamNames[2]  = { "Red", "Blue" };
static const char* const diffNames[3]  = { "Easy", "Normal", "Hard" };
static const char* const ctrlNames[2]  = { "R fire / L aim", "L fire / R aim" };

static void toastMsg(const char* s)
{
	strncpy(toast, s, sizeof(toast) - 1);
	toast[sizeof(toast) - 1] = '\0';
	toastT = 2.5f;
}

static bool npHost(void) { return gNp && (gNp->state == NP_HOST_ROOM || gNp->state == NP_HOST_MATCH); }
static bool npActive(void) { return gNp && gNp->state != NP_OFF; }

static const char* weaponName(int i) { return weapon_def(i)->name; }

// ----------------------------------------------------------------- formats
static void fmtMode(char* o, int n, int v)  { snprintf(o, n, "%s%s", modeShort[v], mode_available(v) ? "" : " *"); }
static void fmtMap(char* o, int n, int v)   { snprintf(o, n, "%s%s", mapShort[v], map_implemented(v) ? "" : " *"); }
static void fmtDur(char* o, int n, int v)   { snprintf(o, n, "%d min", match_duration_minutes_at(v)); }
static void fmtScore(char* o, int n, int v) { snprintf(o, n, "%d %s", match_score_at(gCfg->mode, v), match_score_unit(gCfg->mode)); }
static void fmtVol(char* o, int n, int v)   { snprintf(o, n, "%d", v); }
static void fmtWeaponFav(char* o, int n, int v) { snprintf(o, n, "%s", weaponName(v)); }
static void fmtWeaponRow(char* o, int n, int id)
{
	int w = id - I_W_BASE;
	snprintf(o, n, "%s", w == gProf->favoriteWeapon ? "PRIMARY" : (w == W_EGGSTOL ? "SECONDARY" : ""));
}
static void fmtMapRow(char* o, int n, int id) { int m = id - I_M_BASE; snprintf(o, n, "%s", !map_implemented(m) ? "coming soon" : (m == gCfg->map ? "SELECTED" : "")); }

static void fmtCos(char* o, int n, int cat)
{
	int idx = cosPrev[cat];
	const char* name = cos_name(cat, idx);
	if (gProf->cos.sel[cat] == idx) snprintf(o, n, "%s *", name);
	else if (cos_unlocked(&gProf->cos, cat, idx)) snprintf(o, n, "%s", name);
	else snprintf(o, n, "%s (%dc)", name, cos_price(cat, idx));
}
static void fmtCosShell(char* o, int n, int v) { (void)v; fmtCos(o, n, COS_SHELL); }
static void fmtCosHat(char* o, int n, int v)   { (void)v; fmtCos(o, n, COS_HAT); }
static void fmtCosAcc(char* o, int n, int v)   { (void)v; fmtCos(o, n, COS_ACC); }
static void fmtCosSkin(char* o, int n, int v)  { (void)v; fmtCos(o, n, COS_SKIN); }

static int maxTotalBots(void) { return gCfg->mode == MODE_PRACTICE ? 7 : MAX_BOTS_TOTAL; }
static int maxMode(void) { return npHost() ? MODE_PRACTICE - 1 : MODE_COUNT - 1; }   // no Practice in multiplayer

// -------------------------------------------------------------- item setup
static void addItem(int s, Item it)
{
	Screen* S = &scr[s];
	if (S->count < MAX_ITEMS) S->items[S->count++] = it;
}

static Item button(const char* label, int id, const char* help)
{
	Item it;
	memset(&it, 0, sizeof(it));
	it.label = label; it.type = IT_BUTTON; it.id = id; it.help = help;
	return it;
}

static Item choice(const char* label, int* v, int mn, int mx, const char* const* names,
                   void (*fmt)(char*, int, int), int id, int flags, const char* help)
{
	Item it;
	memset(&it, 0, sizeof(it));
	it.label = label; it.type = IT_CHOICE; it.value = v; it.min = mn; it.max = mx;
	it.names = names; it.fmt = fmt; it.id = id; it.flags = flags; it.help = help;
	return it;
}

static Item number(const char* label, int* v, int mn, int mx, int (*maxFn)(void),
                   void (*fmt)(char*, int, int), int id, int flags, const char* help)
{
	Item it;
	memset(&it, 0, sizeof(it));
	it.label = label; it.type = IT_NUMBER; it.value = v; it.min = mn; it.max = mx;
	it.maxFn = maxFn; it.fmt = fmt; it.id = id; it.flags = flags; it.help = help;
	return it;
}

static Item toggle(const char* label, int* v, int id, int flags, const char* help)
{
	Item it;
	memset(&it, 0, sizeof(it));
	it.label = label; it.type = IT_TOGGLE; it.value = v; it.min = 0; it.max = 1;
	it.id = id; it.flags = flags; it.help = help;
	return it;
}

static Item onPage(Item it, int page) { it.page = page; return it; }

void menu_init(PlayerProfile* prof)
{
	gProf = prof;
	gCfg = &prof->lobby;
	memset(scr, 0, sizeof(scr));

	// Main menu: big Play, a 3 x 2 grid, then Credits and Exit
	addItem(MS_MAIN, button("Play", I_PLAY, "Start a match with your lobby settings"));
	addItem(MS_MAIN, button("Multiplayer", I_MULTI, "Play with friends over local wireless"));
	addItem(MS_MAIN, button("Lobby", I_LOBBY, "Choose mode, map, bots and rules"));
	addItem(MS_MAIN, button("Weapons", I_WEAPONS, "Browse weapons and pick your starter"));
	addItem(MS_MAIN, button("Maps", I_MAPS, "Preview the arenas"));
	addItem(MS_MAIN, button("Customize", I_CUSTOM, "Shells, hats, accessories and weapon skins"));
	addItem(MS_MAIN, button("Settings", I_SETTINGS, "Sensitivity, controls and volume"));
	addItem(MS_MAIN, button("Stats", I_STATS, "Your career totals"));
	addItem(MS_MAIN, button("Credits", I_CREDITS, "Who made this"));
	addItem(MS_MAIN, button("Exit", I_EXIT, "Close the game"));

	// Bot lobby, page 1: rules
	{
		Item modeItem = onPage(choice("Mode", &gCfg->mode, 0, MODE_COUNT - 1, NULL, fmtMode, I_L_MODE, 0, "* modes are not playable yet"), 1);
		modeItem.maxFn = maxMode;
		addItem(MS_LOBBY, modeItem);
	}
	addItem(MS_LOBBY, onPage(choice("Map", &gCfg->map, 0, MAP_COUNT - 1, NULL, fmtMap, I_L_MAP, 0, "* maps are not built yet"), 1));
	addItem(MS_LOBBY, onPage(choice("My team", &gCfg->team, 0, 1, teamNames, NULL, I_L_TEAM, F_TEAMMODE, "Which side you fight for"), 1));
	addItem(MS_LOBBY, onPage(choice("Match time", &gCfg->durationIdx, 0, DURATION_COUNT - 1, NULL, fmtDur, I_L_DURATION, F_NOTPRACTICE, "Match length"), 1));
	addItem(MS_LOBBY, onPage(choice("Score limit", &gCfg->scoreIdx, 0, SCORE_COUNT - 1, NULL, fmtScore, I_L_SCORE, F_NOTPRACTICE, "First to the limit wins"), 1));
	addItem(MS_LOBBY, onPage(choice("Primary", &gProf->favoriteWeapon, 0, PRIMARY_COUNT - 1, NULL, fmtWeaponFav, I_L_WEAPON, 0, "Your main weapon. The Eggstol is always your secondary"), 1));
	// page 2: bots
	addItem(MS_LOBBY, onPage(choice("Difficulty", &gCfg->difficulty, 0, 2, diffNames, NULL, I_L_DIFF, 0, "Smarter bots, never cheating bots"), 2));
	addItem(MS_LOBBY, onPage(number("Total bots", &gCfg->totalBots, 0, MAX_BOTS_TOTAL, maxTotalBots, NULL, I_L_TOTAL, 0, "How many bots are in the match"), 2));
	addItem(MS_LOBBY, onPage(number("Red bots", &gCfg->redBots, 0, MAX_BOTS_PER_TEAM, NULL, NULL, I_L_RED, F_TEAMMODE, "0 to 7 bots on the red team"), 2));
	addItem(MS_LOBBY, onPage(number("Blue bots", &gCfg->blueBots, 0, MAX_BOTS_PER_TEAM, NULL, NULL, I_L_BLUE, F_TEAMMODE, "0 to 7 bots on the blue team"), 2));
	addItem(MS_LOBBY, onPage(toggle("Balance teams", &gCfg->balance, I_L_BALANCE, F_TEAMMODE, "Keep both teams the same size"), 2));

	// Settings
	addItem(MS_SETTINGS, number("Look sensitivity", &gProf->settings.lookSens, 1, 10, NULL, fmtVol, I_S_SENS, 0, "C-Stick and touch look speed"));
	addItem(MS_SETTINGS, toggle("Invert Y", &gProf->settings.invertY, I_S_INVERT, 0, "Flip vertical look"));
	addItem(MS_SETTINGS, toggle("Touch camera", &gProf->settings.touchLook, I_S_TOUCH, 0, "Drag the bottom screen to look (original 3DS)"));
	addItem(MS_SETTINGS, choice("Controls", &gProf->settings.swapTriggers, 0, 1, ctrlNames, NULL, I_S_SWAP, 0, "Swap the fire and aim triggers"));
	addItem(MS_SETTINGS, number("Sound volume", &gProf->settings.sfxVol, 0, 10, NULL, fmtVol, I_S_SFX, 0, "Volume of all sound effects"));

	// Weapons
	for (int w = 0; w < WEAPON_COUNT; w++) {
		Item it = button(weaponName(w), I_W_BASE + w, w == W_EGGSTOL ? "Your secondary: it is always the Eggstol" : "A: set as your primary weapon");
		it.fmt = fmtWeaponRow;
		it.value = NULL;
		addItem(MS_WEAPONS, it);
	}

	// Maps
	for (int m = 0; m < MAP_COUNT; m++) {
		Item it = button(map_name(m), I_M_BASE + m, "A: use this map in the lobby");
		it.fmt = fmtMapRow;
		addItem(MS_MAPS, it);
	}

	// Customize
	addItem(MS_CUSTOM, choice("Shell", &cosPrev[COS_SHELL], 0, cos_count(COS_SHELL) - 1, NULL, fmtCosShell, I_C_BASE + COS_SHELL, 0, "A: equip or buy"));
	addItem(MS_CUSTOM, choice("Hat", &cosPrev[COS_HAT], 0, cos_count(COS_HAT) - 1, NULL, fmtCosHat, I_C_BASE + COS_HAT, 0, "A: equip or buy"));
	addItem(MS_CUSTOM, choice("Accessory", &cosPrev[COS_ACC], 0, cos_count(COS_ACC) - 1, NULL, fmtCosAcc, I_C_BASE + COS_ACC, 0, "A: equip or buy"));
	addItem(MS_CUSTOM, choice("Weapon skin", &cosPrev[COS_SKIN], 0, cos_count(COS_SKIN) - 1, NULL, fmtCosSkin, I_C_BASE + COS_SKIN, 0, "A: equip or buy"));

	// Pause and post-match
	addItem(MS_PAUSE, button("Resume", I_P_RESUME, ""));
	{
		Item respawn = button("Respawn", I_P_RESPAWN, "Leave in 2 seconds, change weapon, no death");
		respawn.flags = F_NOTMP;
		addItem(MS_PAUSE, respawn);
	}
	addItem(MS_PAUSE, button("Settings", I_P_SETTINGS, ""));
	addItem(MS_PAUSE, button("Quit to menu", I_P_QUIT, ""));
	addItem(MS_MP, button("Host a game", I_MP_HOST, "Open a room. Friends nearby can join you"));
	addItem(MS_MP, button("Join a game", I_MP_JOIN, "Look for a room a friend has opened"));
	addItem(MS_POST, button("Rematch", I_R_REMATCH, ""));
	addItem(MS_POST, button("Back to menu", I_R_MENU, ""));

	cur = MS_MAIN;
	lobbyPage = 0;
	menuTime = 0.0f;
	toastT = 0.0f;
}

static void syncCosPreview(void)
{
	for (int c = 0; c < COS_CATEGORIES; c++) cosPrev[c] = gProf->cos.sel[c];
}

static void enter(int s)
{
	cur = s;
	if (s == MS_CUSTOM) syncCosPreview();
	if (s == MS_MAPS) scr[s].focus = gCfg->map;
	if (s == MS_WEAPONS) scr[s].focus = gProf->favoriteWeapon;
	if (s == MS_LOBBY) { lobbyPage = 0; scr[s].focus = 0; scr[s].scroll = 0; }
	if (s == MS_PAUSE || s == MS_POST || s == MS_MP) { scr[s].focus = 0; scr[s].scroll = 0; }
	if (s == MS_JOIN) { roomCount = 0; roomSel = 0; roomsSearching = true; }
}

static void setItemLabel(int s, int id, const char* label)
{
	for (int i = 0; i < scr[s].count; i++) if (scr[s].items[i].id == id) scr[s].items[i].label = label;
}

void menu_set_net(Np* np) { gNp = np; }
bool menu_multiplayer(void) { return npActive(); }
void menu_show_mp(void) { enter(MS_MP); }
void menu_show_room(void) { enter(MS_ROOM); }
void menu_set_rooms(const NpRoomInfo* rooms, int count)
{
	roomCount = count > 5 ? 5 : count;
	for (int i = 0; i < roomCount; i++) roomList[i] = rooms[i];
	if (roomSel >= roomCount) roomSel = roomCount > 0 ? roomCount - 1 : 0;
	roomsSearching = false;
}
int menu_selected_room(void) { return roomSel; }

void menu_notify(const char* msg) { toastMsg(msg); }

void menu_show_main(void)  { enter(MS_MAIN); }
void menu_show_pause(void)
{
	setItemLabel(MS_PAUSE, I_P_QUIT, npActive() ? "Leave match" : "Quit to menu");
	enter(MS_PAUSE);
}

void menu_show_post(const Match* m, int coinsEarned, bool won, const ScoreRow* rows, int rowCount)
{
	postRowCount = rowCount > 16 ? 16 : rowCount;
	for (int i = 0; i < postRowCount; i++) postRows[i] = rows[i];
	postMatch = *m;
	postCoins = coinsEarned;
	postWon = won;

	setItemLabel(MS_POST, I_R_REMATCH, npActive() ? "Back to room" : "Rematch");
	setItemLabel(MS_POST, I_R_MENU, npActive() ? "Leave room" : "Back to menu");
	enter(MS_POST);
}

// ------------------------------------------------------------- list engine
static bool itemVisible(const Item* it)
{
	bool team = mode_is_team(gCfg->mode);
	if ((it->flags & F_TEAMMODE) && !team) return false;
	if ((it->flags & F_NOTPRACTICE) && gCfg->mode == MODE_PRACTICE) return false;
	if ((it->flags & F_NOTMP) && npActive()) return false;
	if (it->page && it->page != lobbyPage + 1) return false;
	return true;
}

static int visibleItems(const Screen* S, int* vis)
{
	int n = 0;
	for (int i = 0; i < S->count; i++)
		if (itemVisible(&S->items[i])) vis[n++] = i;
	return n;
}

static bool adjust(Item* it, int dir, bool wrap)
{
	int mx = it->maxFn ? it->maxFn() : it->max;
	int v = *it->value, old = v;
	if (it->type == IT_TOGGLE) {
		v = !v;
	} else {
		v += dir;
		if (v > mx) v = wrap ? it->min : mx;
		if (v < it->min) v = wrap ? mx : it->min;
	}
	*it->value = v;
	return v != old;
}

// Screens whose buttons are placed freely (not a scrolling list of rows)
static bool freeLayout(int s) { return s == MS_MAIN || s == MS_PAUSE || s == MS_POST || s == MS_MP; }

static float listTop(int s) { return s == MS_LOBBY ? 60.0f : 40.0f; }

// Rectangle of the idx-th visible item; false when it is scrolled out of view
static bool itemRect(int s, int idx, int scroll, float* x, float* y, float* w, float* h)
{
	if (s == MS_MAIN) {
		if (idx == 0) { *x = 12; *y = 40; *w = 190; *h = 52; }
		else if (idx == 1) { *x = 208; *y = 40; *w = 100; *h = 52; }
		else if (idx <= 7) { int c = (idx - 2) % 3, r = (idx - 2) / 3; *x = 12.0f + c * 101.0f; *y = 100.0f + r * 50.0f; *w = 94; *h = 44; }
		else if (idx == 8) { *x = 12; *y = 204; *w = 100; *h = 28; }
		else { *x = 208; *y = 204; *w = 100; *h = 28; }
		return true;
	}
	if (s == MS_MP) { *x = 30; *y = 44.0f + idx * 62.0f; *w = 260; *h = 54; return true; }
	if (s == MS_PAUSE) { *x = 40; *y = 42.0f + idx * 48.0f; *w = 240; *h = 40; return true; }
	if (s == MS_POST)  { *x = 40; *y = 150.0f + idx * 44.0f; *w = 240; *h = 38; return true; }
	int r = idx - scroll;
	if (r < 0 || r >= ROWS_VISIBLE) return false;
	*x = LIST_X; *y = listTop(s) + r * ROW_H; *w = LIST_W; *h = ROW_H - 2;
	return true;
}

// Move focus to the nearest button in a direction (0 up, 1 down, 2 left, 3 right)
static int geomNav(int s, int count, int focus, int dir)
{
	float fx, fy, fw, fh;
	itemRect(s, focus, 0, &fx, &fy, &fw, &fh);
	float cx = fx + fw * 0.5f, cy = fy + fh * 0.5f;
	int best = focus;
	float bestScore = 1e9f;
	for (int j = 0; j < count; j++) {
		if (j == focus) continue;
		float x, y, w, h;
		itemRect(s, j, 0, &x, &y, &w, &h);
		float dx = x + w * 0.5f - cx, dy = y + h * 0.5f - cy;
		float along = dir == 0 ? -dy : dir == 1 ? dy : dir == 2 ? -dx : dx;
		float perp = (dir < 2) ? fabsf(dx) : fabsf(dy);
		if (along < 1.0f) continue;
		float score = along + perp * 2.0f;
		if (score < bestScore) { bestScore = score; best = j; }
	}
	return best;
}

static Evt listUpdate(Screen* S, const UiInput* in, int s)
{
	Evt ev = { 0, 0 };
	int vis[MAX_ITEMS];
	int nv = visibleItems(S, vis);
	if (nv == 0) return ev;
	if (S->focus >= nv) S->focus = nv - 1;
	bool free = freeLayout(s);

	if (free) {
		if (in->up)    S->focus = geomNav(s, nv, S->focus, 0);
		if (in->down)  S->focus = geomNav(s, nv, S->focus, 1);
		if (in->left)  S->focus = geomNav(s, nv, S->focus, 2);
		if (in->right) S->focus = geomNav(s, nv, S->focus, 3);
	} else {
		if (in->up && S->focus > 0) S->focus--;
		if (in->down && S->focus < nv - 1) S->focus++;
		if (S->focus < S->scroll) S->scroll = S->focus;
		if (S->focus >= S->scroll + ROWS_VISIBLE) S->scroll = S->focus - ROWS_VISIBLE + 1;
	}

	Item* it = &S->items[vis[S->focus]];
	if (!free && it->type != IT_BUTTON && (in->left || in->right)) {
		if (adjust(it, in->left ? -1 : 1, false)) { ev.kind = 1; ev.id = it->id; }
	}
	if (in->accept) {
		if (it->type == IT_BUTTON) { ev.kind = 2; ev.id = it->id; }
		else if (it->type == IT_TOGGLE) { adjust(it, 1, true); ev.kind = 1; ev.id = it->id; }
		else if (it->id >= I_C_BASE && it->id < I_C_BASE + COS_CATEGORIES) { ev.kind = 2; ev.id = it->id; }
		else { adjust(it, 1, true); ev.kind = 1; ev.id = it->id; }
	}

	if (in->touchPressed) {
		if (free) {
			for (int i = 0; i < nv; i++) {
				float x, y, w, h;
				itemRect(s, i, 0, &x, &y, &w, &h);
				if (ui_hit(x, y, w, h, in->tx, in->ty)) {
					S->focus = i;
					ev.kind = 2; ev.id = S->items[vis[i]].id;
				}
			}
		} else {
			float top = listTop(s);
			if (nv > ROWS_VISIBLE && in->tx >= ARROW_X) {
				if (in->ty >= top && in->ty < top + 72 && S->scroll > 0) S->scroll--;
				else if (in->ty >= top + 72 && in->ty < top + 144 && S->scroll + ROWS_VISIBLE < nv) S->scroll++;
				if (S->focus < S->scroll) S->focus = S->scroll;
				if (S->focus >= S->scroll + ROWS_VISIBLE) S->focus = S->scroll + ROWS_VISIBLE - 1;
			}
			for (int r = 0; r < ROWS_VISIBLE; r++) {
				int idx = S->scroll + r;
				if (idx >= nv) break;
				float x, y, w, h;
				itemRect(s, idx, S->scroll, &x, &y, &w, &h);
				if (!ui_hit(x, y, w, h, in->tx, in->ty)) continue;
				Item* t = &S->items[vis[idx]];
				S->focus = idx;
				if (t->type == IT_BUTTON) { ev.kind = 2; ev.id = t->id; }
				else if (t->type == IT_TOGGLE) { adjust(t, 1, true); ev.kind = 1; ev.id = t->id; }
				else if (in->tx >= VAL_LEFT_X - 20 && in->tx < VAL_LEFT_X + 30) {
					if (adjust(t, -1, false)) { ev.kind = 1; ev.id = t->id; }
				} else if (in->tx >= VAL_RIGHT_X - 30 && in->tx < VAL_RIGHT_X + 8) {
					if (adjust(t, 1, false)) { ev.kind = 1; ev.id = t->id; }
				} else if (t->id >= I_C_BASE && t->id < I_C_BASE + COS_CATEGORIES) {
					ev.kind = 2; ev.id = t->id;
				}
			}
		}
	}
	return ev;
}

// ------------------------------------------------------------ item events
static void onChanged(int id)
{
	switch (id) {
	case I_L_MODE:
		match_sanitize(gCfg);
		if (mode_is_team(gCfg->mode)) { gCfg->totalBots = gCfg->redBots + gCfg->blueBots; }
		break;
	case I_L_TOTAL:
		if (mode_is_team(gCfg->mode)) match_apply_total(gCfg);
		break;
	case I_L_TEAM:
	case I_L_BALANCE:
		if (gCfg->balance) match_apply_balance(gCfg);
		break;
	case I_L_RED:
	case I_L_BLUE:
		gCfg->balance = 0;
		gCfg->totalBots = gCfg->redBots + gCfg->blueBots;
		break;
	case I_S_SENS: case I_S_INVERT: case I_S_TOUCH: case I_S_SWAP: case I_S_MUSIC: case I_S_SFX:
		settings_clamp(&gProf->settings);
		break;
	default: break;
	}
	if (id >= I_L_MODE && id <= I_L_WEAPON2) match_sanitize(gCfg);
}

// Host presses START in the waiting room
static MenuAction mpTryStart(void)
{
	if (!mode_available(gCfg->mode)) { toastMsg("That mode is not playable yet"); return MENU_NONE; }
	if (!map_implemented(gCfg->map)) { toastMsg("That map is not built yet"); return MENU_NONE; }
	match_sanitize(gCfg);
	if (gCfg->mode == MODE_ELIM) {                    // someone has to be on the other side to eliminate
		int redH = 0, blueH = 0;
		for (int i = 0; i < np_human_count(gNp); i++) { const NpHuman* h = np_human(gNp, i); if (h->team == TEAM_RED) redH++; else blueH++; }
		int red = gCfg->redBots + (gCfg->team == TEAM_RED ? 1 : 0), blue = gCfg->blueBots + (gCfg->team == TEAM_BLUE ? 1 : 0);
		if (red < redH) red = redH;
		if (blue < blueH) blue = blueH;
		if (red < 1 || blue < 1) { toastMsg("Team Elimination needs both teams"); return MENU_NONE; }
	}
	return MENU_MP_START;
}

static MenuAction tryStart(void)
{
	if (npHost()) return mpTryStart();
	if (!mode_available(gCfg->mode)) { toastMsg("That mode is not playable yet"); return MENU_NONE; }
	if (!map_implemented(gCfg->map)) { toastMsg("That map is not built yet"); return MENU_NONE; }
	match_sanitize(gCfg);
	if (gCfg->mode == MODE_ELIM) {                    // someone has to be on the other side to eliminate
		int red = gCfg->redBots + (gCfg->team == TEAM_RED ? 1 : 0);
		int blue = gCfg->blueBots + (gCfg->team == TEAM_BLUE ? 1 : 0);
		if (red < 1 || blue < 1) { toastMsg("Team Elimination needs both teams"); return MENU_NONE; }
	}
	return MENU_START_MATCH;
}

static MenuAction onActivated(int id)
{
	if (id >= I_C_BASE && id < I_C_BASE + COS_CATEGORIES) {
		int cat = id - I_C_BASE;
		int idx = cosPrev[cat];
		CosResult r = cos_try_buy(&gProf->stats, &gProf->cos, cat, idx);
		if (r == COS_NEED_COINS) {
			char msg[48];
			snprintf(msg, sizeof(msg), "Need %d more coins", cos_price(cat, idx) - (int)gProf->stats.coins);
			toastMsg(msg);
			return MENU_NONE;
		}
		gProf->cos.sel[cat] = idx;
		if (r == COS_BOUGHT) { audio_play(SFX_BUY, 0.0f, 1.0f); boughtFlag = true; }
		toastMsg(r == COS_BOUGHT ? "Purchased and equipped!" : "Equipped");
		return MENU_SAVE;
	}
	if (id >= I_W_BASE && id < I_W_BASE + WEAPON_COUNT) {
		if (id - I_W_BASE == W_EGGSTOL) { toastMsg("The Eggstol is always your secondary"); return MENU_NONE; }
		profile_set_weapon(gProf, 0, id - I_W_BASE);
		toastMsg("Primary weapon set");
		return MENU_SAVE;
	}
	if (id >= I_M_BASE && id < I_M_BASE + MAP_COUNT) {
		int m = id - I_M_BASE;
		if (!map_implemented(m)) { toastMsg("That map is not built yet"); return MENU_NONE; }
		gCfg->map = m;
		toastMsg("Map selected");
		return MENU_SAVE;
	}
	switch (id) {
	case I_PLAY:     return tryStart();
	case I_MULTI:    enter(MS_MP); break;
	case I_MP_HOST:  return MENU_MP_HOST;
	case I_MP_JOIN:  enter(MS_JOIN); return MENU_MP_SCAN;
	case I_LOBBY:    enter(MS_LOBBY); break;
	case I_WEAPONS:  enter(MS_WEAPONS); break;
	case I_MAPS:     enter(MS_MAPS); break;
	case I_SETTINGS: settingsReturn = MS_MAIN; enter(MS_SETTINGS); break;
	case I_STATS:    enter(MS_STATS); break;
	case I_CUSTOM:   enter(MS_CUSTOM); break;
	case I_CREDITS:  enter(MS_CREDITS); break;
	case I_EXIT:     return MENU_QUIT_APP;
	case I_P_RESUME: return MENU_RESUME;
	case I_P_RESPAWN: return MENU_RESPAWN;
	case I_P_SETTINGS: settingsReturn = MS_PAUSE; enter(MS_SETTINGS); break;
	case I_P_QUIT:   return MENU_TO_MENU;
	case I_R_REMATCH: return MENU_REMATCH;
	case I_R_MENU:   return MENU_TO_MENU;
	default: break;
	}
	return MENU_NONE;
}

static MenuAction goBack(void)
{
	switch (cur) {
	case MS_MAIN: return MENU_NONE;
	case MS_MP:   enter(MS_MAIN); return MENU_NONE;
	case MS_JOIN: enter(MS_MP); return MENU_NONE;
	case MS_ROOM: return MENU_MP_LEAVE;
	case MS_LOBBY: if (npHost()) { enter(MS_ROOM); return MENU_SAVE; } enter(MS_MAIN); return MENU_SAVE;
	case MS_PAUSE: return MENU_RESUME;
	case MS_POST: return MENU_NONE;
	case MS_SETTINGS: enter(settingsReturn); return MENU_SAVE;
	default: enter(MS_MAIN); return MENU_SAVE;
	}
}

static bool footerBack(void) { return cur != MS_MAIN && cur != MS_PAUSE && cur != MS_POST; }

static void setLobbyPage(int p)
{
	if (p < 0 || p > 1 || p == lobbyPage) return;
	lobbyPage = p;
	scr[MS_LOBBY].focus = 0;
	scr[MS_LOBBY].scroll = 0;
	audio_play(SFX_UI_TAB, 0.0f, 1.0f);
}

// ---- multiplayer screens
#define JOIN_ROW_Y(i) (44.0f + (i) * 32.0f)
#define ROOM_SET_X 20
#define ROOM_SET_Y 150
#define ROOM_SET_W 280
#define ROOM_SET_H 40

static MenuAction joinUpdate(const UiInput* in)
{
	if (in->touchPressed && ui_hit(10, FOOT_Y, 100, FOOT_H, in->tx, in->ty)) {
		audio_play(SFX_MENU_BACK, 0.0f, 1.0f);
		return goBack();
	}
	if (roomsSearching) return MENU_NONE;
	if ((in->start || in->extra) || (in->touchPressed && ui_hit(170, FOOT_Y, 140, FOOT_H, in->tx, in->ty))) {
		roomsSearching = true;
		roomCount = 0;
		audio_play(SFX_MENU_SELECT, 0.0f, 1.0f);
		return MENU_MP_SCAN;
	}
	if (roomCount > 0) {
		if (in->up && roomSel > 0) { roomSel--; audio_play(SFX_MENU_MOVE, 0.0f, 1.0f); }
		if (in->down && roomSel < roomCount - 1) { roomSel++; audio_play(SFX_MENU_MOVE, 0.0f, 1.0f); }
		bool join = in->accept;
		if (in->touchPressed) {
			for (int i = 0; i < roomCount; i++)
				if (ui_hit(10, JOIN_ROW_Y(i), 300, 28, in->tx, in->ty)) { roomSel = i; join = true; }
		}
		if (join) {
			if (roomList[roomSel].inMatch) { toastMsg("That match already started"); return MENU_NONE; }
			if (roomList[roomSel].humans >= NET_MAX_NODES) { toastMsg("That room is full"); return MENU_NONE; }
			audio_play(SFX_UI_START, 0.0f, 1.0f);
			return MENU_MP_JOINROOM;
		}
	}
	return MENU_NONE;
}

static MenuAction roomUpdate(const UiInput* in)
{
	if (!npActive()) return MENU_NONE;               // the game is about to notice and send us away
	if (in->touchPressed && ui_hit(10, FOOT_Y, 100, FOOT_H, in->tx, in->ty)) {
		audio_play(SFX_MENU_BACK, 0.0f, 1.0f);
		return MENU_MP_LEAVE;
	}
	if (!npHost()) return MENU_NONE;
	if (in->accept || (in->touchPressed && ui_hit(ROOM_SET_X, ROOM_SET_Y, ROOM_SET_W, ROOM_SET_H, in->tx, in->ty))) {
		audio_play(SFX_MENU_SELECT, 0.0f, 1.0f);
		enter(MS_LOBBY);
		return MENU_NONE;
	}
	if (in->start || (in->touchPressed && ui_hit(170, FOOT_Y, 140, FOOT_H, in->tx, in->ty))) {
		MenuAction a = mpTryStart();
		audio_play(a == MENU_MP_START ? SFX_UI_START : SFX_UI_TAP, 0.0f, 1.0f);
		return a;
	}
	return MENU_NONE;
}

MenuAction menu_update(const UiInput* in, float dt)
{
	menuTime += dt;
	if (toastT > 0.0f) toastT -= dt;
	heldNow = in->touchHeld;
	heldX = in->tx; heldY = in->ty;

	if (in->back) {
		MenuAction a = goBack();
		if (a != MENU_NONE) { audio_play(SFX_MENU_BACK, 0.0f, 1.0f); return a; }
	}

	if (cur == MS_JOIN) return joinUpdate(in);
	if (cur == MS_ROOM) return roomUpdate(in);

	// Lobby pages: L / R or tap a tab
	if (cur == MS_LOBBY) {
		if (in->pageL) setLobbyPage(0);
		if (in->pageR) setLobbyPage(1);
		if (in->touchPressed && in->ty >= TAB_Y && in->ty < TAB_Y + TAB_H) setLobbyPage(in->tx < 160 ? 0 : 1);
	}

	Screen* S = &scr[cur];
	int screenBefore = cur, focusBefore = S->focus;
	Evt ev = listUpdate(S, in, cur);
	MenuAction act = MENU_NONE;
	if (ev.kind == 1) { onChanged(ev.id); audio_play(SFX_MENU_MOVE, 0.0f, 1.0f); }
	else if (ev.kind == 2) {
		boughtFlag = false;
		act = onActivated(ev.id);
		if (act == MENU_START_MATCH || act == MENU_REMATCH) audio_play(SFX_UI_START, 0.0f, 1.0f);
		else if (!boughtFlag) audio_play(SFX_MENU_SELECT, 0.0f, 1.0f);
	} else if (S->focus != focusBefore && cur == screenBefore) {
		audio_play(SFX_MENU_MOVE, 0.0f, 1.0f);
	}
	if (act != MENU_NONE) return act;

	// Footer buttons
	if (in->touchPressed && footerBack()) {
		if (ui_hit(10, FOOT_Y, 100, FOOT_H, in->tx, in->ty)) {
			audio_play(SFX_MENU_BACK, 0.0f, 1.0f);
			return goBack();
		}
		if (cur == MS_LOBBY && ui_hit(170, FOOT_Y, 140, FOOT_H, in->tx, in->ty)) {
			if (npHost()) { audio_play(SFX_UI_TAP, 0.0f, 1.0f); enter(MS_ROOM); return MENU_SAVE; }
			MenuAction a = tryStart();
			audio_play(a == MENU_START_MATCH ? SFX_UI_START : SFX_UI_TAP, 0.0f, 1.0f);
			return a;
		}
	}
	if (in->start && (cur == MS_LOBBY || cur == MS_MAIN)) {
		if (cur == MS_LOBBY && npHost()) { audio_play(SFX_UI_TAP, 0.0f, 1.0f); enter(MS_ROOM); return MENU_SAVE; }
		MenuAction a = tryStart();
		audio_play(a == MENU_START_MATCH ? SFX_UI_START : SFX_UI_TAP, 0.0f, 1.0f);
		return a;
	}
	return MENU_NONE;
}

// ----------------------------------------------------------------- drawing
// printf-style text without the drop shadow (dark text on light cards)
static void tf(float x, float y, float scale, u32 color, int align, const char* fmt, ...)
{
	char buf[96];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	ui_text(x, y, scale, color, align, buf);
}

static bool pressedRect(float x, float y, float w, float h)
{
	return heldNow && ui_hit(x, y, w, h, heldX, heldY);
}

static void outlineText(float x, float y, float scale, u32 color, int align, const char* str)
{
	u32 ink = T_INK;
	ui_text(x - 2, y, scale, ink, align, str);
	ui_text(x + 2, y, scale, ink, align, str);
	ui_text(x, y - 2, scale, ink, align, str);
	ui_text(x, y + 2, scale, ink, align, str);
	ui_text(x + 2, y + 3, scale, ink, align, str);
	ui_text(x, y, scale, color, align, str);
}

// Wide banner title on the top screen
static void topBanner(const char* text, u32 fill)
{
	float w = ui_text_width(text, 0.8f) + 56.0f;
	if (w < 150.0f) w = 150.0f;
	float x = 200.0f - w * 0.5f;
	ui_round_rect(x, 8, w, 28, 12, UI_COLA(58, 38, 78, 80));
	ui_round_rect(x, 6, w, 28, 12, T_INK);
	ui_round_rect(x + 2, 8, w - 4, 24, 10, ui_shade(fill, 0.75f));
	ui_round_rect(x + 2, 8, w - 4, 21, 10, fill);
	ui_text_shadow(200, 7, 0.8f, UI_COL(255, 255, 255), UI_CENTER, text);
}

static void topHelp(const char* text)
{
	if (!text || !text[0]) return;
	ui_text_shadow(200, 212, 0.45f, UI_COL(255, 255, 255), UI_CENTER, text);
}

static void drawRibbon(const char* title, bool coins)
{
	ui_title_ribbon(title, false, gProf->stats.coins, coins);
	if (toastT > 0.0f) {
		float w = ui_text_width(toast, 0.45f) + 24.0f;
		if (w < 120.0f) w = 120.0f;
		if (w > 300.0f) w = 300.0f;
		float x = 160.0f - w * 0.5f;
		ui_round_rect(x, 4, w, 24, 10, T_INK);
		ui_round_rect(x + 2, 6, w - 4, 20, 8, UI_COL(92, 62, 120));
		ui_text(160, 9, 0.45f, T_YOLK, UI_CENTER, toast);
	}
}

static void drawBackBtn(const char* label)
{
	bool pr = pressedRect(10, FOOT_Y, 100, FOOT_H);
	ui_chunky(10, FOOT_Y, 100, FOOT_H, T_YOLK, false, pr);
	float o = pr ? 2.0f : 0.0f;
	ui_triangle(34, FOOT_Y + 8 + o, 34, FOOT_Y + 20 + o, 24, FOOT_Y + 14 + o, T_INK);
	ui_text(66, FOOT_Y + 5 + o, 0.55f, T_INK, UI_CENTER, label);
}

static void drawStartBtn(bool enabled, const char* label)
{
	bool pr = enabled && pressedRect(170, FOOT_Y, 140, FOOT_H);
	ui_chunky(170, FOOT_Y, 140, FOOT_H, enabled ? T_GRASS : UI_COL(170, 160, 180), false, pr);
	float o = pr ? 2.0f : 0.0f;
	ui_triangle(194, FOOT_Y + 7 + o, 194, FOOT_Y + 21 + o, 206, FOOT_Y + 14 + o, UI_COL(255, 255, 255));
	ui_text_shadow(250, FOOT_Y + 5 + o, 0.55f, UI_COL(255, 255, 255), UI_CENTER, label);
}

static void drawTag(float rx, float y, const char* text, u32 fill)
{
	float w = ui_text_width(text, 0.38f) + 14.0f;
	ui_round_rect(rx - w, y, w, 15, 7, T_INK);
	ui_round_rect(rx - w + 2, y + 2, w - 4, 11, 5, fill);
	ui_text(rx - w * 0.5f, y + 1, 0.38f, UI_COL(255, 255, 255), UI_CENTER, text);
}

static void drawRow(const Item* it, float y, bool focus)
{
	float x = LIST_X, w = LIST_W, h = ROW_H - 2;
	if (focus) ui_round_rect(x - 3, y - 2, w + 6, h + 4, 10, UI_COL(255, 255, 255));
	ui_round_rect(x, y, w, h, 8, T_INK);
	ui_round_rect(x + 2, y + 2, w - 4, h - 4, 6, focus ? T_YOLK : T_CREAM);
	u32 fg = T_INK;

	char val[48] = "";
	if (it->type == IT_TOGGLE) snprintf(val, sizeof(val), "%s", *it->value ? "ON" : "OFF");
	else if (it->type != IT_BUTTON && it->value) {
		int v = *it->value;
		if (it->names) snprintf(val, sizeof(val), "%s", it->names[v]);
		else if (it->fmt) it->fmt(val, sizeof(val), v);
		else snprintf(val, sizeof(val), "%d", v);
	} else if (it->type == IT_BUTTON && it->fmt) {
		it->fmt(val, sizeof(val), it->id);
	}

	float labelX = x + 12;
	if (it->id >= I_W_BASE && it->id < I_W_BASE + WEAPON_COUNT) {          // weapons get a little picture
		ui_weapon_icon(it->id - I_W_BASE, x + 8, y + 2, 40, h - 4);
		labelX = x + 56;
	}

	if (it->type == IT_BUTTON) {
		ui_text(labelX, y + 3, 0.5f, fg, UI_LEFT, it->label);
		if (val[0]) {
			u32 tag = !strcmp(val, "PRIMARY") || !strcmp(val, "SELECTED") ? T_GRASS : (!strcmp(val, "SECONDARY") ? T_ORANGE : UI_COL(160, 150, 176));
			drawTag(x + w - 8, y + 3, val, tag);
		} else {
			ui_triangle(x + w - 16, y + 6, x + w - 16, y + h - 6, x + w - 9, y + h * 0.5f, ui_alpha(T_INK, 150));
		}
		return;
	}
	ui_text(labelX, y + 3, 0.5f, fg, UI_LEFT, it->label);
	if (it->type == IT_TOGGLE) {
		bool on = *it->value != 0;
		float sx = x + w - 54, sy = y + 3;
		ui_round_rect(sx, sy, 42, 16, 8, T_INK);
		ui_round_rect(sx + 2, sy + 2, 38, 12, 6, on ? T_GRASS : UI_COL(190, 180, 200));
		ui_ellipse(sx + (on ? 26 : 2), sy + 1, 14, 14, T_INK);
		ui_ellipse(sx + (on ? 28 : 4), sy + 3, 10, 10, UI_COL(255, 255, 255));
		return;
	}
	float cy = y + h * 0.5f;
	ui_triangle(VAL_LEFT_X + 5, cy - 6, VAL_LEFT_X + 5, cy + 6, VAL_LEFT_X - 3, cy, fg);
	ui_triangle(VAL_RIGHT_X - 5, cy - 6, VAL_RIGHT_X - 5, cy + 6, VAL_RIGHT_X + 3, cy, fg);
	ui_text(VAL_CENTER, y + 3, 0.5f, fg, UI_CENTER, val);
}

static void drawList(Screen* S, int s)
{
	int vis[MAX_ITEMS];
	int nv = visibleItems(S, vis);
	float top = listTop(s);
	for (int r = 0; r < ROWS_VISIBLE; r++) {
		int idx = S->scroll + r;
		if (idx >= nv) break;
		drawRow(&S->items[vis[idx]], top + r * ROW_H, idx == S->focus);
	}
	if (nv > ROWS_VISIBLE) {
		bool canUp = S->scroll > 0, canDown = S->scroll + ROWS_VISIBLE < nv;
		ui_chunky(ARROW_X, top, 20, 66, canUp ? T_SKY : UI_COL(190, 180, 200), false, false);
		ui_chunky(ARROW_X, top + 76, 20, 66, canDown ? T_SKY : UI_COL(190, 180, 200), false, false);
		ui_triangle(ARROW_X + 4, top + 38, ARROW_X + 16, top + 38, ARROW_X + 10, top + 24, UI_COL(255, 255, 255));
		ui_triangle(ARROW_X + 4, top + 104, ARROW_X + 16, top + 104, ARROW_X + 10, top + 118, UI_COL(255, 255, 255));
	}
}

static void drawTabs(void)
{
	static const char* const names[2] = { "MATCH", "BOTS" };
	for (int i = 0; i < 2; i++) {
		float x = 12.0f + i * 150.0f, w = 146.0f;
		bool on = (i == lobbyPage);
		if (on) {
			ui_chunky(x, TAB_Y, w, TAB_H, T_ORANGE, false, false);
			ui_text_shadow(x + w * 0.5f, TAB_Y + 2, 0.5f, UI_COL(255, 255, 255), UI_CENTER, names[i]);
		} else {
			ui_round_rect(x, TAB_Y + 2, w, TAB_H - 2, 8, T_INK);
			ui_round_rect(x + 2, TAB_Y + 4, w - 4, TAB_H - 6, 6, UI_COL(226, 208, 232));
			ui_text(x + w * 0.5f, TAB_Y + 2, 0.5f, ui_alpha(T_INK, 190), UI_CENTER, names[i]);
		}
		// shoulder hint
		float bx = i == 0 ? x + 8 : x + w - 22;
		ui_round_rect(bx, TAB_Y + 4, 14, 12, 4, T_INK);
		ui_text(bx + 7, TAB_Y + 3, 0.34f, UI_COL(255, 255, 255), UI_CENTER, i == 0 ? "L" : "R");
	}
}

static void drawGlyph(int id, float cx, float cy, u32 fill)
{
	u32 w = UI_COL(255, 255, 255);
	switch (id) {
	case I_LOBBY:   ui_icon_egg_small(cx, cy, 15, T_CREAM); break;
	case I_WEAPONS:
		ui_ellipse(cx - 8, cy - 8, 16, 16, w);
		ui_ellipse(cx - 5.5f, cy - 5.5f, 11, 11, ui_shade(fill, 0.9f));
		ui_rect(cx - 1, cy - 10, 2, 20, w);
		ui_rect(cx - 10, cy - 1, 20, 2, w);
		break;
	case I_MAPS:
		ui_ellipse(cx - 6, cy - 9, 12, 12, w);
		ui_triangle(cx - 5.5f, cy - 2, cx + 5.5f, cy - 2, cx, cy + 9, w);
		ui_ellipse(cx - 2.5f, cy - 5.5f, 5, 5, ui_shade(fill, 0.9f));
		break;
	case I_CUSTOM:  ui_icon_star(cx, cy, 17, T_YOLK); break;
	case I_SETTINGS:
		ui_rect(cx - 9, cy - 2, 18, 4, w);
		ui_rect(cx - 2, cy - 9, 4, 18, w);
		ui_ellipse(cx - 7, cy - 7, 14, 14, w);
		ui_ellipse(cx - 3, cy - 3, 6, 6, ui_shade(fill, 0.9f));
		break;
	case I_STATS:
		ui_rect(cx - 9, cy + 1, 5, 8, w);
		ui_rect(cx - 2.5f, cy - 5, 5, 14, w);
		ui_rect(cx + 4, cy - 10, 5, 19, w);
		break;
	default: break;
	}
}

static void drawMainButton(const Item* it, int i, bool focus)
{
	float x, y, w, h;
	itemRect(MS_MAIN, i, 0, &x, &y, &w, &h);
	bool pr = pressedRect(x, y, w, h);
	u32 fill;
	switch (it->id) {
	case I_PLAY:     fill = T_GRASS; break;
	case I_MULTI:    fill = UI_COL(40, 170, 200); break;
	case I_LOBBY:    fill = T_SKY; break;
	case I_WEAPONS:  fill = T_ORANGE; break;
	case I_MAPS:     fill = T_GRAPE; break;
	case I_CUSTOM:   fill = T_PINK; break;
	case I_SETTINGS: fill = T_TEAL; break;
	case I_STATS:    fill = UI_COL(120, 150, 255); break;
	case I_CREDITS:  fill = UI_COL(190, 168, 224); break;
	default:         fill = T_CORAL; break;
	}
	float bob = focus ? sinf(menuTime * 8.0f) * 1.0f : 0.0f;
	ui_chunky(x, y, w, h, fill, focus, pr);
	float o = pr ? 2.0f : 0.0f;
	u32 white = UI_COL(255, 255, 255);
	if (it->id == I_PLAY) {
		ui_triangle(x + 24, y + 13 + o + bob, x + 24, y + 39 + o + bob, x + 48, y + 26 + o + bob, T_INK);
		ui_triangle(x + 22, y + 11 + o + bob, x + 22, y + 37 + o + bob, x + 46, y + 24 + o + bob, white);
		outlineText(x + w * 0.5f + 22, y + 8 + o, 1.0f, white, UI_CENTER, "PLAY");
	} else if (it->id == I_MULTI) {
		ui_icon_egg_small(x + w * 0.5f - 12, y + 16 + o + bob, 18, T_CREAM);
		ui_icon_egg_small(x + w * 0.5f + 12, y + 16 + o + bob, 18, T_YOLK);
		ui_text_shadow(x + w * 0.5f, y + 29 + o, 0.46f, white, UI_CENTER, it->label);
	} else if (i >= 2 && i <= 7) {
		drawGlyph(it->id, x + w * 0.5f, y + 14 + o + bob, fill);
		ui_text_shadow(x + w * 0.5f, y + 24 + o, 0.5f, white, UI_CENTER, it->label);
	} else {
		ui_text_shadow(x + w * 0.5f, y + 4 + o, 0.5f, white, UI_CENTER, it->label);
	}
}

static void drawFreeButtons(Screen* S, int s)
{
	int vis[MAX_ITEMS];
	int nv = visibleItems(S, vis);
	for (int i = 0; i < nv; i++) {
		const Item* it = &S->items[vis[i]];
		bool focus = (i == S->focus);
		if (s == MS_MAIN) { drawMainButton(it, i, focus); continue; }
		float x, y, w, h;
		itemRect(s, i, 0, &x, &y, &w, &h);
		u32 fill = T_SKY;
		if (it->id == I_P_RESUME || it->id == I_R_REMATCH) fill = T_GRASS;
		else if (it->id == I_P_RESPAWN) fill = T_ORANGE;
		else if (it->id == I_P_SETTINGS) fill = T_TEAL;
		else if (it->id == I_MP_HOST) fill = T_GRASS;
		else if (it->id == I_P_QUIT || it->id == I_R_MENU) fill = T_CORAL;
		bool pr = pressedRect(x, y, w, h);
		ui_chunky(x, y, w, h, fill, focus, pr);
		float o = pr ? 2.0f : 0.0f;
		ui_text_shadow(x + w * 0.5f, y + h * 0.5f - 11 + o, 0.7f, UI_COL(255, 255, 255), UI_CENTER, it->label);
	}
}

static void statTile(float x, float y, const char* label, const char* value, u32 color, int icon)
{
	ui_card(x, y, 146, 44, T_CREAM);
	ui_ellipse(x + 8, y + 8, 28, 28, T_INK);
	ui_ellipse(x + 10, y + 10, 24, 24, color);
	if (icon == 0) ui_icon_egg_small(x + 22, y + 22, 15, T_CREAM);
	else if (icon == 1) ui_icon_heart(x + 22, y + 22, 15, UI_COL(255, 255, 255));
	else ui_icon_star(x + 22, y + 22, 16, T_YOLK);
	ui_text(x + 42, y + 5, 0.6f, T_INK, UI_LEFT, value);
	ui_text(x + 42, y + 26, 0.4f, ui_alpha(T_INK, 190), UI_LEFT, label);
}

static void drawRoomRow(int i, bool focus)
{
	const NpRoomInfo* r = &roomList[i];
	float x = 10, y = JOIN_ROW_Y(i), w = 300, h = 28;
	if (focus) ui_round_rect(x - 3, y - 2, w + 6, h + 4, 10, UI_COL(255, 255, 255));
	ui_round_rect(x, y, w, h, 8, T_INK);
	ui_round_rect(x + 2, y + 2, w - 4, h - 4, 6, focus ? T_YOLK : T_CREAM);
	tf(x + 12, y + 1, 0.5f, T_INK, UI_LEFT, "%s", r->name);
	int mode = r->mode < MODE_COUNT ? r->mode : 0, map = r->map < MAP_COUNT ? r->map : 0;
	tf(x + 12, y + 15, 0.36f, ui_alpha(T_INK, 190), UI_LEFT, "%s on %s", modeShort[mode], mapShort[map]);
	if (r->inMatch) drawTag(x + w - 8, y + 7, "IN MATCH", UI_COL(160, 150, 176));
	else {
		char b[16];
		snprintf(b, sizeof(b), "%d / %d", r->humans, NET_MAX_NODES);
		drawTag(x + w - 8, y + 7, b, r->humans >= NET_MAX_NODES ? T_CORAL : T_GRASS);
	}
}

static void drawMpBottom(void)
{
	char b[48];
	if (cur == MS_MP) {
		drawRibbon("MULTIPLAYER", false);
		drawFreeButtons(&scr[MS_MP], MS_MP);
		drawBackBtn("BACK");
	} else if (cur == MS_JOIN) {
		drawRibbon("JOIN A GAME", false);
		if (roomsSearching) {
			int dots = (int)(menuTime * 3.0f) % 4;
			snprintf(b, sizeof(b), "Searching%.*s", dots, "...");
			ui_card(20, 70, 280, 80, T_CREAM);
			ui_text(160, 92, 0.6f, T_INK, UI_CENTER, b);
			ui_text(160, 122, 0.4f, ui_alpha(T_INK, 180), UI_CENTER, "Looking for rooms nearby");
		} else if (roomCount == 0) {
			ui_card(20, 60, 280, 110, T_CREAM);
			ui_text(160, 76, 0.6f, T_INK, UI_CENTER, "No rooms found");
			ui_text(160, 108, 0.4f, ui_alpha(T_INK, 190), UI_CENTER, "Ask a friend to host a game,");
			ui_text(160, 124, 0.4f, ui_alpha(T_INK, 190), UI_CENTER, "stay close, then press REFRESH");
		} else {
			for (int i = 0; i < roomCount; i++) drawRoomRow(i, i == roomSel);
		}
		drawBackBtn("BACK");
		bool pr = !roomsSearching && pressedRect(170, FOOT_Y, 140, FOOT_H);
		ui_chunky(170, FOOT_Y, 140, FOOT_H, roomsSearching ? UI_COL(170, 160, 180) : T_SKY, false, pr);
		ui_text_shadow(240, FOOT_Y + 5 + (pr ? 2.0f : 0.0f), 0.55f, UI_COL(255, 255, 255), UI_CENTER, "REFRESH");
	} else {   // MS_ROOM
		drawRibbon(npHost() ? "YOUR ROOM" : "WAITING ROOM", false);
		if (npActive()) {
			int n = np_human_count(gNp);
			ui_card(20, 44, 280, 96, T_CREAM);
			snprintf(b, sizeof(b), "%d of %d players", n, NET_MAX_NODES);
			ui_text(160, 52, 0.6f, T_INK, UI_CENTER, b);
			if (npHost()) {
				ui_text(160, 82, 0.4f, ui_alpha(T_INK, 190), UI_CENTER, "Friends nearby can join from");
				ui_text(160, 98, 0.4f, ui_alpha(T_INK, 190), UI_CENTER, "Multiplayer > Join a game");
				ui_text(160, 116, 0.4f, ui_alpha(T_INK, 190), UI_CENTER, "Bots fill the empty places");
			} else {
				ui_text(160, 86, 0.46f, T_INK, UI_CENTER, "Waiting for the host");
				ui_text(160, 108, 0.4f, ui_alpha(T_INK, 190), UI_CENTER, gNp->inMatchRoom ? "The match is on, hang on" : "to start the match");
			}
		}
		if (npHost()) {
			bool pr = pressedRect(ROOM_SET_X, ROOM_SET_Y, ROOM_SET_W, ROOM_SET_H);
			ui_chunky(ROOM_SET_X, ROOM_SET_Y, ROOM_SET_W, ROOM_SET_H, T_SKY, false, pr);
			ui_text_shadow(160, ROOM_SET_Y + 9 + (pr ? 2.0f : 0.0f), 0.6f, UI_COL(255, 255, 255), UI_CENTER, "Match settings");
		}
		drawBackBtn("LEAVE");
		if (npHost()) drawStartBtn(true, "START!");
	}
}

void menu_draw_bottom(void)
{
	ui_backdrop_panel();
	Screen* S = &scr[cur];
	char buf[32];

	switch (cur) {
	case MS_MAIN:
		drawRibbon("EGGSTRIKE 3DS", true);
		drawFreeButtons(S, MS_MAIN);
		break;
	case MS_LOBBY:
		drawRibbon("LOBBY", false);
		drawTabs();
		drawList(S, MS_LOBBY);
		drawBackBtn("BACK");
		if (npHost()) drawStartBtn(true, "DONE");
		else drawStartBtn(mode_available(gCfg->mode) && map_implemented(gCfg->map), "PLAY!");
		break;
	case MS_WEAPONS:   drawRibbon("WEAPONS", false);  drawList(S, cur); drawBackBtn("BACK"); break;
	case MS_MAPS:      drawRibbon("MAPS", false);     drawList(S, cur); drawBackBtn("BACK"); break;
	case MS_SETTINGS:  drawRibbon("SETTINGS", false); drawList(S, cur); drawBackBtn("BACK"); break;
	case MS_CUSTOM:    drawRibbon("CUSTOMIZE", true); drawList(S, cur); drawBackBtn("BACK"); break;
	case MS_STATS: {
		const Stats* s = &gProf->stats;
		drawRibbon("STATS", false);
		float kd = s->deaths ? (float)s->kills / (float)s->deaths : (float)s->kills;
		snprintf(buf, sizeof(buf), "%u", s->kills);          statTile(10, 40, "Kills", buf, T_CORAL, 0);
		snprintf(buf, sizeof(buf), "%u", s->deaths);         statTile(164, 40, "Deaths", buf, T_GRAPE, 1);
		snprintf(buf, sizeof(buf), "%.2f", kd);              statTile(10, 90, "Kill / death", buf, T_TEAL, 2);
		snprintf(buf, sizeof(buf), "%u", s->matchesWon);     statTile(164, 90, "Matches won", buf, T_GRASS, 2);
		snprintf(buf, sizeof(buf), "%u", s->matchesPlayed);  statTile(10, 140, "Matches played", buf, T_SKY, 0);
		snprintf(buf, sizeof(buf), "%u", s->flagsCaptured);  statTile(164, 140, "Flags captured", buf, T_ORANGE, 2);
		ui_text(160, 188, 0.42f, ui_alpha(T_INK, 210), UI_CENTER, "Career totals, saved on your SD card");
		drawBackBtn("BACK");
		break;
	}
	case MS_CREDITS:
		drawRibbon("CREDITS", false);
		ui_card(14, 42, 292, 150, T_CREAM);
		ui_icon_egg_small(160, 72, 36, T_YOLK);
		ui_text(160, 100, 0.6f, T_INK, UI_CENTER, "EggStrike 3DS");
		ui_text(160, 124, 0.42f, ui_alpha(T_INK, 220), UI_CENTER, "An original egg arena shooter");
		ui_text(160, 142, 0.42f, ui_alpha(T_INK, 220), UI_CENTER, "Made with devkitPro, libctru,");
		ui_text(160, 158, 0.42f, ui_alpha(T_INK, 220), UI_CENTER, "citro3d and citro2d");
		ui_icon_heart(160, 180, 14, T_CORAL);
		drawBackBtn("BACK");
		break;
	case MS_MP:
	case MS_JOIN:
	case MS_ROOM:
		drawMpBottom();
		break;
	case MS_PAUSE:
		drawRibbon("PAUSED", false);
		drawFreeButtons(S, MS_PAUSE);
		break;
	case MS_POST: {
		drawRibbon("MATCH OVER", false);
		ui_card(40, 40, 240, 100, T_CREAM);
		ui_icon_coin(94, 80, 40);
		snprintf(buf, sizeof(buf), "+%d", postCoins);
		outlineText(176, 52, 1.0f, T_YOLK, UI_CENTER, buf);
		ui_text(176, 98, 0.45f, T_INK, UI_CENTER, "coins earned");
		tf(160, 116, 0.42f, ui_alpha(T_INK, 200), UI_CENTER, "You now have %u", gProf->stats.coins);
		drawFreeButtons(S, MS_POST);
		break;
	}
	default: break;
	}
}

// ---- top screens

static void drawStatBar(float x, float y, const char* name, float frac)
{
	ui_text(x, y, 0.42f, T_INK, UI_LEFT, name);
	ui_pill_bar(x + 84, y + 2, 150, 13, frac, T_ORANGE, UI_COL(226, 208, 232));
}

static void ensurePreview(int id)
{
	if (previewId == id) return;
	previewId = id;
	if (!map_load(&previewMap, id)) previewMap.boxCount = 0;
}

static void drawMapPreview(float x, float y, float w, float h, int id)
{
	ui_card(x - 5, y - 5, w + 10, h + 10, UI_COL(70, 64, 96));
	ensurePreview(id);
	if (previewMap.boxCount > 0) {
		UiMapView v;
		ui_map_view(&v, &previewMap, x, y, w, h);
		ui_map_draw(&v, &previewMap);
	} else {
		ui_rect(x, y, w, h, UI_COL(120, 110, 150));
		ui_text_shadow(x + w * 0.5f, y + h * 0.5f - 8, 0.55f, UI_COL(255, 255, 255), UI_CENTER, "Coming soon");
	}
}

static const char* focusedHelp(void)
{
	Screen* S = &scr[cur];
	int vis[MAX_ITEMS];
	int nv = visibleItems(S, vis);
	if (nv == 0) return "";
	int f = S->focus < nv ? S->focus : nv - 1;
	return S->items[vis[f]].help ? S->items[vis[f]].help : "";
}

static void infoLine(float x, float y, const char* label, const char* value, u32 valueCol)
{
	ui_text(x, y, 0.46f, ui_alpha(T_INK, 170), UI_LEFT, label);
	ui_text(x + 168, y, 0.5f, valueCol, UI_RIGHT, value);
}

void menu_draw_top(void)
{
	ui_backdrop_sky(menuTime);
	char buf[48];
	switch (cur) {
	case MS_MAIN: {
		outlineText(200, 12, 1.5f, T_YOLK, UI_CENTER, "EGGSTRIKE");
		ui_round_rect(178, 66, 44, 24, 10, T_INK);
		ui_round_rect(180, 68, 40, 20, 8, T_CORAL);
		ui_text_shadow(200, 68, 0.55f, UI_COL(255, 255, 255), UI_CENTER, "3DS");
		float bob = fabsf(sinf(menuTime * 2.6f)) * 8.0f;
		ui_draw_egg(200, 160 - bob, 1.2f, gProf->cos.sel[COS_SHELL], gProf->cos.sel[COS_HAT], gProf->cos.sel[COS_ACC], menuTime);
		topHelp(focusedHelp());
		break;
	}
	case MS_LOBBY: {
		const MatchConfig* c = gCfg;
		topBanner("LOBBY", T_ORANGE);
		drawMapPreview(18, 46, 140, 140, c->map);
		ui_card(176, 42, 208, 152, T_CREAM);
		float x = 188, y = 48;
		infoLine(x, y, "Mode", mode_name(c->mode), T_INK);
		infoLine(x, y + 19, "Map", map_name(c->map), T_INK);
		if (mode_is_team(c->mode)) {
			snprintf(buf, sizeof(buf), "%d%s", c->redBots, c->team == TEAM_RED ? " + you" : "");
			infoLine(x, y + 38, "Red team", buf, UI_COL(210, 50, 50));
			snprintf(buf, sizeof(buf), "%d%s", c->blueBots, c->team == TEAM_BLUE ? " + you" : "");
			infoLine(x, y + 57, "Blue team", buf, UI_COL(40, 100, 220));
		} else {
			snprintf(buf, sizeof(buf), "%d", c->totalBots);
			infoLine(x, y + 38, "Bots", buf, T_INK);
			infoLine(x, y + 57, "Loadout", weaponName(gProf->favoriteWeapon), T_INK);
		}
		infoLine(x, y + 76, "Bot skill", diffNames[c->difficulty], T_INK);
		if (c->mode != MODE_PRACTICE) {
			snprintf(buf, sizeof(buf), "%d min", match_duration_minutes_at(c->durationIdx));
			infoLine(x, y + 95, "Time", buf, T_INK);
			snprintf(buf, sizeof(buf), "%d %s", match_score_limit(c), match_score_unit(c->mode));
			infoLine(x, y + 114, "Limit", buf, T_INK);
		}
		ui_text(x, y + 132, 0.38f, ui_alpha(T_INK, 150), UI_LEFT, "L / R switches page");
		topHelp(mode_help(c->mode));
		break;
	}
	case MS_WEAPONS: {
		int w = scr[MS_WEAPONS].focus;
		const WeaponDef* d = weapon_def(w);
		topBanner(d->name, T_ORANGE);
		float power = d->damage * (d->shot == SHOT_HITSCAN ? (float)d->pellets : 1.0f) / 100.0f;
		float rate = 1.0f / d->interval / 12.0f;
		if (d->mode == FIRE_BURST) rate = (float)d->burstCount / d->interval / 12.0f;
		ui_card(60, 42, 280, 52, T_CREAM);
		ui_weapon_icon(w, 90, 46, 220, 44);
		ui_card(60, 100, 280, 104, T_CREAM);
		drawStatBar(76, 108, "Power",  power);
		drawStatBar(76, 126, "Fire rate", rate);
		drawStatBar(76, 144, "Range",  d->range / 150.0f);
		drawStatBar(76, 162, "Ammo",   (float)d->magSize / 50.0f);
		drawStatBar(76, 180, "Reload", (4.0f - d->reloadTime) / 2.5f);
		topHelp(d->shot == SHOT_ROCKET ? "Explosive launcher with splash damage" :
		        d->mode == FIRE_AUTO ? "Automatic" : d->mode == FIRE_BURST ? "Three-round burst" : "Semi-automatic");
		break;
	}
	case MS_MAPS: {
		int m = scr[MS_MAPS].focus;
		topBanner(map_name(m), T_GRAPE);
		drawMapPreview(120, 46, 160, 150, m);
		topHelp(map_help(m));
		break;
	}
	case MS_SETTINGS: {
		topBanner("SETTINGS", T_TEAL);
		ui_draw_egg(200, 130, 1.1f, gProf->cos.sel[COS_SHELL], gProf->cos.sel[COS_HAT], gProf->cos.sel[COS_ACC], menuTime);
		topHelp(focusedHelp());
		break;
	}
	case MS_STATS: {
		const Stats* s = &gProf->stats;
		topBanner("YOUR CAREER", UI_COL(120, 150, 255));
		ui_draw_egg(200, 134, 1.1f, gProf->cos.sel[COS_SHELL], gProf->cos.sel[COS_HAT], gProf->cos.sel[COS_ACC], menuTime);
		int winPct = s->matchesPlayed ? (int)(100.0f * s->matchesWon / s->matchesPlayed + 0.5f) : 0;
		ui_card(18, 74, 100, 62, T_CREAM);
		ui_text(68, 80, 0.7f, T_INK, UI_CENTER, (snprintf(buf, sizeof(buf), "%d%%", winPct), buf));
		ui_text(68, 112, 0.4f, ui_alpha(T_INK, 190), UI_CENTER, "win rate");
		ui_card(282, 74, 100, 62, T_CREAM);
		snprintf(buf, sizeof(buf), "%u:%02u", s->playSeconds / 3600, (s->playSeconds / 60) % 60);
		ui_text(332, 80, 0.7f, T_INK, UI_CENTER, buf);
		ui_text(332, 112, 0.4f, ui_alpha(T_INK, 190), UI_CENTER, "hours played");
		ui_icon_coin(200, 214, 16);
		ui_textf(214, 206, 0.5f, UI_COL(255, 255, 255), UI_LEFT, "%u", s->coins);
		break;
	}
	case MS_CREDITS: {
		topBanner("CREDITS", UI_COL(190, 168, 224));
		ui_draw_egg(200, 120, 1.0f, gProf->cos.sel[COS_SHELL], gProf->cos.sel[COS_HAT], gProf->cos.sel[COS_ACC], menuTime);
		ui_card(60, 150, 280, 50, T_CREAM);
		ui_text(200, 156, 0.46f, T_INK, UI_CENTER, "Original art, names and sounds");
		ui_text(200, 174, 0.42f, ui_alpha(T_INK, 200), UI_CENTER, "Inspired by the genre, thanks for playing!");
		break;
	}
	case MS_CUSTOM: {
		topBanner("CUSTOMIZE", T_PINK);
		ui_draw_egg(200, 130, 1.35f, cosPrev[COS_SHELL], cosPrev[COS_HAT], cosPrev[COS_ACC], menuTime);
		ui_round_rect(300, 10, 90, 22, 10, T_INK);
		ui_round_rect(302, 12, 86, 18, 8, T_CREAM);
		ui_icon_coin(314, 21, 13);
		tf(384, 12, 0.45f, T_INK, UI_RIGHT, "%u", gProf->stats.coins);
		topHelp(focusedHelp());
		break;
	}
	case MS_MP: {
		topBanner("MULTIPLAYER", UI_COL(40, 170, 200));
		int shell2 = (gProf->cos.sel[COS_SHELL] + 1) % cos_count(COS_SHELL);
		ui_draw_egg(150, 134, 1.0f, gProf->cos.sel[COS_SHELL], gProf->cos.sel[COS_HAT], gProf->cos.sel[COS_ACC], menuTime);
		ui_draw_egg(250, 134, 1.0f, shell2, 0, 0, menuTime + 1.3f);
		ui_text_shadow(200, 168, 0.45f, UI_COL(255, 255, 255), UI_CENTER, "Local wireless: up to 4 players");
		topHelp(focusedHelp());
		break;
	}
	case MS_JOIN: {
		topBanner("JOIN A GAME", T_SKY);
		ui_draw_egg(96, 140, 1.0f, gProf->cos.sel[COS_SHELL], gProf->cos.sel[COS_HAT], gProf->cos.sel[COS_ACC], menuTime);
		ui_card(176, 46, 206, 140, T_CREAM);
		if (roomsSearching || roomCount == 0) {
			ui_text(279, 100, 0.5f, ui_alpha(T_INK, 200), UI_CENTER, roomsSearching ? "Searching..." : "No room selected");
		} else {
			const NpRoomInfo* r = &roomList[roomSel];
			int mode = r->mode < MODE_COUNT ? r->mode : 0, map = r->map < MAP_COUNT ? r->map : 0;
			ui_text(188, 52, 0.55f, T_INK, UI_LEFT, r->name);
			infoLine(188, 82, "Mode", mode_name(mode), T_INK);
			infoLine(188, 100, "Map", map_name(map), T_INK);
			snprintf(buf, sizeof(buf), "%d of %d", r->humans, NET_MAX_NODES);
			infoLine(188, 118, "Players", buf, T_INK);
			infoLine(188, 136, "Bot skill", diffNames[r->difficulty < 3 ? r->difficulty : 1], T_INK);
			ui_text(188, 160, 0.4f, ui_alpha(T_INK, 170), UI_LEFT, r->inMatch ? "A match is running" : "A or tap to join");
		}
		topHelp("START or REFRESH looks again");
		break;
	}
	case MS_ROOM: {
		topBanner(npHost() ? "YOUR ROOM" : "WAITING ROOM", T_GRASS);
		if (!npActive()) break;
		const MatchConfig* c = npHost() ? gCfg : &gNp->cfg;
		drawMapPreview(18, 52, 112, 112, c->map);
		ui_card(146, 42, 242, 156, T_CREAM);
		float x = 156, y = 47;
		infoLine(x, y, "Mode", mode_name(c->mode), T_INK);
		infoLine(x, y + 17, "Map", map_name(c->map), T_INK);
		snprintf(buf, sizeof(buf), "%d min, %d %s", match_duration_minutes_at(c->durationIdx), match_score_limit(c), match_score_unit(c->mode));
		infoLine(x, y + 34, "Limit", buf, T_INK);
		if (mode_is_team(c->mode)) snprintf(buf, sizeof(buf), "%d red, %d blue", c->redBots, c->blueBots);
		else snprintf(buf, sizeof(buf), "%d, %s", c->totalBots, diffNames[c->difficulty < 3 ? c->difficulty : 1]);
		infoLine(x, y + 51, "Bots", buf, T_INK);
		ui_rect(156, 120, 222, 2, ui_alpha(T_INK, 60));
		int n = np_human_count(gNp);
		for (int i = 0; i < n && i < NET_MAX_NODES; i++) {
			const NpHuman* h = np_human(gNp, i);
			float ry = 126.0f + i * 17.0f;
			if (mode_is_team(c->mode)) {
				ui_round_rect(156, ry + 2, 11, 11, 3, T_INK);
				ui_round_rect(157, ry + 3, 9, 9, 2, h->team == TEAM_RED ? T_RED_TEAM : T_BLUE_TEAM);
			}
			tf(173, ry, 0.44f, T_INK, UI_LEFT, "%s%s", h->name, i == gNp->myIdx ? " (you)" : "");
			if (i == 0) drawTag(378, ry, "HOST", T_ORANGE);
		}
		topHelp(npHost() ? "A: match settings    START: begin the match" : "The host chooses the rules and starts the match");
		break;
	}
	case MS_PAUSE: {
		topBanner("PAUSED", T_TEAL);
		ui_draw_egg(200, 130, 1.1f, gProf->cos.sel[COS_SHELL], gProf->cos.sel[COS_HAT], gProf->cos.sel[COS_ACC], menuTime);
		topHelp(focusedHelp());
		break;
	}
	case MS_POST: {
		const Match* m = &postMatch;
		const char* head;
		u32 hc;
		if (m->cfg.mode == MODE_PRACTICE) { head = "SESSION OVER"; hc = T_SKY; }
		else if (postWon)                 { head = "VICTORY!";     hc = T_GRASS; }
		else if (m->winner == -1)         { head = "DRAW";         hc = UI_COL(160, 150, 190); }
		else                              { head = "DEFEAT";       hc = T_CORAL; }
		topBanner(head, hc);
		ui_card(10, 44, 170, 152, T_CREAM);
		ui_text(22, 50, 0.46f, T_INK, UI_LEFT, mode_name(m->cfg.mode));
		tf(22, 70, 0.44f, T_INK, UI_LEFT, "Kills %d    Deaths %d", m->playerKills, m->playerDeaths);
		if (mode_is_team(m->cfg.mode)) {
			ui_round_rect(22, 94, 70, 24, 8, T_INK);
			ui_round_rect(24, 96, 66, 20, 6, T_RED_TEAM);
			tf(57, 98, 0.5f, UI_COL(255, 255, 255), UI_CENTER, "%d", m->redScore);
			ui_round_rect(98, 94, 70, 24, 8, T_INK);
			ui_round_rect(100, 96, 66, 20, 6, T_BLUE_TEAM);
			tf(133, 98, 0.5f, UI_COL(255, 255, 255), UI_CENTER, "%d", m->blueScore);
		}
		tf(22, 128, 0.44f, T_INK, UI_LEFT, "Time %d:%02d", (int)m->elapsed / 60, (int)m->elapsed % 60);
		ui_icon_coin(30, 168, 16);
		tf(44, 160, 0.5f, T_INK, UI_LEFT, "+%d coins", postCoins);

		ui_card(188, 44, 202, 188, T_CREAM);
		ui_text(200, 50, 0.36f, ui_alpha(T_INK, 160), UI_LEFT, "PLAYER");
		ui_text(340, 50, 0.36f, ui_alpha(T_INK, 160), UI_RIGHT, "K");
		ui_text(376, 50, 0.36f, ui_alpha(T_INK, 160), UI_RIGHT, "D");
		int shown = postRowCount > 10 ? 10 : postRowCount;
		for (int i = 0; i < shown; i++) {
			float y = 66.0f + i * 16.0f;
			const ScoreRow* r = &postRows[i];
			u32 col = r->team == 0 ? UI_COL(200, 50, 50) : (r->team == 1 ? UI_COL(40, 90, 210) : T_INK);
			if (r->isPlayer) ui_round_rect(194, y - 1, 190, 16, 6, UI_COLA(255, 208, 64, 170));
			tf(200, y, 0.38f, col, UI_LEFT, "%d. %s", i + 1, r->name);
			tf(340, y, 0.38f, col, UI_RIGHT, "%d", r->kills);
			tf(376, y, 0.38f, col, UI_RIGHT, "%d", r->deaths);
		}
		break;
	}
	default: break;
	}
}
