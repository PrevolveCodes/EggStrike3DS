#include "weapons.h"
#include <math.h>

// Firing bloom, after Shell Shockers' stability: every shot widens the cone by (1 - stability) * SHOT_BLOOM,
// up to SHOT_BLOOM_MAX, and it settles back at SHOT_BLOOM_RECOVER * (0.5 + stability) radians a second.
#define SHOT_BLOOM         0.025f
#define SHOT_BLOOM_MAX     0.035f   // tuned down: a full spray was too wild on the small screen
#define SHOT_BLOOM_RECOVER 0.050f   // slower, so spamming a semi-auto (Free Ranger) costs accuracy like in Shell Shockers

static const WeaponDef defs[WEAPON_COUNT] = {
	[W_SPLATTERPOP] = {
		.name = "Splatterpop", .mode = FIRE_SEMI, .shot = SHOT_HITSCAN,
		.pellets = 20, .damage = 7.5f, .spread = 0.070f, .adsSpread = 0.055f,        // Scrambler: 20 x 7.5 = 150
		.moveSpread = 0.000f, .accuracy = 1.0f, .stability = 0.6f,                       // pattern size 0.14 (full width, so 0.07 either side)
		.interval = 60.0f / 225.0f, .magSize = 2, .reloadTime = 155.0f / 60.0f, .reloadEmpty = 155.0f / 60.0f,          // 225 RPM
		.range = 40.0f, .falloffStart = 4.0f, .falloffEnd = 26.0f, .falloffMin = 0.0f, .edgeMin = 0.0f,
		.recoil = 0.060f, .zoom = 1.15f, .brackets = true,
		.reserveMax = 24,
		.color = { 0.80f, 0.42f, 0.22f } },
	[W_YOLKBUSTER] = {
		.name = "Yolkbuster", .mode = FIRE_SEMI, .shot = SHOT_ROCKET,
		.pellets = 1, .damage = 100.0f, .hipSpread = 0.010f, .moveSpread = 0.070f, .accuracy = 0.99f, .stability = 0.2f,  // RPEGG: splash 100 - 3, direct hit 200
		.interval = 60.0f / 45.0f, .magSize = 1, .reloadTime = 170.0f / 60.0f, .reloadEmpty = 170.0f / 60.0f,           // 45 RPM
		.range = 100.0f, .falloffStart = 100.0f, .falloffEnd = 101.0f, .falloffMin = 1.0f, .edgeMin = 1.0f,
		.projSpeed = 30.0f, .splashRadius = 4.0f,
		.recoil = 0.050f, .zoom = 2.0f, .scoped = true,
		.reserveMax = 3,
		.color = { 0.82f, 0.28f, 0.22f } },
	[W_LONGBEAK] = {
		.name = "Longbeak", .mode = FIRE_SEMI, .shot = SHOT_HITSCAN,
		.pellets = 1, .damage = 110.0f, .spread = 0.050f, .adsSpread = 0.006f,      // Crackshot: 110
		.hipSpread = 0.100f, .moveSpread = 0.160f, .accuracy = 1.0f, .stability = 1.0f,
		.scopeSettle = 0.060f,   // tuned: a perfect scope was too strong, so it is a little loose and needs half a second to steady
		.interval = 1.3f, .magSize = 1, .reloadTime = 144.0f / 60.0f, .reloadEmpty = 144.0f / 60.0f,
		.range = 150.0f, .falloffStart = 150.0f, .falloffEnd = 151.0f, .falloffMin = 1.0f, .edgeMin = 0.0f,
		.recoil = 0.080f, .zoom = 3.0f, .scoped = true,
		.reserveMax = 12,
		.color = { 0.28f, 0.38f, 0.30f } },
	[W_PECKMATIC] = {
		.name = "Peckmatic", .mode = FIRE_AUTO, .shot = SHOT_HITSCAN,
		.pellets = 1, .damage = 30.0f, .spread = 0.022f, .adsSpread = 0.016f,        // EggK-47: 30 - 2
		.hipSpread = 0.040f, .moveSpread = 0.060f, .accuracy = 0.96f, .stability = 0.37f,
		.interval = 60.0f / 600.0f, .magSize = 30, .reloadTime = 160.0f / 60.0f, .reloadEmpty = 205.0f / 60.0f,         // 600 RPM
		.range = 80.0f, .falloffStart = 80.0f, .falloffEnd = 81.0f, .falloffMin = 1.0f, .edgeMin = 2.0f / 30.0f,
		.recoil = 0.012f, .zoom = 1.3f,
		.reserveMax = 240,
		.color = { 0.38f, 0.42f, 0.52f } },
	[W_FARMHAND] = {
		.name = "Farmhand", .mode = FIRE_SEMI, .shot = SHOT_HITSCAN,
		.pellets = 1, .damage = 101.0f, .spread = 0.012f, .adsSpread = 0.004f,       // Free Ranger: 101 - 1 (bots: 0.012, tuned from 0.006)
		.hipSpread = 0.010f, .moveSpread = 0.100f, .accuracy = 0.99f, .stability = 0.16f,
		.interval = 60.0f / 138.0f, .magSize = 15, .reloadTime = 165.0f / 60.0f, .reloadEmpty = 225.0f / 60.0f,         // 138 RPM
		.range = 120.0f, .falloffStart = 40.0f, .falloffEnd = 100.0f, .falloffMin = 0.7f, .edgeMin = 1.0f / 101.0f,   // balance: loses up to 30% past 40 m
		.recoil = 0.030f, .zoom = 2.2f, .scoped = true,
		.reserveMax = 60,
		.color = { 0.45f, 0.55f, 0.35f } },
	[W_TRIPLET] = {
		.name = "Triplet", .mode = FIRE_BURST, .shot = SHOT_HITSCAN,
		.pellets = 1, .damage = 35.0f, .spread = 0.012f, .adsSpread = 0.012f,        // Tri-Hard: 35
		.hipSpread = 0.030f, .moveSpread = 0.050f, .accuracy = 0.97f, .stability = 0.55f,
		.interval = 60.0f / 120.0f - 0.12f, .burstCount = 3, .burstInterval = 0.06f,   // 120 bursts a minute (the pause starts after the third shot)
		.magSize = 24, .reloadTime = 160.0f / 60.0f, .reloadEmpty = 205.0f / 60.0f,
		.range = 90.0f, .falloffStart = 90.0f, .falloffEnd = 91.0f, .falloffMin = 1.0f, .edgeMin = 2.0f / 35.0f,
		.recoil = 0.020f, .zoom = 1.4f,
		.reserveMax = 150,
		.color = { 0.55f, 0.30f, 0.58f } },
	[W_EGGSTOL] = {
		.name = "Eggstol", .mode = FIRE_SEMI, .shot = SHOT_HITSCAN,
		.pellets = 1, .damage = 23.0f, .spread = 0.016f, .adsSpread = 0.018f,        // Cluck 9mm: 23
		.hipSpread = 0.045f, .moveSpread = 0.030f, .accuracy = 0.94f, .stability = 0.38f,   // tuned: 0.06 felt too loose
		.interval = 60.0f / 450.0f, .magSize = 15, .reloadTime = 160.0f / 60.0f, .reloadEmpty = 195.0f / 60.0f,         // 450 RPM
		.range = 60.0f, .falloffStart = 60.0f, .falloffEnd = 61.0f, .falloffMin = 1.0f, .edgeMin = 2.0f / 23.0f,
		.recoil = 0.018f, .zoom = 1.25f,
		.reserveMax = 60,
		.color = { 0.95f, 0.80f, 0.30f } },
};

