#ifndef PHYSICS_H
#define PHYSICS_H

#include <stdbool.h>
#include "map.h"

typedef struct {
	float x, y, z;     // feet position
	float vy;
	bool  onGround;
} Body;

// Moves a body for one frame: horizontal velocity (wx, wz) in m/s, optional jump,
// gravity, stair step-up and collision. landImpact receives the downward speed on the
// frame the body lands (0 otherwise); jumped is set when a jump started.
void physics_step(Body* b, const Map* map, float wx, float wz, bool jump, float dt,
                  float* landImpact, bool* jumped);

#endif
