#include "objective.h"
#include "audio.h"
#include "combat.h"
#include "renderer.h"
#include "synth.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static float dist2d(float ax, float az, float bx, float bz)
{
	return sqrtf((ax - bx) * (ax - bx) + (az - bz) * (az - bz));
}

static void say(Combat* c, int team, const char* text)
{
	Objective* o = &c->obj;
	snprintf(o->msg, sizeof(o->msg), "%s", text);
	o->msgT = 3.5f;
	o->msgTeam = team;
}

static const char* teamName(int t) { return t == TEAM_RED ? "RED" : "BLUE"; }

// ------------------------------------------------------------------------ setup
static int nodeNear(const Nav* nav, float x, float z, float y)
{
	return nav_nearest(nav, x, y, z);
}

// True when nothing solid sticks up inside a disc of radius r around the spot (floors below it and
// things overhead, such as a bridge or a gallery higher than a player, do not count).
static bool discClear(const Map* map, float x, float y, float z, float r)
{
	for (int i = 0; i < map->boxCount; i++) {
		const Box* b = &map->boxes[i];
		if (b->max[1] <= y + 0.3f) continue;       // the floor it lies on, or lower
		if (b->min[1] >= y + 2.0f) continue;       // above head height
		float cx = x < b->min[0] ? b->min[0] : (x > b->max[0] ? b->max[0] : x);
		float cz = z < b->min[2] ? b->min[2] : (z > b->max[2] ? b->max[2] : z);
		if (dist2d(cx, cz, x, z) < r) return false;
	}
	return true;
}

// The closest node to a spot that still has clear ground around it
static int nodeNearClear(const Combat* c, float x, float z, float y, float r, float maxDist)
{
	const Nav* nav = &c->nav;
	int best = -1;
	float bestD = 1.0e9f;
	for (int n = 0; n < nav->nodeCount; n++) {
		const NavNode* a = &nav->nodes[n];
		if (!a->reachable || fabsf(a->y - y) > 1.5f) continue;
		float d = dist2d(a->x, a->z, x, z);
		if (d > maxDist || d >= bestD) continue;
		if (!discClear(c->map, a->x, a->y, a->z, r)) continue;
		bestD = d; best = n;
	}
	return best;
}

static void placePan(Combat* c)
{
	const Nav* nav = &c->nav;
	int best = -1;
	float bestScore = -1.0e9f;
	for (int n = 0; n < nav->nodeCount; n++) {
		const NavNode* a = &nav->nodes[n];
		if (!a->reachable) continue;
		float dc = dist2d(a->x, a->z, 0.0f, 0.0f);
		if (dc > 28.0f) continue;
		int flat = 0;
		for (int m = 0; m < nav->nodeCount; m++) {
			const NavNode* b = &nav->nodes[m];
			if (!b->reachable || fabsf(b->y - a->y) > 0.35f) continue;
			if (dist2d(a->x, a->z, b->x, b->z) < PAN_RADIUS) flat++;
		}
		float score = (float)flat - dc * 0.8f;
		if (flat >= 6 && score > bestScore && discClear(c->map, a->x, a->y, a->z, PAN_RADIUS - 0.4f)) { bestScore = score; best = n; }
	}
	if (best < 0) best = nodeNear(nav, 0.0f, 0.0f, 0.0f);
	if (best >= 0) {
		c->obj.panPos[0] = nav->nodes[best].x;
		c->obj.panPos[1] = nav->nodes[best].y;
		c->obj.panPos[2] = nav->nodes[best].z;
	}
	c->obj.holder = -1;
}

