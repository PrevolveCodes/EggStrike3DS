#include "hud.h"
#include "config.h"
#include <math.h>
#include <stdio.h>
#include "renderer.h"
#include "combat.h"
#include "scoreboard.h"
#include "ui.h"

bool hud_touch_is_ui(int x, int y)
{
	if (y >= HUD_STRIP_Y) return true;
	return hud_pause_hit(x, y);
}

int hud_weapon_at(int x, int y)
{
	if (y < HUD_STRIP_Y || y >= HUD_STRIP_Y + HUD_STRIP_H) return -1;
	return x < 160 ? 0 : 1;
}

bool hud_pause_hit(int x, int y)
{
	return x >= HUD_PAUSE_X && x < HUD_PAUSE_X + HUD_PAUSE_W &&
	       y >= HUD_PAUSE_Y && y < HUD_PAUSE_Y + HUD_PAUSE_H;
}

#define HUD_Z     (-0.25f)
#define HALF_W    0.2917f   // half screen width at HUD_Z for the 70 degree view
#define HALF_H    0.175f

void hud_draw_view(const Player* p)
{
	if (!p->alive) {            // the map flyover plays behind; just frame it with cinema bars
		hud_draw_intro_view();
		return;
	}

	const WeaponDef* d = weapon_def(p->cur);
	bool scoped = p->aiming && d->scoped;

	// Where the next shot can land: the real spread cone drawn on the HUD plane (it follows the zoom,
	// because zooming narrows the view). Standing still it is tight; walking or jumping it opens up.
	float cone = weapon_spread(d, &p->ws[p->cur], p->aiming, p->bloom);
	// Same mapping as the real view: offset on the HUD plane = distance * tan(angle) * zoom
	float spreadGap = tanf(cone) * 0.25f * (p->zoom > 1.0f ? p->zoom : 1.0f);
	if (spreadGap > 0.15f) spreadGap = 0.15f;

	if (scoped) {
		// Round scope: everything outside a circle is black (side bars, top/bottom bars and four
		// corner wedges between the square and the circle), so the view looks like a lens.
		const float z = HUD_Z + 0.01f;
		const float R = 0.15f, EW = 0.32f, EH = 0.20f;     // circle radius, edge of the screen at this depth
		renderer_view_box(-(R + EW) * 0.5f, 0, z, (EW - R) * 0.5f, EH, 0.0005f, 0, 0, 0);
		renderer_view_box( (R + EW) * 0.5f, 0, z, (EW - R) * 0.5f, EH, 0.0005f, 0, 0, 0);
		renderer_view_box(0,  (R + EH) * 0.5f, z, R, (EH - R) * 0.5f, 0.0005f, 0, 0, 0);
		renderer_view_box(0, -(R + EH) * 0.5f, z, R, (EH - R) * 0.5f, 0.0005f, 0, 0, 0);
		static const float blk[3][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };
		const int SEG = 14;                                   // per quarter turn
		for (int qx = -1; qx <= 1; qx += 2) {
			for (int qy = -1; qy <= 1; qy += 2) {
				for (int k = 0; k < SEG; k++) {
					const float a0 = 1.5707963f * (float)k / (float)SEG, a1 = 1.5707963f * (float)(k + 1) / (float)SEG;
					const float t[3][3] = { { qx * R, qy * R, z },
					                        { qx * R * cosf(a0), qy * R * sinf(a0), z },
					                        { qx * R * cosf(a1), qy * R * sinf(a1), z } };
					renderer_view_tri(t, blk);
				}
			}
		}
		// Scope reticle: four arms that start at the shot cone and run out to the edge of the lens,
		// two pixels thick so they can be seen on the 3DS screen, plus a centre dot.
		float cr = 0.0f, cg = 0.0f, cb = 0.0f;
		if (p->hitMarkerT > 0.0f) { cr = 0.95f; }
		float g = 0.004f + spreadGap;
		if (g > R - 0.03f) g = R - 0.03f;
		const float end = R - 0.002f, len = (end - g) * 0.5f, th = 0.0012f;
		renderer_view_box(-(g + len), 0, HUD_Z, len, th, 0.0005f, cr, cg, cb);
		renderer_view_box( (g + len), 0, HUD_Z, len, th, 0.0005f, cr, cg, cb);
		renderer_view_box(0,  (g + len), HUD_Z, th, len, 0.0005f, cr, cg, cb);
		renderer_view_box(0, -(g + len), HUD_Z, th, len, 0.0005f, cr, cg, cb);
		renderer_view_box(0, 0, HUD_Z, 0.0018f, 0.0018f, 0.0005f, cr, cg, cb);
	} else if (d->brackets) {
		// Shotgun: four fixed brackets around the pellet zone. Walking does not change them; aiming
		// down sights tightens them in proportion to the tighter cone.
		float r = 1.0f, g = 1.0f, b = 1.0f;
		if (p->hitMarkerT > 0.0f) { g = 0.2f; b = 0.2f; }
		const float k  = cone / (d->hipSpread > 0.0f ? d->hipSpread : d->spread);
		const float vy = 0.0875f * k, hx = 0.151f * k;      // top/bottom and left/right distances
		const float bw = 0.028f * k, bh = 0.0146f, tick = 0.011f, th = 0.0012f;
		// top and bottom: a horizontal bar with short ticks turned toward the centre
		for (int s = -1; s <= 1; s += 2) {
			renderer_view_box(0, s * vy, HUD_Z, bw, th, 0.0005f, r, g, b);
			for (int e = -1; e <= 1; e += 2)
				renderer_view_box(e * bw, s * (vy - tick * 0.5f), HUD_Z, th, tick * 0.5f, 0.0005f, r, g, b);
		}
		// left and right: a vertical bar with short ticks turned toward the centre
		for (int s = -1; s <= 1; s += 2) {
			renderer_view_box(s * hx, 0, HUD_Z, th, bh, 0.0005f, r, g, b);
			for (int e = -1; e <= 1; e += 2)
				renderer_view_box(s * (hx - tick * 0.5f), e * bh, HUD_Z, tick * 0.5f, th, 0.0005f, r, g, b);
		}
		renderer_view_box(0, 0, HUD_Z, 0.0014f, 0.0014f, 0.0005f, r, g, b);   // centre dot
	} else {
		float r = 1.0f, g = 1.0f, b = 1.0f;
		if (p->hitMarkerT > 0.0f) { g = 0.2f; b = 0.2f; }
		const float gap = 0.004f + spreadGap, len = 0.0117f, th = 0.0008f;   // ticks are 8 pixels long
		renderer_view_box(-(gap + len * 0.5f), 0, HUD_Z, len * 0.5f, th, 0.0005f, r, g, b);
		renderer_view_box( (gap + len * 0.5f), 0, HUD_Z, len * 0.5f, th, 0.0005f, r, g, b);
		renderer_view_box(0,  (gap + len * 0.5f), HUD_Z, th, len * 0.5f, 0.0005f, r, g, b);
		renderer_view_box(0, -(gap + len * 0.5f), HUD_Z, th, len * 0.5f, 0.0005f, r, g, b);
	}

	if (p->invuln > 0.0f) {                      // protected: a pale blue frame
		const float t = 0.008f;
		renderer_view_box(0,  HALF_H - t * 0.5f, HUD_Z, HALF_W + 0.01f, t * 0.5f + 0.002f, 0.0005f, 0.4f, 0.85f, 1.0f);
		renderer_view_box(0, -HALF_H + t * 0.5f, HUD_Z, HALF_W + 0.01f, t * 0.5f + 0.002f, 0.0005f, 0.4f, 0.85f, 1.0f);
		renderer_view_box(-HALF_W + t * 0.5f, 0, HUD_Z, t * 0.5f + 0.002f, HALF_H + 0.01f, 0.0005f, 0.4f, 0.85f, 1.0f);
		renderer_view_box( HALF_W - t * 0.5f, 0, HUD_Z, t * 0.5f + 0.002f, HALF_H + 0.01f, 0.0005f, 0.4f, 0.85f, 1.0f);
	}

	if (p->damageFlashT > 0.0f) {
		float k = p->damageFlashT / 0.4f;
		if (k > 1.0f) k = 1.0f;
		const float t = 0.012f;
		renderer_view_box(0,  HALF_H - t * 0.5f, HUD_Z, HALF_W + 0.01f, t * 0.5f + 0.004f, 0.0005f, k, 0, 0);
		renderer_view_box(0, -HALF_H + t * 0.5f, HUD_Z, HALF_W + 0.01f, t * 0.5f + 0.004f, 0.0005f, k, 0, 0);
		renderer_view_box(-HALF_W + t * 0.5f, 0, HUD_Z, t * 0.5f + 0.004f, HALF_H + 0.01f, 0.0005f, k, 0, 0);
		renderer_view_box( HALF_W - t * 0.5f, 0, HUD_Z, t * 0.5f + 0.004f, HALF_H + 0.01f, 0.0005f, k, 0, 0);
	}
}

