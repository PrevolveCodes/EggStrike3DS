#include "pickups.h"
#include "audio.h"
#include "combat.h"
#include "renderer.h"
#include "synth.h"
#include <math.h>
#include <stdio.h>

static float dist2d(float ax, float az, float bx, float bz)
{
	return sqrtf((ax - bx) * (ax - bx) + (az - bz) * (az - bz));
}

// Picks spots that are reachable, away from spawns, flags and the pan, and spread out from each other
void ammo_init(Combat* c)
{
	c->ammoBoxCount = 0;
	const Nav* nav = &c->nav;
	const Map* map = c->map;

	float ax[MAX_SPAWNS + 8 + AMMO_BOX_MAX], az[MAX_SPAWNS + 8 + AMMO_BOX_MAX];
	int an = 0;
	for (int i = 0; i < map->spawnCount && an < MAX_SPAWNS; i++) { ax[an] = map->spawns[i].x; az[an] = map->spawns[i].z; an++; }
	if (c->mode == MODE_KOTP) { ax[an] = c->obj.panPos[0]; az[an] = c->obj.panPos[2]; an++; }
	if (c->mode == MODE_CTF) for (int t = 0; t < 2; t++) { ax[an] = c->obj.flag[t].home[0]; az[an] = c->obj.flag[t].home[2]; an++; }

	float cx = 0.0f, cz = 0.0f; int cn = 0;
	for (int i = 0; i < nav->nodeCount; i++) if (nav->nodes[i].reachable) { cx += nav->nodes[i].x; cz += nav->nodes[i].z; cn++; }
	if (cn == 0) return;
	cx /= cn; cz /= cn;

	for (int k = 0; k < AMMO_BOX_COUNT; k++) {
		int best = -1;
		float bestScore = -1.0e9f;
		for (int i = 0; i < nav->nodeCount; i++) {
			const NavNode* n = &nav->nodes[i];
			if (!n->reachable || n->nbrCount < 3) continue;
			float d = 1.0e9f;
			for (int a = 0; a < an; a++) { float da = dist2d(n->x, n->z, ax[a], az[a]); if (da < d) d = da; }
			if (d < 6.0f) continue;
			float score = fminf(d, 24.0f) - 0.35f * dist2d(n->x, n->z, cx, cz);   // spread out, but not all in the far corners
			if (score > bestScore) { bestScore = score; best = i; }
		}
		if (best < 0) break;
		AmmoBox* b = &c->ammoBox[c->ammoBoxCount++];
		b->pos[0] = nav->nodes[best].x; b->pos[1] = nav->nodes[best].y; b->pos[2] = nav->nodes[best].z;
		b->timer = 0.0f;
		ax[an] = b->pos[0]; az[an] = b->pos[2]; an++;
	}
}

static bool needsAmmo(const Player* p)
{
	if (p->grenades < GRENADE_MAX) return true;
	for (int s = 0; s < 2; s++) {
		int w = p->loadout[s];
		if (p->ws[w].reserve >= 0 && p->ws[w].reserve < weapon_def(w)->reserveMax) return true;
	}
	return false;
}

void ammo_update(Combat* c, Player* p, float dt)
{
	if (c->frozen) return;
	for (int i = 0; i < c->ammoBoxCount; i++) {
		AmmoBox* b = &c->ammoBox[i];
		if (c->obj.newRound) b->timer = 0.0f;             // Team Elimination: a new round puts every crate back
		if (b->timer > 0.0f) { b->timer -= dt; continue; }
		if (!p->alive || !needsAmmo(p)) continue;
		if (dist2d(p->x, p->z, b->pos[0], b->pos[2]) > AMMO_TOUCH || fabsf(p->y - b->pos[1]) > 2.0f) continue;
		for (int s = 0; s < 2; s++) p->ws[p->loadout[s]].reserve = weapon_def(p->loadout[s])->reserveMax;
		p->grenades = GRENADE_MAX;
		b->timer = AMMO_RESPAWN;
		snprintf(c->perkMsg, sizeof(c->perkMsg), "AMMO REFILLED");
		c->perkMsgT = 1.6f;
		audio_play(SFX_BUY, 0.0f, 1.0f);
	}
}

// Client: the crate list comes from the host, the player is local. Refills like ammo_update does and
// returns the crate's index so the host can be told, or -1 when nothing was picked up.
int ammo_client_pickup(Combat* c, Player* p)
{
	if (c->frozen || !p->alive || !needsAmmo(p)) return -1;
	for (int i = 0; i < c->ammoBoxCount; i++) {
		AmmoBox* b = &c->ammoBox[i];
		if (b->timer > 0.0f) continue;
		if (dist2d(p->x, p->z, b->pos[0], b->pos[2]) > AMMO_TOUCH || fabsf(p->y - b->pos[1]) > 2.0f) continue;
		for (int s = 0; s < 2; s++) p->ws[p->loadout[s]].reserve = weapon_def(p->loadout[s])->reserveMax;
		p->grenades = GRENADE_MAX;
		b->timer = AMMO_RESPAWN;
		snprintf(c->perkMsg, sizeof(c->perkMsg), "AMMO REFILLED");
		c->perkMsgT = 1.6f;
		audio_play(SFX_BUY, 0.0f, 1.0f);
		return i;
	}
	return -1;
}

// Host: a remote player says they took crate 'idx'. Accepted when the crate is there and they are close to it.
void ammo_remote_pickup(Combat* c, const Bot* b, int idx)
{
	if (idx < 0 || idx >= c->ammoBoxCount || !b->alive) return;
	AmmoBox* box = &c->ammoBox[idx];
	if (box->timer > 0.0f) return;
	if (dist2d(b->x, b->z, box->pos[0], box->pos[2]) > AMMO_TOUCH + 2.5f) return;     // some slack for lag
	box->timer = AMMO_RESPAWN;
}

void ammo_draw(const Combat* c)
{
	for (int i = 0; i < c->ammoBoxCount; i++) {
		const AmmoBox* b = &c->ammoBox[i];
		if (b->timer > 0.0f) continue;
		float x = b->pos[0], y = b->pos[1], z = b->pos[2];
		float bob = sinf((c->frame + i * 11) * 0.12f) * 0.05f;
		renderer_world_box(x, y + 0.22f, z, 0.40f, 0.22f, 0.28f, 0.27f, 0.36f, 0.20f);     // crate
		renderer_world_box(x, y + 0.45f, z, 0.43f, 0.04f, 0.31f, 0.20f, 0.27f, 0.15f);     // lid
		renderer_world_box(x, y + 0.22f, z + 0.285f, 0.14f, 0.20f, 0.01f, 0.95f, 0.80f, 0.22f);   // yellow label on the front
		for (int k = -1; k <= 1; k++)                                                      // three rounds floating above it
			renderer_world_box(x + k * 0.2f, y + 0.78f + bob, z, 0.055f, 0.13f, 0.055f, 0.95f, 0.78f, 0.25f);
	}
}