static void placeFlags(Combat* c)
{
	for (int t = 0; t < 2; t++) {
		Flag* f = &c->obj.flag[t];
		float bx = c->base[t][0], bz = c->base[t][2];
		float dx = -bx, dz = -bz;                       // toward the middle of the map
		float l = sqrtf(dx * dx + dz * dz);
		if (l > 1.0e-3f) { dx /= l; dz /= l; }
		int n = nodeNearClear(c, bx + dx * 7.0f, bz + dz * 7.0f, c->base[t][1], 1.6f, 6.0f);   // clear of walls, pools and crates
		if (n < 0) n = nodeNear(&c->nav, bx + dx * 7.0f, bz + dz * 7.0f, c->base[t][1]);
		if (n >= 0) { f->home[0] = c->nav.nodes[n].x; f->home[1] = c->nav.nodes[n].y; f->home[2] = c->nav.nodes[n].z; }
		else        { f->home[0] = bx; f->home[1] = c->base[t][1]; f->home[2] = bz; }
		for (int k = 0; k < 3; k++) f->pos[k] = f->home[k];
		f->carrier = -1;
		f->atHome = true;
		f->returnTimer = 0.0f;
	}
}

static void countRound(Combat* c, const Player* p)
{
	Objective* o = &c->obj;
	o->roundSize[0] = o->roundSize[1] = 0;
	for (int id = 0; id <= c->botCount; id++) {
		int t = combat_team_of(c, p, id);
		if (t == 0 || t == 1) o->roundSize[t]++;
	}
}

void obj_recount(Combat* c)
{
	if (c->mode != MODE_ELIM) return;
	countRound(c, NULL);
	c->obj.aliveCount[0] = c->obj.roundSize[0];
	c->obj.aliveCount[1] = c->obj.roundSize[1];
}

void obj_init(Combat* c)
{
	memset(&c->obj, 0, sizeof(c->obj));
	c->obj.holder = -1;
	c->obj.roundWinner = -1;
	c->obj.round = 1;
	c->obj.msgTeam = -1;
	if (c->mode == MODE_KOTP) placePan(c);
	if (c->mode == MODE_CTF) placeFlags(c);
	if (c->mode == MODE_ELIM) {
		// The human is not in the bot list yet; count the team they will join
		c->obj.roundSize[0] = c->obj.roundSize[1] = 0;
		for (int i = 0; i < c->botCount; i++) c->obj.roundSize[c->bots[i].team & 1]++;
		if (!c->noPlayer) c->obj.roundSize[c->playerTeam & 1]++;
		c->obj.aliveCount[0] = c->obj.roundSize[0];
		c->obj.aliveCount[1] = c->obj.roundSize[1];
	}
}

// ------------------------------------------------------------------------ flags
static void dropFlag(Combat* c, int f, const float pos[3])
{
	Flag* fl = &c->obj.flag[f];
	fl->carrier = -1;
	fl->atHome = false;
	fl->returnTimer = FLAG_RETURN_TIME;
	for (int k = 0; k < 3; k++) fl->pos[k] = pos[k];
}

static void returnFlag(Combat* c, int f)
{
	Flag* fl = &c->obj.flag[f];
	fl->carrier = -1;
	fl->atHome = true;
	fl->returnTimer = 0.0f;
	for (int k = 0; k < 3; k++) fl->pos[k] = fl->home[k];
}

void obj_on_death(Combat* c, int id, const float pos[3])
{
	if (c->mode != MODE_CTF) return;
	for (int f = 0; f < 2; f++) {
		if (c->obj.flag[f].carrier != id) continue;
		dropFlag(c, f, pos);
		char m[56];
		snprintf(m, sizeof(m), "%s FLAG DROPPED", teamName(f));
		say(c, f, m);
		audio_play(SFX_SWITCH, 0.0f, 1.0f);
	}
}