void hud_draw_intro_view(void)
{
	renderer_view_box(0,  HALF_H - 0.012f, HUD_Z, HALF_W + 0.02f, 0.020f, 0.0005f, 0.02f, 0.02f, 0.05f);
	renderer_view_box(0, -HALF_H + 0.012f, HUD_Z, HALF_W + 0.02f, 0.020f, 0.0005f, 0.02f, 0.02f, 0.05f);
}

// ------------------------------------------------------------------ leaderboard
// Small list in the top-left corner of the top screen. In team modes the two teams are listed
// apart (your team first, each sorted by kills); otherwise one list. Your row is highlighted and
// fighters who are down are faded.
#define LB_X 5.0f
#define LB_Y 5.0f
#define LB_W 108.0f
#define LB_H 12.0f
#define LB_GAP 1.5f

static void lbRow(float y, const ScoreRow* r, int style)
{
	// style 0 red team, 1 blue team, 2 free for all
	u32 bg, fg = UI_COL(255, 255, 255);
	if (style == 0)      bg = UI_COLA(240, 84, 78, 225);
	else if (style == 1) bg = UI_COLA(70, 150, 255, 225);
	else                 bg = UI_COLA(130, 110, 170, 220);
	if (r->isPlayer)     { bg = UI_COLA(255, 208, 64, 245); fg = T_INK; }
	if (!r->alive && !r->isPlayer) {
		bg = UI_COLA(170, 170, 185, 130);
		fg = UI_COLA(255, 255, 255, 190);
	}
	ui_round_rect(LB_X, y, LB_W, LB_H, 3, bg);
	ui_text(LB_X + 5.0f, y + 0.5f, 0.36f, fg, UI_LEFT, r->name);
	char kb[8];
	snprintf(kb, sizeof(kb), "%d", r->kills);
	ui_text(LB_X + LB_W - 5.0f, y + 0.5f, 0.36f, fg, UI_RIGHT, kb);
}

