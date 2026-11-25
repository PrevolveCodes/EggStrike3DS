#include "hud.h"
#include "loadout.h"
#include "ui.h"
#include <math.h>
#include <stdio.h>

// Bottom-screen HUD in the bright cartoon style: a top bar (pause, timer, scores), kills and deaths,
// the weapon card, health, grenades, the minimap and the two weapon slots along the bottom.

static void pill(float x, float y, float w, float h, u32 fill)
{
	ui_round_rect(x, y, w, h, h * 0.5f, T_INK);
	ui_round_rect(x + 2, y + 2, w - 4, h - 4, h * 0.5f - 2, fill);
}

static void drawTopBar(const Match* m, bool pauseHeld)
{
	ui_rect_grad(0, 0, 320, 31, UI_COL(255, 168, 66), UI_COL(255, 128, 50));
	ui_rect(0, 31, 320, 3, T_INK);
	ui_rect(0, 28, 320, 3, UI_COL(255, 214, 120));

	// pause button
	ui_chunky(HUD_PAUSE_X, HUD_PAUSE_Y - 1, HUD_PAUSE_W, HUD_PAUSE_H, T_YOLK, false, pauseHeld);
	float o = pauseHeld ? 2.0f : 0.0f;
	ui_round_rect(HUD_PAUSE_X + 14, HUD_PAUSE_Y + 4 + o, 6, 13, 2, T_INK);
	ui_round_rect(HUD_PAUSE_X + 24, HUD_PAUSE_Y + 4 + o, 6, 13, 2, T_INK);

	// timer
	int secs = (m->cfg.mode == MODE_PRACTICE) ? (int)m->elapsed : (int)(m->timeLeft + 0.99f);
	char buf[24];
	pill(118, 3, 84, 24, T_CREAM);
	snprintf(buf, sizeof(buf), "%d:%02d", secs / 60, secs % 60);
	ui_text(160, 5, 0.66f, T_INK, UI_CENTER, buf);

	// score
	if (mode_is_team(m->cfg.mode)) {
		pill(214, 3, 42, 24, T_RED_TEAM);
		snprintf(buf, sizeof(buf), "%d", m->redScore);
		ui_text_shadow(235, 6, 0.55f, UI_COL(255, 255, 255), UI_CENTER, buf);
		pill(262, 3, 42, 24, T_BLUE_TEAM);
		snprintf(buf, sizeof(buf), "%d", m->blueScore);
		ui_text_shadow(283, 6, 0.55f, UI_COL(255, 255, 255), UI_CENTER, buf);
	} else if (m->cfg.mode == MODE_PRACTICE) {
		pill(214, 3, 90, 24, T_SKY);
		ui_text_shadow(259, 7, 0.46f, UI_COL(255, 255, 255), UI_CENTER, "PRACTICE");
	} else {
		pill(214, 3, 90, 24, T_YOLK);
		snprintf(buf, sizeof(buf), "%d / %d", m->playerKills, match_score_limit(&m->cfg));
		ui_text(259, 6, 0.52f, T_INK, UI_CENTER, buf);
	}
}

