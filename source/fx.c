#include "fx.h"
#include "config.h"
#include "renderer.h"
#include "rng.h"
#include <stdbool.h>
#include <math.h>
#include <stddef.h>

typedef struct {
	bool active;
	float p[3], v[3];
	float life, maxLife, size, grav;
	float r, g, b;
} Particle;

typedef struct {
	bool  active;
	float from[3], dir[3];
	float dist;     // length of the flight
	float t;        // distance travelled by the head so far
} Tracer;

#define TRACER_SPEED 70.0f   // metres per second
#define TRACER_LEN   0.95f
#define TRACER_MAXD  80.0f   // far shots fade out here instead of flying forever

static Tracer tracers[MAX_TRACERS];
static int nextTracer;
static Particle parts[MAX_PARTICLES];
static int nextSlot;

void fx_init(void)
{
	for (int i = 0; i < MAX_PARTICLES; i++) parts[i].active = false;
	nextSlot = 0;
	for (int i = 0; i < MAX_TRACERS; i++) tracers[i].active = false;
	nextTracer = 0;
}

static Particle* alloc(void)
{
	for (int i = 0; i < MAX_PARTICLES; i++)
		if (!parts[i].active) return &parts[i];
	Particle* p = &parts[nextSlot];       // pool full: recycle round-robin
	nextSlot = (nextSlot + 1) % MAX_PARTICLES;
	return p;
}

void fx_burst(const float pos[3], int count, float speed, float size, float life,
              float r, float g, float b)
{
	for (int i = 0; i < count; i++) {
		Particle* q = alloc();
		q->active = true;
		for (int a = 0; a < 3; a++) q->p[a] = pos[a];
		q->v[0] = (rng_float() - 0.5f) * 2.0f * speed;
		q->v[1] = rng_float() * speed;
		q->v[2] = (rng_float() - 0.5f) * 2.0f * speed;
		q->life = q->maxLife = life * (0.6f + 0.4f * rng_float());
		q->size = size;
		q->grav = 9.0f;
		q->r = r; q->g = g; q->b = b;
	}
}

void fx_flash(const float pos[3], float size, float life, float r, float g, float b)
{
	Particle* q = alloc();
	q->active = true;
	for (int a = 0; a < 3; a++) { q->p[a] = pos[a]; q->v[a] = 0.0f; }
	q->life = q->maxLife = life;
	q->size = size;
	q->grav = 0.0f;
	q->r = r; q->g = g; q->b = b;
}

void fx_tracer(const float from[3], const float to[3])
{
	float d[3] = { to[0] - from[0], to[1] - from[1], to[2] - from[2] };
	float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	if (len < 0.6f) return;                       // hit right at the muzzle: nothing to see
	Tracer* q = NULL;
	for (int i = 0; i < MAX_TRACERS && !q; i++) if (!tracers[i].active) q = &tracers[i];
	if (!q) { q = &tracers[nextTracer]; nextTracer = (nextTracer + 1) % MAX_TRACERS; }
	q->active = true;
	for (int a = 0; a < 3; a++) { q->from[a] = from[a]; q->dir[a] = d[a] / len; }
	q->dist = len > TRACER_MAXD ? TRACER_MAXD : len;
	q->t = 0.0f;
}

int fx_tracer_count(void)
{
	int n = 0;
	for (int i = 0; i < MAX_TRACERS; i++) n += tracers[i].active ? 1 : 0;
	return n;
}

void fx_update(float dt)
{
	for (int i = 0; i < MAX_TRACERS; i++) {
		Tracer* q = &tracers[i];
		if (!q->active) continue;
		q->t += TRACER_SPEED * dt;
		if (q->t - TRACER_LEN >= q->dist) q->active = false;   // the tail has arrived too
	}
	for (int i = 0; i < MAX_PARTICLES; i++) {
		Particle* q = &parts[i];
		if (!q->active) continue;
		q->life -= dt;
		if (q->life <= 0.0f) { q->active = false; continue; }
		q->v[1] -= q->grav * dt;
		for (int a = 0; a < 3; a++) q->p[a] += q->v[a] * dt;
	}
}

// A tracer is two crossed, pointed slivers (like two small white diamonds), so it reads from any angle.
static void drawTracer(const Tracer* q)
{
	float head = q->t > q->dist ? q->dist : q->t;
	float tail = q->t - TRACER_LEN;
	if (tail < 0.0f) tail = 0.0f;
	if (head - tail < 0.05f) return;
	float H[3], T[3], M[3];
	const float midK = 0.6f;                     // widest point sits toward the front
	for (int a = 0; a < 3; a++) {
		H[a] = q->from[a] + q->dir[a] * head;
		T[a] = q->from[a] + q->dir[a] * tail;
		M[a] = T[a] + (H[a] - T[a]) * midK;
	}
	// two perpendicular axes to the flight direction
	float ref[3] = { 0.0f, 1.0f, 0.0f };
	if (fabsf(q->dir[1]) > 0.95f) { ref[0] = 1.0f; ref[1] = 0.0f; }
	float u[3] = { q->dir[1] * ref[2] - q->dir[2] * ref[1], q->dir[2] * ref[0] - q->dir[0] * ref[2], q->dir[0] * ref[1] - q->dir[1] * ref[0] };
	float ul = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
	for (int a = 0; a < 3; a++) u[a] /= ul;
	float v[3] = { q->dir[1] * u[2] - q->dir[2] * u[1], q->dir[2] * u[0] - q->dir[0] * u[2], q->dir[0] * u[1] - q->dir[1] * u[0] };
	const float w = 0.055f;
	const float white[3] = { 1.0f, 1.0f, 1.0f }, dim[3] = { 0.78f, 0.86f, 1.0f };
	for (int k = 0; k < 2; k++) {
		const float* s = k == 0 ? u : v;
		float L[3] = { M[0] + s[0] * w, M[1] + s[1] * w, M[2] + s[2] * w };
		float R[3] = { M[0] - s[0] * w, M[1] - s[1] * w, M[2] - s[2] * w };
		float p1[3][3], c1[3][3], p2[3][3], c2[3][3];
		for (int a = 0; a < 3; a++) { p1[0][a] = H[a]; p1[1][a] = L[a]; p1[2][a] = R[a]; p2[0][a] = T[a]; p2[1][a] = L[a]; p2[2][a] = R[a]; }
		for (int a = 0; a < 3; a++) { c1[0][a] = white[a]; c1[1][a] = white[a]; c1[2][a] = white[a]; c2[0][a] = dim[a]; c2[1][a] = white[a]; c2[2][a] = white[a]; }
		renderer_world_tri(p1, c1);
		renderer_world_tri(p2, c2);
	}
}

void fx_draw(void)
{
	for (int i = 0; i < MAX_TRACERS; i++)
		if (tracers[i].active) drawTracer(&tracers[i]);
	for (int i = 0; i < MAX_PARTICLES; i++) {
		const Particle* q = &parts[i];
		if (!q->active) continue;
		float h = 0.5f * q->size * (q->life / q->maxLife);
		renderer_world_box(q->p[0], q->p[1], q->p[2], h, h, h, q->r, q->g, q->b);
	}
}
