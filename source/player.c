#include "player.h"
#include <string.h>
#include "audio.h"
#include "collision.h"
#include "config.h"
#include "physics.h"
#include <math.h>

void player_init(Player* p)
{
	p->kills = 0;
	p->deaths = 0;
	memset(&p->perks, 0, sizeof(p->perks));
	p->loadout[0] = W_PECKMATIC;
	p->loadout[1] = W_EGGSTOL;
	p->slot = 0;
	p->cur = p->loadout[0];
}

void player_spawn(Player* p, const Spawn* s)
{
	p->x = s->x; p->y = s->y; p->z = s->z;
	p->yaw = s->yaw; p->pitch = 0.0f;
	p->vy = 0.0f;
	p->onGround = false;

	p->perks.shield = p->perks.hotT = p->perks.crackT = 0.0f;   // the streak count stays (pause-menu Respawn)
	p->alive = true;
	p->hp = PLAYER_MAX_HP;
	p->lastHitTimer = 0.0f;
	p->lastAttacker = -1;
	p->invuln = SPAWN_PROTECTION;
	p->deadTimer = 0.0f;
	p->despawnT = 0.0f;
	p->damageFlashT = 0.0f;
	p->aiming = false;
	p->zoom = 1.0f;
	p->kick = 0.0f;
	p->recoilPitch = 0.0f;
	p->bobPhase = 0.0f;
	p->lastStep = 0;
	p->bobBlend = 0.0f;
	p->swayX = 0.0f;
	p->swayY = 0.0f;
	p->landDip = 0.0f;
	p->switchAnim = 0.0f;
	p->meleeT = p->meleeCd = p->lungeT = 0.0f;
	p->meleeHitDone = p->meleeConnected = false;
	p->flashT = 0.0f;
	p->bloom = 0.0f;
	p->hitMarkerT = 0.0f;
	weapons_reset(p->ws);
	p->grenades = GRENADE_START;
	p->grenadeCd = 0.0f;
	p->slot = 0;
	p->cur = p->loadout[0];
}

void player_take_damage(Player* p, float amount)
{
	if (!p->alive || amount <= 0.0f || p->invuln > 0.0f) return;   // spawn protection
	p->hp -= amount;
	p->lastHitTimer = 0.0f;
	p->damageFlashT = 0.4f;
	audio_play(SFX_HURT, 0.0f, 1.0f);
}

void player_eye(const Player* p, float out[3])
{
	out[0] = p->x;
	out[1] = p->y + EYE_HEIGHT;
	out[2] = p->z;
}

void player_camera_pose(const Player* p, float eye[3], float fwd[3], float* roll)
{
	float damp = p->aiming ? 0.35f : 1.0f;
	float b = p->bobBlend * damp;
	float ph = p->bobPhase;

	float bobY = sinf(ph * 2.0f) * 0.045f * b;
	float bobX = cosf(ph) * 0.025f * b;
	float pitch = p->pitch + sinf(ph * 2.0f + 1.0f) * 0.008f * b;

	eye[0] = p->x + cosf(p->yaw) * bobX;
	eye[1] = p->y + EYE_HEIGHT + bobY - p->landDip;
	eye[2] = p->z + sinf(p->yaw) * bobX;

	fwd[0] = sinf(p->yaw) * cosf(pitch);
	fwd[1] = sinf(pitch);
	fwd[2] = -cosf(p->yaw) * cosf(pitch);

	*roll = sinf(ph) * 0.020f * b;
}

void player_forward(const Player* p, float out[3])
{
	out[0] = sinf(p->yaw) * cosf(p->pitch);
	out[1] = sinf(p->pitch);
	out[2] = -cosf(p->yaw) * cosf(p->pitch);
}

