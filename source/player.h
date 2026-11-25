#ifndef PLAYER_H
#define PLAYER_H

#include <stdbool.h>
#include "input.h"
#include "map.h"
#include "weapons.h"
#include "perks.h"

typedef struct {
	float x, y, z;       // feet position
	float vy;
	float yaw, pitch;
	bool  onGround;

	// combat
	bool  alive;
	float hp;
	float lastHitTimer;
	float invuln;         // spawn protection time left
	int   lastAttacker;   // fighter id of whoever hurt the player last (-1 none)
	float deadTimer;
	float despawnT;       // > 0: the pause-menu Respawn is counting down (the egg is still in play)
	float damageFlashT;
	int   kills, deaths;
	Perks perks;         // kill streak and its perks
	int   grenades;      // carried (a crate refills them)
	float grenadeCd;     // time until the next throw
	WeaponState ws[WEAPON_COUNT];
	int   cur;           // current weapon id
	int   loadout[2];    // primary and secondary weapon ids
	int   slot;          // which loadout slot is held
	bool  aiming;
	float aimHeld;       // seconds aiming has been held (scopes steady over SCOPE_SETTLE_TIME)
	float zoom;          // current zoom factor (1 = none)
	float kick;          // visual recoil
	float recoilPitch;   // camera kick not yet recovered (recovers automatically)
	float flashT;        // muzzle flash time left
	float bloom;         // 0 standing still .. 1 jumping: movement inaccuracy (smoothed)

	// visual animation state (does not affect gameplay)
	float bobPhase;      // advances with distance walked
	int   lastStep;      // last footstep sound index
	float bobBlend;      // 0..1 how much walking bob is active
	float swayX, swayY;  // weapon lag when turning
	float landDip;       // camera dip after landing
	float animTime;
	float switchAnim;    // weapon raise animation time left
	float meleeT;        // whisk swing time left (0 = not swinging)
	float meleeCd;       // time until the next swing
	bool  meleeHitDone;  // this swing has already been tested for a hit
	bool  meleeConnected;// this swing hit someone (for the animation and sound)
	float lungeT;        // a short pull toward the egg being whacked
	float lungeDir[2];   // x, z

	// cosmetics chosen in the menu
	int   shell, skin;
	float hitMarkerT;    // crosshair hit marker time left
} Player;

void player_init(Player* p);                    // zero stats, pick default weapon
void player_spawn(Player* p, const Spawn* s);   // full health and ammo
void player_update(Player* p, const PlayerInput* in, const Map* map, float dt);
void player_take_damage(Player* p, float amount);
void player_eye(const Player* p, float out[3]);
void player_forward(const Player* p, float out[3]); // unit vector
// Camera pose including walking bob, landing dip and roll wobble (visual only).
// Orbiting camera used for the map flyover; fills the pose fields of 'cam'.
void player_panorama_pose(Player* cam, float angle, float cx, float cz, float radiusBase, float height);
void player_camera_pose(const Player* p, float eye[3], float fwd[3], float* roll);

#endif