void hud_draw_leaderboard(const Player* p, const Combat* c)
{
	ScoreRow rows[MAX_FIGHTERS];
	int n = combat_scoreboard(c, p, rows, MAX_FIGHTERS);   // already sorted by kills
	float y = LB_Y;
	if (combat_is_team_mode(c)) {
		const int first = (c->playerTeam == 1) ? 1 : 0, second = 1 - first;
		const int order[2] = { first, second };
		for (int t = 0; t < 2; t++) {
			for (int i = 0; i < n; i++) {
				if (rows[i].team != order[t]) continue;
				lbRow(y, &rows[i], order[t] == 0 ? 0 : 1);
				y += LB_H + LB_GAP;
			}
			y += 3.0f;     // a little space between the teams
		}
	} else {
		for (int i = 0; i < n; i++) {
			lbRow(y, &rows[i], 2);
			y += LB_H + LB_GAP;
		}
	}
}

void hud_draw_respawn(const Player* p)
{
	if (!p->alive || p->despawnT <= 0.0f) return;
	float k = 1.0f - p->despawnT / DESPAWN_TIME;
	ui_text_shadow(200, 150, 0.55f, UI_COL(255, 255, 255), UI_CENTER, "RESPAWNING");
	ui_pill_bar(140, 170, 120, 12, k, T_YOLK, UI_COL(226, 208, 232));
	ui_textf(200, 186, 0.4f, UI_COL(255, 255, 255), UI_CENTER, "%.1f", p->despawnT);
}

