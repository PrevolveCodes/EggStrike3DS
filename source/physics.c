#include "physics.h"
#include "collision.h"
#include "config.h"

// Move horizontally by (dx,dz); sliding along walls comes from calling this once per axis.
static void moveAxis(Body* b, const Map* map, float dx, float dz)
{
	float nx = b->x + dx, nz = b->z + dz;

	if (!collision_blocked(map, nx, b->y, nz)) {
		b->x = nx; b->z = nz;
		return;
	}

	// Blocked: try stepping up onto a low ledge (stairs)
	if (b->onGround && !collision_blocked(map, nx, b->y + STEP_HEIGHT, nz)) {
		float g = collision_ground_height(map, nx, nz, b->y + STEP_HEIGHT);
		if (g < b->y) g = b->y;
		b->x = nx; b->z = nz; b->y = g;
	}
}

void physics_step(Body* b, const Map* map, float wx, float wz, bool jump, float dt,
                  float* landImpact, bool* jumped)
{
	if (landImpact) *landImpact = 0.0f;
	if (jumped) *jumped = false;

	moveAxis(b, map, wx * dt, 0.0f);
	moveAxis(b, map, 0.0f, wz * dt);

	if (jump && b->onGround) {
		b->vy = JUMP_SPEED;
		b->onGround = false;
		if (jumped) *jumped = true;
	}

	b->vy -= GRAVITY * dt;
	float ny = b->y + b->vy * dt;
	b->onGround = false;

	if (!collision_blocked(map, b->x, ny, b->z)) {
		b->y = ny;
	} else if (b->vy <= 0.0f) {
		float g = collision_ground_height(map, b->x, b->z, b->y + 0.01f);
		if (g > -1.0e8f) b->y = g;
		if (landImpact) *landImpact = b->vy;
		b->vy = 0.0f;
		b->onGround = true;
	} else {
		float c = collision_ceiling_height(map, b->x, b->z, b->y + PLAYER_HEIGHT);
		if (c < 1.0e8f) b->y = c - PLAYER_HEIGHT;
		b->vy = 0.0f;
	}
}
