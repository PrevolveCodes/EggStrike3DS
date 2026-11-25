#include "combat.h"
#include "audio.h"
#include "collision.h"
#include "egg.h"
#include "fx.h"
#include "renderer.h"
#include "rng.h"
#include "synth.h"
#include "ui.h"
#include "viewmodel.h"
#include "objective.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

_Static_assert(SFX_GUN_COUNT == WEAPON_COUNT, "one fire sound per weapon");

CombatHooks combat_hooks;

// A fighter that is a person (this console's player, or someone on another console): their rockets
// fly inside the crosshair cone and their bullets leave from the same place bots' do
static bool isPerson(const Combat* c, int id)
{
	if (id == FIGHTER_PLAYER) return true;
	return id >= 1 && id <= c->botCount && c->bots[id - 1].remote;
}

// ------------------------------------------------------------------ fighters
bool combat_is_team_mode(const Combat* c)
{
	return mode_is_team(c->mode);
}

int combat_team_of(const Combat* c, const Player* p, int id)
{
	(void)p;
	if (!combat_is_team_mode(c)) return -1;
	if (id == FIGHTER_PLAYER) return c->noPlayer ? -1 : c->playerTeam;
	return c->bots[id - 1].team;
}

bool combat_is_enemy(const Combat* c, const Player* p, int a, int b)
{
	if (a == b) return false;
	if (!combat_is_team_mode(c)) return true;
	return combat_team_of(c, p, a) != combat_team_of(c, p, b);
}

bool combat_alive(const Combat* c, const Player* p, int id)
{
	if (id == FIGHTER_PLAYER) return p->alive;
	if (id < 1 || id > c->botCount) return false;
	return c->bots[id - 1].alive;
}

void combat_pos(const Combat* c, const Player* p, int id, float out[3])
{
	if (id == FIGHTER_PLAYER) { out[0] = p->x; out[1] = p->y; out[2] = p->z; return; }
	const Bot* b = &c->bots[id - 1];
	out[0] = b->x; out[1] = b->y; out[2] = b->z;
}

static void fighterBounds(const Combat* c, const Player* p, int id, float mn[3], float mx[3])
{
	float f[3];
	if (c->rewindOn && id != c->rewindSelf) { for (int a = 0; a < 3; a++) f[a] = c->hist[c->rewindSlot][id][a]; }
	else combat_pos(c, p, id, f);
	mn[0] = f[0] - TARGET_HALF_W; mn[1] = f[1];                mn[2] = f[2] - TARGET_HALF_W;
	mx[0] = f[0] + TARGET_HALF_W; mx[1] = f[1] + TARGET_HEIGHT; mx[2] = f[2] + TARGET_HALF_W;
}

int combat_bots_alive(const Combat* c)
{
	int n = 0;
	for (int i = 0; i < c->botCount; i++) if (c->bots[i].alive) n++;
	return n;
}

int combat_best_bot_kills(const Combat* c)
{
	int best = 0;
	for (int i = 0; i < c->botCount; i++) if (c->bots[i].kills > best) best = c->bots[i].kills;
	return best;
}

// -------------------------------------------------------------------- spawns
int combat_choose_spawn(const Combat* c, const Player* p, const Map* map, int team, int selfId)
{
	bool teamMode = combat_is_team_mode(c);

	// Pass 0 skips spawns with someone standing on them; pass 1 (only if every spawn on
	// the team's side is taken) accepts the least bad one so nobody ever starts on the wrong side.
	for (int pass = 0; pass < 2; pass++) {
		int best = -1, second = -1;
		float bestScore = -1.0f, secondScore = -1.0f;

		for (int s = 0; s < map->spawnCount; s++) {
			const Spawn* sp = &map->spawns[s];
			if (teamMode && sp->team != team) continue;

			float minDist = 1.0e9f;
			bool blocked = false;
			for (int id = 0; id <= c->botCount; id++) {
				if (id == selfId) continue;
				if (id == FIGHTER_PLAYER && (!p || !p->alive)) continue;
				if (id != FIGHTER_PLAYER && !c->bots[id - 1].alive) continue;
				float f[3];
				combat_pos(c, p, id, f);
				float d = sqrtf((f[0] - sp->x) * (f[0] - sp->x) + (f[2] - sp->z) * (f[2] - sp->z));
				if (d < 2.5f) blocked = true;
				if (combat_is_enemy(c, p, selfId, id) && d < minDist) minDist = d;
			}
			if (blocked && pass == 0) continue;
			float score = minDist > 1.0e8f ? 50.0f : minDist;
			if (blocked) score *= 0.01f;
			if (score > bestScore) { second = best; secondScore = bestScore; best = s; bestScore = score; }
			else if (score > secondScore) { second = s; secondScore = score; }
		}
		if (best >= 0) {
			if (second >= 0 && rng_float() < 0.35f) return second;
			return best;
		}
	}
	return map->spawnCount > 0 ? (int)(rng_float() * map->spawnCount) % map->spawnCount : 0;
}

// ------------------------------------------------------------------- creation
static const char* const botNames[MAX_BOTS] = {
	"Yolko", "Eggbert", "Omelia", "Poach", "Shelly", "Sunny", "Benny",
	"Hatch", "Humpty", "Albumen", "Fritz", "Quiche", "Dozen", "Nesta"
};
const char* bot_name(int index) { return botNames[index % MAX_BOTS]; }
const char* bot_style_name(BotStyle st)
{
	switch (st) {
	case STYLE_RUSHER:  return "Rusher";
	case STYLE_HOLDER:  return "Holder";
	case STYLE_FLANKER: return "Flanker";
	default:            return "Balanced";
	}
}

static void computeBases(Combat* c, const Map* map)
{
	// Team bases: the average of each team's spawn points
	int cnt[2] = { 0, 0 };
	for (int s = 0; s < map->spawnCount; s++) {
		int t = map->spawns[s].team;
		if (t < 0 || t > 1) continue;
		c->base[t][0] += map->spawns[s].x; c->base[t][1] += map->spawns[s].y; c->base[t][2] += map->spawns[s].z;
		cnt[t]++;
	}
	for (int t = 0; t < 2; t++) if (cnt[t]) for (int k = 0; k < 3; k++) c->base[t][k] /= (float)cnt[t];
	c->hasBase = cnt[0] > 0 && cnt[1] > 0;
}

static void initCommon(Combat* c, const Map* map, const MatchConfig* cfg, bool noPlayer);

void combat_init(Combat* c, const Map* map, const MatchConfig* cfg) { initCommon(c, map, cfg, false); }
void combat_init_server(Combat* c, const Map* map, const MatchConfig* cfg) { initCommon(c, map, cfg, true); }

static void initCommon(Combat* c, const Map* map, const MatchConfig* cfg, bool noPlayer)
{
	memset(c, 0, sizeof(*c));
	c->noPlayer = noPlayer;
	c->map = map;
	c->mode = cfg->mode;
	c->playerTeam = cfg->team;
	c->difficulty = cfg->difficulty;
	nav_build(&c->nav, map);

	computeBases(c, map);

	// Who is on the field
	int red = 0, blue = 0;
	if (mode_is_team(cfg->mode)) { red = cfg->redBots; blue = cfg->blueBots; }
	else red = cfg->totalBots;
	if (red + blue > MAX_BOTS) { red = red > MAX_BOTS ? MAX_BOTS : red; blue = MAX_BOTS - red; }

	int n = 0;
	for (int i = 0; i < red && n < MAX_BOTS; i++, n++) bot_create(c, n, mode_is_team(cfg->mode) ? TEAM_RED : -1);
	for (int i = 0; i < blue && n < MAX_BOTS; i++, n++) bot_create(c, n, TEAM_BLUE);
	c->botCount = n;

	for (int i = 0; i < c->botCount; i++) bot_respawn(c, NULL, &c->bots[i]);
	obj_init(c);
	ammo_init(c);
}