// Top-centre status line for the objective modes plus the latest event ("BLUE TEAM CAPTURED THE FLAG!")
void hud_draw_objective(const Player* p, const Combat* c)
{
	(void)p;
	const Objective* o = &c->obj;
	char line[64];
	if (obj_status(c, line, sizeof(line)) > 0) {
		float w = ui_text_width(line, 0.5f) + 22.0f;
		ui_round_rect(200 - w * 0.5f, 4, w, 20, 9, UI_COLA(58, 38, 78, 190));
		ui_text(200, 6, 0.5f, UI_COL(255, 255, 255), UI_CENTER, line);
	}

	if (o->msgT > 0.0f && o->msg[0]) {
		u32 col = o->msgTeam == TEAM_RED ? UI_COL(255, 130, 120) : (o->msgTeam == TEAM_BLUE ? UI_COL(130, 190, 255) : UI_COL(255, 230, 130));
		float mw = ui_text_width(o->msg, 0.6f) + 24.0f;
		ui_round_rect(200 - mw * 0.5f, 27, mw, 24, 10, UI_COLA(58, 38, 78, 200));
		ui_text(200, 29, 0.6f, col, UI_CENTER, o->msg);
	}
	if (c->mode == MODE_ELIM && o->roundPause > 0.0f && o->roundWinner >= 0)
		ui_textf(200, 56, 0.42f, UI_COL(255, 255, 255), UI_CENTER, "Next round in %d", (int)(o->roundPause + 0.99f));
	if (c->mode == MODE_CTF) {
		int carry = -1;
		for (int f = 0; f < 2; f++) if (o->flag[f].carrier == FIGHTER_PLAYER) carry = f;
		if (carry >= 0) ui_text_shadow(200, 56, 0.5f, UI_COL(255, 230, 120), UI_CENTER, "YOU HAVE THE FLAG - GET BACK TO YOUR STAND");
	}
}

// Bottom-left of the top screen: your streak, shield and the timed perks; a banner when you earn one
void hud_draw_streak(const Player* p, const Combat* c)
{
	if (!p->alive) return;
	const Perks* k = &p->perks;
	float y = 190.0f;
	if (k->crackT > 0.0f) { ui_textf(8, y, 0.4f, UI_COL(255, 120, 90), UI_LEFT, "CRACK SHOT %.0fs", k->crackT + 0.5f); y -= 14.0f; }
	if (k->hotT > 0.0f)   { ui_textf(8, y, 0.4f, UI_COL(255, 200, 70), UI_LEFT, "HOT YOLK %.0fs", k->hotT + 0.5f); y -= 14.0f; }
	if (k->shield > 0.0f) { ui_textf(8, y, 0.4f, UI_COL(120, 200, 255), UI_LEFT, "SHIELD %d", (int)(k->shield + 0.5f)); y -= 14.0f; }
	if (k->streak >= 2)   { ui_textf(8, y, 0.45f, UI_COL(255, 255, 255), UI_LEFT, "STREAK %d", k->streak); }
	if (c->perkMsgT > 0.0f && c->perkMsg[0])
		ui_text_shadow(200, 78, 0.7f, UI_COL(255, 225, 90), UI_CENTER, c->perkMsg);
}

