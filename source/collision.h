#ifndef COLLISION_H
#define COLLISION_H

#include <stdbool.h>
#include "map.h"

// True if a player-sized box with feet at (x,y,z) overlaps any map box.
bool  collision_blocked(const Map* m, float x, float y, float z);

// Highest box top at or below 'y' under the player footprint (very negative if none).
float collision_ground_height(const Map* m, float x, float z, float y);

// Lowest box bottom at or above 'headY' over the player footprint (very positive if none).
float collision_ceiling_height(const Map* m, float x, float z, float headY);

// Ray vs one axis-aligned box. Direction must be normalised.
bool  collision_ray_aabb(const float bmin[3], const float bmax[3], const float origin[3],
                         const float dir[3], float maxDist, float* outDist);

// Ray vs all boxes. Direction must be normalised. Returns true on hit and writes distance.
// Bots will use this for line-of-sight and bullet traces.
bool  collision_raycast(const Map* m, const float origin[3], const float dir[3],
                        float maxDist, float* outDist);

#endif
