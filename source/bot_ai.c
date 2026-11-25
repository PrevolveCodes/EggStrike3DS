// Bot decision making: which state a bot is in, who it fights, where it goes.
// Bots only act on what they have seen, been told by a teammate, or been hurt by.
#include "bots.h"
#include "combat.h"
#include "rng.h"
#include <math.h>
#include <stddef.h>

static float dist2(float ax, float az, float bx, float bz)
{
	return sqrtf((ax - bx) * (ax - bx) + (az - bz) * (az - bz));
}

static void setState(Bot* b, BotState s)
{
	b->state = s;
	b->stateTime = 0.0f;
	b->lookTimer = 0.0f;
}

// ---------------------------------------------------------------- targeting
static void pickTarget(Combat* c, Player* p, Bot* b)
{
	const BotSkill* sk = bot_skill(c->difficulty);
	int best = -1;
	float bestScore = 1.0e9f;

	for (int id = 0; id <= c->botCount; id++) {
		if (id == b->id) continue;
		if (!combat_alive(c, p, id) || !combat_is_enemy(c, p, b->id, id)) continue;
		if (b->seenTimer[id] > 0.35f) continue;           // not in sight right now
		float d = dist2(b->x, b->z, b->seenPos[id][0], b->seenPos[id][2]);
		float score = d + b->targetBias[id];              // every bot has its own taste in victims
		if (id == b->target) score -= 8.0f;               // but sticks with a fight it is in
		if (score < bestScore) { bestScore = score; best = id; }
	}

	if (best != b->target) {
		if (b->target >= 0) b->lastTarget = b->target;
		b->target = best;
		if (best >= 0) {
			b->reactLeft = sk->reactMin + rng_float() * (sk->reactMax - sk->reactMin);
			b->onTarget = 0.0f;
			b->aimValid = false;
		}
	}
}

// Most recently seen enemy that is not in sight now (a callout from a teammate counts)
static int newestKnownEnemy(Combat* c, Player* p, Bot* b)
{
	int best = -1;
	float bestAge = 8.0f;
	for (int id = 0; id <= c->botCount; id++) {
		if (id == b->id) continue;
		if (!combat_alive(c, p, id) || !combat_is_enemy(c, p, b->id, id)) continue;
		if (b->seenTimer[id] < bestAge) { bestAge = b->seenTimer[id]; best = id; }
	}
	return best;
}

// ------------------------------------------------------------------- roles
static void reassignRole(Combat* c, Bot* b)
{
	b->roleTimer = 25.0f + rng_float() * 25.0f;
	if (!combat_is_team_mode(c)) { b->role = ROLE_ROAMER; return; }

	int own = 0, enemy = 0;
	for (int i = 0; i < c->botCount; i++) {
		if (!c->bots[i].alive) continue;
		if (c->bots[i].team == b->team) own++; else enemy++;
	}
	float r = rng_float();
	if (c->mode == MODE_CTF) {                      // raid, guard, escort
		b->role = r < 0.45f ? ROLE_ATTACKER : (r < 0.75f ? ROLE_DEFENDER : ROLE_SUPPORT);
		return;
	}
	if (c->mode == MODE_KOTP) {                     // most go for the pan, a few flank
		b->role = r < 0.75f ? ROLE_OBJECTIVE : (r < 0.85f ? ROLE_ATTACKER : (r < 0.95f ? ROLE_DEFENDER : ROLE_ROAMER));
		return;
	}
	if (c->mode == MODE_ELIM) {                     // nobody respawns, so keep the fight moving
		b->role = r < 0.55f ? ROLE_ATTACKER : (r < 0.70f ? ROLE_DEFENDER : (r < 0.90f ? ROLE_ROAMER : ROLE_SUPPORT));
		return;
	}
	if (own < enemy && r < 0.4f) { b->role = ROLE_DEFENDER; return; }   // outnumbered: dig in
	if (r < 0.35f) b->role = ROLE_ATTACKER;
	else if (r < 0.55f) b->role = ROLE_DEFENDER;
	else if (r < 0.80f) b->role = ROLE_ROAMER;
	else b->role = ROLE_SUPPORT;
}

// ---------------------------------------------------------------- objectives
// Bots only use what a player can see too: the flags and the pan are in plain view and the
// scoreboard is public. Where enemies are standing is never part of it.
static int carriedFlag(const Combat* c, const Bot* b)
{
	if (c->mode != MODE_CTF) return -1;
	for (int f = 0; f < 2; f++) if (c->obj.flag[f].carrier == b->id) return f;
	return -1;
}