const WeaponDef* weapon_def(int id)
{
	if (id < 0 || id >= WEAPON_COUNT) id = 0;
	return &defs[id];
}

void weapons_reset(WeaponState ws[WEAPON_COUNT])
{
	for (int i = 0; i < WEAPON_COUNT; i++) {
		ws[i].ammo = defs[i].magSize;
		ws[i].cooldown = 0.0f;
		ws[i].reloadLeft = 0.0f;
		ws[i].burstLeft = 0;
		ws[i].burstTimer = 0.0f;
		ws[i].shotBloom = 0.0f;
		ws[i].reserve = defs[i].reserveMax;
	}
}

float weapon_damage_at(const WeaponDef* d, float dist)
{
	if (dist <= d->falloffStart) return d->damage;
	if (dist >= d->falloffEnd) return d->damage * d->falloffMin;
	float t = (dist - d->falloffStart) / (d->falloffEnd - d->falloffStart);
	return d->damage * (1.0f + (d->falloffMin - 1.0f) * t);
}

float weapon_center_mul(const WeaponDef* d, float k)
{
	if (k < 0.0f) k = 0.0f;
	if (k > 1.0f) k = 1.0f;
	return 1.0f - (1.0f - d->edgeMin) * k * k;
}

void weapon_state_init(WeaponState* ws, int id)
{
	ws->ammo = weapon_def(id)->magSize;
	ws->cooldown = 0.0f;
	ws->reloadLeft = 0.0f;
	ws->burstLeft = 0;
	ws->burstTimer = 0.0f;
	ws->shotBloom = 0.0f;
	ws->reserve = -1;     // bots never run dry
}