// A client's copy of the match: same settings, no bots of its own and no pathfinding. netplay.c fills it in.
void combat_init_mirror(Combat* c, const Map* map, const MatchConfig* cfg)
{
	memset(c, 0, sizeof(*c));
	c->map = map;
	c->mode = cfg->mode;
	c->playerTeam = cfg->team;
	c->difficulty = cfg->difficulty;
	computeBases(c, map);
	c->obj.holder = -1;
	c->obj.roundWinner = -1;
	c->obj.round = 1;
	c->obj.msgTeam = -1;
	c->netRole = NET_CLIENT;
}

// -------------------------------------------------------------------- damage
Perks* combat_perks(Combat* c, Player* p, int id)
{
	if (id == FIGHTER_PLAYER) return &p->perks;
	return &c->bots[id - 1].perks;
}

void combat_banner(Combat* c, const char* text)
{
	snprintf(c->perkMsg, sizeof(c->perkMsg), "%s", text);
	c->perkMsgT = 2.8f;
	audio_play(SFX_BUY, 0.0f, 1.0f);
}

static void perkBanner(Combat* c, int id, const char* text)
{
	if (id != FIGHTER_PLAYER) return;
	combat_banner(c, text);
}

const char* combat_streak_banner(int streak)
{
	if (streak == STREAK_SHIELD_AT) return "SHELL SHIELD!";
	if (streak == STREAK_HOT_AT) return "HOT YOLK!";
	if (streak == STREAK_CRACK_AT) return "CRACK SHOT!";
	if (streak >= STREAK_GOLDEN_AT && (streak - STREAK_GOLDEN_AT) % STREAK_GOLDEN_STEP == 0) return "GOLDEN EGG!";
	return NULL;
}

static void awardKill(Combat* c, Player* p, int id)
{
	if (id < 0 || id > c->botCount) return;
	Perks* k = combat_perks(c, p, id);
	k->streak++;
	float* hp = id == FIGHTER_PLAYER ? &p->hp : &c->bots[id - 1].hp;
	float maxHp = id == FIGHTER_PLAYER ? PLAYER_MAX_HP : BOT_MAX_HP;
	if (k->streak == STREAK_SHIELD_AT) {
		k->shield = k->shield + PERK_SHIELD_GAIN > PERK_SHIELD_MAX ? PERK_SHIELD_MAX : k->shield + PERK_SHIELD_GAIN;
		perkBanner(c, id, "SHELL SHIELD!");
	} else if (k->streak == STREAK_HOT_AT) {
		k->hotT = PERK_HOT_TIME;
		perkBanner(c, id, "HOT YOLK!");
	} else if (k->streak == STREAK_CRACK_AT) {
		k->crackT = PERK_CRACK_TIME;
		perkBanner(c, id, "CRACK SHOT!");
	} else if (k->streak >= STREAK_GOLDEN_AT && (k->streak - STREAK_GOLDEN_AT) % STREAK_GOLDEN_STEP == 0) {
		*hp = maxHp;
		k->shield = PERK_SHIELD_MAX;
		perkBanner(c, id, "GOLDEN EGG!");
	}
}

static void endStreak(Perks* k)
{
	k->streak = 0;
	k->shield = k->hotT = k->crackT = 0.0f;
}

#ifdef EGG_BALANCE_TEST
double g_killDistSum[16]; long g_killDistN[16];
#endif
static void killBot(Combat* c, Player* p, Bot* b, int attacker)
{
#ifdef EGG_BALANCE_TEST
	if (attacker >= 1 && attacker <= c->botCount) {
		Bot* a = &c->bots[attacker - 1];
		g_killDistSum[a->weapon] += sqrtf((a->x - b->x) * (a->x - b->x) + (a->z - b->z) * (a->z - b->z));
		g_killDistN[a->weapon]++;
	}
#endif
	endStreak(&b->perks);
	b->alive = false;
	b->deaths++;
	b->state = BS_RESPAWN;
	b->respawnTimer = b->remote ? RESPAWN_DELAY : BOT_RESPAWN;
	b->target = -1;
	{ float fp[3] = { b->x, b->y, b->z }; obj_on_death(c, b->id, fp); }

	float pos[3] = { b->x, b->y + 0.8f, b->z };
	fx_burst(pos, 10, 5.0f, 0.25f, 0.7f, 1.0f, 0.9f, 0.3f);
	audio_play_at(SFX_EGG_CRACK, p->x, p->z, p->yaw, b->x, b->z, 1.0f);

	if (attacker == FIGHTER_PLAYER) {
		p->kills++;
		audio_play(SFX_KILL, 0.0f, 1.0f);
	} else if (attacker >= 1 && attacker <= c->botCount) {
		c->bots[attacker - 1].kills++;
	}
	awardKill(c, p, attacker);
	if (c->mode == MODE_TDM && attacker >= 0) {
		int t = combat_team_of(c, p, attacker);
		if (t == 0 || t == 1) c->teamScore[t]++;
	}
}

void combat_damage(Combat* c, Player* p, int attacker, int victim, float dmg)
{
	if (c->netRole == NET_CLIENT) return;            // the host decides who gets hurt
	if (dmg <= 0.0f || !combat_is_enemy(c, p, attacker, victim)) return;
	if (c->obj.roundPause > 0.0f) return;     // Team Elimination: nobody can be hurt between rounds
	if (attacker >= 0 && attacker <= c->botCount && combat_perks(c, p, attacker)->crackT > 0.0f) dmg *= PERK_CRACK_DAMAGE;

	if (victim == FIGHTER_PLAYER) {
		if (!p->alive || p->invuln > 0.0f) return;      // spawn protection
		if (p->perks.shield > 0.0f) {                   // the shield soaks damage first
			float soak = dmg < p->perks.shield ? dmg : p->perks.shield;
			p->perks.shield -= soak;
			dmg -= soak;
			p->damageFlashT = 0.25f;
			if (dmg <= 0.0f) { p->lastAttacker = attacker; if (combat_hooks.onHit) combat_hooks.onHit(attacker, victim); return; }
		}
		player_take_damage(p, dmg);
		p->lastAttacker = attacker;
		if (combat_hooks.onHit) combat_hooks.onHit(attacker, victim);
		return;
	}

	Bot* b = &c->bots[victim - 1];
	if (!b->alive || b->invuln > 0.0f) return;         // spawn protection
	if (b->perks.shield > 0.0f) {
		float soak = dmg < b->perks.shield ? dmg : b->perks.shield;
		b->perks.shield -= soak;
		dmg -= soak;
	}
	b->hp -= dmg;
	b->hitFlash = 0.12f;
	b->lastHitTimer = 0.0f;
	b->lastAttacker = attacker;
	if (combat_hooks.onHit) combat_hooks.onHit(attacker, victim);

	// Hurt by someone unseen: the bot learns only the rough direction of the shot
	float ap[3];
	combat_pos(c, p, attacker, ap);
	b->alertYaw = atan2f(ap[0] - b->x, -(ap[2] - b->z)) + (rng_float() - 0.5f) * 0.7f;
	b->alertValid = true;
	b->alertUsed = false;

	if (attacker == FIGHTER_PLAYER) {
		p->hitMarkerT = 0.15f;
		audio_play(SFX_HITMARK, 0.0f, 1.0f);
	}
	if (b->hp <= 0.0f) killBot(c, p, b, attacker);
}

