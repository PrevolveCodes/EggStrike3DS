#ifndef FX_H
#define FX_H

void fx_init(void);
void fx_update(float dt);
void fx_draw(void);
// Burst of small cubes flying outward.
void fx_burst(const float pos[3], int count, float speed, float size, float life,
              float r, float g, float b);
// Single stationary cube that shrinks away (muzzle/explosion flash).
void fx_flash(const float pos[3], float size, float life, float r, float g, float b);

// A little white bullet that flies from 'from' to 'to' so every shot can be seen leaving the gun.
void fx_tracer(const float from[3], const float to[3]);
int  fx_tracer_count(void);   // bullets currently in flight (for tests)

#endif
