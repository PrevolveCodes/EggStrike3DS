#include "bots.h"
#include "audio.h"
#include "collision.h"
#include "combat.h"
#include "config.h"
#include "physics.h"
#include "rng.h"
#include <math.h>
#include <string.h>

#define PI_F 3.14159265f

//                                 vision fovCos reactMin reactMax turn track settle errMax sprd  tol  speed retreat offMin offMax strafe noise lead
static const BotSkill skills[3] = {
	/* easy   */ { 32.0f, 0.50f, 0.70f, 1.10f, 1.9f, 2.5f, 1.6f, 0.090f, 2.20f, 0.15f, 0.75f, 0.50f, 0.50f, 1.20f, 0.35f, 0.080f, 0.50f },
	/* normal */ { 45.0f, 0.34f, 0.35f, 0.60f, 3.5f, 5.0f, 1.0f, 0.045f, 1.50f, 0.09f, 0.90f, 0.35f, 0.20f, 0.60f, 0.60f, 0.045f, 0.80f },
	/* hard   */ { 60.0f, 0.17f, 0.20f, 0.30f, 5.8f, 9.0f, 0.6f, 0.025f, 1.15f, 0.05f, 1.00f, 0.30f, 0.10f, 0.35f, 0.80f, 0.030f, 1.00f },
};

const BotSkill* bot_skill(int difficulty)
{
	if (difficulty < 0) difficulty = 0;
	if (difficulty > 2) difficulty = 2;
	return &skills[difficulty];
}

// Distances each weapon is comfortable at (min, max)
static const float prefRange[WEAPON_COUNT][2] = {
	/* Splatterpop */ {  2.0f, 11.0f },
	/* Yolkbuster  */ {  8.0f, 22.0f },
	/* Longbeak    */ { 22.0f, 45.0f },
	/* Peckmatic   */ {  6.0f, 20.0f },
	/* Farmhand    */ { 10.0f, 24.0f },
	/* Triplet     */ {  6.0f, 20.0f },
	/* Eggstol     */ {  3.0f, 18.0f },
};

void bot_weapon_range(int weapon, float* prefMin, float* prefMax)
{
	if (weapon < 0 || weapon >= WEAPON_COUNT) weapon = 0;
	*prefMin = prefRange[weapon][0];
	*prefMax = prefRange[weapon][1];
}

