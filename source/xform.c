#include "xform.h"
#include <math.h>

void xform_set(Xform* x, float px, float py, float pz, float pitch, float yaw, float roll)
{
	x->px = px; x->py = py; x->pz = pz;
	x->cp = cosf(pitch); x->sp = sinf(pitch);
	x->cy = cosf(yaw);   x->sy = sinf(yaw);
	x->cr = cosf(roll);  x->sr = sinf(roll);
}

void xform_point(const Xform* x, const float in[3], float out[3])
{
	float x1 = in[0] * x->cr - in[1] * x->sr;
	float y1 = in[0] * x->sr + in[1] * x->cr;
	float z1 = in[2];

	float y2 = y1 * x->cp - z1 * x->sp;
	float z2 = y1 * x->sp + z1 * x->cp;
	float x2 = x1;

	float x3 = x2 * x->cy + z2 * x->sy;
	float z3 = -x2 * x->sy + z2 * x->cy;

	out[0] = x3 + x->px;
	out[1] = y2 + x->py;
	out[2] = z3 + x->pz;
}
