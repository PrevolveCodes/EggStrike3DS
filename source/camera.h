#ifndef CAMERA_H
#define CAMERA_H

#include <citro3d.h>
#include "player.h"

void camera_view_matrix(C3D_Mtx* out, const Player* p);

#endif