static bool goPos(Combat* c, Bot* b, const float pos[3])
{
	return bot_goto_pos(c, b, pos);
}

// A random spot inside the pan, so the bots that hold it do not all stand on one point
static bool goIntoPan(Combat* c, Bot* b)
{
	const Nav* nav = &c->nav;
	int pick = -1, seen = 0;
	for (int n = 0; n < nav->nodeCount; n++) {
		const NavNode* nd = &nav->nodes[n];
		if (!nd->reachable || fabsf(nd->y - c->obj.panPos[1]) > 1.0f) continue;
		if (dist2(nd->x, nd->z, c->obj.panPos[0], c->obj.panPos[2]) > PAN_RADIUS - 1.0f) continue;
		seen++;
		if ((int)(rng_float() * seen) == 0) pick = n;      // reservoir sampling
	}
	if (pick >= 0) return bot_goto_node(c, b, pick);
	return goPos(c, b, c->obj.panPos);
}

static bool objectiveGoal(Combat* c, Player* p, Bot* b)
{
	(void)p;
	if (c->mode == MODE_KOTP) {
		bool ownHolds = c->obj.holder == b->team;
		if (b->role == ROLE_OBJECTIVE || (b->role == ROLE_DEFENDER && ownHolds)) return goIntoPan(c, b);
		if (!ownHolds && (b->role == ROLE_ATTACKER) && rng_float() < 0.5f) return goIntoPan(c, b);
		return false;
	}
	if (c->mode != MODE_CTF) return false;

	const Objective* o = &c->obj;
	int mine = b->team, theirs = 1 - mine;
	const Flag* ownFlag = &o->flag[mine];
	const Flag* enemyFlag = &o->flag[theirs];

	int carry = carriedFlag(c, b);
	if (carry >= 0) {                              // run it home; if our flag is out, fetch that first
		if (!ownFlag->atHome && ownFlag->carrier < 0) return goPos(c, b, ownFlag->pos);
		return goPos(c, b, ownFlag->home);
	}
	// Our flag lies dropped: bring it back
	if (!ownFlag->atHome && ownFlag->carrier < 0 && (b->role != ROLE_SUPPORT || rng_float() < 0.5f)) return goPos(c, b, ownFlag->pos);
	// An enemy has our flag: hunt the carrier (the flag shows where they are)
	if (ownFlag->carrier >= 0 && b->role != ROLE_ATTACKER) return goPos(c, b, ownFlag->pos);
	// A teammate has the enemy flag: escort them, or guard the stand
	if (enemyFlag->carrier >= 0 && combat_team_of(c, p, enemyFlag->carrier) == mine) {
		if (b->role == ROLE_DEFENDER) return goPos(c, b, ownFlag->home);
		return goPos(c, b, enemyFlag->pos);
	}
	switch (b->role) {
	case ROLE_ATTACKER:
		return goPos(c, b, enemyFlag->carrier >= 0 ? ownFlag->home : enemyFlag->pos);
	case ROLE_DEFENDER:
		if (rng_float() < 0.6f) return goPos(c, b, ownFlag->home);
		return false;
	default:
		return false;
	}
}

// How much of the surroundings can be seen from a spot: eight directions, clear for 20 m or not.
// Holders like spots where a lot of the map is in view, so they can see enemies coming.
static int sightlines(const Combat* c, const NavNode* nd)
{
	int open = 0;
	float eye[3] = { nd->x, nd->y + 1.55f, nd->z };
	for (int k = 0; k < 8; k++) {
		float a = (float)k * (6.2831853f / 8.0f);
		float to[3] = { nd->x + sinf(a) * 20.0f, nd->y + 1.55f, nd->z - cosf(a) * 20.0f };
		if (bot_line_clear(c->map, eye, to)) open++;
	}
	return open;
}

