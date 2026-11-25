#ifndef CONFIG_H
#define CONFIG_H

// Player body (axis-aligned box, feet-centred)
#define PLAYER_RADIUS   0.5f
#define PLAYER_HEIGHT   1.7f
#define EYE_HEIGHT      1.55f

// Movement tuning (units ~ metres)
#define GRAVITY         30.0f
#define JUMP_SPEED      9.5f   // apex ~1.5 m
#define WALK_SPEED      6.0f
#define STEP_HEIGHT     0.55f  // auto step-up onto stairs
#define MAX_DT          0.05f

// Look tuning
#define LOOK_STICK_RATE 2.5f   // radians/second at full C-Stick deflection
#define LOOK_TOUCH_SENS 0.008f // radians per pixel dragged
#define BASE_FOV        70.0f

// Map limits
#define MAX_BOXES       480
#define MAX_ROUNDS      110    // round things (columns, silos, domes, tree canopies); each one also adds a few collision boxes
#define MAX_SPAWNS      24

// Combat
#define PLAYER_MAX_HP   100.0f
#define REGEN_DELAY     5.0f   // seconds after last hit before health regenerates
#define REGEN_RATE      12.0f  // hp per second
#define RESPAWN_DELAY   2.5f
#define DESPAWN_TIME    2.0f   // pause-menu Respawn: seconds your egg stays in the arena before it leaves
#define SPAWN_PROTECTION 2.5f  // seconds of invincibility after spawning (ends early if you fire)
#define AUTO_RESPAWN    15.0f  // seconds after death before spawning automatically

// Map flyover before a match
#define PANO_RADIUS     38.0f
#define PANO_HEIGHT     17.0f
#define PANO_SPEED      0.25f  // radians per second
#define SELF_DAMAGE_SCALE 0.6f
#define TARGET_MAX_HP   100.0f
#define EGG_HALF_W      0.68f   // how wide the drawn egg is (about 0.8 of its height, like Shell Shockers)
#define TARGET_HALF_W   0.62f   // bullets hit this box, a little slimmer than the drawn egg
#define TARGET_HEIGHT   1.7f
#define TARGET_RESPAWN  3.0f
#define MAX_PROJECTILES 8
#define MAX_GRENADES    12
#define GRENADE_START   2        // grenades you spawn with
#define GRENADE_MAX     3
#define GRENADE_FUSE    2.2f
#define GRENADE_COOLDOWN 0.9f    // between your throws
#define GRENADE_SPEED   15.0f
#define GRENADE_DAMAGE  150.0f   // Shell Shockers grenade: 150 at the centre
#define ROCKET_DIRECT_DAMAGE 200.0f // Shell Shockers RPEGG: a direct hit is an instant kill
#define GRENADE_RADIUS  3.0f     // was 4.2: with 150 damage it cleared whole rooms
#define MAX_PARTICLES   40
#define MAX_TRACERS     64     // little white bullets in flight (a shotgun blast uses 8)
#define SCOPE_SETTLE_TIME 0.5f   // seconds for a scope to steady (see WeaponDef.scopeSettle)

// Melee: the whisk whack (D-Pad Down, or ZL / ZR on a New 3DS)
#define MELEE_TIME      0.45f  // the whole swing animation
#define MELEE_HIT_AT    0.12f  // seconds into the swing when it connects
#define MELEE_COOLDOWN  0.75f  // from the start of one swing to the next
#define MELEE_RANGE     2.3f   // metres from your eye to the edge of the target box
#define MELEE_CONE_COS  0.64f  // about 50 degrees either side of where you look
#define MELEE_DAMAGE    55.0f  // two whacks crack an egg
#define MELEE_BACK_DAMAGE 100.0f // from behind: one whack
#define MELEE_BACK_COS  0.5f   // within 60 degrees of facing the same way as the victim counts as behind
#define MELEE_LUNGE     3.2f   // a target this close in the cone pulls you in a little
#define MELEE_LUNGE_SPEED 9.0f

#endif
