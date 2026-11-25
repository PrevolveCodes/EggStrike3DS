#ifndef SURFACE_H
#define SURFACE_H

#include "map.h"

// Surface materials. The map has no real textures: every box face is cut into small flat or
// smoothly shaded patches (bricks, planks, tiles, grass patches...) so large areas are no
// longer one solid colour. Everything here is plain C, no 3DS calls.
enum {
	MAT_PLAIN, MAT_LAWN, MAT_PLASTER, MAT_PANEL, MAT_STONE, MAT_WOOD, MAT_CRATE, MAT_ROOF,
	MAT_HEDGE, MAT_BARK, MAT_WATER, MAT_SAND, MAT_ROCK, MAT_MARBLE, MAT_PILLAR, MAT_GOLD,
	MAT_METAL, MAT_FLOOR, MAT_COUNT
};

// Same memory layout as the renderer's Vertex (position, colour).
typedef struct { float x, y, z, r, g, b; } SurfVertex;

int surface_material_for(float r, float g, float b);

// Builds every visible face of the map as triangles. Hidden faces (under the ground, glued to
// another box, outside the arena) are skipped or drawn flat. Detail is reduced automatically
// until the result fits in `cap` vertices. Returns the vertex count.
int surface_build_map(const Map* m, SurfVertex* out, int cap, float* detailUsed);

#endif
