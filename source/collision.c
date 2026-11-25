#include "collision.h"
#include <math.h>

#define NO_GROUND (-1.0e9f)
#define NO_CEIL   ( 1.0e9f)

static bool footprintOverlaps(const Box* b, float x, float z)
{
	return x + PLAYER_RADIUS > b->min[0] && x - PLAYER_RADIUS < b->max[0] &&
	       z + PLAYER_RADIUS > b->min[2] && z - PLAYER_RADIUS < b->max[2];
}

bool collision_blocked(const Map* m, float x, float y, float z)
{
	for (int i = 0; i < m->boxCount; i++) {
		const Box* b = &m->boxes[i];
		if (footprintOverlaps(b, x, z) &&
		    y + PLAYER_HEIGHT > b->min[1] && y < b->max[1])
			return true;
	}
	return false;
}

float collision_ground_height(const Map* m, float x, float z, float y)
{
	float best = NO_GROUND;
	for (int i = 0; i < m->boxCount; i++) {
		const Box* b = &m->boxes[i];
		if (footprintOverlaps(b, x, z) && b->max[1] <= y + 0.001f && b->max[1] > best)
			best = b->max[1];
	}
	return best;
}

float collision_ceiling_height(const Map* m, float x, float z, float headY)
{
	float best = NO_CEIL;
	for (int i = 0; i < m->boxCount; i++) {
		const Box* b = &m->boxes[i];
		if (footprintOverlaps(b, x, z) && b->min[1] >= headY - 0.001f && b->min[1] < best)
			best = b->min[1];
	}
	return best;
}

bool collision_ray_aabb(const float bmin[3], const float bmax[3], const float o[3],
                        const float d[3], float maxDist, float* outDist)
{
	float tmin = 0.0f, tmax = maxDist;

	for (int a = 0; a < 3; a++) {
		if (fabsf(d[a]) < 1.0e-6f) {
			if (o[a] < bmin[a] || o[a] > bmax[a]) return false;
		} else {
			float inv = 1.0f / d[a];
			float t1 = (bmin[a] - o[a]) * inv;
			float t2 = (bmax[a] - o[a]) * inv;
			if (t1 > t2) { float t = t1; t1 = t2; t2 = t; }
			if (t1 > tmin) tmin = t1;
			if (t2 < tmax) tmax = t2;
			if (tmin > tmax) return false;
		}
	}
	if (outDist) *outDist = tmin;
	return true;
}

bool collision_raycast(const Map* m, const float o[3], const float d[3],
                       float maxDist, float* outDist)
{
	float best = maxDist;
	bool  hit  = false;

	for (int i = 0; i < m->boxCount; i++) {
		float t;
		if (collision_ray_aabb(m->boxes[i].min, m->boxes[i].max, o, d, best, &t)) {
			best = t;
			hit = true;
		}
	}

	if (hit && outDist) *outDist = best;
	return hit;
}