static void drawMinimap(const Player* p, const Combat* c, const Map* map)
{
	const float x = 190, y = 42, w = 120, h = 120;
	ui_card(x - 5, y - 5, w + 10, h + 10, UI_COL(70, 64, 96));
	UiMapView v;
	ui_map_view(&v, map, x, y, w, h);
	ui_map_draw(&v, map);

	// Teammates are always shown; enemies only while the player can actually see them
	bool teamMode = combat_is_team_mode(c);
	for (int i = 0; i < c->botCount; i++) {
		const Bot* bt = &c->bots[i];
		if (!bt->alive) continue;
		bool friendly = teamMode && bt->team == c->playerTeam;
		if (!friendly && !bt->visibleToPlayer) continue;
		float sx, sy;
		ui_map_point(&v, bt->x, bt->z, &sx, &sy);
		u32 col = !teamMode ? T_ORANGE : (bt->team == TEAM_RED ? T_RED_TEAM : T_BLUE_TEAM);
		ui_ellipse(sx - 3.5f, sy - 3.5f, 7, 7, T_INK);
		ui_ellipse(sx - 2.5f, sy - 2.5f, 5, 5, col);
	}

	for (int i = 0; i < c->ammoBoxCount; i++) {          // ammo crates that are ready
		if (c->ammoBox[i].timer > 0.0f) continue;
		float sx, sy;
		ui_map_point(&v, c->ammoBox[i].pos[0], c->ammoBox[i].pos[2], &sx, &sy);
		ui_rect(sx - 3, sy - 3, 6, 6, T_INK);
		ui_rect(sx - 2, sy - 2, 4, 4, T_YOLK);
	}
	// Objectives are public, so they are always on the map
	if (c->mode == MODE_KOTP) {
		float sx, sy;
		ui_map_point(&v, c->obj.panPos[0], c->obj.panPos[2], &sx, &sy);
		u32 col = c->obj.holder == 0 ? T_RED_TEAM : (c->obj.holder == 1 ? T_BLUE_TEAM : (c->obj.holder == 2 ? T_YOLK : UI_COL(235, 235, 240)));
		ui_ellipse(sx - 7, sy - 7, 14, 14, T_INK);
		ui_ellipse(sx - 5.5f, sy - 5.5f, 11, 11, col);
		ui_ellipse(sx - 2, sy - 2, 4, 4, T_INK);
	}
	if (c->mode == MODE_CTF) {
		for (int t = 0; t < 2; t++) {
			const Flag* f = &c->obj.flag[t];
			float sx, sy;
			ui_map_point(&v, f->pos[0], f->pos[2], &sx, &sy);
			u32 col = t == 0 ? T_RED_TEAM : T_BLUE_TEAM;
			ui_rect(sx - 1, sy - 8, 2, 12, T_INK);
			ui_rect(sx, sy - 8, 7, 5, T_INK);
			ui_rect(sx, sy - 7, 6, 3, col);
			if (!f->atHome && f->carrier < 0) ui_rect(sx - 4, sy + 4, 8, 2, T_YOLK);   // dropped
		}
	}

	float px, py;
	ui_map_point(&v, p->x, p->z, &px, &py);
	float dx = sinf(p->yaw), dz = -cosf(p->yaw);
	float nx = -dz, nz = dx;
	ui_triangle(px + dx * 9, py + dz * 9,
	            px - dx * 5 + nx * 5, py - dz * 5 + nz * 5,
	            px - dx * 5 - nx * 5, py - dz * 5 - nz * 5,
	            T_INK);
	ui_triangle(px + dx * 7, py + dz * 7,
	            px - dx * 4 + nx * 4, py - dz * 4 + nz * 4,
	            px - dx * 4 - nx * 4, py - dz * 4 - nz * 4,
	            UI_COL(255, 255, 255));
}

static void drawWeaponStrip(const Player* p)
{
	ui_rect(0, HUD_STRIP_Y - 2, 320, HUD_STRIP_H + 2, UI_COL(255, 224, 168));
	ui_rect(0, HUD_STRIP_Y - 2, 320, 2, T_INK);
	for (int s = 0; s < 2; s++) {
		int w = p->loadout[s];
		const WeaponDef* d = weapon_def(w);
		const WeaponState* ws = &p->ws[w];
		float x = s * 160.0f;
		bool cur = (p->slot == s);
		ui_round_rect(x + 4, HUD_STRIP_Y + 2, 152, HUD_STRIP_H - 4, 9, T_INK);
		ui_round_rect(x + 6, HUD_STRIP_Y + 4, 148, HUD_STRIP_H - 8, 7, cur ? T_YOLK : T_CREAM);
		ui_weapon_icon(w, x + 10, HUD_STRIP_Y + 7, 48, HUD_STRIP_H - 14);
		ui_text(x + 64, HUD_STRIP_Y + 5, 0.46f, T_INK, UI_LEFT, d->name);
		char buf[24];
		snprintf(buf, sizeof(buf), "%d / %d", ws->ammo, ws->reserve < 0 ? d->magSize : ws->reserve);
		ui_text(x + 64, HUD_STRIP_Y + 18, 0.4f, ui_alpha(T_INK, 210), UI_LEFT, buf);
		if (cur) ui_triangle(x + 142, HUD_STRIP_Y + 10, x + 142, HUD_STRIP_Y + 22, x + 150, HUD_STRIP_Y + 16, T_INK);
	}
}

