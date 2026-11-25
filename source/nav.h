#ifndef NAV_H
#define NAV_H

#include <stdbool.h>
#include "map.h"

// Navigation graph built automatically from the map's boxes: a grid of standable points
// (one per walkable surface in each cell), linked when a body can walk, step or jump
// between them, or drop down.
#define NAV_CELL       2.0f
#define NAV_GRID_MAX   72
#define NAV_MAX_NODES  9000
#define NAV_MAX_LINKS  8
#define NAV_MAX_PATH   160

typedef struct {
	float x, y, z;
	short nbr[NAV_MAX_LINKS];
	unsigned char nbrCount;
	unsigned char jumpMask;    // bit i set: link i needs a jump
	bool  reachable;           // reachable from a spawn point
} NavNode;

typedef struct {
	NavNode nodes[NAV_MAX_NODES];
	int   nodeCount;
	float minX, minZ;
	int   gridW, gridH;
	short cellStart[NAV_GRID_MAX * NAV_GRID_MAX];
	unsigned char cellCount[NAV_GRID_MAX * NAV_GRID_MAX];
} Nav;

void nav_build(Nav* nav, const Map* map);
// Nearest reachable node to a point (prefers the same floor level); -1 if none.
int  nav_nearest(const Nav* nav, float x, float y, float z);
// A* between two nodes. Writes node indices (start first) into out; returns the length or 0.
int  nav_path(const Nav* nav, int start, int goal, short* out, int maxLen);
// Random reachable node, or -1.
int  nav_random_node(const Nav* nav);

#endif
