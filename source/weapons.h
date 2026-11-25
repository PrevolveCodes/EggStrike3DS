#ifndef WEAPONS_H
#define WEAPONS_H

#include <stdbool.h>

typedef enum {
	W_SPLATTERPOP,   // pump shotgun
	W_YOLKBUSTER,    // launcher
	W_LONGBEAK,      // bolt-action sniper
	W_PECKMATIC,     // automatic rifle
	W_FARMHAND,      // semi-auto marksman rifle
	W_TRIPLET,       // 3-round burst rifle
	W_EGGSTOL,       // pistol: the one fixed secondary, never selectable as a primary
	WEAPON_COUNT
} WeaponId;

// Weapons 0..PRIMARY_COUNT-1 can be picked as the primary. The secondary is always W_EGGSTOL.
#define PRIMARY_COUNT W_EGGSTOL

typedef enum { FIRE_SEMI, FIRE_AUTO, FIRE_BURST } FireMode;
typedef enum { SHOT_HITSCAN, SHOT_ROCKET } ShotType;

typedef struct {
	const char* name;
	FireMode mode;
	ShotType shot;
	int    pellets;       // hitscan pellets per shot
	float  damage;        // per pellet (hitscan) or centre-of-blast damage (rocket)
	float  spread;        // hip-fire cone, radians
	float  adsSpread;     // aimed cone, radians
	float  hipSpread;     // the player's un-aimed cone when it should differ from 'spread' (bots use 'spread'); 0 = same
	float  moveSpread;    // extra cone, radians, added at full movement inaccuracy (walking or jumping)
	float  interval;      // seconds between shots (or between bursts)
	int    burstCount;
	float  burstInterval;
	int    magSize;
	float  reloadTime;    // reload with rounds still in the magazine (seconds)
	float  reloadEmpty;   // reload from an empty magazine (longer for most guns, like Shell Shockers)
	float  range;
	float  falloffStart, falloffEnd, falloffMin; // damage multiplier falls to falloffMin
	float  edgeMin;       // damage multiplier for a hit on the very edge of the egg (1 at the centre; see weapon_center_mul)
	float  accuracy;      // Shell Shockers accuracy (1 = perfect); the resting hip cone is 1 - accuracy radians
	float  stability;     // Shell Shockers stability (1 = no bloom); each shot widens the cone by (1 - stability) * SHOT_BLOOM
	float  scopeSettle;   // extra cone (radians) right after scoping in, fading over SCOPE_SETTLE_TIME (no instant quickscopes)
	float  projSpeed;     // rockets
	float  splashRadius;  // rockets
	float  recoil;        // camera kick, radians
	float  zoom;          // aim-down-sights zoom factor
	bool   scoped;        // aiming looks through a scope (black frame, model hidden)
	bool   brackets;      // crosshair is four fixed brackets (shotgun) instead of four lines that spread
	int    reserveMax;    // spare rounds the player can carry besides the loaded magazine
	float  color[3];      // main body colour of the first-person model
} WeaponDef;

typedef struct {
	int   ammo;
	float cooldown;
	float reloadLeft;
	float reloadTotal;    // how long the current reload takes (for progress bars and animations)
	float shotBloom;      // extra cone (radians) from firing quickly; settles back on its own
	int   burstLeft;
	float burstTimer;
	int   reserve;        // spare rounds; -1 = unlimited (bots)
} WeaponState;

typedef struct { bool shot, reloadStarted, reloadDone; } WeaponEvents;

const WeaponDef* weapon_def(int id);
void weapon_state_init(WeaponState* ws, int id);
// Advances cooldown, reload and burst timers for one frame and reports what happened.
// 'held' is the trigger held, 'pressed' the trigger pressed this frame.
WeaponEvents weapon_update(WeaponState* ws, const WeaponDef* d, float dt, bool held, bool pressed, bool reloadRequested);
void  weapons_reset(WeaponState ws[WEAPON_COUNT]);
float weapon_damage_at(const WeaponDef* d, float dist);
// Like Shell Shockers: a bullet through the middle of an egg does full damage, one that grazes the
// edge much less. k = 0 at the centre .. 1 at the edge. Multiplier = 1 - (1 - edgeMin) * k^2.
float weapon_center_mul(const WeaponDef* d, float k);
// Cone half-angle (radians) for the player's next shot: the resting cone (hip or aimed), plus the
// movement penalty (bloom: 0 standing still .. 1 jumping), plus the firing bloom in ws->shotBloom.
float weapon_spread(const WeaponDef* d, const WeaponState* ws, bool aiming, float bloom);
// The cone for a shot weapon_update just reported (its own kick left out), and the bloom bots add.
float weapon_fire_spread(const WeaponDef* d, const WeaponState* ws, bool aiming, float bloom);
float weapon_shot_bloom(const WeaponDef* d, const WeaponState* ws);

#endif