static void updateFlags(Combat* c, Player* p, float dt)
{
	Objective* o = &c->obj;
	for (int f = 0; f < 2; f++) {
		Flag* fl = &o->flag[f];
		if (fl->carrier >= 0) {
			if (!combat_alive(c, p, fl->carrier)) {      // safety net: the carrier is gone
				float last[3] = { fl->pos[0], fl->pos[1], fl->pos[2] };
				dropFlag(c, f, last);
			} else {
				combat_pos(c, p, fl->carrier, fl->pos);
			}
		}
		if (fl->carrier >= 0) continue;

		if (!fl->atHome) {
			fl->returnTimer -= dt;
			if (fl->returnTimer <= 0.0f) {
				returnFlag(c, f);
				char m[56];
				snprintf(m, sizeof(m), "%s FLAG RETURNED", teamName(f));
				say(c, f, m);
			}
		}

		for (int id = 0; id <= c->botCount && fl->carrier < 0; id++) {
			if (!combat_alive(c, p, id)) continue;
			int t = combat_team_of(c, p, id);
			float pos[3];
			combat_pos(c, p, id, pos);
			if (dist2d(pos[0], pos[2], fl->pos[0], fl->pos[2]) > FLAG_TOUCH || fabsf(pos[1] - fl->pos[1]) > 2.2f) continue;
			char m[56];
			if (t == f) {
				if (!fl->atHome) {                          // your own dropped flag: touch it to send it home
					returnFlag(c, f);
					snprintf(m, sizeof(m), "%s FLAG RETURNED", teamName(f));
					say(c, f, m);
					audio_play(SFX_SWITCH, 0.0f, 1.0f);
				}
			} else {                                        // an enemy takes it
				fl->carrier = id;
				fl->atHome = false;
				snprintf(m, sizeof(m), "%s TEAM TOOK THE %s FLAG", teamName(t), teamName(f));
				say(c, t, m);
				audio_play(t == c->playerTeam ? SFX_GO : SFX_EMPTY, 0.0f, 1.0f);
			}
		}
	}

	// Bringing the enemy flag to your own stand scores, but only while your own flag is home
	for (int g = 0; g < 2; g++) {
		int carrier = o->flag[g].carrier;
		if (carrier < 0) continue;
		int t = 1 - g;
		if (!o->flag[t].atHome) continue;
		float pos[3];
		combat_pos(c, p, carrier, pos);
		if (dist2d(pos[0], pos[2], o->flag[t].home[0], o->flag[t].home[2]) > FLAG_TOUCH + 0.5f || fabsf(pos[1] - o->flag[t].home[1]) > 2.2f) continue;
		c->teamScore[t]++;
		returnFlag(c, g);
		char m[56];
		snprintf(m, sizeof(m), "%s TEAM CAPTURED THE FLAG!", teamName(t));
		say(c, t, m);
		audio_play(t == c->playerTeam ? SFX_BUY : SFX_HURT, 0.0f, 1.0f);
	}
}

// ------------------------------------------------------------------ king of the pan
static void updatePan(Combat* c, Player* p, float dt)
{
	Objective* o = &c->obj;
	int cnt[2] = { 0, 0 };
	for (int id = 0; id <= c->botCount; id++) {
		if (!combat_alive(c, p, id)) continue;
		int t = combat_team_of(c, p, id);
		if (t < 0) continue;
		float pos[3];
		combat_pos(c, p, id, pos);
		if (dist2d(pos[0], pos[2], o->panPos[0], o->panPos[2]) < PAN_RADIUS && fabsf(pos[1] - o->panPos[1]) < 3.0f) cnt[t]++;
	}
	int holder = (cnt[0] > 0 && cnt[1] > 0) ? 2 : (cnt[0] > 0 ? 0 : (cnt[1] > 0 ? 1 : -1));
	if (holder != o->holder && holder >= 0 && holder < 2 && o->holder != 2) {
		char m[56];
		snprintf(m, sizeof(m), "%s TEAM HOLDS THE PAN", teamName(holder));
		say(c, holder, m);
	}
	o->holder = holder;
	if (holder == 0 || holder == 1) {
		o->pointAcc[holder] += dt;
		while (o->pointAcc[holder] >= 1.0f) { o->pointAcc[holder] -= 1.0f; c->teamScore[holder]++; }
	}
}