// Pick where to go next, weighted by role, and keep teammates spread out
static bool chooseGoal(Combat* c, Player* p, Bot* b)
{
	const Nav* nav = &c->nav;
	bool teamMode = combat_is_team_mode(c);
	if ((c->mode == MODE_CTF || c->mode == MODE_KOTP) && objectiveGoal(c, p, b)) return true;
	const float* enemyBase = NULL;
	const float* ownBase = NULL;
	if (teamMode && c->hasBase) { ownBase = c->base[b->team]; enemyBase = c->base[1 - b->team]; }

	// Anchor for support bots: a living teammate (the human counts)
	float anchor[3];
	bool hasAnchor = false;
	if (teamMode) {
		int cand[MAX_FIGHTERS], nc = 0;
		for (int id = 0; id <= c->botCount; id++)
			if (id != b->id && combat_alive(c, p, id) && !combat_is_enemy(c, p, b->id, id)) cand[nc++] = id;
		if (nc > 0) {
			int pick = cand[(int)(rng_float() * nc) % nc];
			combat_pos(c, p, pick, anchor);
			hasAnchor = true;
		}
	}

	// A round that drags on: nobody is hiding, everybody pushes toward the enemy's side of the map
	// (the layout of the map is known to everyone, enemy positions are not)
	BotRole role = b->role;
	if (c->mode == MODE_ELIM && c->obj.roundTime > 35.0f && role != ROLE_DEFENDER) role = ROLE_ATTACKER;
	if (c->mode == MODE_ELIM && c->obj.roundTime > 80.0f) role = ROLE_ATTACKER;
	// ...and personalities stop mattering too: after 35 s holders stop waiting, after 80 s everyone plays the plain way
	BotStyle style = b->style;
	if (c->mode == MODE_ELIM && c->obj.roundTime > 35.0f && style == STYLE_HOLDER) style = STYLE_BALANCED;
	if (c->mode == MODE_ELIM && c->obj.roundTime > 80.0f) style = STYLE_BALANCED;

	int bestNode = -1;
	float bestScore = -1.0e9f;
	for (int k = 0; k < 8; k++) {
		int n = nav_random_node(nav);
		if (n < 0) continue;
		const NavNode* nd = &nav->nodes[n];
		float dBot = dist2(nd->x, nd->z, b->x, b->z);
		float score = rng_float() * 8.0f;

		switch (role) {
		case ROLE_ATTACKER:
			score += enemyBase ? -dist2(nd->x, nd->z, enemyBase[0], enemyBase[2]) * 0.9f : fminf(dBot, 30.0f) * 0.6f;
			break;
		case ROLE_DEFENDER:
			score += ownBase ? -dist2(nd->x, nd->z, ownBase[0], ownBase[2]) * 1.1f : fminf(dBot, 30.0f) * 0.3f;
			break;
		case ROLE_SUPPORT:
			score += hasAnchor ? -dist2(nd->x, nd->z, anchor[0], anchor[2]) * 1.0f : fminf(dBot, 30.0f) * 0.6f;
			break;
		default:
			score += fminf(dBot, 30.0f) * 0.6f;
			break;
		}

		// A very long round: everybody closes in on the middle of the map, so the last bots on each side meet
		// (no knowledge of where anyone is, just a place to gather, so two attackers cannot keep trading bases)
		if (c->mode == MODE_ELIM && c->obj.roundTime > 80.0f)
			score += -dist2(nd->x, nd->z, 0.0f, 0.0f) * (1.2f + (c->obj.roundTime - 80.0f) * 0.02f);

		// Personality on top of the role
		switch (style) {
		case STYLE_RUSHER:         // straight at the enemy's side, or the middle of the map
			score += enemyBase ? -dist2(nd->x, nd->z, enemyBase[0], enemyBase[2]) * 0.5f
			                   : -dist2(nd->x, nd->z, 0.0f, 0.0f) * 0.35f;
			break;
		case STYLE_HOLDER:         // spots with long views, not too far from where it already is
			score += (float)sightlines(c, nd) * 4.5f - fmaxf(dBot - 28.0f, 0.0f) * 0.6f;
			break;
		case STYLE_FLANKER:
			if (enemyBase && ownBase) {
				float ax = enemyBase[0] - ownBase[0], az = enemyBase[2] - ownBase[2];
				float al = sqrtf(ax * ax + az * az) + 1.0e-4f;
				ax /= al; az /= al;
				float rx = nd->x - ownBase[0], rz = nd->z - ownBase[2];
				float along = rx * ax + rz * az, side = fabsf(rx * -az + rz * ax);
				if (b->flankLeg == 0) score += fminf(side, 26.0f) * 1.4f + along * 0.25f;   // out to the flank first...
				else                  score -= dist2(nd->x, nd->z, enemyBase[0], enemyBase[2]) * 0.8f;   // ...then in
			} else {
				score += dBot * 0.5f;
			}
			break;
		default:
			break;
		}

		// Avoid clustering: stay away from where teammates already are
		for (int i = 0; i < c->botCount; i++) {
			const Bot* t = &c->bots[i];
			if (t == b || !t->alive) continue;
			if (teamMode && t->team != b->team) continue;
			float dt = dist2(nd->x, nd->z, t->x, t->z);
			if (dt < 8.0f) score -= (8.0f - dt) * 1.2f;
		}
		if (dBot < 6.0f) score -= 20.0f;

		if (score > bestScore) { bestScore = score; bestNode = n; }
	}
	bool ok = bestNode >= 0 && bot_goto_node(c, b, bestNode);
	if (ok && style == STYLE_FLANKER) b->flankLeg ^= 1;
	if (ok && style == STYLE_HOLDER) b->holdPending = true;
	return ok;
}

