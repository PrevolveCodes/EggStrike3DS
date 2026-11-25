#ifndef VIEWMODEL_H
#define VIEWMODEL_H

#include <stdbool.h>
#include "player.h"

void viewmodel_draw(const Player* p);

// Where the muzzle is: camera space for the first-person gun (x right, y up, z forward, metres) when
// 'aim' is 0 (hip) .. 1 (aimed), and how far a bot's gun muzzle sits in front of its hand.
void  viewmodel_muzzle_view(int weapon, float aim, float out[3]);
float viewmodel_muzzle_ahead(int weapon);

// A weapon model drawn in the world (held by a bot). Position is the gun's centre.
void viewmodel_draw_world(int weapon, float x, float y, float z, float yaw, float pitch, bool flash);
// A whisk swung by another egg; x, y, z is the shoulder, t = 0..1 through the swing.
void viewmodel_draw_world_whisk(float x, float y, float z, float yaw, float t);

// Side-view icon of a weapon (muzzle pointing left), fitted into the box x,y,w,h.
// Calls fn once per quad (screen corners in order around the outline) with its colour;
// draw it as two triangles.
typedef void (*IconQuadFn)(const float q[4][2], const float rgb[3], void* user);
void viewmodel_icon(int weapon, float x, float y, float w, float h, IconQuadFn fn, void* user);

#endif
