#include "camera.h"
#include <math.h>

void camera_view_matrix(C3D_Mtx* out, const Player* p)
{
	float e[3], f[3], roll;
	player_camera_pose(p, e, f, &roll);

	// Up vector tilted sideways by the roll wobble
	float rx = cosf(p->yaw), rz = sinf(p->yaw);
	float sr = sinf(roll), cr = cosf(roll);

	C3D_FVec eye = FVec3_New(e[0], e[1], e[2]);
	C3D_FVec at  = FVec3_New(e[0] + f[0], e[1] + f[1], e[2] + f[2]);
	C3D_FVec up  = FVec3_New(sr * rx, cr, sr * rz);
	Mtx_LookAt(out, eye, at, up, false);
}