// -------------------------------------------------------------------- cover
static int findCover(Combat* c, Bot* b, const float enemy[3])
{
	const Nav* nav = &c->nav;
	float eEye[3] = { enemy[0], enemy[1] + 1.5f, enemy[2] };
	int best = -1;
	float bestScore = -1.0e9f;

	for (int k = 0; k < 18; k++) {
		int n = nav_random_node(nav);
		if (n < 0) continue;
		const NavNode* nd = &nav->nodes[n];
		float d = dist2(nd->x, nd->z, b->x, b->z);
		if (d > 24.0f || d < 3.0f) continue;

		float eye[3] = { nd->x, nd->y + 1.55f, nd->z };
		bool hidden = !bot_line_clear(c->map, eye, eEye);
		float score = (hidden ? 60.0f : 0.0f) - d * 0.8f + rng_float() * 5.0f;
		if (dist2(nd->x, nd->z, enemy[0], enemy[2]) < 8.0f) score -= 30.0f;
		if (score > bestScore) { bestScore = score; best = n; }
	}
	return best;
}

static void enterRetreat(Combat* c, Bot* b, const float enemy[3])
{
	int cover = findCover(c, b, enemy);
	setState(b, BS_RETREAT);
	b->retreatCool = 8.0f;
	if (cover < 0 || !bot_goto_node(c, b, cover)) bot_stop_path(b);
}

static void enterSearch(Combat* c, Bot* b, const float pos[3], float duration)
{
	setState(b, BS_SEARCH);
	b->searchTimer = duration;
	b->searchPos[0] = pos[0]; b->searchPos[1] = pos[1]; b->searchPos[2] = pos[2];
	if (!bot_goto_pos(c, b, pos)) bot_stop_path(b);
}