WeaponEvents weapon_update(WeaponState* ws, const WeaponDef* d, float dt, bool held, bool pressed, bool reloadRequested)
{
	WeaponEvents ev = { false, false, false };

	ws->cooldown -= dt;
	ws->shotBloom -= SHOT_BLOOM_RECOVER * (0.5f + d->stability) * dt;
	if (ws->shotBloom < 0.0f) ws->shotBloom = 0.0f;
	if (ws->cooldown < 0.0f) ws->cooldown = 0.0f;

	if (ws->reloadLeft > 0.0f) {
		ws->reloadLeft -= dt;
		if (ws->reloadLeft <= 0.0f) {
			ws->reloadLeft = 0.0f;
			if (ws->reserve < 0) {
				ws->ammo = d->magSize;
			} else {
				int take = d->magSize - ws->ammo;
				if (take > ws->reserve) take = ws->reserve;
				ws->ammo += take;
				ws->reserve -= take;
			}
			ev.reloadDone = true;
		}
	} else if (((reloadRequested && ws->ammo < d->magSize) || ws->ammo == 0) && ws->reserve != 0) {
		ws->reloadTotal = (ws->ammo == 0 && d->reloadEmpty > 0.0f) ? d->reloadEmpty : d->reloadTime;
		ws->reloadLeft = ws->reloadTotal;
		ws->burstLeft = 0;
		ev.reloadStarted = true;
	}

	bool canFire = ws->reloadLeft <= 0.0f && ws->ammo > 0;
	if (ws->burstLeft > 0) {
		if (!canFire) {
			ws->burstLeft = 0;
		} else {
			ws->burstTimer -= dt;
			if (ws->burstTimer <= 0.0f) {
				ev.shot = true;
				ws->shotBloom = fminf(ws->shotBloom + (1.0f - d->stability) * SHOT_BLOOM, SHOT_BLOOM_MAX);
				ws->ammo--;
				ws->burstLeft--;
				ws->burstTimer = d->burstInterval;
				if (ws->burstLeft == 0) ws->cooldown = d->interval;
			}
		}
	} else if (ws->cooldown <= 0.0f && canFire) {
		bool trigger = (d->mode == FIRE_AUTO) ? held : pressed;
		if (trigger) {
			ev.shot = true;
			ws->shotBloom = fminf(ws->shotBloom + (1.0f - d->stability) * SHOT_BLOOM, SHOT_BLOOM_MAX);
			ws->ammo--;
			if (d->mode == FIRE_BURST && d->burstCount > 1) {
				ws->burstLeft = d->burstCount - 1;
				ws->burstTimer = d->burstInterval;
			} else {
				ws->cooldown = d->interval;
			}
		}
	}
	return ev;
}



float weapon_spread(const WeaponDef* d, const WeaponState* ws, bool aiming, float bloom)
{
	if (bloom < 0.0f) bloom = 0.0f;
	if (bloom > 1.0f) bloom = 1.0f;
	float hip = d->hipSpread > 0.0f ? d->hipSpread : d->spread;
	float base = aiming ? d->adsSpread : hip;
	float fire = ws ? ws->shotBloom * (aiming ? 0.6f : 1.0f) : 0.0f;   // aiming steadies it a little
	// Aiming softens the movement penalty a little but never removes it
	return base + d->moveSpread * bloom * (aiming ? 0.75f : 1.0f) + fire;
}

float weapon_fire_spread(const WeaponDef* d, const WeaponState* ws, bool aiming, float bloom)
{
	// weapon_update has already added this shot's own kick; the shot itself only feels the earlier ones
	float kick = fminf(ws->shotBloom, (1.0f - d->stability) * SHOT_BLOOM) * (aiming ? 0.6f : 1.0f);
	return weapon_spread(d, ws, aiming, bloom) - kick;
}

float weapon_shot_bloom(const WeaponDef* d, const WeaponState* ws)
{
	return fmaxf(0.0f, ws->shotBloom - (1.0f - d->stability) * SHOT_BLOOM);
}