// ------------------------------------------------------------------ team elimination
static void updateElim(Combat* c, Player* p, float dt)
{
	Objective* o = &c->obj;
	if (o->roundPause > 0.0f) {
		o->roundPause -= dt;
		if (o->roundPause <= 0.0f) {
			o->roundPause = 0.0f;
			o->round++;
			o->roundWinner = -1;
			o->roundTime = 0.0f;
			memset(c->proj, 0, sizeof(c->proj));
			for (int i = 0; i < c->botCount; i++) bot_respawn(c, p, &c->bots[i]);
			o->newRound = true;               // the game puts the human back in
			countRound(c, p);
			char m[56];
			snprintf(m, sizeof(m), "ROUND %d", o->round);
			say(c, -1, m);
			audio_play(SFX_GO, 0.0f, 1.0f);
		}
		return;
	}

	o->roundTime += dt;
	o->aliveCount[0] = o->aliveCount[1] = 0;
	for (int id = 0; id <= c->botCount; id++) {
		if (!combat_alive(c, p, id)) continue;
		int t = combat_team_of(c, p, id);
		if (t == 0 || t == 1) o->aliveCount[t]++;
	}
	if (o->roundSize[0] == 0 || o->roundSize[1] == 0) return;     // one side is empty: nothing to eliminate
	if (o->aliveCount[0] > 0 && o->aliveCount[1] > 0) return;

	int w = o->aliveCount[0] > 0 ? 0 : (o->aliveCount[1] > 0 ? 1 : 2);
	o->roundWinner = w;
	o->roundPause = ROUND_PAUSE;
	char m[56];
	if (w == 2) { snprintf(m, sizeof(m), "DRAW - NOBODY IS LEFT"); say(c, -1, m); }
	else {
		c->teamScore[w]++;
		snprintf(m, sizeof(m), "%s TEAM WINS THE ROUND!", teamName(w));
		say(c, w, m);
		audio_play(w == c->playerTeam ? SFX_BUY : SFX_HURT, 0.0f, 1.0f);
	}
}

void obj_update(Combat* c, Player* p, float dt)
{
	Objective* o = &c->obj;
	if (o->msgT > 0.0f) o->msgT -= dt;
	if (c->frozen) return;
	switch (c->mode) {
	case MODE_KOTP: updatePan(c, p, dt); break;
	case MODE_CTF:  updateFlags(c, p, dt); break;
	case MODE_ELIM: updateElim(c, p, dt); break;
	default: break;
	}
}

int obj_status(const Combat* c, char* out, int n)
{
	const Objective* o = &c->obj;
	out[0] = '\0';
	switch (c->mode) {
	case MODE_KOTP:
		snprintf(out, n, "PAN: %s", o->holder == 0 ? "RED HOLDS IT" : o->holder == 1 ? "BLUE HOLDS IT" : o->holder == 2 ? "CONTESTED" : "EMPTY");
		break;
	case MODE_CTF: {
		const char* st[2];
		for (int f = 0; f < 2; f++) st[f] = o->flag[f].carrier >= 0 ? "TAKEN" : (o->flag[f].atHome ? "HOME" : "DROPPED");
		snprintf(out, n, "RED FLAG %s   BLUE FLAG %s", st[0], st[1]);
		break;
	}
	case MODE_ELIM:
		if (o->roundPause > 0.0f) snprintf(out, n, "ROUND %d OVER", o->round);
		else snprintf(out, n, "ROUND %d   RED %d LEFT   BLUE %d LEFT", o->round, o->aliveCount[0], o->aliveCount[1]);
		break;
	default: break;
	}
	return (int)strlen(out);
}

// ------------------------------------------------------------------------ drawing
static void tri(const float a[3], const float b[3], const float c[3], const float col[3])
{
	const float p[3][3] = { { a[0], a[1], a[2] }, { b[0], b[1], b[2] }, { c[0], c[1], c[2] } };
	const float cc[3][3] = { { col[0], col[1], col[2] }, { col[0], col[1], col[2] }, { col[0], col[1], col[2] } };
	renderer_world_tri(p, cc);
}

