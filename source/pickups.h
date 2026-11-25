#ifndef PICKUPS_H
#define PICKUPS_H

#include <stdbool.h>
#include "bots.h"
#include "player.h"

// Ammo crates. The player carries a limited number of spare rounds per gun (WeaponDef.reserveMax);
// bots have unlimited ammo. Walking over a crate refills the spare rounds of both your guns and your grenades, then the crate
// is gone for AMMO_RESPAWN seconds. Crates sit on reachable spots spread away from the spawns.
#define AMMO_BOX_MAX     8
#define AMMO_BOX_COUNT   6
#define AMMO_RESPAWN     20.0f
#define AMMO_TOUCH       1.4f

typedef struct {
	float pos[3];       // on the floor
	float timer;        // > 0: waiting to come back
} AmmoBox;

struct Combat;

void ammo_init(struct Combat* c);
void ammo_update(struct Combat* c, Player* p, float dt);
void ammo_draw(const struct Combat* c);
int  ammo_client_pickup(struct Combat* c, Player* p);                 // client: take a crate if standing on one (index or -1)
void ammo_remote_pickup(struct Combat* c, const Bot* b, int idx);   // host: a remote player took crate idx

#endif
