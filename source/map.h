#ifndef MAP_H
#define MAP_H

#include <stdbool.h>
#include "config.h"

typedef struct {
	float min[3];
	float max[3];
	float color[3];
	bool  hidden;      // collision only: a Round is drawn in its place (a few boxes make an octagon-ish column)
} Box;

// Round things. They are drawn as real curved shapes with smooth lighting, and collide as a ring of
// boxes (octagon-like) so bots, bullets and players treat them like solid columns.
enum { ROUND_CYL, ROUND_DOME, ROUND_BALL };
typedef struct {
	int   kind;
	float x, z, y0, y1;
	float r0, r1;       // radius at the bottom and the top (cylinder / cone); domes and balls use r0
	float color[3];
	float bandH;        // > 0: alternating lighter / darker bands this tall (hooped tanks, striped silos)
	float flute;        // vertical stripes on columns, or leaf-to-leaf colour variation on balls (0 = smooth)
	int   sides;
	bool  cap;          // closed top (cylinders)
} Round;

typedef struct {
	float x, y, z, yaw;
	int   team;     // 0 red, 1 blue, 2 anyone
} Spawn;

typedef struct {
	const char* name;
	Box   boxes[MAX_BOXES];
	int   boxCount;
	Spawn spawns[MAX_SPAWNS];
	int   spawnCount;
	float killY; // falling below this kills the player
	float panoRadius, panoHeight;   // orbit used for the pre-match flyover
	int   sky;                      // sky preset (sky.h)
	bool  blocky;                   // drawn as 1 m blocks (Shell Shockers style): every face shows the block grid
	Round rounds[MAX_ROUNDS];
	int   roundCount;
} Map;


#define MAP_COUNT 7
const char* map_name(int id);
const char* map_help(int id);
bool map_implemented(int id);
bool map_load(Map* m, int id);   // false if the map is not built yet

#endif