static void drawScoreboard(const Player* p, const Combat* c)
{
	ScoreRow rows[MAX_FIGHTERS];
	int n = combat_scoreboard(c, p, rows, MAX_FIGHTERS);

	ui_card(4, 38, 312, 162, T_CREAM);
	ui_round_rect(100, 41, 120, 17, 8, T_ORANGE);
	ui_text_shadow(160, 41, 0.45f, UI_COL(255, 255, 255), UI_CENTER, "SCOREBOARD");
	ui_text(22, 60, 0.36f, ui_alpha(T_INK, 160), UI_LEFT, "PLAYER");
	ui_text(250, 60, 0.36f, ui_alpha(T_INK, 160), UI_RIGHT, "K");
	ui_text(292, 60, 0.36f, ui_alpha(T_INK, 160), UI_RIGHT, "D");
	int shown = n > 10 ? 10 : n;
	for (int i = 0; i < shown; i++) {
		float y = 72.0f + i * 12.5f;
		u32 col = rows[i].team == 0 ? UI_COL(214, 56, 52) : (rows[i].team == 1 ? UI_COL(40, 100, 220) : T_INK);
		if (rows[i].isPlayer) ui_round_rect(14, y - 1, 292, 13, 5, UI_COLA(255, 208, 64, 200));
		char buf[24];
		snprintf(buf, sizeof(buf), "%d", i + 1);
		ui_text(22, y, 0.38f, ui_alpha(T_INK, 160), UI_LEFT, buf);
		ui_text(40, y, 0.38f, col, UI_LEFT, rows[i].name);
		snprintf(buf, sizeof(buf), "%d", rows[i].kills);
		ui_text(250, y, 0.38f, col, UI_RIGHT, buf);
		snprintf(buf, sizeof(buf), "%d", rows[i].deaths);
		ui_text(292, y, 0.38f, col, UI_RIGHT, buf);
	}
}