// ------------------------------------------------------------------ decisions
void bot_decide(Combat* c, Player* p, Bot* b, float dt)
{
	pickTarget(c, p, b);
	if (b->retreatCool > 0.0f) b->retreatCool -= dt;

	b->roleTimer -= dt;
	if (b->roleTimer <= 0.0f) reassignRole(c, b);

	float hpFrac = b->hp / BOT_MAX_HP;
	bool seeing = b->target >= 0;

	// Carrying a flag: keep running for home, shooting on the move, whatever else is going on
	if (carriedFlag(c, b) >= 0) {
		b->objTimer -= dt;
		if (b->state != BS_PATROL) setState(b, BS_PATROL);
		if (b->objTimer <= 0.0f || bot_path_done(b)) {
			b->objTimer = 0.6f;
			if (!objectiveGoal(c, p, b)) bot_stop_path(b);
		}
		return;
	}

	// Objective modes: look again every so often so a flag drop or a lost pan is noticed
	if ((c->mode == MODE_CTF || c->mode == MODE_KOTP) && !seeing && b->state == BS_PATROL) {
		b->objTimer -= dt;
		if (b->objTimer <= 0.0f) {
			b->objTimer = 2.0f + rng_float() * 1.5f;
			if (bot_path_done(b) || c->mode == MODE_CTF) objectiveGoal(c, p, b);
		}
	}

	// Flag raiders keep moving toward the objective and only stop to fight when an enemy is close
	bool racing = c->mode == MODE_CTF && b->role == ROLE_ATTACKER;
	// Pan holders stand their ground in and around the pan instead of chasing across the map
	bool onPan = c->mode == MODE_KOTP && b->role == ROLE_OBJECTIVE;
	bool holding = onPan && dist2(b->x, b->z, c->obj.panPos[0], c->obj.panPos[2]) < PAN_RADIUS + 3.0f;
	if (onPan && !holding) racing = true;        // still on the way to the pan: keep going, shoot only what is close

	if (seeing) {
		float pmin, pmax;
		bot_weapon_range(b->weapon, &pmin, &pmax);
		const float* tp = b->seenPos[b->target];
		float dist = dist2(b->x, b->z, tp[0], tp[2]);

		if (hpFrac < b->retreatHp && b->state != BS_RETREAT && b->retreatCool <= 0.0f) {
			enterRetreat(c, b, tp);
		} else if (racing && dist > 11.0f && b->state != BS_RETREAT) {
			if (b->state != BS_PATROL) { setState(b, BS_PATROL); bot_stop_path(b); }
		} else if (holding && dist > pmax * 1.4f && b->state != BS_RETREAT) {
			if (b->state != BS_ATTACK) { setState(b, BS_ATTACK); bot_stop_path(b); }
		} else if (b->state == BS_RETREAT) {
			// Reached cover with the enemy still on us: fight back
			if (bot_path_done(b) && hpFrac > 0.12f) setState(b, BS_ATTACK);
		} else if (dist > pmax * 1.4f) {
			if (b->state != BS_CHASE) { setState(b, BS_CHASE); b->repathTimer = 0.0f; }
		} else if (b->state != BS_ATTACK) {
			setState(b, BS_ATTACK);
			bot_stop_path(b);
		}
	} else if (b->state == BS_ATTACK || b->state == BS_CHASE) {
		// Lost sight: go and look where the enemy was last seen, not where they are now
		if (racing || holding) setState(b, BS_PATROL);          // objective first
		else if (b->lastTarget >= 0 && b->seenTimer[b->lastTarget] < 20.0f)
			enterSearch(c, b, b->seenPos[b->lastTarget], 5.0f + rng_float() * 4.0f);
		else
			setState(b, BS_PATROL);
	}

	// Hurt by someone unseen: only the direction is known, so check that way
	if (b->alertValid && !b->alertUsed && !seeing && b->state != BS_RETREAT && !racing && !holding) {
		b->alertUsed = true;
		float dir[3] = { sinf(b->alertYaw), 0.0f, -cosf(b->alertYaw) };
		float guess[3] = { b->x + dir[0] * 12.0f, b->y, b->z + dir[2] * 12.0f };
		if (hpFrac < b->retreatHp && b->retreatCool <= 0.0f) enterRetreat(c, b, guess);
		else enterSearch(c, b, guess, 6.0f);
	}

	switch (b->state) {
	case BS_IDLE:
		if (b->stateTime > b->idleTime) setState(b, BS_PATROL);
		break;

	case BS_PATROL: {
		int known = newestKnownEnemy(c, p, b);     // a teammate's callout or an earlier sighting
		if (known >= 0 && !seeing && !racing && !holding) {
			enterSearch(c, b, b->seenPos[known], 6.0f);
			break;
		}
		if (b->holdLeft > 0.0f && !(c->mode == MODE_ELIM && c->obj.roundTime > 35.0f)) {            // a holder watching its spot: stand still and sweep the view
			b->holdLeft -= dt;
			b->holdFlip -= dt;
			if (b->holdFlip <= 0.0f) { b->holdDir = -b->holdDir; b->holdFlip = 1.2f + rng_float() * 1.6f; }
			b->moveYaw += b->holdDir * 0.7f * dt;
			if (b->moveYaw > 3.14159265f) b->moveYaw -= 6.2831853f;
			if (b->moveYaw < -3.14159265f) b->moveYaw += 6.2831853f;
			break;
		}
		if (bot_path_done(b)) {
			if (b->holdPending) {            // just arrived at the chosen spot
				b->holdPending = false;
				b->holdLeft = 3.0f + rng_float() * 5.0f;
				b->holdFlip = 1.0f + rng_float();
				break;
			}
			if (!chooseGoal(c, p, b)) {
				int n = nav_random_node(&c->nav);
				if (n >= 0) bot_goto_node(c, b, n);
			}
		}
		break;
	}

	case BS_SEARCH:
		b->searchTimer -= dt;
		if (bot_path_done(b)) {
			b->lookTimer += dt;                      // look around on arrival
			if (b->lookTimer > 1.5f) setState(b, BS_PATROL);
		} else if (b->searchTimer <= 0.0f) {
			setState(b, BS_PATROL);
		}
		break;

	case BS_CHASE:
		b->repathTimer -= dt;
		if (b->repathTimer <= 0.0f && seeing) {
			if (bot_goto_pos(c, b, b->seenPos[b->target])) b->repathTimer = 0.7f;
			else b->repathTimer = 0.1f;              // out of path budget this frame
		}
		break;

	case BS_RETREAT:
		if (bot_path_done(b)) {
			b->lookTimer += dt;                      // wait in cover to recover
			if (hpFrac > 0.7f || b->lookTimer > 7.0f) setState(b, BS_PATROL);
		}
		break;

	case BS_ATTACK:
	case BS_RESPAWN:
	default:
		break;
	}
}