static void flatRing(float cx, float y, float cz, float r0, float r1, int segs, const float col[3])
{
	for (int i = 0; i < segs; i++) {
		float a0 = 6.2831853f * (float)i / (float)segs, a1 = 6.2831853f * (float)(i + 1) / (float)segs;
		float p0[3] = { cx + cosf(a0) * r0, y, cz + sinf(a0) * r0 };
		float p1[3] = { cx + cosf(a1) * r0, y, cz + sinf(a1) * r0 };
		float q0[3] = { cx + cosf(a0) * r1, y, cz + sinf(a0) * r1 };
		float q1[3] = { cx + cosf(a1) * r1, y, cz + sinf(a1) * r1 };
		tri(p0, p1, q1, col);
		tri(p0, q1, q0, col);
	}
}

static const float RED_COL[3]  = { 0.92f, 0.22f, 0.22f };
static const float BLUE_COL[3] = { 0.22f, 0.42f, 0.95f };

static void drawFlag(const Flag* f, int team, bool carried, const float carrierPos[3])
{
	const float* col = team == TEAM_RED ? RED_COL : BLUE_COL;
	float x = f->pos[0], y = f->pos[1], z = f->pos[2];
	if (carried) {            // held high above the carrier so everyone can spot them
		y = carrierPos[1] + 1.75f;
		renderer_world_box(x, y + 0.55f, z, 0.04f, 0.55f, 0.04f, 0.85f, 0.85f, 0.88f);
		renderer_world_box(x + 0.32f, y + 0.95f, z, 0.32f, 0.2f, 0.02f, col[0], col[1], col[2]);
		return;
	}
	renderer_world_box(x, y + 0.05f, z, 0.7f, 0.05f, 0.7f, col[0] * 0.7f, col[1] * 0.7f, col[2] * 0.7f);   // base plate
	renderer_world_box(x, y + 1.35f, z, 0.05f, 1.35f, 0.05f, 0.85f, 0.85f, 0.88f);                          // pole
	renderer_world_box(x + 0.55f, y + 2.25f, z, 0.55f, 0.33f, 0.02f, col[0], col[1], col[2]);              // banner
}

void obj_draw(const Combat* c)
{
	const Objective* o = &c->obj;
	if (c->mode == MODE_KOTP) {
		float x = o->panPos[0], y = o->panPos[1] + 0.04f, z = o->panPos[2];
		float col[3] = { 0.78f, 0.78f, 0.82f };
		if (o->holder == 0) { col[0] = RED_COL[0]; col[1] = RED_COL[1]; col[2] = RED_COL[2]; }
		else if (o->holder == 1) { col[0] = BLUE_COL[0]; col[1] = BLUE_COL[1]; col[2] = BLUE_COL[2]; }
		else if (o->holder == 2) { bool on = (c->frame / 8) & 1; col[0] = on ? 1.0f : 0.95f; col[1] = on ? 0.85f : 0.45f; col[2] = on ? 0.2f : 0.2f; }
		const float dark[3] = { 0.16f, 0.16f, 0.18f };
		flatRing(x, y, z, 0.0f, PAN_RADIUS - 0.35f, 24, col);               // the pan floor
		flatRing(x, y + 0.01f, z, PAN_RADIUS - 0.35f, PAN_RADIUS, 24, dark); // its rim
		renderer_world_box(x + PAN_RADIUS + 1.5f, y + 0.05f, z, 1.5f, 0.05f, 0.28f, 0.30f, 0.20f, 0.12f);   // handle
		renderer_world_box(x, y + 5.0f, z, 0.1f, 5.0f, 0.1f, col[0], col[1], col[2]);                        // beam, seen from afar
	}
	if (c->mode == MODE_CTF) {
		for (int t = 0; t < 2; t++) {
			const Flag* f = &o->flag[t];
			drawFlag(f, t, f->carrier >= 0, f->pos);
			if (f->carrier >= 0) {   // the empty stand stays visible
				Flag home = *f;
				for (int k = 0; k < 3; k++) home.pos[k] = f->home[k];
				const float* col = t == TEAM_RED ? RED_COL : BLUE_COL;
				renderer_world_box(home.pos[0], home.pos[1] + 0.05f, home.pos[2], 0.7f, 0.05f, 0.7f, col[0] * 0.45f, col[1] * 0.45f, col[2] * 0.45f);
			}
		}
	}
}