// ------------------------------------------------------------ health and ammo (top screen)
// A heart with a health bar bottom-left and the weapon with its ammo bottom-right, so you can
// keep your eyes on the action. Drawn in 2D after the 3D view.
void hud_draw_status(const Player* p, const Combat* c, const Match* m)
{
	(void)c; (void)m;
	if (!p->alive) return;
	const WeaponDef* d = weapon_def(p->cur);
	const WeaponState* ws = &p->ws[p->cur];

	float frac = p->hp / PLAYER_MAX_HP;
	u32 col = frac > 0.5f ? T_GRASS : (frac > 0.25f ? T_YOLK : T_CORAL);
	if (p->invuln > 0.0f) col = UI_COL(120, 220, 255);
	ui_pill_bar(26, 214, 112, 16, frac, col, UI_COL(58, 38, 78));
	ui_icon_heart(18, 223, 24, frac > 0.25f ? T_CORAL : UI_COL(255, 255, 255));
	char buf[24];
	snprintf(buf, sizeof(buf), "%d", p->hp > 0 ? (int)(p->hp + 0.5f) : 0);
	ui_text_shadow(82, 214, 0.45f, UI_COL(255, 255, 255), UI_CENTER, buf);

	// ammo
	bool dry = ws->ammo == 0 && ws->reserve == 0;
	float bx = 262, bw = 132;
	ui_round_rect(bx - 2, 202, bw + 4, 34, 10, UI_COLA(58, 38, 78, 200));
	ui_weapon_icon(p->cur, bx + 2, 206, 44, 26);
	snprintf(buf, sizeof(buf), "%d", ws->ammo);
	ui_text(bx + bw - 44, 204, 0.8f, dry ? T_CORAL : UI_COL(255, 255, 255), UI_RIGHT, buf);
	snprintf(buf, sizeof(buf), "/%d", ws->reserve < 0 ? d->magSize : ws->reserve);
	ui_text(bx + bw - 42, 216, 0.45f, UI_COL(255, 236, 170), UI_LEFT, buf);
	if (ws->reloadLeft > 0.0f) ui_pill_bar(bx + 4, 195, bw - 8, 8, 1.0f - ws->reloadLeft / (ws->reloadTotal > 0.0f ? ws->reloadTotal : d->reloadTime), T_YOLK, UI_COL(58, 38, 78));
	if (dry) ui_text_shadow(330, 178, 0.45f, UI_COL(255, 150, 140), UI_CENTER, "NO AMMO - FIND A BOX");
}

// ------------------------------------------------------------ while you wait to respawn
// Drawn over the map flyover: who got you and the respawn countdown.
void hud_draw_death(const Player* p, const Combat* c)
{
	if (p->alive) return;
	char line[48];
	int k = p->lastAttacker;
	if (k >= 1 && k <= c->botCount) snprintf(line, sizeof(line), "Cracked by %s", c->bots[k - 1].name);
	else if (k == FIGHTER_PLAYER) snprintf(line, sizeof(line), "You cracked yourself");
	else snprintf(line, sizeof(line), "You took a tumble");
	float w = ui_text_width(line, 0.7f) + 36.0f;
	ui_round_rect(200 - w * 0.5f, 170, w, 28, 12, UI_COLA(58, 38, 78, 215));
	ui_text(200, 173, 0.7f, UI_COL(255, 255, 255), UI_CENTER, line);
	if (c->mode == MODE_ELIM) ui_text_shadow(200, 202, 0.5f, T_YOLK, UI_CENTER, "Watching until the next round");
	else if (p->deadTimer > 0.0f) {
		char t[32];
		snprintf(t, sizeof(t), "Back in %.1f", p->deadTimer);
		ui_text_shadow(200, 202, 0.55f, T_YOLK, UI_CENTER, t);
	} else ui_text_shadow(200, 202, 0.55f, T_YOLK, UI_CENTER, "Pick a weapon and deploy!");
}

// ------------------------------------------------------------ joining a match
void hud_draw_joining_top(float frac)
{
	float w = 190.0f;
	ui_round_rect(200 - w * 0.5f, 92, w, 56, 14, UI_COLA(58, 38, 78, 215));
	ui_text_shadow(200, 98, 0.62f, UI_COL(255, 255, 255), UI_CENTER, "Joining match...");
	ui_pill_bar(200 - 70, 126, 140, 12, frac, T_YOLK, UI_COL(226, 208, 232));
}