static float clampf(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

void player_update(Player* p, const PlayerInput* in, const Map* map, float dt)
{
	float startX = p->x, startZ = p->z;
	float landImpact = 0.0f;
	p->yaw += in->lookYaw;
	p->pitch += in->lookPitch;
	if (p->pitch > 1.4f)  p->pitch = 1.4f;
	if (p->pitch < -1.4f) p->pitch = -1.4f;

	float fx = sinf(p->yaw), fz = -cosf(p->yaw);
	float rx = cosf(p->yaw), rz = sinf(p->yaw);

	float wx = fx * in->moveY + rx * in->moveX;
	float wz = fz * in->moveY + rz * in->moveX;
	float len = sqrtf(wx * wx + wz * wz);
	if (len > 1.0f) { wx /= len; wz /= len; }

	float vx = wx * WALK_SPEED, vz = wz * WALK_SPEED;
	if (p->lungeT > 0.0f) {                              // a whisk whack pulls you a step toward your target
		vx += p->lungeDir[0] * MELEE_LUNGE_SPEED;
		vz += p->lungeDir[1] * MELEE_LUNGE_SPEED;
		p->lungeT -= dt;
	}

	Body body = { p->x, p->y, p->z, p->vy, p->onGround };
	bool jumped = false;
	physics_step(&body, map, vx, vz, in->jump, dt, &landImpact, &jumped);
	p->x = body.x; p->y = body.y; p->z = body.z; p->vy = body.vy; p->onGround = body.onGround;
	if (jumped) audio_play(SFX_JUMP, 0.0f, 1.0f);

	// ---- visual animation: walking bob, sprint blend, look sway, landing dip ----
	float dx = p->x - startX, dz = p->z - startZ;
	float dist = sqrtf(dx * dx + dz * dz);
	float actualSpeed = dist / dt;
	bool moving = p->onGround && actualSpeed > 0.5f;

	// Aim gets worse while moving and much worse in the air; it settles again soon after stopping
	float bloomTarget = !p->onGround ? 1.0f : 0.75f * clampf(actualSpeed / WALK_SPEED, 0.0f, 1.0f);
	float bk = dt * (bloomTarget > p->bloom ? 14.0f : 7.0f);
	if (bk > 1.0f) bk = 1.0f;
	p->bloom += (bloomTarget - p->bloom) * bk;
	float k = dt * 10.0f;
	if (k > 1.0f) k = 1.0f;

	float targetBob = moving ? clampf(actualSpeed / WALK_SPEED, 0.0f, 1.0f) : 0.0f;
	p->bobBlend += (targetBob - p->bobBlend) * k;
	if (p->onGround) p->bobPhase += dist * 2.3f;
	int stepIdx = (int)(p->bobPhase / 3.14159f);
	if (moving && stepIdx != p->lastStep) audio_play(SFX_FOOTSTEP, (stepIdx & 1) ? 0.15f : -0.15f, 0.7f);
	p->lastStep = stepIdx;

	float dk = dt * 9.0f;
	if (dk > 1.0f) dk = 1.0f;
	p->swayX -= p->swayX * dk;
	p->swayY -= p->swayY * dk;
	p->swayX = clampf(p->swayX - in->lookYaw * 0.5f, -0.07f, 0.07f);
	p->swayY = clampf(p->swayY - in->lookPitch * 0.5f, -0.05f, 0.05f);

	if (landImpact < -6.0f) audio_play(SFX_LAND, 0.0f, landImpact < -10.0f ? 1.0f : 0.7f);
	if (landImpact < -5.0f) p->landDip = clampf(-landImpact * 0.012f, 0.0f, 0.14f);
	float lk = dt * 7.0f;
	if (lk > 1.0f) lk = 1.0f;
	p->landDip -= p->landDip * lk;

	p->animTime += dt;
}

void player_panorama_pose(Player* cam, float angle, float cx, float cz, float radiusBase, float heightBase)
{
	float radius = radiusBase + 4.0f * sinf(angle * 0.5f);
	float height = heightBase + 3.0f * sinf(angle * 0.3f);
	cam->x = cx + radius * sinf(angle);
	cam->z = cz + radius * cosf(angle);
	cam->y = height - EYE_HEIGHT;
	cam->yaw = -angle;                        // look toward the centre
	cam->pitch = -atan2f(height - 2.0f, radius);
	cam->bobBlend = 0.0f;
	cam->bobPhase = 0.0f;
	cam->landDip = 0.0f;
	cam->aiming = false;
}
