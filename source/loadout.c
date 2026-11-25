#include "loadout.h"
#include <stdio.h>
#include "audio.h"
#include "ui.h"
#include "weapons.h"

#define GRID_COLS 3
#define TILE_W   98
#define TILE_H   42
#define TILE_GAP 4
#define GRID_X   9
#define GRID_Y   58
#define SLIDER_X 74
#define SLIDER_W 170
#define SLIDER_Y 178
#define BTN_X    20
#define BTN_Y    198
#define BTN_W    280
#define BTN_H    38

static int focus;      // highlighted primary weapon (the secondary is always the Eggstol)

void loadout_open(const PlayerProfile* prof)
{
	focus = prof->favoriteWeapon;
}

static float tileX(int i) { return GRID_X + (i % GRID_COLS) * (TILE_W + TILE_GAP); }
static float tileY(int i) { return GRID_Y + (i / GRID_COLS) * (TILE_H + TILE_GAP); }

LoadoutAction loadout_update(const UiInput* in, PlayerProfile* prof, float waitTime)
{
	bool ready = waitTime <= 0.0f;

	if (in->left)  focus = (focus + PRIMARY_COUNT - 1) % PRIMARY_COUNT;
	if (in->right) focus = (focus + 1) % PRIMARY_COUNT;
	if (in->up   && focus >= GRID_COLS) focus -= GRID_COLS;
	if (in->down && focus + GRID_COLS < PRIMARY_COUNT) focus += GRID_COLS;
	if (in->left || in->right || in->up || in->down) audio_play(SFX_MENU_MOVE, 0.0f, 1.0f);
	if (in->accept) { profile_set_weapon(prof, 0, focus); audio_play(SFX_UI_TAP, 0.0f, 1.0f); }

	if (in->touchPressed || in->touchHeld) {
		// Volume slider
		if (ui_hit(SLIDER_X - 8, SLIDER_Y - 8, SLIDER_W + 16, 26, in->tx, in->ty)) {
			int v = (int)(((float)(in->tx - SLIDER_X) / SLIDER_W) * 10.0f + 0.5f);
			prof->settings.sfxVol = v < 0 ? 0 : (v > 10 ? 10 : v);
		}
	}
	if (in->touchPressed) {
		for (int i = 0; i < PRIMARY_COUNT; i++) {
			if (ui_hit(tileX(i), tileY(i), TILE_W, TILE_H, in->tx, in->ty)) {
				focus = i;
				profile_set_weapon(prof, 0, i);
				audio_play(SFX_UI_TAP, 0.0f, 1.0f);
			}
		}
		if (ready && ui_hit(BTN_X, BTN_Y, BTN_W, BTN_H, in->tx, in->ty)) { audio_play(SFX_UI_START, 0.0f, 1.0f); return LO_SPAWN; }
	}
	if (ready && in->extra) { audio_play(SFX_UI_START, 0.0f, 1.0f); return LO_SPAWN; }
	return LO_NONE;
}

// Tabs under the top bar: the chosen primary (orange, being edited) and the fixed secondary (locked)
static void drawTab(float x, int slot, const PlayerProfile* prof)
{
	bool sel = (slot == 0);
	int w = slot == 0 ? prof->favoriteWeapon : W_EGGSTOL;
	ui_round_rect(x, 37, 148, 19, 8, T_INK);
	ui_round_rect(x + 2, 39, 144, 15, 6, sel ? T_ORANGE : UI_COL(214, 200, 226));
	u32 fg = sel ? UI_COL(255, 255, 255) : ui_alpha(T_INK, 200);
	ui_text(x + 8, 39, 0.38f, fg, UI_LEFT, slot == 0 ? "PRIMARY" : "SECONDARY");
	ui_text(x + 140, 39, 0.38f, fg, UI_RIGHT, weapon_def(w)->name);
}

static void drawLoadout(const PlayerProfile* prof, float waitTime, const char* waitText);

void loadout_draw(const PlayerProfile* prof, float waitTime) { drawLoadout(prof, waitTime, NULL); }
void loadout_draw_wait(const PlayerProfile* prof, float waitTime, const char* waitText) { drawLoadout(prof, waitTime, waitText); }

static void drawLoadout(const PlayerProfile* prof, float waitTime, const char* waitText)
{
	bool ready = waitTime <= 0.0f;
	char buf[48];

	ui_backdrop_panel();      // the top 34 pixels are covered by the match top bar

	drawTab(10, 0, prof);
	drawTab(162, 1, prof);

	for (int i = 0; i < PRIMARY_COUNT; i++) {
		float x = tileX(i), y = tileY(i);
		bool isFocus = (i == focus);
		bool isPrimary = (i == prof->favoriteWeapon);
		if (isFocus) ui_round_rect(x - 3, y - 3, TILE_W + 6, TILE_H + 6, 11, UI_COL(255, 255, 255));
		ui_round_rect(x, y, TILE_W, TILE_H, 9, T_INK);
		ui_round_rect(x + 2, y + 2, TILE_W - 4, TILE_H - 4, 7, isFocus ? T_YOLK : T_CREAM);
		ui_weapon_icon(i, x + 7, y + 6, TILE_W - 14, TILE_H - 12);
		if (isPrimary) {
			ui_ellipse(x + 4, y + 4, 14, 14, T_INK);
			ui_ellipse(x + 5.5f, y + 5.5f, 11, 11, T_GRASS);
			ui_text(x + 11, y + 4, 0.34f, UI_COL(255, 255, 255), UI_CENTER, "1");
		}
	}

	ui_text_shadow(160, 148, 0.66f, UI_COL(255, 255, 255), UI_CENTER, weapon_def(focus)->name);

	// Volume slider (the setting is saved)
	int vol = prof->settings.sfxVol;
	ui_text(10, 175, 0.4f, T_INK, UI_LEFT, "Volume");
	ui_pill_bar(SLIDER_X, SLIDER_Y + 1, SLIDER_W, 10, vol / 10.0f, T_ORANGE, UI_COL(255, 255, 255));
	float kx = SLIDER_X + 3 + (SLIDER_W - 6) * vol / 10.0f;
	ui_ellipse(kx - 8, SLIDER_Y - 2, 16, 16, T_INK);
	ui_ellipse(kx - 6, SLIDER_Y, 12, 12, UI_COL(255, 255, 255));
	snprintf(buf, sizeof(buf), "%d%%", vol * 10);
	ui_text(312, 175, 0.4f, T_INK, UI_RIGHT, buf);

	// Big button
	if (ready) {
		ui_chunky(BTN_X, BTN_Y, BTN_W, BTN_H - 4, T_GRASS, true, false);
		ui_triangle(BTN_X + 40, BTN_Y + 8, BTN_X + 40, BTN_Y + 26, BTN_X + 56, BTN_Y + 17, UI_COL(255, 255, 255));
		ui_text_shadow(160, BTN_Y + 4, 0.85f, UI_COL(255, 255, 255), UI_CENTER, "DEPLOY!");
	} else {
		ui_chunky(BTN_X, BTN_Y, BTN_W, BTN_H - 4, UI_COL(176, 166, 190), false, false);
		if (waitText) snprintf(buf, sizeof(buf), "%s", waitText);
		else snprintf(buf, sizeof(buf), "RESPAWN IN %.1f", waitTime);
		ui_text_shadow(160, BTN_Y + 6, 0.62f, UI_COL(255, 255, 255), UI_CENTER, buf);
	}
}
