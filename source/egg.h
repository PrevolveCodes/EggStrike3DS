#ifndef EGG_H
#define EGG_H

// Smooth, glossy egg for bots and target dummies: no face, just a shell.
// (x, y, z) is the point on the ground under the egg.
void egg_draw(float x, float y, float z, float r, float g, float b);

// A slightly bigger copy of the egg in one flat colour, drawn behind the real egg as an outline.
// thick = how far the outline sticks out, in metres.
void egg_outline(float x, float y, float z, float thick, float r, float g, float b);

// Mesh access (used by tests)
int  egg_triangle_count(void);
void egg_vertex(int tri, int corner, float pos[3], float* shade, float* spec);

#endif