static float wrapPi(float a)
{
	while (a > PI_F) a -= 2.0f * PI_F;
	while (a < -PI_F) a += 2.0f * PI_F;
	return a;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool bot_line_clear(const Map* map, const float a[3], const float b[3])
{
	float d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
	float dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	if (dist < 0.3f) return true;
	d[0] /= dist; d[1] /= dist; d[2] /= dist;
	float t;
	return !collision_raycast(map, a, d, dist - 0.25f, &t);
}

// ------------------------------------------------------------------ creation
void bot_create(Combat* c, int index, int team)
{
	Bot* b = &c->bots[index];
	memset(b, 0, sizeof(*b));
	b->active = true;
	b->id = index + 1;
	b->team = team < 0 ? 0 : team;
	strncpy(b->name, bot_name(index), sizeof(b->name) - 1);

	// Roles are dealt out per team so a team starts with a mix
	static const BotRole pattern[5] = { ROLE_ATTACKER, ROLE_DEFENDER, ROLE_ROAMER, ROLE_SUPPORT, ROLE_ATTACKER };
	int sameTeam = 0;
	for (int i = 0; i < index; i++) if (c->bots[i].team == b->team) sameTeam++;
	b->role = (team < 0) ? ROLE_ROAMER : pattern[sameTeam % 5];
	// Personalities are dealt out like roles, so every team has a mix (and free-for-all gets a random one)
	static const BotStyle stylePattern[5] = { STYLE_RUSHER, STYLE_HOLDER, STYLE_FLANKER, STYLE_BALANCED, STYLE_RUSHER };
	b->style = (team < 0) ? (BotStyle)((int)(rng_float() * STYLE_COUNT) % STYLE_COUNT) : stylePattern[sameTeam % 5];
	b->holdDir = 1.0f;
	b->roleTimer = 25.0f + rng_float() * 25.0f;
	if (c->mode == MODE_CTF || c->mode == MODE_KOTP || c->mode == MODE_ELIM) b->roleTimer = 0.0f;   // roles come from the mode

	const BotSkill* sk = bot_skill(c->difficulty);
	b->retreatHp = clampf(sk->retreatHp + (rng_float() - 0.5f) * 0.16f, 0.15f, 0.7f);
	if (b->style == STYLE_RUSHER) b->retreatHp = clampf(b->retreatHp - 0.10f, 0.12f, 0.7f);
	if (b->style == STYLE_HOLDER) b->retreatHp = clampf(b->retreatHp + 0.06f, 0.15f, 0.7f);
	for (int i = 0; i < MAX_FIGHTERS; i++) {
		b->targetBias[i] = rng_float() * 8.0f;
		b->seenTimer[i] = 999.0f;
	}
}

#ifdef EGG_BALANCE_TEST
int g_botWeaponOffset = -1;   // balance harness only: bot id + offset picks the weapon
#endif
static int pickWeapon(const Combat* c, const Bot* b)
{
#ifdef EGG_BALANCE_TEST
	if (g_botWeaponOffset >= 0) return (b->id + g_botWeaponOffset) % PRIMARY_COUNT;
#endif
	float w[PRIMARY_COUNT], sum = 0.0f;   // bots pick a primary like players do, with a taste that fits their personality
	for (int i = 0; i < PRIMARY_COUNT; i++) {
		w[i] = 1.0f;
		if (i == W_LONGBEAK) w[i] = c->difficulty == DIFF_EASY ? 0.3f : 0.6f;
		float mid = 0.5f * (prefRange[i][0] + prefRange[i][1]);
		if (b->style == STYLE_RUSHER)  w[i] *= 0.7f + 1.1f * clampf((16.0f - mid) / 10.0f, 0.0f, 1.0f);
		if (b->style == STYLE_HOLDER)  w[i] *= 0.7f + 1.0f * clampf((mid - 12.0f) / 14.0f, 0.0f, 1.0f);
		if (b->style == STYLE_FLANKER) w[i] *= 0.8f + 0.5f * clampf((15.0f - mid) / 10.0f, 0.0f, 1.0f);
		sum += w[i];
	}
	float r = rng_float() * sum;
	for (int i = 0; i < PRIMARY_COUNT; i++) { r -= w[i]; if (r <= 0.0f) return i; }
	return W_PECKMATIC;
}

void bot_respawn(Combat* c, const Player* p, Bot* b)
{
	int team = combat_is_team_mode(c) ? b->team : -1;
	int s = combat_choose_spawn(c, p, c->map, team, b->id);
	const Spawn* sp = &c->map->spawns[s];

	b->x = sp->x; b->y = sp->y; b->z = sp->z;
	b->yaw = sp->yaw; b->pitch = 0.0f;
	b->flankLeg = 0; b->holdLeft = 0.0f; b->holdPending = false;
	b->vy = 0.0f; b->onGround = false;
	b->perks.shield = b->perks.hotT = b->perks.crackT = 0.0f;
	b->grenadeCd = 6.0f + rng_float() * 12.0f;
	b->hp = BOT_MAX_HP;
	b->alive = true;
	b->spawnSeq++;                          // a person on another console teleports when this changes
	b->lastHitTimer = 10.0f;
	b->invuln = SPAWN_PROTECTION;
	b->hitFlash = b->flashT = 0.0f;
	b->meleeT = 0.0f;
	b->meleeCd = 1.0f;
	b->meleeHitDone = true;

	b->weapon = pickWeapon(c, b);
	weapon_state_init(&b->ws, b->weapon);

	b->state = BS_IDLE;
	b->stateTime = 0.0f;
	b->idleTime = 0.4f + rng_float() * 1.2f;
	b->target = b->lastTarget = -1;
	b->aimValid = false;
	b->reactLeft = 0.0f;
	b->pathLen = b->pathIdx = 0;
	b->alertValid = false;
	b->stuckTimer = 0.0f;
	b->progX = b->x; b->progZ = b->z; b->progTimer = 0.0f;
	b->moveYaw = b->yaw;
	b->retreatCool = 0.0f;
	b->objTimer = rng_float() * 1.5f;
	for (int i = 0; i < MAX_FIGHTERS; i++) b->seenTimer[i] = 999.0f;
}

// ------------------------------------------------------------------- pathing
void bot_stop_path(Bot* b) { b->pathLen = b->pathIdx = 0; }
bool bot_path_done(const Bot* b) { return b->pathIdx >= b->pathLen; }

bool bot_goto_node(Combat* c, Bot* b, int node)
{
	if (c->pathBudget <= 0 || node < 0) return false;
	int s = nav_nearest(&c->nav, b->x, b->y, b->z);
	if (s < 0) return false;
	c->pathBudget--;
	int n = nav_path(&c->nav, s, node, b->path, NAV_MAX_PATH);
	if (n <= 0) { bot_stop_path(b); return false; }
	b->pathLen = n;
	b->pathIdx = 0;
	// Skip the first node when it is behind us or we are already there
	if (n > 1) {
		const NavNode* a = &c->nav.nodes[b->path[0]];
		const NavNode* nx = &c->nav.nodes[b->path[1]];
		float toA = (a->x - b->x) * (nx->x - a->x) + (a->z - b->z) * (nx->z - a->z);
		float dA = (a->x - b->x) * (a->x - b->x) + (a->z - b->z) * (a->z - b->z);
		if (dA < 1.0f || toA < 0.0f) b->pathIdx = 1;
	}
	return true;
}

bool bot_goto_pos(Combat* c, Bot* b, const float pos[3])
{
	int g = nav_nearest(&c->nav, pos[0], pos[1], pos[2]);
	return bot_goto_node(c, b, g);
}

// ---------------------------------------------------------------- perception
static bool canSee(const Combat* c, const Player* p, const Map* map, const Bot* b, const BotSkill* sk, int id)
{
	float f[3];
	combat_pos(c, p, id, f);
	float eye[3] = { b->x, b->y + EYE_HEIGHT, b->z };
	float chest[3] = { f[0], f[1] + 0.9f, f[2] };
	float dx = chest[0] - eye[0], dy = chest[1] - eye[1], dz = chest[2] - eye[2];
	float dist = sqrtf(dx * dx + dy * dy + dz * dz);
	if (dist > sk->vision) return false;

	// Field of view (enemies right next to a bot are noticed regardless)
	if (dist > 4.0f) {
		float fwd[3] = { sinf(b->yaw) * cosf(b->pitch), sinf(b->pitch), -cosf(b->yaw) * cosf(b->pitch) };
		float dot = (fwd[0] * dx + fwd[1] * dy + fwd[2] * dz) / dist;
		if (dot < sk->fovCos) return false;
	}

	// Walls and solid objects block sight
	if (bot_line_clear(map, eye, chest)) return true;
	float head[3] = { f[0], f[1] + 1.5f, f[2] };
	return bot_line_clear(map, eye, head);
}

static void shareSighting(Combat* c, Player* p, const Bot* b, int enemy, const float pos[3])
{
	if (!combat_is_team_mode(c)) return;
	for (int i = 0; i < c->botCount; i++) {
		Bot* t = &c->bots[i];
		if (t == b || !t->alive || t->team != b->team) continue;
		float d = sqrtf((t->x - b->x) * (t->x - b->x) + (t->z - b->z) * (t->z - b->z));
		if (d > 45.0f) continue;
		if (t->seenTimer[enemy] > 0.9f) {        // a callout, not eyes on the target
			t->seenTimer[enemy] = 0.9f;
			t->seenPos[enemy][0] = pos[0]; t->seenPos[enemy][1] = pos[1]; t->seenPos[enemy][2] = pos[2];
		}
	}
	(void)p;
}

static void perceive(Combat* c, Player* p, const Map* map, Bot* b, float dt)
{
	const BotSkill* sk = bot_skill(c->difficulty);
	b->shareTimer -= dt;

	for (int id = 0; id <= c->botCount; id++) {
		if (id == b->id) continue;
		if (!combat_alive(c, p, id) || !combat_is_enemy(c, p, b->id, id)) continue;
		bool check = (id == b->target) || (((unsigned)id + b->frame + (unsigned)b->id) % 3u == 0u);
		if (!check) continue;
		if (canSee(c, p, map, b, sk, id)) {
			float f[3];
			combat_pos(c, p, id, f);
			float gap = b->seenTimer[id];            // seconds since the last sighting
			if (gap > 0.001f && gap < 0.6f) {
				for (int a = 0; a < 3; a += 2) b->seenVel[id][a] = b->seenVel[id][a] * 0.5f + 0.5f * (f[a] - b->seenPos[id][a]) / gap;
			} else {
				b->seenVel[id][0] = b->seenVel[id][2] = 0.0f;
			}
			b->seenTimer[id] = 0.0f;
			b->seenPos[id][0] = f[0]; b->seenPos[id][1] = f[1]; b->seenPos[id][2] = f[2];
			if (b->shareTimer <= 0.0f) { shareSighting(c, p, b, id, f); b->shareTimer = 1.0f; }
		}
	}
}

// -------------------------------------------------------------------- steering
static void steer(Combat* c, const Map* map, Bot* b, float dt)
{
	const BotSkill* sk = bot_skill(c->difficulty);
	float wx = 0.0f, wz = 0.0f;
	bool jump = false;
	float speed = WALK_SPEED * sk->speedMul;
	if (b->state == BS_RETREAT) speed *= 1.15f;

	bool fighting = b->state == BS_ATTACK && b->target >= 0 && b->seenTimer[b->target] < 0.45f;

	if (fighting) {
		const float* tp = b->seenPos[b->target];
		float dx = tp[0] - b->x, dz = tp[2] - b->z;
		float d = sqrtf(dx * dx + dz * dz) + 1.0e-4f;
		float tx = dx / d, tz = dz / d;
		float pmin, pmax;
		bot_weapon_range(b->weapon, &pmin, &pmax);
		float approach = d > pmax ? 0.7f : (d < pmin * 0.8f ? -0.9f : 0.0f);
		float strafeMul = 1.0f, hopGap = 1.0f, hopChance = 0.35f;
		if (b->style == STYLE_RUSHER) {            // keeps pushing in, never stands still
			if (d > pmin + 0.35f * (pmax - pmin)) approach = 0.55f;
			strafeMul = 1.1f; hopGap = 0.8f; hopChance = 0.42f;
		} else if (b->style == STYLE_HOLDER) {      // stays at range and stays put
			if (d < pmin * 1.1f) approach = -0.5f; else if (d <= pmax * 1.1f) approach = 0.0f;
			strafeMul = 0.55f; hopChance = 0.0f;
		} else if (b->style == STYLE_FLANKER) {
			strafeMul = 1.1f;
		}

		b->strafeTimer -= dt;
		if (b->strafeTimer <= 0.0f) {
			b->strafeDir = rng_float() < 0.5f ? -1.0f : 1.0f;
			b->strafeTimer = 0.7f + rng_float() * 1.4f;
		}
		float s = sk->strafe * strafeMul * b->strafeDir;
		wx = tx * approach + (-tz) * s;
		wz = tz * approach + tx * s;

		b->jumpTimer -= dt;
		if (sk->strafe > 0.5f && b->jumpTimer <= 0.0f && b->onGround) {
			if (rng_float() < hopChance) jump = true;
			b->jumpTimer = (2.0f + rng_float() * 3.0f) * hopGap;
		}
	} else if (b->pathIdx < b->pathLen) {
		const NavNode* n = &c->nav.nodes[b->path[b->pathIdx]];
		float dx = n->x - b->x, dz = n->z - b->z, dy = n->y - b->y;
		float d = sqrtf(dx * dx + dz * dz);
		if (d < 0.6f && fabsf(dy) < 1.5f) {
			b->pathIdx++;
		} else if (d > 1.0e-4f) {
			wx = dx / d; wz = dz / d;
			if (dy > STEP_HEIGHT + 0.05f && d < 1.7f && b->onGround) jump = true;   // hop up onto crates
		}
	}

	float len = sqrtf(wx * wx + wz * wz);
	if (len > 1.0f) { wx /= len; wz /= len; len = 1.0f; }
	if (len > 0.1f) b->moveYaw = atan2f(wx, -wz);

	// Stuck detection: trying to move but not getting anywhere
	b->progTimer += dt;
	if (b->progTimer >= 0.5f) {
		float moved = sqrtf((b->x - b->progX) * (b->x - b->progX) + (b->z - b->progZ) * (b->z - b->progZ));
		float expected = len * speed * b->progTimer;
		if (len > 0.3f && moved < expected * 0.35f) b->stuckTimer += b->progTimer;
		else b->stuckTimer = 0.0f;
		b->progX = b->x; b->progZ = b->z; b->progTimer = 0.0f;
	}
	if (b->stuckTimer >= 1.0f) { jump = true; b->strafeDir = -b->strafeDir; }
	if (b->stuckTimer >= 2.0f) { bot_stop_path(b); b->stuckTimer = 0.0f; b->repathTimer = 0.0f; }

	Body body = { b->x, b->y, b->z, b->vy, b->onGround };
	physics_step(&body, map, wx * speed, wz * speed, jump, dt, NULL, NULL);
	b->x = body.x; b->y = body.y; b->z = body.z; b->vy = body.vy; b->onGround = body.onGround;
}

// ------------------------------------------------------------ aiming and firing
static void turnToward(Bot* b, float desYaw, float desPitch, float maxStep)
{
	float dyaw = wrapPi(desYaw - b->yaw);
	b->yaw = wrapPi(b->yaw + clampf(dyaw, -maxStep, maxStep));
	float dp = desPitch - b->pitch;
	b->pitch = clampf(b->pitch + clampf(dp, -maxStep, maxStep), -1.2f, 1.2f);
}

static void aimAndShoot(Combat* c, Player* p, const Map* map, Bot* b, float dt)
{
	const BotSkill* sk = bot_skill(c->difficulty);
	const WeaponDef* d = weapon_def(b->weapon);
	float eyeY = b->y + EYE_HEIGHT;
	bool trigger = false;
	bool seeing = b->target >= 0 && b->seenTimer[b->target] < 0.45f;

	if (seeing) {
		const float* tp = b->seenPos[b->target];
		float chest[3] = { tp[0], tp[1] + 1.0f, tp[2] };
		if (d->shot == SHOT_ROCKET && d->projSpeed > 1.0f) {   // a slow shell lands where the target will be, like a human leading it
			float dd = sqrtf((chest[0] - b->x) * (chest[0] - b->x) + (chest[2] - b->z) * (chest[2] - b->z));
			float t = fminf(dd / d->projSpeed, 1.2f) * sk->leadMul;
			chest[0] += b->seenVel[b->target][0] * t;
			chest[2] += b->seenVel[b->target][2] * t;
		}
		if (!b->aimValid) {
			// First glimpse: the aim point starts off the mark and has to settle
			b->aimPos[0] = chest[0] + (rng_float() - 0.5f) * 3.0f;
			b->aimPos[1] = chest[1] + (rng_float() - 0.5f) * 1.5f;
			b->aimPos[2] = chest[2] + (rng_float() - 0.5f) * 3.0f;
			b->aimValid = true;
		}
		float k = clampf(sk->trackRate * dt, 0.0f, 1.0f);   // the aim point lags a moving target
		for (int a = 0; a < 3; a++) b->aimPos[a] += (chest[a] - b->aimPos[a]) * k;

		float dx = b->aimPos[0] - b->x, dy = b->aimPos[1] - eyeY, dz = b->aimPos[2] - b->z;
		float horiz = sqrtf(dx * dx + dz * dz);
		turnToward(b, atan2f(dx, -dz), atan2f(dy, horiz), sk->turnRate * dt);   // limited turning speed

		b->reactLeft -= dt;
		b->onTarget += dt;

		// How far off the real target is the gun pointing right now?
		float cx = chest[0] - b->x, cy = chest[1] - eyeY, cz = chest[2] - b->z;
		float dist = sqrtf(cx * cx + cy * cy + cz * cz) + 1.0e-4f;
		float fwd[3] = { sinf(b->yaw) * cosf(b->pitch), sinf(b->pitch), -cosf(b->yaw) * cosf(b->pitch) };
		float cosErr = (fwd[0] * cx + fwd[1] * cy + fwd[2] * cz) / dist;
		float err = acosf(clampf(cosErr, -1.0f, 1.0f));

		float pmin, pmax;
		bot_weapon_range(b->weapon, &pmin, &pmax);
		float maxRange = fminf(d->range * 0.9f, pmax * 1.5f);

		b->burstTimer -= dt;
		if (b->burstTimer <= 0.0f) {
			b->burstOn = !b->burstOn;
			b->burstTimer = b->burstOn ? 0.5f + rng_float() * 1.0f
			                           : sk->offMin + rng_float() * (sk->offMax - sk->offMin);
		}
		trigger = b->reactLeft <= 0.0f && err < sk->tolerance && dist <= maxRange &&
		          (d->mode != FIRE_AUTO || b->burstOn);

		// An occasional grenade at a target that is far enough away to be safe (an arc towards where it was seen)
		b->grenadeCd -= dt;
		float hd = sqrtf(cx * cx + cz * cz);
		if (b->grenadeCd <= 0.0f && b->reactLeft <= 0.0f && hd > 7.0f && hd < 18.0f) {
			float v = 20.0f;
			float s2 = clampf(hd * 20.0f / (v * v), 0.0f, 0.98f);
			float th = 0.5f * asinf(s2) + cy / (hd + 1.0f) * 0.25f;
			float gy = atan2f(cx, -cz) + (rng_float() - 0.5f) * 0.20f;
			float gdir[3] = { sinf(gy) * cosf(th), sinf(th), -cosf(gy) * cosf(th) };
			float geye[3] = { b->x, eyeY, b->z };
			combat_throw_grenade(c, b->id, geye, gdir, v);
			b->grenadeCd = (c->difficulty == DIFF_EASY ? 24.0f : (c->difficulty == DIFF_NORMAL ? 16.0f : 12.0f)) + rng_float() * 10.0f;   // seconds between throws
		}

		// Up close, a bot may whack with the whisk instead of shooting (rushers love it)
		if (b->meleeCd <= 0.0f && b->meleeT <= 0.0f && b->reactLeft <= 0.0f && hd < MELEE_RANGE + 0.2f && err < 0.7f) {
			float chance = b->style == STYLE_RUSHER ? 0.85f : (c->difficulty == DIFF_EASY ? 0.30f : (c->difficulty == DIFF_NORMAL ? 0.50f : 0.70f));
			if (rng_float() < chance) {
				b->meleeT = MELEE_TIME;
				b->meleeHitDone = false;
				b->ws.reloadLeft = 0.0f;
				b->invuln = 0.0f;
				audio_play_at(SFX_MELEE_SWING, p->x, p->z, p->yaw, b->x, b->z, 0.8f);
			}
			b->meleeCd = MELEE_COOLDOWN + (c->difficulty == DIFF_EASY ? 1.0f : (c->difficulty == DIFF_NORMAL ? 0.5f : 0.2f)) + rng_float() * 0.3f;
		}
	} else {
		b->aimValid = false;
		b->onTarget = 0.0f;
		if (b->alertValid) {
			turnToward(b, b->alertYaw, 0.0f, sk->turnRate * dt);
			if (fabsf(wrapPi(b->alertYaw - b->yaw)) < 0.15f) b->alertValid = false;
		} else {
			turnToward(b, b->moveYaw, 0.0f, sk->turnRate * 0.7f * dt);
		}
	}

	b->meleeCd -= dt;
	if (b->meleeT > 0.0f) {                     // mid-swing: no shooting; the whisk lands a moment in
		b->meleeT -= dt;
		trigger = false;
		if (!b->meleeHitDone && MELEE_TIME - b->meleeT >= MELEE_HIT_AT) {
			b->meleeHitDone = true;
			float eye[3] = { b->x, eyeY, b->z };
			combat_melee(c, p, map, b->id, eye, b->yaw, NULL);
		}
	}

	bool wantReload = !seeing && b->ws.ammo < d->magSize / 2 && b->meleeT <= 0.0f;
	WeaponEvents ev = weapon_update(&b->ws, d, b->perks.hotT > 0.0f ? dt * PERK_HOT_SPEED : dt, trigger, trigger, wantReload);

	if (ev.reloadStarted) audio_play_at(SFX_RELOAD, p->x, p->z, p->yaw, b->x, b->z, 0.5f);
	if (ev.shot) {
		float moving = (fabsf(b->vy) > 0.5f) ? 0.01f : 0.0f;
		float settleErr = sk->errMax * fmaxf(0.0f, 1.0f - b->onTarget / sk->settle);
		float spread = d->spread * sk->spreadMul + settleErr + moving + weapon_shot_bloom(d, &b->ws);   // bots bloom too
		// Every shot gets its own aim error on top of the weapon spread
		float g1 = (rng_float() + rng_float() + rng_float() - 1.5f) * 2.0f;
		float g2 = (rng_float() + rng_float() + rng_float() - 1.5f) * 2.0f;
		float sy = b->yaw + g1 * sk->aimNoise, sp = clampf(b->pitch + g2 * sk->aimNoise * 0.7f, -1.2f, 1.2f);
		float dir[3] = { sinf(sy) * cosf(sp), sinf(sp), -cosf(sy) * cosf(sp) };
		float eye[3] = { b->x, eyeY, b->z };
		b->invuln = 0.0f;                       // firing ends spawn protection
		combat_fire(c, p, map, b->id, eye, dir, spread, b->weapon);
		audio_play_at(SFX_FIRE_BASE + b->weapon, p->x, p->z, p->yaw, b->x, b->z, 0.8f);
		b->flashT = 0.06f;
	}
}

// ---------------------------------------------------------------------- update
void bot_update(Combat* c, Player* p, const Map* map, Bot* b, float dt)
{
	b->frame++;
	b->hitFlash = fmaxf(0.0f, b->hitFlash - dt);
	b->flashT = fmaxf(0.0f, b->flashT - dt);
	b->lastHitTimer += dt;

	if (!b->alive) {
		if (c->mode == MODE_ELIM) return;          // Team Elimination: back only when the next round starts
		b->respawnTimer -= dt;
		if (b->respawnTimer <= 0.0f) bot_respawn(c, p, b);
		return;
	}

	b->stateTime += dt;
	if (b->invuln > 0.0f) b->invuln -= dt;
	for (int i = 0; i <= c->botCount; i++) if (b->seenTimer[i] < 999.0f) b->seenTimer[i] += dt;

	if (b->hp < BOT_MAX_HP && b->lastHitTimer > REGEN_DELAY) {
		b->hp += REGEN_RATE * dt;
		if (b->hp > BOT_MAX_HP) b->hp = BOT_MAX_HP;
	}

	perceive(c, p, map, b, dt);
	bot_decide(c, p, b, dt);
	steer(c, map, b, dt);
	aimAndShoot(c, p, map, b, dt);

	// What the HUD radar may show: only bots the player can actually see
	if (p->alive && ((unsigned)b->id + b->frame) % 3u == 0u) {
		float eye[3] = { p->x, p->y + EYE_HEIGHT, p->z };
		float chest[3] = { b->x, b->y + 0.9f, b->z };
		float dx = chest[0] - eye[0], dz = chest[2] - eye[2];
		b->visibleToPlayer = (dx * dx + dz * dz < 70.0f * 70.0f) && bot_line_clear(map, eye, chest);
	} else if (!p->alive) {
		b->visibleToPlayer = false;
	}

	if (b->y < map->killY) {                       // fell out of the map
		{ float fp[3] = { b->x, b->y, b->z }; obj_on_death(c, b->id, fp); }
		if (c->mode == MODE_ELIM) { b->alive = false; b->state = BS_RESPAWN; b->deaths++; }
		else bot_respawn(c, p, b);
	}
}
