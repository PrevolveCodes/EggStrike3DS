#ifndef RENDERER_H
#define RENDERER_H

#include <stdbool.h>
#include "map.h"
#include "player.h"

bool renderer_init(void);                 // call after gfxInitDefault()
void renderer_load_map(const Map* m);     // builds the static vertex buffer and picks the sky

// Per-frame dynamic geometry: call dyn_begin, then add boxes, then renderer_draw.
// Boxes are given by centre and half-extents.
void renderer_dyn_begin(void);
void renderer_world_box(float cx, float cy, float cz, float hx, float hy, float hz,
                        float r, float g, float b);   // world space
// One triangle with per-vertex colours (used for smooth-shaded eggs).
// While a transform is set, world boxes and triangles are rotated/translated by it (held weapons).
void renderer_world_xform(float x, float y, float z, float pitch, float yaw, float roll);
void renderer_world_xform_clear(void);
void renderer_world_tri(const float p[3][3], const float c[3][3]);
// Team outline triangle: drawn before the world geometry without writing depth (see renderer_top_3d).
void renderer_outline_tri(const float p[3][3], const float c[3][3]);
void renderer_view_box(float cx, float cy, float cz, float hx, float hy, float hz,
                       float r, float g, float b);    // camera space (viewmodel/HUD)
// One triangle with per-vertex colours in camera space (round weapon parts).
void renderer_view_tri(const float p[3][3], const float c[3][3]);

// While a transform is set, view boxes are rotated/translated by it (weapon model parts
// are given in weapon space). Clear it before drawing HUD elements.
void renderer_view_xform(float x, float y, float z, float pitch, float yaw, float roll);
void renderer_view_xform_clear(void);

// Frame structure: frame_begin, then one top scene (3D or 2D), then bottom_2d, then frame_end.
void renderer_frame_begin(void);
void renderer_top_3d(const Player* p, float zoom);   // world + viewmodel on the top screen
void renderer_top_overlay(void);                       // 2D on top of the 3D scene (names), no clear
void renderer_top_2d(unsigned int clearColor);        // begin a citro2d scene on the top screen
void renderer_bottom_2d(unsigned int clearColor);     // begin a citro2d scene on the bottom screen
void renderer_frame_end(void);
void renderer_shutdown(void);

#endif
