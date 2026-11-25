#ifndef XFORM_H
#define XFORM_H

#include <stdbool.h>

// Rotation (roll, then pitch, then yaw) followed by a translation.
// Pitch > 0 tilts -Z forward up, yaw > 0 turns it toward -X (left).
typedef struct {
	float px, py, pz;
	float cp, sp, cy, sy, cr, sr;
} Xform;

void xform_set(Xform* x, float px, float py, float pz, float pitch, float yaw, float roll);
void xform_point(const Xform* x, const float in[3], float out[3]);

#endif