// ------------------------------------------------------------------- shooting
static void norm3(float v[3])
{
	float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (l > 1.0e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

static void spawnProjectile(Combat* c, const float pos[3], const float dir[3], int weapon, int owner)
{
	const WeaponDef* d = weapon_def(weapon);
	for (int i = 0; i < MAX_PROJECTILES; i++) {
		Projectile* pr = &c->proj[i];
		if (pr->active) continue;
		pr->active = true;
		pr->weapon = weapon;
		pr->owner = owner;
		pr->life = 4.0f;
		for (int a = 0; a < 3; a++) { pr->pos[a] = pos[a]; pr->vel[a] = dir[a] * d->projSpeed; }
		return;
	}
}

void combat_fire(Combat* c, Player* p, const Map* map, int shooter, const float eye[3],
                 const float dir[3], float spread, int weapon)
{
	const WeaponDef* d = weapon_def(weapon);
	const bool visual = c->netRole == NET_CLIENT;      // a client only shows shots, the host works out who is hit
	if (c->netRole == NET_HOST && combat_hooks.onShot) combat_hooks.onShot(c, shooter, weapon, eye, dir, spread);

	float fwd[3] = { dir[0], dir[1], dir[2] };
	norm3(fwd);
	float right[3] = { -fwd[2], 0.0f, fwd[0] };
	if (fabsf(right[0]) + fabsf(right[2]) < 1.0e-4f) { right[0] = 1.0f; right[2] = 0.0f; }
	norm3(right);
	float up[3] = {
		right[1] * fwd[2] - right[2] * fwd[1],
		right[2] * fwd[0] - right[0] * fwd[2],
		right[0] * fwd[1] - right[1] * fwd[0]
	};

	if (d->shot == SHOT_ROCKET) {
		// The player's rockets fly inside the crosshair cone like bullets do. Bots keep their
		// perfectly straight rockets (their aim error is applied to the direction they choose).
		if (visual) return;                      // the rocket arrives in the host's snapshots
		if (isPerson(c, shooter) && spread > 0.0f) {
			float rr = spread * sqrtf(rng_float());
			float ang = rng_float() * 6.2831853f;
			for (int a = 0; a < 3; a++) fwd[a] += right[a] * rr * cosf(ang) + up[a] * rr * sinf(ang);
			norm3(fwd);
		}
		float start[3] = { eye[0] + fwd[0] * 0.6f, eye[1] + fwd[1] * 0.6f - 0.1f, eye[2] + fwd[2] * 0.6f };
		spawnProjectile(c, start, fwd, weapon, shooter);
		return;
	}

	// Where the bullets leave the gun: the first-person muzzle for the player, the bot's gun for bots
	float muzzle[3];
	if (shooter == FIGHTER_PLAYER) {
		float aim = d->zoom > 1.0f ? (p->zoom - 1.0f) / (d->zoom - 1.0f) : 0.0f;
		if (aim < 0.0f) aim = 0.0f;
		if (aim > 1.0f) aim = 1.0f;
		float mv[3];
		viewmodel_muzzle_view(weapon, aim, mv);
		for (int a = 0; a < 3; a++) muzzle[a] = eye[a] + right[a] * mv[0] + up[a] * mv[1] + fwd[a] * mv[2];
	} else {
		float ahead = 0.48f + viewmodel_muzzle_ahead(weapon);
		for (int a = 0; a < 3; a++) muzzle[a] = eye[a] + right[a] * 0.74f + fwd[a] * ahead - (a == 1 ? 0.25f : 0.0f);
	}

	for (int n = 0; n < d->pellets; n++) {
		float rr = spread * sqrtf(rng_float());
		float ang = rng_float() * 6.2831853f;
		float ox = rr * cosf(ang), oy = rr * sinf(ang);
		float rd[3];
		for (int a = 0; a < 3; a++) rd[a] = fwd[a] + right[a] * ox + up[a] * oy;
		norm3(rd);

		float tBest = d->range, t;
		bool hitMap = collision_raycast(map, eye, rd, d->range, &t);
		if (hitMap) tBest = t;

		int hitId = -1;
		for (int id = 0; id <= c->botCount; id++) {
			if (!combat_alive(c, p, id) || !combat_is_enemy(c, p, shooter, id)) continue;
			float mn[3], mx[3];
			fighterBounds(c, p, id, mn, mx);
			if (collision_ray_aabb(mn, mx, eye, rd, tBest, &t)) { tBest = t; hitId = id; }
		}

		float at[3] = { eye[0] + rd[0] * tBest, eye[1] + rd[1] * tBest, eye[2] + rd[2] * tBest };
		fx_tracer(muzzle, at);               // a little white bullet flies from the gun to where it lands
		if (hitId >= 0) {
			if (!visual) {
				// How close to the middle of the egg did the bullet pass? (the egg is taller than wide, so
				// height is squashed to make it a sphere; k = 0 dead centre .. 1 at the edge)
				float mn[3], mx[3];
				fighterBounds(c, p, hitId, mn, mx);
				float ctr[3] = { (mn[0] + mx[0]) * 0.5f, (mn[1] + mx[1]) * 0.5f, (mn[2] + mx[2]) * 0.5f };
				float off[3] = { ctr[0] - eye[0], ctr[1] - eye[1], ctr[2] - eye[2] };
				float along = off[0] * rd[0] + off[1] * rd[1] + off[2] * rd[2];
				for (int a = 0; a < 3; a++) off[a] -= rd[a] * along;
				off[1] *= TARGET_HALF_W / (TARGET_HEIGHT * 0.5f);
				float k = sqrtf(off[0] * off[0] + off[1] * off[1] + off[2] * off[2]) / TARGET_HALF_W;
				combat_damage(c, p, shooter, hitId, weapon_damage_at(d, tBest) * weapon_center_mul(d, k));
#ifdef MELEE_TEST
				if (shooter == FIGHTER_PLAYER) {
					char msg[32];
					snprintf(msg, sizeof(msg), "k %.2f dmg %d", k, (int)(weapon_damage_at(d, tBest) * weapon_center_mul(d, k)));
					combat_banner(c, msg);
				}
#endif
			}
			fx_burst(at, 2, 3.0f, 0.12f, 0.35f, 1.0f, 0.9f, 0.5f);
		} else if (hitMap) {
			fx_burst(at, d->pellets > 1 ? 1 : 3, 3.0f, 0.08f, 0.3f, 0.7f, 0.7f, 0.7f);
		}
	}
}

void combat_boom_fx(Combat* c, const Player* p, const float pos[3], float radius)
{
	(void)c;
	audio_play_at(SFX_EXPLOSION, p->x, p->z, p->yaw, pos[0], pos[2], 1.0f);
	fx_flash(pos, radius * 0.5f, 0.25f, 1.0f, 0.7f, 0.2f);
	fx_burst(pos, 10, 7.0f, 0.35f, 0.6f, 1.0f, 0.45f, 0.1f);
}

static void explode(Combat* c, Player* p, const Map* map, const float pos[3], const WeaponDef* d, int owner)
{
	combat_boom_fx(c, p, pos, d->splashRadius);
	if (c->netRole == NET_HOST && combat_hooks.onBoom) combat_hooks.onBoom(pos, d->splashRadius);

	float R = d->splashRadius;
	for (int id = 0; id <= c->botCount; id++) {
		if (!combat_alive(c, p, id)) continue;
		bool self = (id == owner);
		if (self && owner != FIGHTER_PLAYER) continue;          // bots do not hurt themselves
		if (!self && !combat_is_enemy(c, p, owner, id)) continue;

		float f[3];
		combat_pos(c, p, id, f);
		float ctr[3] = { f[0], f[1] + 0.85f, f[2] };
		float dir[3] = { ctr[0] - pos[0], ctr[1] - pos[1], ctr[2] - pos[2] };
		float dist = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
		if (dist >= R) continue;
		if (dist > 0.05f) {
			norm3(dir);
			float tw;
			if (collision_raycast(map, pos, dir, dist, &tw) && tw < dist - 0.05f) continue;   // wall in the way
		}
		float dmg = d->damage * (1.0f - 0.97f * dist / R);     // like Shell Shockers: full at the centre, 3% at the edge
		if (self) { p->lastAttacker = FIGHTER_PLAYER; player_take_damage(p, dmg * SELF_DAMAGE_SCALE); }
		else combat_damage(c, p, owner, id, dmg);
	}
}

static void updateProjectiles(Combat* c, Player* p, const Map* map, float dt)
{
	for (int i = 0; i < MAX_PROJECTILES; i++) {
		Projectile* pr = &c->proj[i];
		if (!pr->active) continue;

		const WeaponDef* d = weapon_def(pr->weapon);
		float speed = d->projSpeed;
		float dir[3] = { pr->vel[0] / speed, pr->vel[1] / speed, pr->vel[2] / speed };
		float step = speed * dt;

		float bestT = step, t;
		bool hit = false;
		int direct = -1;                                        // a fighter the rocket struck head-on
		if (collision_raycast(map, pr->pos, dir, step, &t)) { bestT = t; hit = true; }

		for (int id = 0; id <= c->botCount; id++) {
			if (!combat_alive(c, p, id) || !combat_is_enemy(c, p, pr->owner, id)) continue;
			float mn[3], mx[3];
			fighterBounds(c, p, id, mn, mx);
			if (collision_ray_aabb(mn, mx, pr->pos, dir, bestT, &t)) { bestT = t; hit = true; direct = id; }
		}

		if (hit) {
			float back = bestT > 0.1f ? bestT - 0.1f : 0.0f;   // pull the blast off the surface
			float at[3] = { pr->pos[0] + dir[0] * back, pr->pos[1] + dir[1] * back, pr->pos[2] + dir[2] * back };
			int owner = pr->owner;
			pr->active = false;
			if (direct >= 0) combat_damage(c, p, owner, direct, ROCKET_DIRECT_DAMAGE);   // a direct hit always cracks
			explode(c, p, map, at, d, owner);
		} else {
			for (int a = 0; a < 3; a++) pr->pos[a] += dir[a] * step;
			pr->life -= dt;
			if (pr->life <= 0.0f) pr->active = false;
		}
	}
}

// ----------------------------------------------------------------- grenades
static const WeaponDef grenadeDef = { .name = "Grenade", .pellets = 1, .damage = GRENADE_DAMAGE, .splashRadius = GRENADE_RADIUS };

void combat_throw_grenade(Combat* c, int owner, const float eye[3], const float dir[3], float speed)
{
	for (int i = 0; i < MAX_GRENADES; i++) {
		Grenade* g = &c->gren[i];
		if (g->active) continue;
		g->active = true;
		g->owner = owner;
		g->fuse = GRENADE_FUSE;
		for (int a = 0; a < 3; a++) { g->pos[a] = eye[a] + dir[a] * 0.5f; g->vel[a] = dir[a] * speed; }
		return;
	}
}

// Falls under gravity, bounces off walls and floors (losing speed) and blows up when the fuse runs out
static void updateGrenades(Combat* c, Player* p, const Map* map, float dt)
{
	for (int i = 0; i < MAX_GRENADES; i++) {
		Grenade* g = &c->gren[i];
		if (!g->active) continue;
		g->vel[1] -= 20.0f * dt;
		for (int a = 0; a < 3; a++) {
			float step = g->vel[a] * dt;
			if (fabsf(step) < 1.0e-6f) continue;
			float sgn = step > 0.0f ? 1.0f : -1.0f;
			float dir[3] = { 0.0f, 0.0f, 0.0f };
			dir[a] = sgn;
			float t;
			if (collision_raycast(map, g->pos, dir, fabsf(step) + 0.12f, &t)) {
				float room = t - 0.12f;
				if (room > 0.0f) g->pos[a] += sgn * room;
				g->vel[a] *= -0.45f;
				int o1 = (a + 1) % 3, o2 = (a + 2) % 3;
				g->vel[o1] *= 0.85f; g->vel[o2] *= 0.85f;          // scrubs speed off on every bounce
				if (a == 1 && fabsf(g->vel[1]) < 1.5f) g->vel[1] = 0.0f;
			} else {
				g->pos[a] += step;
			}
		}
		g->fuse -= dt;
		if (g->fuse <= 0.0f || g->pos[1] < map->killY) {
			g->active = false;
			if (g->pos[1] >= map->killY) explode(c, p, map, g->pos, &grenadeDef, g->owner);
		}
	}
}

// ---------------------------------------------------------------------- melee
static float fighterYaw(const Combat* c, const Player* p, int id)
{
	return id == FIGHTER_PLAYER ? p->yaw : c->bots[id - 1].yaw;
}

int combat_melee_target(const Combat* c, const Player* p, const Map* map, int attacker,
                        const float eye[3], float yaw, float range, float* distOut)
{
	const float fx = sinf(yaw), fz = -cosf(yaw);
	int best = -1;
	float bestD = range;
	for (int id = 0; id <= c->botCount; id++) {
		if (!combat_alive(c, p, id) || !combat_is_enemy(c, p, attacker, id)) continue;
		if (id == FIGHTER_PLAYER && c->noPlayer) continue;
		float mn[3], mx[3];
		fighterBounds(c, p, id, mn, mx);
		// distance from the eye to the nearest point of the target box
		float q[3], d2 = 0.0f;
		for (int a = 0; a < 3; a++) {
			q[a] = eye[a] < mn[a] ? mn[a] : (eye[a] > mx[a] ? mx[a] : eye[a]);
			d2 += (q[a] - eye[a]) * (q[a] - eye[a]);
		}
		float d = sqrtf(d2);
		if (d > bestD) continue;
		float cx = (mn[0] + mx[0]) * 0.5f - eye[0], cz = (mn[2] + mx[2]) * 0.5f - eye[2];
		float h = sqrtf(cx * cx + cz * cz);
		if (h > 0.9f && (cx * fx + cz * fz) / h < MELEE_CONE_COS) continue;   // not in front (point blank always counts)
		float cy = (mn[1] + mx[1]) * 0.5f - eye[1];
		float to[3] = { cx, cy, cz };
		float L = sqrtf(cx * cx + cy * cy + cz * cz);
		if (L > 0.5f) {                                                  // no whacking through walls
			for (int a = 0; a < 3; a++) to[a] /= L;
			float t;
			if (collision_raycast(map, eye, to, L - 0.45f, &t)) continue;
		}
		best = id;
		bestD = d;
	}
	if (distOut) *distOut = bestD;
	return best;
}

int combat_melee(Combat* c, Player* p, const Map* map, int attacker, const float eye[3], float yaw, bool* backOut)
{
	if (backOut) *backOut = false;
	int victim = combat_melee_target(c, p, map, attacker, eye, yaw, MELEE_RANGE, NULL);
	if (c->netRole == NET_HOST && combat_hooks.onMelee) combat_hooks.onMelee(attacker, victim >= 0);
	if (victim < 0) return -1;

	// From behind (both facing roughly the same way): one whack cracks the egg
	float vy = fighterYaw(c, p, victim);
	bool back = sinf(yaw) * sinf(vy) + cosf(yaw) * cosf(vy) > MELEE_BACK_COS;
	if (backOut) *backOut = back;

	float vp[3];
	combat_pos(c, p, victim, vp);
	float at[3] = { vp[0], vp[1] + 1.0f, vp[2] };
	fx_burst(at, back ? 8 : 5, 4.0f, 0.18f, 0.45f, 1.0f, 0.95f, 0.75f);
	audio_play_at(SFX_MELEE_HIT, p->x, p->z, p->yaw, vp[0], vp[2], 1.0f);

	if (c->netRole != NET_CLIENT) {                                       // the host decides who gets hurt
		combat_damage(c, p, attacker, victim, back ? MELEE_BACK_DAMAGE : MELEE_DAMAGE);
		if (attacker == FIGHTER_PLAYER && back && !combat_alive(c, p, victim)) combat_banner(c, "BACKWHISK!");
#ifdef MELEE_TEST
		else if (attacker == FIGHTER_PLAYER && victim >= 1) {
			char msg[32];
			snprintf(msg, sizeof(msg), "HIT %s hp %d", back ? "back" : "front", (int)c->bots[victim - 1].hp);
			combat_banner(c, msg);
		} else if (victim == FIGHTER_PLAYER) {
			char msg[32];
			snprintf(msg, sizeof(msg), "WHISKED BY BOT %d", attacker);
			combat_banner(c, msg);
		}
#endif
	}
	return victim;
}

// --------------------------------------------------------------------- update
static float decayf(float v, float dt)
{
	v -= dt;
	return v < 0.0f ? 0.0f : v;
}

static void playerFire(Combat* c, Player* p, const Map* map, const WeaponDef* d)
{
	float eye[3], fwd[3];
	player_eye(p, eye);
	player_forward(p, fwd);
	float spread = weapon_fire_spread(d, &p->ws[p->cur], p->aiming, p->bloom);
	if (p->aiming && d->scopeSettle > 0.0f && p->aimHeld < SCOPE_SETTLE_TIME)        // just scoped in: not steady yet
		spread += d->scopeSettle * (1.0f - p->aimHeld / SCOPE_SETTLE_TIME);
#ifdef MELEE_TEST
	{ char msg[32]; snprintf(msg, sizeof(msg), "%s %.1f deg", d->name, spread * 57.3f); combat_banner(c, msg); }
#endif
	combat_fire(c, p, map, FIGHTER_PLAYER, eye, fwd, spread, p->cur);
	if (c->netRole == NET_CLIENT && combat_hooks.onLocalFire) combat_hooks.onLocalFire(p->cur, spread, fwd);

	audio_play(SFX_FIRE_BASE + p->cur, 0.0f, 1.0f);
	p->invuln = 0.0f;                                   // firing ends spawn protection
	p->flashT = 0.06f;
	p->kick += d->recoil;
	p->pitch += d->recoil * 0.5f;
	p->recoilPitch += d->recoil * 0.5f;
	if (p->pitch > 1.4f) p->pitch = 1.4f;
}

static void playerTimers(Player* p, float dt)
{
	p->flashT = decayf(p->flashT, dt);
	p->hitMarkerT = decayf(p->hitMarkerT, dt);
	p->damageFlashT = decayf(p->damageFlashT, dt);
	p->switchAnim = decayf(p->switchAnim, dt);
	p->kick -= p->kick * 10.0f * dt;
	if (p->kick < 0.0005f) p->kick = 0.0f;
	p->lastHitTimer += dt;

	// Camera recoil recovers on its own so sustained fire stays controllable
	if (p->recoilPitch > 0.0f) {
		float rec = 0.6f * dt;
		if (rec > p->recoilPitch) rec = p->recoilPitch;
		p->pitch -= rec;
		p->recoilPitch -= rec;
	}
}

static void playerWeapons(Combat* c, Player* p, const PlayerInput* in, const Map* map, float dt)
{
	// Weapon switching between the two loadout slots (cancels reload and burst)
#ifdef MELEE_TEST
	if (in->prevWeapon) {                              // test build: D-Pad Left tries the next primary gun
		p->loadout[0] = (p->loadout[0] + 1) % PRIMARY_COUNT;
		weapon_state_init(&p->ws[p->loadout[0]], p->loadout[0]);
		p->ws[p->loadout[0]].reserve = weapon_def(p->loadout[0])->reserveMax;
		p->cur = p->loadout[1];                        // forces the switch below
		p->slot = 0;
		combat_banner(c, weapon_def(p->loadout[0])->name);
	}
	int slot = in->prevWeapon ? 0 : p->slot;
	if (in->selectWeapon >= 0 && in->selectWeapon < 2) slot = in->selectWeapon;
	else if (in->nextWeapon) slot ^= 1;
#else
	int slot = p->slot;
	if (in->selectWeapon >= 0 && in->selectWeapon < 2) slot = in->selectWeapon;
	else if (in->nextWeapon || in->prevWeapon) slot ^= 1;
#endif
	int want = p->loadout[slot];
	if (want != p->cur) {
		p->ws[p->cur].reloadLeft = 0.0f;
		p->ws[p->cur].burstLeft = 0;
		p->cur = want;
		p->slot = slot;
		p->ws[want].reloadLeft = 0.0f;
		p->ws[want].burstLeft = 0;
		p->ws[want].cooldown = 0.3f;
		p->switchAnim = 0.25f;
		audio_play(SFX_SWITCH, 0.0f, 1.0f);
	}

	WeaponState* ws = &p->ws[p->cur];
	const WeaponDef* d = weapon_def(p->cur);

	// Whisk whack: the gun drops out of the way, the whisk swings across, and it lands a moment in
	p->meleeCd = decayf(p->meleeCd, dt);
	if (in->melee && p->meleeCd <= 0.0f && p->meleeT <= 0.0f && !c->frozen) {
		p->meleeT = MELEE_TIME;
		p->meleeCd = MELEE_COOLDOWN;
		p->meleeHitDone = false;
		p->meleeConnected = false;
		ws->reloadLeft = 0.0f;                              // a swing cancels the reload
		ws->burstLeft = 0;
		p->invuln = 0.0f;
		audio_play(SFX_MELEE_SWING, 0.0f, 1.0f);
		float eye[3], dist;
		player_eye(p, eye);
		int t = combat_melee_target(c, p, map, FIGHTER_PLAYER, eye, p->yaw, MELEE_LUNGE, &dist);
		if (t >= 0 && dist > 1.0f) {                        // a step toward an egg that is just out of reach
			float tp[3];
			combat_pos(c, p, t, tp);
			float dx = tp[0] - p->x, dz = tp[2] - p->z, l = sqrtf(dx * dx + dz * dz);
			if (l > 0.01f) {
				p->lungeDir[0] = dx / l;
				p->lungeDir[1] = dz / l;
				p->lungeT = fminf((dist - 0.9f) / MELEE_LUNGE_SPEED, 0.16f);
			}
		}
	}
	const bool swinging = p->meleeT > 0.0f;
	if (swinging) {
		p->meleeT = decayf(p->meleeT, dt);
		if (!p->meleeHitDone && MELEE_TIME - p->meleeT >= MELEE_HIT_AT) {
			p->meleeHitDone = true;
			float eye[3];
			player_eye(p, eye);
			p->meleeConnected = combat_melee(c, p, map, FIGHTER_PLAYER, eye, p->yaw, NULL) >= 0;
			if (c->netRole == NET_CLIENT && combat_hooks.onLocalMelee) combat_hooks.onLocalMelee(p->yaw);
			if (p->meleeConnected) p->kick += 0.04f;
		}
	}

	WeaponEvents ev = weapon_update(ws, d, p->perks.hotT > 0.0f ? dt * PERK_HOT_SPEED : dt,
	                                in->fire && !swinging, in->fireDown && !swinging, in->reload && !swinging);
	if (ev.reloadStarted) audio_play(SFX_RELOAD, 0.0f, 1.0f);
	if (ev.reloadDone) audio_play(SFX_RELOAD_DONE, 0.0f, 1.0f);

	p->grenadeCd = decayf(p->grenadeCd, dt);
	if (in->grenade && p->grenades > 0 && p->grenadeCd <= 0.0f && !c->frozen) {
		float eye[3], fwd[3];
		player_eye(p, eye);
		player_forward(p, fwd);
		fwd[1] += 0.22f;                                   // lobbed slightly upward
		float l = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
		for (int a = 0; a < 3; a++) fwd[a] /= l;
		if (c->netRole == NET_CLIENT) { if (combat_hooks.onLocalGrenade) combat_hooks.onLocalGrenade(fwd); }   // the host throws it
		else combat_throw_grenade(c, FIGHTER_PLAYER, eye, fwd, GRENADE_SPEED);
		p->grenades--;
		p->grenadeCd = GRENADE_COOLDOWN;
		p->invuln = 0.0f;
		audio_play(SFX_JUMP, 0.0f, 1.0f);
	}

	p->aiming = in->aim && ws->reloadLeft <= 0.0f && !swinging;
	p->aimHeld = p->aiming ? p->aimHeld + dt : 0.0f;
	if (ev.shot) playerFire(c, p, map, d);

	// Smooth zoom
	float target = p->aiming ? d->zoom : 1.0f;
	float k = 12.0f * dt;
	if (k > 1.0f) k = 1.0f;
	p->zoom += (target - p->zoom) * k;
}

#ifdef MELEE_TEST
// Test build only (make CFLAGS_EXTRA=-DMELEE_TEST=1 or 2). Bot 1 is put in front of the player at the
// start of each of its lives; the other bots stand still.
//   1: a dummy. Lives cycle: facing you at 1.7 m, facing away at 1.7 m, facing you at 3.0 m (lunge).
//   2: the bot thinks for itself (watch it swing back), placed 2 m away facing you.
static void meleeTest(Combat* c, Player* p, const Map* map, float dt)
{
	static int lives = 0;
	static bool wasAlive = false;
	Bot* b = &c->bots[0];
	if (!b->alive) {
		wasAlive = false;
		b->respawnTimer -= dt;
		if (b->respawnTimer <= 0.0f) bot_respawn(c, p, b);
		return;
	}
	if (!wasAlive && p->alive && !c->frozen) {
		wasAlive = true;
		int phase = lives++ % 3;
		float d = MELEE_TEST == 2 ? 2.0f : (phase == 2 ? 3.0f : 1.7f);
		b->x = p->x + sinf(p->yaw) * d;
		b->z = p->z - cosf(p->yaw) * d;
		b->y = p->y;
		b->yaw = (MELEE_TEST == 1 && phase == 1) ? p->yaw : p->yaw + 3.1415927f;
		b->invuln = 0.0f;
		char msg[32];
		snprintf(msg, sizeof(msg), "%s %.1fm", MELEE_TEST == 2 ? "AI" : (phase == 1 ? "BACK" : "FRONT"), d);
		combat_banner(c, msg);
	}
	if (MELEE_TEST == 2 && !c->frozen) {
		bot_update(c, p, map, b, dt);
		p->hp = PLAYER_MAX_HP;                       // stay alive to watch it swing
	}
}
#endif

void combat_update(Combat* c, Player* p, const PlayerInput* in, const Map* map, float dt)
{
	c->frame++;
	c->pathBudget = 2;

	updateProjectiles(c, p, map, dt);
	updateGrenades(c, p, map, dt);

#ifdef MELEE_TEST
	meleeTest(c, p, map, dt);
	for (int i = 1; i < c->botCount; i++) c->bots[i].invuln = 0.0f;
#else
	for (int i = 0; i < c->botCount; i++) {
		Bot* b = &c->bots[i];
		if (b->remote) combat_remote_update(c, p, b, dt);          // a person on another console: no AI
		else if (!c->frozen && c->obj.roundPause <= 0.0f) bot_update(c, p, map, b, dt);
	}
#endif
	obj_update(c, p, dt);
	ammo_update(c, p, dt);

	if (!c->frozen) {          // perk timers
		if (c->perkMsgT > 0.0f) c->perkMsgT -= dt;
		Perks* all[MAX_BOTS + 1];
		int n = 0;
		all[n++] = &p->perks;
		for (int i = 0; i < c->botCount; i++) all[n++] = &c->bots[i].perks;
		for (int i = 0; i < n; i++) {
			if (all[i]->hotT > 0.0f) all[i]->hotT -= dt;
			if (all[i]->crackT > 0.0f) all[i]->crackT -= dt;
		}
	}

	// Spawn protection only counts down while the match is live
	if (!c->frozen && p->invuln > 0.0f) p->invuln = decayf(p->invuln, dt);

	playerTimers(p, dt);

	// Pause-menu Respawn: the egg stays in the arena (and can be shot) until the timer runs out.
	// Leaving this way is not a death: no death is counted and nobody gets a kill.
	if (p->alive && p->despawnT > 0.0f && p->hp > 0.0f && !c->frozen) {
		p->despawnT -= dt;
		if (p->despawnT <= 0.0f) {
			p->despawnT = 0.0f;
			p->alive = false;
			p->deadTimer = 0.0f;          // can pick a weapon and come back straight away
			{ float fp[3] = { p->x, p->y, p->z }; obj_on_death(c, FIGHTER_PLAYER, fp); }
			p->aiming = false;
			p->zoom = 1.0f;
			p->lastAttacker = -1;
			audio_play(SFX_EGG_CRACK, 0.0f, 1.0f);
			return;
		}
	}

	if (p->alive && p->hp <= 0.0f) {
		p->despawnT = 0.0f;               // killed first: an ordinary death
		p->alive = false;
		p->deaths++;
		p->deadTimer = RESPAWN_DELAY;
		p->aiming = false;
		audio_play(SFX_DEATH, 0.0f, 1.0f);
		{ float fp[3] = { p->x, p->y, p->z }; obj_on_death(c, FIGHTER_PLAYER, fp); }

		endStreak(&p->perks);
		int killer = p->lastAttacker;
		if (killer >= 1 && killer <= c->botCount) {
			c->bots[killer - 1].kills++;
			awardKill(c, p, killer);
			if (c->mode == MODE_TDM) {
				int t = c->bots[killer - 1].team;
				if (t == 0 || t == 1) c->teamScore[t]++;
			}
		}
	}
	if (!p->alive) {
		p->deadTimer -= dt;
		p->zoom = 1.0f;
		return;
	}

	if (p->hp < PLAYER_MAX_HP && p->lastHitTimer > REGEN_DELAY) {
		p->hp += REGEN_RATE * dt;
		if (p->hp > PLAYER_MAX_HP) p->hp = PLAYER_MAX_HP;
	}

	playerWeapons(c, p, in, map, dt);
}

// -------------------------------------------------------------- local multiplayer
int combat_add_remote(Combat* c, const char* name, int team, int node, int weapon)
{
	const bool teamMode = combat_is_team_mode(c);
	int idx = -1;
	for (int i = c->botCount - 1; i >= 0; i--) {             // take over an AI slot on that team, last one first
		const Bot* b = &c->bots[i];
		if (b->remote || (teamMode && b->team != team)) continue;
		idx = i;
		break;
	}
	if (idx < 0) {
		if (c->botCount >= MAX_BOTS) return -1;
		idx = c->botCount++;
		bot_create(c, idx, teamMode ? team : -1);
	}
	Bot* b = &c->bots[idx];
	b->remote = true;
	b->node = node;
	b->team = teamMode ? team : 0;
	snprintf(b->name, sizeof(b->name), "%s", name);
	if (weapon < 0 || weapon >= PRIMARY_COUNT) weapon = W_PECKMATIC;
	bot_respawn(c, NULL, b);
	b->weapon = weapon;
	weapon_state_init(&b->ws, weapon);
	b->ackSpawn = -1;
	b->lastAttacker = -1;
	obj_recount(c);
	return b->id;
}

void combat_drop_remote(Combat* c, int id)
{
	if (id < 1 || id > c->botCount) return;
	Bot* b = &c->bots[id - 1];
	if (!b->remote) return;
	b->remote = false;
	b->node = 0;
	b->target = b->lastTarget = -1;
	b->pathLen = b->pathIdx = 0;
	b->state = BS_IDLE;
	b->idleTime = 0.5f;
	b->aimValid = false;
	b->weapon = b->weapon < 0 || b->weapon >= WEAPON_COUNT ? W_PECKMATIC : b->weapon;
	weapon_state_init(&b->ws, b->weapon);
	if (!b->alive) b->respawnTimer = 1.0f;
}

void combat_remote_update(Combat* c, Player* p, Bot* b, float dt)
{
	b->frame++;
	b->hitFlash = decayf(b->hitFlash, dt);
	b->flashT = decayf(b->flashT, dt);
	b->lastHitTimer += dt;
	b->grenadeCd = decayf(b->grenadeCd, dt);
	b->meleeT = decayf(b->meleeT, dt);
	b->meleeCd = decayf(b->meleeCd, dt);
	if (!b->alive) {
		if (b->respawnTimer > 0.0f) b->respawnTimer -= dt;
		return;
	}
	if (!c->frozen && b->invuln > 0.0f) b->invuln -= dt;
	if (b->hp < BOT_MAX_HP && b->lastHitTimer > REGEN_DELAY && c->obj.roundPause <= 0.0f) {
		b->hp += REGEN_RATE * dt;
		if (b->hp > BOT_MAX_HP) b->hp = BOT_MAX_HP;
	}
	if (p->alive && ((unsigned)b->id + b->frame) % 3u == 0u) {      // what the host's radar may show
		float eye[3] = { p->x, p->y + EYE_HEIGHT, p->z };
		float chest[3] = { b->x, b->y + 0.9f, b->z };
		float dx = chest[0] - eye[0], dz = chest[2] - eye[2];
		b->visibleToPlayer = (dx * dx + dz * dz < 70.0f * 70.0f) && bot_line_clear(c->map, eye, chest);
	} else if (!p->alive) {
		b->visibleToPlayer = false;
	}
	if (b->y < c->map->killY) {                                     // fell out of the map
		b->hp = 0.0f;
		b->lastAttacker = -1;
		killBot(c, p, b, -1);
	}
}

void combat_remote_respawn(Combat* c, Player* p, int id, int weapon)
{
	if (id < 1 || id > c->botCount) return;
	Bot* b = &c->bots[id - 1];
	if (!b->remote || b->alive || c->frozen) return;
	if (c->mode == MODE_ELIM) return;                     // rounds bring everybody back together
	if (b->respawnTimer > 0.0f) return;
	if (weapon < 0 || weapon >= PRIMARY_COUNT) weapon = W_PECKMATIC;
	bot_respawn(c, p, b);
	b->weapon = weapon;
	weapon_state_init(&b->ws, weapon);
	b->lastAttacker = -1;
}

void combat_record_history(Combat* c, const Player* p)
{
	float (*h)[3] = c->hist[c->frame % HIST_N];
	for (int id = 0; id <= c->botCount; id++) combat_pos(c, p, id, h[id]);
}

void combat_rewind_begin(Combat* c, int frameThen, int selfId)
{
	int lag = c->frame - frameThen;
	if (lag <= 0 || lag >= HIST_N - 1) { c->rewindOn = false; return; }
	c->rewindOn = true;
	c->rewindSlot = ((frameThen % HIST_N) + HIST_N) % HIST_N;
	c->rewindSelf = selfId;
}

void combat_rewind_end(Combat* c) { c->rewindOn = false; }

// A client does not simulate the match. It plays its own egg (movement and weapons), keeps the
// host's rockets and grenades moving between snapshots, and shows effects.
void combat_update_client(Combat* c, Player* p, const PlayerInput* in, const Map* map, float dt)
{
	c->frame++;
	if (c->perkMsgT > 0.0f) c->perkMsgT -= dt;
	if (c->obj.msgT > 0.0f) c->obj.msgT -= dt;

	for (int i = 0; i < MAX_PROJECTILES; i++) {
		Projectile* pr = &c->proj[i];
		if (!pr->active) continue;
		for (int a = 0; a < 3; a++) pr->pos[a] += pr->vel[a] * dt;
		pr->life -= dt;
		if (pr->life <= 0.0f) pr->active = false;
	}
	for (int i = 0; i < MAX_GRENADES; i++) {
		Grenade* g = &c->gren[i];
		if (!g->active) continue;
		g->vel[1] -= 20.0f * dt;
		for (int a = 0; a < 3; a++) g->pos[a] += g->vel[a] * dt;
		g->fuse -= dt;
		if (g->fuse <= -0.5f) g->active = false;               // the host's snapshot normally ends it first
	}
	for (int i = 0; i < c->botCount; i++) {
		Bot* b = &c->bots[i];
		b->hitFlash = decayf(b->hitFlash, dt);
		b->flashT = decayf(b->flashT, dt);
		b->meleeT = decayf(b->meleeT, dt);
	}

	if (p->perks.hotT > 0.0f) p->perks.hotT -= dt;
	if (p->perks.crackT > 0.0f) p->perks.crackT -= dt;
	if (!c->frozen && p->invuln > 0.0f) p->invuln = decayf(p->invuln, dt);
	playerTimers(p, dt);

	if (!p->alive) {
		p->deadTimer -= dt;
		p->zoom = 1.0f;
		return;
	}
	playerWeapons(c, p, in, map, dt);
}

// ------------------------------------------------------------------- scoreboard
int combat_scoreboard(const Combat* c, const Player* p, ScoreRow* rows, int max)
{
	int n = 0;
	if (n < max && !c->noPlayer) {
		strncpy(rows[n].name, "You", sizeof(rows[n].name) - 1);
		rows[n].name[sizeof(rows[n].name) - 1] = '\0';
		rows[n].team = combat_is_team_mode(c) ? c->playerTeam : -1;
		rows[n].kills = p->kills;
		rows[n].deaths = p->deaths;
		rows[n].isPlayer = true;
		rows[n].alive = p->alive;
		n++;
	}
	for (int i = 0; i < c->botCount && n < max; i++) {
		if (!c->bots[i].name[0]) continue;                   // the empty seat of a dedicated server
		strncpy(rows[n].name, c->bots[i].name, sizeof(rows[n].name) - 1);
		rows[n].name[sizeof(rows[n].name) - 1] = '\0';
		rows[n].team = combat_is_team_mode(c) ? c->bots[i].team : -1;
		rows[n].kills = c->bots[i].kills;
		rows[n].deaths = c->bots[i].deaths;
		rows[n].isPlayer = false;
		rows[n].alive = c->bots[i].alive;
		n++;
	}
	// sort by kills, then fewer deaths
	for (int i = 1; i < n; i++) {
		ScoreRow key = rows[i];
		int j = i - 1;
		while (j >= 0 && (rows[j].kills < key.kills || (rows[j].kills == key.kills && rows[j].deaths > key.deaths))) {
			rows[j + 1] = rows[j];
			j--;
		}
		rows[j + 1] = key;
	}
	return n;
}

// ---------------------------------------------------------------------- drawing
static void botShell(const Combat* c, const Bot* b, float rgb[3])
{
	static const float pastel[8][3] = {
		{ 0.98f, 0.95f, 0.85f }, { 0.95f, 0.80f, 0.55f }, { 0.75f, 0.90f, 0.75f }, { 0.95f, 0.75f, 0.85f },
		{ 0.75f, 0.85f, 0.98f }, { 0.98f, 0.90f, 0.55f }, { 0.85f, 0.78f, 0.95f }, { 0.90f, 0.70f, 0.60f }
	};
	if (combat_is_team_mode(c)) {
		if (b->team == TEAM_RED) { rgb[0] = 0.96f; rgb[1] = 0.50f; rgb[2] = 0.46f; }
		else                     { rgb[0] = 0.50f; rgb[1] = 0.66f; rgb[2] = 0.98f; }
	} else {
		for (int k = 0; k < 3; k++) rgb[k] = pastel[b->id % 8][k];
	}
	if (b->invuln > 0.0f && (((int)(b->invuln * 8.0f)) & 1)) { rgb[0] = 0.75f; rgb[1] = 0.95f; rgb[2] = 1.0f; }   // shimmer while protected
	if (b->hitFlash > 0.0f) { rgb[0] = 1.0f; rgb[1] = 1.0f; rgb[2] = 0.5f; }
}

// A big rocket: six-sided red body with a cream nose cone, three dark fins and a flickering flame.
static void rocketTri(const float a[3], const float b[3], const float c[3], const float ca[3], const float cb[3], const float cc[3])
{
	const float p[3][3] = { { a[0], a[1], a[2] }, { b[0], b[1], b[2] }, { c[0], c[1], c[2] } };
	const float col[3][3] = { { ca[0], ca[1], ca[2] }, { cb[0], cb[1], cb[2] }, { cc[0], cc[1], cc[2] } };
	renderer_world_tri(p, col);
}

static void drawRocket(const Projectile* pr, int frame)
{
	float d[3] = { pr->vel[0], pr->vel[1], pr->vel[2] };
	float dl = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	if (dl < 1.0e-4f) return;
	for (int a = 0; a < 3; a++) d[a] /= dl;
	float ref[3] = { 0.0f, 1.0f, 0.0f };
	if (fabsf(d[1]) > 0.95f) { ref[0] = 1.0f; ref[1] = 0.0f; }
	float u[3] = { d[1] * ref[2] - d[2] * ref[1], d[2] * ref[0] - d[0] * ref[2], d[0] * ref[1] - d[1] * ref[0] };
	float ul = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
	for (int a = 0; a < 3; a++) u[a] /= ul;
	float v[3] = { d[1] * u[2] - d[2] * u[1], d[2] * u[0] - d[0] * u[2], d[0] * u[1] - d[1] * u[0] };

	const float R = 0.15f, TAIL = -0.55f, BODY = 0.30f, NOSE = 0.65f;   // metres along the flight direction
	const int N = 6;
	float ring0[6][3], ring1[6][3], tail[3], nose[3], flame[3];
	for (int a = 0; a < 3; a++) {
		tail[a]  = pr->pos[a] + d[a] * TAIL;
		nose[a]  = pr->pos[a] + d[a] * NOSE;
		flame[a] = pr->pos[a] + d[a] * (TAIL - 0.45f - 0.2f * (float)(frame & 1));
	}
	for (int i = 0; i < N; i++) {
		float ang = 6.2831853f * (float)i / (float)N, cs = cosf(ang) * R, sn = sinf(ang) * R;
		for (int a = 0; a < 3; a++) {
			float off = u[a] * cs + v[a] * sn;
			ring0[i][a] = pr->pos[a] + d[a] * TAIL + off;
			ring1[i][a] = pr->pos[a] + d[a] * BODY + off;
		}
	}
	const float red[3] = { 0.86f, 0.22f, 0.18f }, redDark[3] = { 0.62f, 0.14f, 0.12f };
	const float cream[3] = { 0.98f, 0.90f, 0.62f }, creamDark[3] = { 0.80f, 0.70f, 0.45f };
	const float finCol[3] = { 0.22f, 0.22f, 0.28f };
	const float fHot[3] = { 1.0f, 0.95f, 0.55f }, fTip[3] = { 1.0f, 0.40f, 0.05f };
	const float dark[3] = { 0.25f, 0.10f, 0.08f };

	for (int i = 0; i < N; i++) {
		int j = (i + 1) % N;
		const float* c1 = (i & 1) ? red : redDark;
		rocketTri(ring0[i], ring0[j], ring1[j], c1, c1, c1);
		rocketTri(ring0[i], ring1[j], ring1[i], c1, c1, c1);
		const float* c2 = (i & 1) ? cream : creamDark;
		rocketTri(ring1[i], ring1[j], nose, c2, c2, c2);          // nose cone
		rocketTri(ring0[i], ring0[j], tail, dark, dark, dark);    // back plate
		rocketTri(ring0[i], ring0[j], flame, fHot, fHot, fTip);   // flame
	}
	// three fins at the tail
	for (int k = 0; k < 3; k++) {
		float ang = 6.2831853f * (float)k / 3.0f, cs = cosf(ang), sn = sinf(ang);
		float out[3], base0[3], base1[3], tip[3];
		for (int a = 0; a < 3; a++) {
			out[a]   = u[a] * cs + v[a] * sn;
			base0[a] = pr->pos[a] + d[a] * (TAIL + 0.35f) + out[a] * R;
			base1[a] = pr->pos[a] + d[a] * TAIL + out[a] * R;
			tip[a]   = pr->pos[a] + d[a] * (TAIL - 0.12f) + out[a] * R * 2.3f;
		}
		rocketTri(base0, base1, tip, finCol, finCol, finCol);
	}
}

void combat_draw(const Combat* c, const float cam[3])
{
	obj_draw(c);
	ammo_draw(c);
	for (int i = 0; i < c->botCount; i++) {
		const Bot* b = &c->bots[i];
		if (!b->alive) continue;
		float rgb[3];
		botShell(c, b, rgb);
		if (combat_is_team_mode(c)) {
			// outline in the team colour, a bit thicker the further away the egg is so it stays visible
			float dx = b->x - cam[0], dy = b->y + 0.85f - cam[1], dz = b->z - cam[2];
			float dist = sqrtf(dx * dx + dy * dy + dz * dz);
			float thick = 0.0085f * dist;
			if (thick < 0.05f) thick = 0.05f;
			if (thick > 0.24f) thick = 0.24f;
			if (b->team == TEAM_RED) egg_outline(b->x, b->y, b->z, thick, 1.00f, 0.22f, 0.18f);
			else                     egg_outline(b->x, b->y, b->z, thick, 0.12f, 0.78f, 1.00f);
		}
		egg_draw(b->x, b->y, b->z, rgb[0], rgb[1], rgb[2]);

		// Team marker above the head
		if (combat_is_team_mode(c)) {
			float m = (b->team == TEAM_RED) ? 1.0f : 0.0f;
			renderer_world_box(b->x, b->y + 2.0f, b->z, 0.12f, 0.12f, 0.12f, m, 0.25f, 1.0f - m * 0.8f);
		}

		// Gun held out in front, aimed where the bot is looking
		if (b->meleeT > 0.0f) {                              // swinging the whisk instead
			viewmodel_draw_world_whisk(b->x, b->y + 0.95f, b->z, b->yaw, 1.0f - b->meleeT / MELEE_TIME);
			continue;
		}
		float fx = sinf(b->yaw), fz = -cosf(b->yaw);
		float rx = cosf(b->yaw), rz = sinf(b->yaw);
		viewmodel_draw_world(b->weapon,b->x + fx * 0.48f + rx * 0.74f, b->y + 0.85f, b->z + fz * 0.48f + rz * 0.74f,
		                     b->yaw, b->pitch, b->flashT > 0.0f);
	}
	for (int i = 0; i < MAX_PROJECTILES; i++) {
		const Projectile* pr = &c->proj[i];
		if (pr->active) drawRocket(pr, c->frame);
	}
	for (int i = 0; i < MAX_GRENADES; i++) {                 // grenades: a green egg-shaped lump that blinks red as it is about to blow
		const Grenade* g = &c->gren[i];
		if (!g->active) continue;
		bool blink = g->fuse < 0.9f && ((c->frame / 3) & 1);
		renderer_world_box(g->pos[0], g->pos[1] + 0.09f, g->pos[2], 0.085f, 0.10f, 0.085f, blink ? 1.0f : 0.28f, blink ? 0.2f : 0.38f, blink ? 0.15f : 0.2f);
		renderer_world_box(g->pos[0], g->pos[1] + 0.20f, g->pos[2], 0.04f, 0.025f, 0.04f, 0.8f, 0.8f, 0.8f);
	}
}

// Names above the bots. Drawn as 2D text after the 3D scene, only for bots the camera can really
// see (no names through walls), smaller the further away they are.
void combat_draw_names(const Combat* c, const Player* cam, float zoom)
{
	if (zoom < 1.0f) zoom = 1.0f;
	float e[3], f[3], roll;
	player_camera_pose(cam, e, f, &roll);
	const float right[3] = { cosf(cam->yaw), 0.0f, sinf(cam->yaw) };
	const float up[3] = { right[1] * f[2] - right[2] * f[1], right[2] * f[0] - right[0] * f[2], right[0] * f[1] - right[1] * f[0] };
	const bool scoped = cam->aiming && weapon_def(cam->cur)->scoped;
	const float focal = 120.0f / tanf(0.5f * (BASE_FOV / zoom) * 3.14159265f / 180.0f);   // pixels, screen is 400 x 240

	for (int i = 0; i < c->botCount; i++) {
		const Bot* b = &c->bots[i];
		if (!b->alive) continue;
		const float dx = b->x - e[0], dy = (b->y + TARGET_HEIGHT + 0.30f) - e[1], dz = b->z - e[2];
		const float zc = dx * f[0] + dy * f[1] + dz * f[2];
		if (zc < 0.5f || zc > 70.0f) continue;
		const float sx = 200.0f + (dx * right[0] + dy * right[1] + dz * right[2]) / zc * focal;
		const float sy = 120.0f - (dx * up[0] + dy * up[1] + dz * up[2]) / zc * focal;
		if (sx < -60.0f || sx > 460.0f || sy < -10.0f || sy > 250.0f) continue;
		if (scoped && hypotf(sx - 200.0f, sy - 120.0f) > 100.0f) continue;   // outside the round lens

		// line of sight to the middle of the egg
		float to[3] = { b->x - e[0], (b->y + TARGET_HEIGHT * 0.5f) - e[1], b->z - e[2] };
		const float dist = sqrtf(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
		if (dist > 0.5f) {
			to[0] /= dist; to[1] /= dist; to[2] /= dist;
			float t;
			if (c->map && collision_raycast(c->map, e, to, dist - 0.4f, &t)) continue;
		}

		float scale = 0.75f - 0.012f * zc;
		if (scale < 0.32f) scale = 0.32f;
		if (scale > 0.62f) scale = 0.62f;
		u32 col = UI_COL(255, 255, 255);
		if (combat_is_team_mode(c)) col = (b->team == TEAM_RED) ? UI_COL(255, 120, 110) : UI_COL(110, 205, 255);
		ui_text_shadow(sx, sy - 14.0f * scale, scale, col, UI_CENTER, b->name);
	}
}