void hud_draw_bottom(const Player* p, const Combat* c, const Map* map, const Match* m,
                     const PlayerProfile* prof, bool showScoreboard)
{
	const WeaponDef* d = weapon_def(p->cur);
	const WeaponState* ws = &p->ws[p->cur];
	char buf[48];

	if (!p->alive) {
		if (c->mode == MODE_ELIM) loadout_draw_wait(prof, 1.0f, c->obj.roundPause > 0.0f ? "NEXT ROUND SOON" : "WAITING FOR NEXT ROUND");
		else loadout_draw(prof, p->deadTimer > 0.0f ? p->deadTimer : 0.0f);
		drawTopBar(m, false);
		return;
	}

	ui_backdrop_panel();
	drawTopBar(m, false);
	drawMinimap(p, c, map);

	// Kills and deaths
	ui_card(8, 40, 82, 34, T_CREAM);
	ui_icon_egg_small(24, 57, 17, T_CORAL);
	snprintf(buf, sizeof(buf), "%d", p->kills);
	ui_text(40, 44, 0.62f, T_INK, UI_LEFT, buf);
	ui_card(96, 40, 82, 34, T_CREAM);
	ui_icon_heart(112, 57, 17, T_GRAPE);
	snprintf(buf, sizeof(buf), "%d", p->deaths);
	ui_text(128, 44, 0.62f, T_INK, UI_LEFT, buf);

	// Weapon and ammo
	bool dry = ws->ammo == 0 && ws->reserve == 0;
	ui_card(8, 80, 170, 66, T_CREAM);
	ui_weapon_icon(p->cur, 14, 84, 56, 24);
	ui_text(76, 86, 0.5f, T_INK, UI_LEFT, d->name);
	snprintf(buf, sizeof(buf), "%d / %d", ws->ammo, ws->reserve < 0 ? d->magSize : ws->reserve);
	ui_text(16, 108, 0.85f, dry ? UI_COL(220, 50, 50) : T_INK, UI_LEFT, buf);
	if (ws->reloadLeft > 0.0f) {
		ui_pill_bar(16, 132, 154, 10, 1.0f - ws->reloadLeft / (ws->reloadTotal > 0.0f ? ws->reloadTotal : d->reloadTime), T_YOLK, UI_COL(226, 208, 232));
		ui_text(170, 112, 0.36f, ui_alpha(T_INK, 200), UI_RIGHT, "RELOADING");
	} else if (dry) {
		ui_text(93, 131, 0.4f, UI_COL(220, 50, 50), UI_CENTER, "NO AMMO - FIND A BOX");
	}

	// Grenades under the minimap
	ui_card(184, 170, 128, 28, T_CREAM);
	for (int g = 0; g < 3; g++) {
		float gx = 198.0f + g * 20.0f;
		bool have = g < p->grenades;
		ui_ellipse(gx - 7, 177, 14, 14, have ? T_INK : UI_COL(190, 180, 200));
		ui_ellipse(gx - 5, 179, 10, 10, have ? T_GRASS : UI_COL(226, 218, 232));
		ui_rect(gx - 1, 174, 3, 4, have ? T_INK : UI_COL(190, 180, 200));
	}
	ui_text(256, 176, 0.42f, ui_alpha(T_INK, 220), UI_LEFT, "Y: throw");
	ui_text(16, 145, 0.34f, ui_alpha(T_INK, p->meleeCd > 0.0f ? 110 : 190), UI_LEFT, "D-PAD DOWN: whisk whack");

	// Health
	float hpFrac = p->hp / PLAYER_MAX_HP;
	u32 hpCol = hpFrac > 0.5f ? T_GRASS : (hpFrac > 0.25f ? T_YOLK : T_CORAL);
	bool shielded = p->invuln > 0.0f;
	if (shielded) hpCol = UI_COL(120, 220, 255);
	ui_pill_bar(8, 160, 170, 28, hpFrac, hpCol, UI_COL(226, 208, 232));
	ui_icon_heart(24, 174, 22, hpFrac > 0.25f ? T_CORAL : UI_COL(255, 255, 255));
	if (shielded) snprintf(buf, sizeof(buf), "PROTECTED %.1f", p->invuln);
	else snprintf(buf, sizeof(buf), "%d", p->hp > 0 ? (int)(p->hp + 0.5f) : 0);
	ui_text_shadow(104, 165, 0.55f, UI_COL(255, 255, 255), UI_CENTER, buf);

	drawWeaponStrip(p);
	if (showScoreboard) drawScoreboard(p, c);

	// Overlays
	if (m->phase == PH_COUNTDOWN) {
		ui_card(60, 80, 200, 80, T_CREAM);
		snprintf(buf, sizeof(buf), "%d", (int)ceilf(m->countdown));
		ui_text_shadow(160, 88, 1.6f, T_ORANGE, UI_CENTER, buf);
	}
}

void hud_draw_joining_bottom(float frac, const Match* m)
{
	ui_backdrop_panel();
	ui_title_ribbon("JOINING MATCH", false, 0, false);
	ui_card(30, 56, 260, 112, T_CREAM);
	float bob = fabsf(sinf(frac * 40.0f)) * 6.0f;
	ui_icon_egg_small(160, 92 - bob, 36, T_YOLK);
	ui_text(160, 120, 0.55f, T_INK, UI_CENTER, mode_name(m->cfg.mode));
	ui_text(160, 140, 0.42f, ui_alpha(T_INK, 200), UI_CENTER, map_name(m->cfg.map));
	ui_pill_bar(50, 184, 220, 16, frac, T_GRASS, UI_COL(226, 208, 232));
	ui_text(160, 204, 0.4f, ui_alpha(T_INK, 200), UI_CENTER, "Catching up with the action");
}
