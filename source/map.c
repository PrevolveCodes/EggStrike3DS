#include "map.h"
#include <math.h>
#include <string.h>

static void addBox(Map* m, float x0, float y0, float z0, float x1, float y1, float z1,
                   float r, float g, float b)
{
	if (m->boxCount >= MAX_BOXES) return;
	Box* bx = &m->boxes[m->boxCount++];
	bx->min[0] = x0; bx->min[1] = y0; bx->min[2] = z0;
	bx->max[0] = x1; bx->max[1] = y1; bx->max[2] = z1;
	bx->color[0] = r; bx->color[1] = g; bx->color[2] = b;
	bx->hidden = false;
}

static void addSpawn(Map* m, float x, float y, float z, float yaw, int team)
{
	if (m->spawnCount >= MAX_SPAWNS) return;
	m->spawns[m->spawnCount++] = (Spawn){ x, y, z, yaw, team };
}

// Solid stair block: step i (1..steps) rises i*rise above baseY and is 'run' deep, starting at
// (x0,z0) and climbing in direction (dirx,dirz) (one of them is +-1, the other 0).
static void addStairsY(Map* m, float baseY, float x0, float z0, int dirx, int dirz, int steps,
                       float width, float rise, float run, float r, float g, float b)
{
	for (int i = 1; i <= steps; i++) {
		float a0 = (float)(i - 1) * run;
		float a1 = (float)i * run;
		float h  = (float)i * rise;
		if (dirz != 0) {
			float za = z0 + dirz * a0, zb = z0 + dirz * a1;
			float zlo = za < zb ? za : zb, zhi = za < zb ? zb : za;
			addBox(m, x0 - width * 0.5f, baseY, zlo, x0 + width * 0.5f, baseY + h, zhi, r, g, b);
		} else {
			float xa = x0 + dirx * a0, xb = x0 + dirx * a1;
			float xlo = xa < xb ? xa : xb, xhi = xa < xb ? xb : xa;
			addBox(m, xlo, baseY, z0 - width * 0.5f, xhi, baseY + h, z0 + width * 0.5f, r, g, b);
		}
	}
}

static void addStairs(Map* m, float x0, float z0, int dirx, int dirz, int steps,
                      float width, float rise, float run, float r, float g, float b)
{
	addStairsY(m, 0.0f, x0, z0, dirx, dirz, steps, width, rise, run, r, g, b);
}


// ============================================================================
// Maps. Team fairness comes from point symmetry: each map is designed for the
// half with z > 0 (the whole x range) and stamped a second time turned by 180
// degrees (x and z negated), so red and blue get the same ground but left and
// right can differ. Pieces in the middle that straddle z = 0 are placed once.
// ============================================================================

#define PI_F 3.14159265f

// Box mirrored by the signs (sx, sz); coordinates are given for the positive quadrant.
static void boxS(Map* m, int sx, int sz, float x0, float y0, float z0, float x1, float y1, float z1, const float c[3])
{
	float ax = x0 * sx, bx = x1 * sx, az = z0 * sz, bz = z1 * sz;
	addBox(m, ax < bx ? ax : bx, y0, az < bz ? az : bz, ax < bx ? bx : ax, y1, az < bz ? bz : az, c[0], c[1], c[2]);
}

static void stairsS(Map* m, int sx, int sz, float baseY, float x0, float z0, int dirx, int dirz, int steps,
                    float width, float rise, float run, const float c[3])
{
	addStairsY(m, baseY, x0 * sx, z0 * sz, dirx * sx, dirz * sz, steps, width, rise, run, c[0], c[1], c[2]);
}

static void spawnFacing(Map* m, float x, float y, float z, int team)
{
	addSpawn(m, x, y, z, atan2f(-x, z), team);
}

// Short names for the point-symmetric stamping: s = +1 for the first copy, -1 for the turned one.
static void P(Map* m, int s, float x0, float y0, float z0, float x1, float y1, float z1, const float c[3])
{
	boxS(m, s, s, x0, y0, z0, x1, y1, z1, c);
}
static void PS(Map* m, int s, float baseY, float x0, float z0, int dirx, int dirz, int steps, float width, const float c[3])
{
	stairsS(m, s, s, baseY, x0, z0, dirx, dirz, steps, width, 0.5f, 1.0f, c);
}
// Plain (unmirrored) box
static void B(Map* m, float x0, float y0, float z0, float x1, float y1, float z1, const float c[3])
{
	addBox(m, x0, y0, z0, x1, y1, z1, c[0], c[1], c[2]);
}
// A rounded-off hill made of steps 0.5 m tall, easy to walk up from every side
static void mound(Map* m, int s, float cx, float cz, float r, float h, const float c[3])
{
	int n = (int)(h / 0.5f + 0.5f);
	for (int i = 1; i <= n; i++) {
		float hw = r - (r - 1.3f) * (float)i / (float)n;
		P(m, s, cx - hw, 0.0f, cz - hw, cx + hw, 0.5f * i, cz + hw, c);
	}
}

// A low dune built for the 2 m nav grid: terraces 3 m wide, 0.5 m per terrace, edges halfway between grid nodes
// (use an even centre and r = 7)
static void dune(Map* m, int s, float cx, float cz, float r, const float c[3])
{
	int n = (int)((r - 1.3f) / 3.0f) + 1;
	for (int i = 1; i <= n; i++) {
		float hw = r - 3.0f * (float)(i - 1);
		P(m, s, cx - hw, 0.0f, cz - hw, cx + hw, 0.5f * i, cz + hw, c);
	}
}


// ------------------------------------------------------------------ round things
// A footprint of boxes that follows a circle closely: a plus shape with a square in the middle, which
// gives an octagon-like outline (never more than about 2% outside the circle). Small things get one box.
static void roundBoxes(Map* m, float x, float z, float y0, float y1, float r, const float c[3])
{
	int first = m->boxCount;
	if (r < 0.75f) {
		float k = r * 0.85f;
		addBox(m, x - k, y0, z - k, x + k, y1, z + k, c[0], c[1], c[2]);
	} else {
		float k = r * 0.94f;
		addBox(m, x - k, y0, z - 0.41f * k, x + k, y1, z + 0.41f * k, c[0], c[1], c[2]);
		addBox(m, x - 0.41f * k, y0, z - k, x + 0.41f * k, y1, z + k, c[0], c[1], c[2]);
		addBox(m, x - 0.74f * k, y0, z - 0.74f * k, x + 0.74f * k, y1, z + 0.74f * k, c[0], c[1], c[2]);
	}
	for (int i = first; i < m->boxCount; i++) m->boxes[i].hidden = true;
}

static Round* addRound(Map* m, int kind, float x, float z, float y0, float y1, float r0, float r1, const float c[3])
{
	if (m->roundCount >= MAX_ROUNDS) return NULL;
	Round* r = &m->rounds[m->roundCount++];
	memset(r, 0, sizeof(*r));
	r->kind = kind; r->x = x; r->z = z; r->y0 = y0; r->y1 = y1; r->r0 = r0; r->r1 = r1;
	r->color[0] = c[0]; r->color[1] = c[1]; r->color[2] = c[2];
	r->sides = r0 < 0.8f ? 8 : (r0 < 2.6f ? 12 : 16);
	r->cap = (kind == ROUND_CYL);
	return r;
}

// Column / barrel / tank: a cylinder (rBase to rTop makes it a cone), with optional bands or flutes
static void RC(Map* m, int s, float x, float z, float y0, float y1, float r, const float c[3], float bandH, float flute)
{
	Round* q = addRound(m, ROUND_CYL, x * s, z * s, y0, y1, r, r, c);
	if (!q) return;
	q->bandH = bandH; q->flute = flute;
	roundBoxes(m, x * s, z * s, y0, y1, r, c);
}
static void RCone(Map* m, int s, float x, float z, float y0, float y1, float rBase, float rTop, const float c[3])
{
	Round* q = addRound(m, ROUND_CYL, x * s, z * s, y0, y1, rBase, rTop, c);
	if (!q) return;
	roundBoxes(m, x * s, z * s, y0, y1, 0.5f * (rBase + rTop) * 1.02f, c);
}
// The last round thing is left open on top (it carries something that sits on its top face)
static void noCap(Map* m) { if (m->roundCount > 0) m->rounds[m->roundCount - 1].cap = false; }
// Dome sitting on y0, rising h; collides only as a low stump so it does not catch the player
static void RD(Map* m, int s, float x, float z, float y0, float h, float r, const float c[3], int collide)
{
	if (!addRound(m, ROUND_DOME, x * s, z * s, y0, y0 + h, r, r, c)) return;
	if (collide) roundBoxes(m, x * s, z * s, y0, y0 + h * 0.55f, r * 0.78f, c);
}
// Ball / egg-shaped blob (tree canopies, bushes): centred at cy, semi-axes r (wide) and ry (tall)
static void RB(Map* m, int s, float x, float z, float cy, float r, float ry, const float c[3], float variation, int collide)
{
	Round* q = addRound(m, ROUND_BALL, x * s, z * s, cy - ry, cy + ry, r, r, c);
	if (!q) return;
	q->flute = variation;
	if (collide == 1) roundBoxes(m, x * s, z * s, cy - ry * 0.55f, cy + ry * 0.55f, r * 0.82f, c);
	if (collide == 2) roundBoxes(m, x * s, z * s, (cy - ry) < 0.0f ? 0.0f : (cy - ry), cy + ry * 0.45f, r * 0.8f, c);     // resting on the ground (ground level 0)
}

#define BOTH(body) for (int s = 1; s >= -1; s -= 2) { body }

// Two team rows of spawns on the z > 0 / z < 0 ends plus a few free-for-all points
static void spawnRows(Map* m, float zA, float zB, float spread, float ffa[][3], int ffaCount)
{
	for (int i = 0; i < 4; i++) {
		float x = (-1.5f + i) * spread;
		addSpawn(m, x, 0.01f,  zA, 0.0f, 0);
		addSpawn(m, x, 0.01f, -zA, PI_F, 1);
		float x2 = (-1.0f + i) * spread;
		addSpawn(m, x2, 0.01f,  zB, 0.0f, 0);
		addSpawn(m, x2, 0.01f, -zB, PI_F, 1);
	}
	for (int i = 0; i < ffaCount; i++) spawnFacing(m, ffa[i][0], ffa[i][1], ffa[i][2], 2);
}

// Perimeter wall of height h and thickness 1 around a floor of half-size e
static void perimeter(Map* m, float e, float h, const float c[3])
{
	B(m, -e, 0, -e,  e, h, -e + 1, c);
	B(m, -e, 0,  e - 1, e, h,  e, c);
	B(m, -e, 0, -e + 1, -e + 1, h, e - 1, c);
	B(m,  e - 1, 0, -e + 1, e, h, e - 1, c);
}

// ------------------------------------------------------------------ colours
static const float C_GRASS[3] = { 0.52f, 0.78f, 0.36f }, C_CREAM[3] = { 0.96f, 0.92f, 0.80f },
                   C_ROOF[3]  = { 0.86f, 0.36f, 0.30f }, C_STONE[3] = { 0.72f, 0.72f, 0.74f },
                   C_WOOD[3]  = { 0.72f, 0.54f, 0.34f }, C_HEDGE[3] = { 0.28f, 0.60f, 0.30f },
                   C_WATER[3] = { 0.45f, 0.75f, 0.95f }, C_TRUNK[3] = { 0.50f, 0.34f, 0.20f },
                   C_LEAF[3]  = { 0.30f, 0.66f, 0.32f }, C_PANEL[3] = { 0.86f, 0.82f, 0.70f };
static const float C_SAND[3] = { 0.86f, 0.70f, 0.46f }, C_ROCK[3] = { 0.70f, 0.46f, 0.32f },
                   C_ROCK2[3] = { 0.78f, 0.55f, 0.38f }, C_DARK[3] = { 0.55f, 0.38f, 0.28f },
                   C_PLANK[3] = { 0.66f, 0.50f, 0.32f };
static const float C_MARBLE[3] = { 0.93f, 0.90f, 0.82f }, C_TERRA[3] = { 0.84f, 0.56f, 0.46f },
                   C_GOLD[3] = { 0.92f, 0.76f, 0.34f }, C_PILLAR[3] = { 0.80f, 0.74f, 0.64f },
                   C_RED[3] = { 0.62f, 0.28f, 0.30f };
static const float C_CREAM_E[3] = { 0.95f, 0.88f, 0.74f };
static const float C_CRATE_HAY[3] = { 0.90f, 0.70f, 0.30f };
static const float C_CONC[3] = { 0.80f, 0.75f, 0.55f }, C_WALLF[3] = { 0.85f, 0.45f, 0.35f },
                   C_STEEL[3] = { 0.50f, 0.60f, 0.75f }, C_CRATE[3] = { 0.90f, 0.70f, 0.30f },
                   C_CRATE2[3] = { 0.75f, 0.55f, 0.25f }, C_GREY[3] = { 0.60f, 0.60f, 0.65f },
                   C_IRON[3] = { 0.45f, 0.45f, 0.50f }, C_BRICK[3] = { 0.70f, 0.50f, 0.40f };

// ================================================================== Eggworks
// An egg factory under a smoggy sunset: a raised reactor floor in the middle, boiler towers joined by a
// catwalk, conveyor belts, press gates and tall silos.
static void eggworksHalf(Map* m, int s)
{
	// Spawn bay: low dock walls with a gap in front of each side
	P(m, s, -15, 0, 24.5f, -6, 1.2f, 25.3f, C_BRICK);
	P(m, s,   6, 0, 24.5f, 15, 1.2f, 25.3f, C_BRICK);

	// West boiler tower with a catwalk down to the reactor floor
	P(m, s, -27, 0, 4, -19, 4.5f, 12, C_GREY);
	PS(m, s, 0.0f, -23, 21, 0, -1, 9, 4.0f, C_IRON);                    // stairs up from the spawn side
	P(m, s, -27, 4.5f, 4, -19, 5.5f, 4.5f, C_IRON);                      // roof rails
	P(m, s, -27, 4.5f, 4.5f, -26.5f, 5.5f, 11.5f, C_IRON);
	P(m, s, -27, 4.5f, 11.5f, -25.5f, 5.5f, 12, C_IRON);
	P(m, s, -20.5f, 4.5f, 11.5f, -19, 5.5f, 12, C_IRON);
	P(m, s, -19, 4.0f, 6.5f, -13, 4.5f, 9.5f, C_STEEL);                  // the bridge
	PS(m, s, 2.5f, -9, 8, -1, 0, 4, 3.0f, C_STEEL);                       // down to the reactor floor
	P(m, s, -18.8f, 0, 6.5f, -18.4f, 4.0f, 7, C_IRON);                    // bridge posts (beside the tower, not inside it)
	P(m, s, -13.4f, 0, 9, -13, 4.0f, 9.5f, C_IRON);

	// East boiler tower
	P(m, s, 19, 0, 13, 27, 3.5f, 21, C_GREY);
	PS(m, s, 0.0f, 12, 17, 1, 0, 7, 4.0f, C_IRON);
	P(m, s, 22, 3.5f, 15, 24.5f, 4.75f, 17.5f, C_CRATE);

	// Conveyor belts (low enough to hop onto) with gaps to cross
	P(m, s, -11, 0, 13, -9, 0.9f, 22, C_IRON);
	P(m, s, -11, 0, 25.3f, -9, 0.9f, 31, C_IRON);
	P(m, s,   8, 0, 14, 10, 0.9f, 22, C_IRON);
	P(m, s,   8, 0, 25.3f, 10, 0.9f, 30, C_IRON);

	// Crate stacks: a crate and a smaller one on top
	P(m, s, -4, 0, 14, -1.5f, 1.25f, 16.5f, C_CRATE);
	P(m, s, -3.4f, 1.25f, 14.6f, -2.0f, 2.5f, 16.0f, C_CRATE2);
	P(m, s, 13, 0, 5, 15.5f, 1.25f, 7.5f, C_CRATE);
	P(m, s, -17, 0, 22, -15, 2.5f, 24, C_CRATE2);
	P(m, s, 26, 0, 25, 28.5f, 1.25f, 27.5f, C_CRATE);
	P(m, s, -33, 0, 4, -30.5f, 1.25f, 6.5f, C_CRATE);
	P(m, s, 17, 0, 27, 20, 1.25f, 29.5f, C_CRATE2);

	// Silos along the walls
	RC(m, s, -33.1f, 18, 0, 9.0f, 3.0f, C_BRICK, 1.5f, 0); noCap(m);
	RD(m, s, -33.1f, 18, 9.0f, 2.0f, 3.0f, C_IRON, 0);
	RC(m, s, 33.0f, 32, 0, 8.0f, 3.0f, C_BRICK, 1.5f, 0); noCap(m);
	RD(m, s, 33.0f, 32, 8.0f, 2.0f, 3.0f, C_IRON, 0);

	// Egg-shaped storage tanks on a pedestal, a smokestack, and barrels
	RC(m, s, -25, 31, 0, 1.4f, 1.1f, C_IRON, 0, 0);
	RB(m, s, -25, 31, 4.2f, 2.3f, 2.9f, C_CREAM_E, 0, 1);
	RC(m, s, 23, 7, 0, 1.4f, 1.1f, C_IRON, 0, 0);
	RB(m, s, 23, 7, 4.2f, 2.3f, 2.9f, C_CREAM_E, 0, 1);
	RCone(m, s, -32.5f, 32.0f, 0, 12.0f, 1.7f, 1.1f, C_BRICK);
	RC(m, s, -7.0f, 28.0f, 0, 1.1f, 0.45f, C_STEEL, 0.55f, 0);
	RC(m, s, -6.1f, 28.8f, 0, 1.1f, 0.45f, C_STEEL, 0.55f, 0);
	RC(m, s, 7.5f, 27.5f, 0, 1.1f, 0.45f, C_STEEL, 0.55f, 0);
	RC(m, s, 13.5f, 10.0f, 0, 1.1f, 0.45f, C_STEEL, 0.55f, 0);
	RC(m, s, -16.0f, 17.5f, 0, 1.1f, 0.45f, C_STEEL, 0.55f, 0);

	// Press gate: two uprights and a beam you can walk under
	P(m, s, -4.5f, 0, 21, -2.5f, 4.2f, 23, C_IRON);
	P(m, s, 2.5f, 0, 21, 4.5f, 4.2f, 23, C_IRON);
	P(m, s, -4.5f, 4.2f, 21, 4.5f, 5.0f, 23, C_STEEL);

	// Loading dock along the east wall, reached by stairs from the south
	P(m, s, 30, 0, 2, 36, 3.0f, 22, C_STEEL);
	PS(m, s, 0.0f, 33, 28, 0, -1, 6, 4.0f, C_IRON);
	P(m, s, 30, 3.0f, 2, 36, 3.9f, 2.6f, C_IRON);                         // rail at the north end
}

static void loadEggworks(Map* m)
{
	memset(m, 0, sizeof(*m));
	m->name = "Eggworks";
	m->sky = 0;
	m->killY = -30.0f;
	m->panoRadius = 56.0f;
	m->panoHeight = 26.0f;

	B(m, -37, -1, -37, 37, 0, 37, C_CONC);
	perimeter(m, 37, 7.0f, C_WALLF);
	B(m, -36, 5.0f, -36, 36, 6.2f, -35.2f, C_IRON);                       // pipe racks along the walls
	B(m, -36, 5.0f, 35.2f, 36, 6.2f, 36, C_IRON);
	// the side racks stop where a silo stands against the wall
	B(m, -36, 5.0f, -29, -35.2f, 6.2f, 15, C_IRON);
	B(m, -36, 5.0f, 21, -35.2f, 6.2f, 35.2f, C_IRON);
	B(m, 35.2f, 5.0f, -35.2f, 36, 6.2f, -21, C_IRON);
	B(m, 35.2f, 5.0f, -15, 36, 6.2f, 29, C_IRON);

	// The reactor floor in the middle: flat on top, stairs from all four sides
	B(m, -9, 0, -9, 9, 2.5f, 9, C_STEEL);
	addStairsY(m, 0.0f, 0, 14, 0, -1, 5, 5.0f, 0.5f, 1.0f, C_IRON[0], C_IRON[1], C_IRON[2]);
	addStairsY(m, 0.0f, 0, -14, 0, 1, 5, 5.0f, 0.5f, 1.0f, C_IRON[0], C_IRON[1], C_IRON[2]);
	addStairsY(m, 0.0f, 14, 0, -1, 0, 5, 5.0f, 0.5f, 1.0f, C_IRON[0], C_IRON[1], C_IRON[2]);
	addStairsY(m, 0.0f, -14, 0, 1, 0, 5, 5.0f, 0.5f, 1.0f, C_IRON[0], C_IRON[1], C_IRON[2]);
	RC(m, 1, -7.9f, -7.9f, 2.5f, 6.5f, 0.8f, C_IRON, 0, 0.1f);          // corner pylons
	RC(m, 1, 7.9f, -7.9f, 2.5f, 6.5f, 0.8f, C_IRON, 0, 0.1f);
	RC(m, 1, -7.9f, 7.9f, 2.5f, 6.5f, 0.8f, C_IRON, 0, 0.1f);
	RC(m, 1, 7.9f, 7.9f, 2.5f, 6.5f, 0.8f, C_IRON, 0, 0.1f);

	BOTH(eggworksHalf(m, s);)

	static float ffa[][3] = { { -33, 0.01f, 0 }, { 33, 0.01f, 0 }, { -24, 0.01f, -30 }, { 24, 0.01f, 30 } };
	spawnRows(m, 31.0f, 34.0f, 8.0f, ffa, 4);
}

// ================================================================ Sunny Side
// A village on a sunny day: two brooks cutting the field, a windmill, a barn with a hayloft, cottages,
// grassy hills and big trees.
static void tree(Map* m, int s, float x, float z, float h)
{
	RC(m, s, x, z, 0, h + 0.3f, 0.5f, C_TRUNK, 0, 0.1f); noCap(m);
	RB(m, s, x, z, h + 1.6f, 2.8f, 1.9f, C_LEAF, 0.07f, 1);
	RB(m, s, x, z, h + 3.1f, 1.7f, 1.3f, C_LEAF, 0.07f, 0);
}

static void sunnyHalf(Map* m, int s)
{
	// Cottage in the base zone (doors on three sides, flat roof)
	P(m, s, 16, 0, 37, 17, 4.0f, 45, C_CREAM);
	P(m, s, 28, 0, 37, 29, 4.0f, 45, C_CREAM);
	P(m, s, 16, 0, 45, 21, 4.0f, 46, C_CREAM);
	P(m, s, 25, 0, 45, 29, 4.0f, 46, C_CREAM);
	P(m, s, 21, 3.2f, 45, 25, 4.0f, 46, C_CREAM);
	P(m, s, 16, 0, 36, 21, 4.0f, 37, C_CREAM);
	P(m, s, 25, 0, 36, 29, 4.0f, 37, C_CREAM);
	P(m, s, 21, 3.2f, 36, 25, 4.0f, 37, C_CREAM);
	P(m, s, 15.5f, 4.0f, 35.5f, 29.5f, 4.5f, 46.5f, C_ROOF);
	P(m, s, 19, 0, 40, 21.4f, 1.25f, 42.4f, C_WOOD);

	// The windmill: a stone base with a deck, a tall wooden tower and four sails
	P(m, s, 20, 0, 8, 26, 4.5f, 14, C_CREAM);
	PS(m, s, 0.0f, 11, 11, 1, 0, 9, 4.0f, C_STONE);
	P(m, s, 20, 4.5f, 8, 26, 5.5f, 8.5f, C_STONE);
	P(m, s, 20, 4.5f, 13.5f, 26, 5.5f, 14, C_STONE);
	RCone(m, s, 23.0f, 11.0f, 4.5f, 12.5f, 1.7f, 1.25f, C_WOOD); noCap(m);
	RD(m, s, 23.0f, 11.0f, 12.5f, 1.9f, 1.8f, C_ROOF, 0);
	P(m, s, 22.7f, 7.0f, 8.9f, 23.3f, 17.0f, 9.4f, C_WOOD);                 // sails
	P(m, s, 17.9f, 11.7f, 8.98f, 28.1f, 12.3f, 9.32f, C_WOOD);

	// Barn with a hayloft (stairs inside), doors at both ends
	P(m, s, -35, 0, 6, -34, 5.0f, 22, C_TERRA);
	P(m, s, -21, 0, 6, -20, 5.0f, 12, C_TERRA);
	P(m, s, -21, 0, 16, -20, 5.0f, 22, C_TERRA);
	P(m, s, -21, 3.5f, 12, -20, 5.0f, 16, C_TERRA);
	P(m, s, -34, 0, 6, -21, 5.0f, 7, C_TERRA);
	P(m, s, -34, 0, 21, -29, 5.0f, 22, C_TERRA);
	P(m, s, -25, 0, 21, -21, 5.0f, 22, C_TERRA);
	P(m, s, -29, 3.5f, 21, -25, 5.0f, 22, C_TERRA);
	P(m, s, -35, 5.0f, 5.5f, -19.5f, 5.5f, 22.5f, C_ROOF);
	P(m, s, -34, 2.5f, 7, -28, 3.0f, 21, C_WOOD);                          // hayloft floor
	PS(m, s, 0.0f, -22, 10, -1, 0, 6, 3.0f, C_WOOD);                       // stairs up to the loft
	P(m, s, -26, 0, 17, -23.6f, 1.25f, 19.4f, C_CRATE_HAY);

	// Garden walls and hedges that break up the long sight lines
	P(m, s, 4, 0, 18, 10, 1.2f, 19, C_STONE);
	P(m, s, 6, 0, 30, 14, 1.4f, 31.2f, C_HEDGE);
	P(m, s, -16, 0, 33, -8, 1.4f, 34.2f, C_HEDGE);
	P(m, s, -16, 0, 29, -14.8f, 1.4f, 33, C_HEDGE);
	P(m, s, 30, 0, 28, 38, 1.4f, 29.2f, C_HEDGE);

	// Hills with a view, trees and hay bales
	mound(m, s, -8, 14.5f, 6.0f, 2.5f, C_GRASS);
	mound(m, s, 38, 14, 7.0f, 3.0f, C_GRASS);
	tree(m, s, -44, 12, 4.5f);
	tree(m, s, -12, 40, 4.0f);
	tree(m, s, 40, 38, 4.5f);
	tree(m, s, 46, 8, 4.0f);
	tree(m, s, 8, 12, 3.5f);
	RC(m, s, -0.8f, 37.2f, 0, 1.3f, 1.25f, C_CRATE_HAY, 0, 0.06f);        // round hay bales and wooden barrels
	RC(m, s, 45.2f, 29.2f, 0, 1.3f, 1.25f, C_CRATE_HAY, 0, 0.06f);
	RC(m, s, -38.8f, 37.2f, 0, 1.3f, 0.7f, C_WOOD, 0.45f, 0);
	RC(m, s, 23.2f, 17.2f, 0, 2.2f, 1.2f, C_WOOD, 0.55f, 0);
	RB(m, s, 12.0f, 33.5f, 0.8f, 1.3f, 0.9f, C_HEDGE, 0.06f, 1);          // round bushes
	RB(m, s, -30.0f, 34.0f, 0.8f, 1.3f, 0.9f, C_HEDGE, 0.06f, 1);
	RB(m, s, 29.5f, 18.0f, 0.8f, 1.3f, 0.9f, C_HEDGE, 0.06f, 1);
	RB(m, s, -4.0f, 25.0f, 0.8f, 1.3f, 0.9f, C_HEDGE, 0.06f, 1);
}

static void loadSunny(Map* m)
{
	memset(m, 0, sizeof(*m));
	m->name = "Sunny Side";
	m->sky = 1;
	m->killY = -30.0f;
	m->panoRadius = 80.0f;
	m->panoHeight = 34.0f;

	// The field, with two brooks 1.2 m down (wade through, or hop out; bridges at the sides)
	B(m, -55, -1.2f, -22, 55, 0, 22, C_GRASS);
	B(m, -55, -1.2f, 28, 55, 0, 55, C_GRASS);
	B(m, -55, -1.2f, -55, 55, 0, -28, C_GRASS);
	B(m, -55, -2.0f, 22, 55, -1.2f, 28, C_WATER);
	B(m, -55, -2.0f, -28, 55, -1.2f, -22, C_WATER);
	perimeter(m, 55, 4.5f, C_PANEL);
	for (int sx = -1; sx <= 1; sx += 2) {                              // stone bridges with low parapets
		float x = 32.0f * sx;
		B(m, x - 3, -1.2f, 22.0f, x + 3, 0, 28.0f, C_STONE);
		B(m, x - 3, -1.2f, -28.0f, x + 3, 0, -22.0f, C_STONE);
		B(m, x - 3, 0, 22.0f, x - 2.4f, 0.9f, 28.0f, C_STONE);
		B(m, x + 2.4f, 0, 22.0f, x + 3, 0.9f, 28.0f, C_STONE);
		B(m, x - 3, 0, -28.0f, x - 2.4f, 0.9f, -22.0f, C_STONE);
		B(m, x + 2.4f, 0, -28.0f, x + 3, 0.9f, -22.0f, C_STONE);
	}
	B(m, -3, -1.2f, 22.0f, 3, -0.1f, 28.0f, C_WOOD);                    // a plank bridge in the middle
	B(m, -3, -1.2f, -28.0f, 3, -0.1f, -22.0f, C_WOOD);

	// Village square: an open stone floor (the Pan goes in the middle) with a little fountain in each corner
	B(m, -9, 0, -9, 9, 0.5f, 9, C_STONE);
	for (int fx = -1; fx <= 1; fx += 2) for (int fz = -1; fz <= 1; fz += 2) {
		float x = 6.5f * fx, z = 6.5f * fz;
		RC(m, 1, x, z, 0.5f, 1.1f, 1.5f, C_STONE, 0, 0);                       // round fountain basins
		RC(m, 1, x, z, 1.1f, 1.25f, 1.2f, C_WATER, 0, 0);
		RCone(m, 1, x, z, 1.25f, 2.6f, 0.35f, 0.2f, C_STONE);
	}

	BOTH(sunnyHalf(m, s);)

	static float ffa[][3] = { { 50, 0.01f, 10 }, { -50, 0.01f, -10 }, { 50, 0.01f, -12 }, { -50, 0.01f, 12 } };
	spawnRows(m, 50.0f, 53.5f, 12.0f, ffa, 4);
}

// ============================================================= Cracked Canyon
// Two plateaus split by a 7 m deep canyon: three bridges, ramps cut into the walls, a rock island in
// the middle, sniper mesas, a tunnel through a rock mass and rock gates.
static void cactus(Map* m, int s, float x, float z)
{
	RC(m, s, x, z, 0, 2.2f, 0.3f, C_HEDGE, 0, 0.1f);
	P(m, s, x + 0.25f, 1.0f, z - 0.17f, x + 0.7f, 1.34f, z + 0.17f, C_HEDGE);
	RC(m, s, x + 0.78f, z, 1.0f, 2.1f, 0.17f, C_HEDGE, 0, 0.1f);
}

static void canyonHalf(Map* m, int s)
{
	// West mesa with a switchback of stairs
	P(m, s, -56, 0, 20, -42, 4.0f, 34, C_ROCK2);
	PS(m, s, 0.0f, -34, 28, -1, 0, 8, 4.0f, C_ROCK2);
	P(m, s, -56, 4.0f, 20, -55, 5.1f, 34, C_ROCK);                            // parapet rocks on top
	P(m, s, -50, 4.0f, 24, -47.5f, 5.2f, 26.5f, C_DARK);

	// East sniper spire: two tiers
	P(m, s, 28, 0, 24, 44, 4.0f, 42, C_ROCK2);
	PS(m, s, 0.0f, 20, 30, 1, 0, 8, 4.0f, C_ROCK);
	P(m, s, 34, 4.0f, 34, 42, 8.0f, 42, C_ROCK2);
	PS(m, s, 4.0f, 38, 26, 0, 1, 8, 4.0f, C_ROCK);
	P(m, s, 30, 4.0f, 29, 31.5f, 5.2f, 33, C_DARK);

	// Tunnel through a rock mass, 4 m wide
	P(m, s, 8, 0, 20, 28, 6.0f, 22, C_ROCK);
	P(m, s, 8, 0, 26, 28, 6.0f, 28, C_ROCK);
	P(m, s, 8, 3.6f, 22, 28, 6.0f, 26, C_ROCK);

	// Rock gate across the middle lane
	RCone(m, s, -10, 38, 0, 6.5f, 2.5f, 2.2f, C_ROCK); noCap(m);
	RCone(m, s, 10, 38, 0, 6.5f, 2.5f, 2.2f, C_ROCK); noCap(m);
	P(m, s, -12, 6.5f, 36, 12, 9.0f, 40, C_ROCK2);

	// Dunes and boulders
	dune(m, s, -26, 22, 7.0f, C_SAND);
	dune(m, s, -36, 44, 7.0f, C_SAND);
	dune(m, s, 52, 48, 7.0f, C_SAND);
	RB(m, s, -19.5f, 35, 1.4f, 2.8f, 1.9f, C_DARK, 0.05f, 2);              // boulders and hoodoos (round rock)
	RB(m, s, 22, 42, 0.9f, 2.2f, 1.2f, C_DARK, 0.05f, 2);
	RB(m, s, -12.5f, 29.5f, 0.8f, 1.8f, 1.1f, C_DARK, 0.05f, 2);
	RCone(m, s, -3, 30, 0, 7.0f, 2.3f, 1.5f, C_ROCK2);
	RB(m, s, -3, 30, 7.4f, 2.8f, 1.2f, C_ROCK, 0.04f, 1);
	RCone(m, s, 14, 48, 0, 6.0f, 2.0f, 1.3f, C_ROCK2);
	RB(m, s, 14, 48, 6.4f, 2.5f, 1.1f, C_ROCK, 0.04f, 1);
	RCone(m, s, -48, 42, 0, 5.5f, 1.9f, 1.2f, C_ROCK2);
	RB(m, s, -48, 42, 5.9f, 2.3f, 1.0f, C_ROCK, 0.04f, 1);
	P(m, s, -8, 0, 46, 0, 2.2f, 47.2f, C_ROCK2);
	P(m, s, 50, 0, 16, 56, 5.0f, 21, C_ROCK);
	RB(m, s, 15.5f, 14, 0.8f, 1.7f, 1.1f, C_DARK, 0.05f, 2);
	RB(m, s, -36, 15.5f, 0.8f, 1.8f, 1.1f, C_DARK, 0.05f, 2);

	cactus(m, s, -18, 24); cactus(m, s, 36, 46); cactus(m, s, -48, 50);
	cactus(m, s, 14, 30); cactus(m, s, 56, 30); cactus(m, s, -6, 18);
}

static void loadCanyon(Map* m)
{
	memset(m, 0, sizeof(*m));
	m->name = "Cracked Canyon";
	m->sky = 2;
	m->killY = -30.0f;
	m->panoRadius = 92.0f;
	m->panoHeight = 38.0f;

	// Two plateaus with cliff faces down to the canyon floor
	B(m, -64, -0.6f, 12, 64, 0, 56, C_SAND);
	B(m, -64, -0.6f, -56, 64, 0, -12, C_SAND);
	B(m, -64, -7.0f, 12, 64, -0.6f, 56, C_ROCK);
	B(m, -64, -7.0f, -56, 64, -0.6f, -12, C_ROCK);
	B(m, -64, -8, -12, 64, -7, 12, C_DARK);                                    // canyon floor
	B(m, -64, -7.0f, -2.5f, -16, -6.9f, 2.5f, C_WATER);                        // a trickle of water, in two parts so it stops at the island ramps
	B(m, 16, -7.0f, -2.5f, 64, -6.9f, 2.5f, C_WATER);
	// outer cliffs
	B(m, -65, -8, -57, 65, 14, -56, C_ROCK);
	B(m, -65, -8, 56, 65, 14, 57, C_ROCK);
	B(m, -65, -8, -56, -64, 14, 56, C_ROCK);
	B(m, 64, -8, -56, 65, 14, 56, C_ROCK);

	// Bridges: wide stone one in the middle, planked ones left and right
	B(m, -4, -1.0f, -12, 4, 0, 12, C_ROCK2);
	B(m, -4, 0, -12, -3.4f, 0.9f, 12, C_ROCK);
	B(m, 3.4f, 0, -12, 4, 0.9f, 12, C_ROCK);
	for (int sx = -1; sx <= 1; sx += 2) {
		float x = 38.0f * sx;
		B(m, x - 3, -0.5f, -12, x + 3, 0, 12, C_PLANK);
		B(m, x - 3, 0, -12, x - 2.7f, 1.0f, 12, C_WOOD);
		B(m, x + 2.7f, 0, -12, x + 3, 1.0f, 12, C_WOOD);
	}
	// A rock island in the canyon, ramps up from both ends, and stairs out of the canyon on each wall
	B(m, -9, -7, -5, 9, -3.5f, 5, C_ROCK2);
	addStairsY(m, -7.0f, -16, 0, 1, 0, 7, 6.0f, 0.5f, 1.0f, C_ROCK2[0], C_ROCK2[1], C_ROCK2[2]);   // ramps end exactly at the island
	addStairsY(m, -7.0f, 16, 0, -1, 0, 7, 6.0f, 0.5f, 1.0f, C_ROCK2[0], C_ROCK2[1], C_ROCK2[2]);
	// Stairs out of the canyon, between the bridges (the planked bridges sit at x = +-38, so keep clear of them)
	addStairsY(m, -7.0f, -20, 10.0f, -1, 0, 14, 4.0f, 0.5f, 1.0f, C_ROCK2[0], C_ROCK2[1], C_ROCK2[2]);
	addStairsY(m, -7.0f, 20, 10.0f, 1, 0, 14, 4.0f, 0.5f, 1.0f, C_ROCK2[0], C_ROCK2[1], C_ROCK2[2]);
	addStairsY(m, -7.0f, 20, -10.0f, 1, 0, 14, 4.0f, 0.5f, 1.0f, C_ROCK2[0], C_ROCK2[1], C_ROCK2[2]);
	addStairsY(m, -7.0f, -20, -10.0f, -1, 0, 14, 4.0f, 0.5f, 1.0f, C_ROCK2[0], C_ROCK2[1], C_ROCK2[2]);

	BOTH(canyonHalf(m, s);)

	static float ffa[][3] = { { 58, -6.9f, 0 }, { -58, -6.9f, 0 }, { 58, 0.01f, 40 }, { -58, 0.01f, -40 } };
	spawnRows(m, 48.0f, 51.5f, 14.0f, ffa, 4);
}

// ================================================================ Pan Palace
// A palace garden at night: a grand hall in the middle with galleries and the golden pan, corner rooms,
// colonnades, a long reflecting pool, lantern posts and open pavilions.
static void lantern(Map* m, int s, float x, float z)
{
	RC(m, s, x, z, 0, 2.6f, 0.2f, C_PILLAR, 0, 0); noCap(m);
	RB(m, s, x, z, 3.05f, 0.5f, 0.46f, C_GOLD, 0, 0);
}

static void palaceHalf(Map* m, int s)
{
	// Hall wall with a 6 m doorway towards the garden, and galleries on the inside (reached by stairs)
	P(m, s, -22.5f, 0, 21.5f, -3, 7.0f, 22.5f, C_TERRA);
	P(m, s, 3, 0, 21.5f, 22.5f, 7.0f, 22.5f, C_TERRA);
	P(m, s, -3, 4.5f, 21.5f, 3, 7.0f, 22.5f, C_TERRA);
	P(m, s, -20, 4.0f, 15.5f, -4.5f, 4.5f, 21.5f, C_MARBLE);                 // west gallery
	P(m, s, 4.5f, 4.0f, 15.5f, 20, 4.5f, 21.5f, C_MARBLE);                   // east gallery
	for (int i = 0; i < 4; i++) {                                           // gallery columns
		RC(m, s, -19.0f + i * 5, 16.0f, 0, 4.0f, 0.55f, C_PILLAR, 0, 0.12f); noCap(m);
		RC(m, s, 6.5f + i * 4.5f, 16.0f, 0, 4.0f, 0.55f, C_PILLAR, 0, 0.12f); noCap(m);
	}
	PS(m, s, 0.0f, -10, 7.5f, 0, 1, 8, 3.0f, C_MARBLE);                       // up to the west gallery
	PS(m, s, 0.0f, 10, 7.5f, 0, 1, 8, 3.0f, C_MARBLE);                        // and the east one
	P(m, s, -20, 4.5f, 15.5f, -12, 5.5f, 16.0f, C_MARBLE);                    // balustrades (open where the stairs arrive)
	P(m, s, -8, 4.5f, 15.5f, -4.5f, 5.5f, 16.0f, C_MARBLE);
	P(m, s, 4.5f, 4.5f, 15.5f, 8, 5.5f, 16.0f, C_MARBLE);
	P(m, s, 12, 4.5f, 15.5f, 20, 5.5f, 16.0f, C_MARBLE);
	P(m, s, 21.5f, 0, 3, 22.5f, 7.0f, 21.5f, C_TERRA);                           // hall side walls (doorways are in the middle)
	P(m, s, -22.5f, 0, 3, -21.5f, 7.0f, 21.5f, C_TERRA);

	// Corner room with doorways, pillars and a balcony
	P(m, s, 26, 0, 26, 27, 7.0f, 36, C_TERRA);
	P(m, s, 26, 0, 44, 27, 7.0f, 55, C_TERRA);
	P(m, s, 26, 4.5f, 36, 27, 7.0f, 44, C_TERRA);
	P(m, s, 27, 0, 26, 36, 7.0f, 27, C_TERRA);
	P(m, s, 44, 0, 26, 55, 7.0f, 27, C_TERRA);
	P(m, s, 36, 4.5f, 26, 44, 7.0f, 27, C_TERRA);
	RC(m, s, 37, 37, 0, 7.0f, 1.0f, C_PILLAR, 0, 0.12f);
	RC(m, s, 45, 45, 0, 7.0f, 1.0f, C_PILLAR, 0, 0.12f);
	P(m, s, 30, 0, 46, 32.4f, 1.25f, 48.4f, C_RED);
	P(m, s, 46, 0, 30, 48, 2.2f, 32.4f, C_RED);
	P(m, s, 50, 0, 30, 55, 3.5f, 52, C_GOLD);
	PS(m, s, 0.0f, 43, 41, 1, 0, 7, 4.0f, C_GOLD);

	// Mirror corner on the other side is a covered colonnade instead of a room (left and right differ)
	for (int i = 0; i < 4; i++) {
		RC(m, s, -29.5f - i * 6, 30.5f, 0, 5.0f, 0.55f, C_PILLAR, 0, 0.12f); noCap(m);
		RC(m, s, -29.5f - i * 6, 38.5f, 0, 5.0f, 0.55f, C_PILLAR, 0, 0.12f); noCap(m);
	}
	P(m, s, -54, 5.0f, 29.5f, -28.5f, 5.8f, 31.5f, C_MARBLE);
	P(m, s, -54, 5.0f, 37.5f, -28.5f, 5.8f, 39.5f, C_MARBLE);

	// Reflecting pool with stepping edges and benches (kept clear of the flag stands in front of the spawns)
	P(m, s, -9, 0, 32, 9, 0.35f, 41, C_WATER);
	P(m, s, -9.6f, 0, 31.4f, 9.6f, 0.7f, 32, C_MARBLE);
	P(m, s, -9.6f, 0, 41, 9.6f, 0.7f, 41.6f, C_MARBLE);
	P(m, s, -9.6f, 0, 32, -9, 0.7f, 41, C_MARBLE);
	P(m, s, 9, 0, 32, 9.6f, 0.7f, 41, C_MARBLE);
	P(m, s, -2, 0.35f, 35, 2, 1.1f, 38, C_MARBLE);                            // little island with a fountain
	RCone(m, s, 0.0f, 36.5f, 1.1f, 2.5f, 0.55f, 0.28f, C_GOLD);                 // fountain spout and the golden drop on top
	RB(m, s, 0.0f, 36.5f, 2.8f, 0.4f, 0.4f, C_GOLD, 0, 0);

	// Open pavilions
	for (int k = 0; k < 2; k++) {
		float cx = k == 0 ? -20.0f : 18.0f, cz = k == 0 ? 42.0f : 30.0f;
		RC(m, s, cx - 3.5f, cz - 3.5f, 0, 4.2f, 0.55f, C_PILLAR, 0, 0.12f); noCap(m);
		RC(m, s, cx + 3.5f, cz - 3.5f, 0, 4.2f, 0.55f, C_PILLAR, 0, 0.12f); noCap(m);
		RC(m, s, cx - 3.5f, cz + 3.5f, 0, 4.2f, 0.55f, C_PILLAR, 0, 0.12f); noCap(m);
		RC(m, s, cx + 3.5f, cz + 3.5f, 0, 4.2f, 0.55f, C_PILLAR, 0, 0.12f); noCap(m);
		P(m, s, cx - 4.6f, 4.2f, cz - 4.6f, cx + 4.6f, 5.0f, cz + 4.6f, C_TERRA);
		RD(m, s, cx, cz, 5.0f, 2.2f, 4.0f, C_GOLD, 0);                          // a golden dome over each pavilion
		P(m, s, cx - 1.2f, 0, cz - 1.2f, cx + 1.2f, 0.6f, cz + 1.2f, C_GOLD);
	}

	// Planters, lanterns and cover
	P(m, s, -34, 0, 12, -30, 1.0f, 13, C_HEDGE);
	P(m, s, 30, 0, 12, 34, 1.0f, 13, C_HEDGE);
	P(m, s, -14, 0, 29, -10, 1.0f, 30, C_HEDGE);
	P(m, s, 10, 0, 50, 14, 1.0f, 51, C_HEDGE);
	P(m, s, -26, 0, 46, -23.6f, 1.25f, 48.4f, C_RED);
	P(m, s, 2, 0, 28, 4.4f, 1.25f, 30.4f, C_RED);
	lantern(m, s, -9, 26); lantern(m, s, 9, 26.5f);
	lantern(m, s, -24, 50); lantern(m, s, 22, 52); lantern(m, s, 0, 44);
	lantern(m, s, -40, 20); lantern(m, s, 40, 16);
}

static void loadPalace(Map* m)
{
	memset(m, 0, sizeof(*m));
	m->name = "Pan Palace";
	m->sky = 3;
	m->killY = -30.0f;
	m->panoRadius = 80.0f;
	m->panoHeight = 36.0f;

	B(m, -57, -1, -57, 57, 0, 57, C_MARBLE);
	perimeter(m, 57, 8.0f, C_TERRA);

	// The pan: a golden dais with steps on all sides
	RC(m, 1, 0, 0, 0, 0.5f, 10.5f, C_PILLAR, 0, 0);                                // a round two-tier dais, 0.5 m per step
	RC(m, 1, 0, 0, 0.5f, 1.0f, 7.5f, C_GOLD, 0, 0);
	// Side doorways of the hall (lintels)
	B(m, 21.5f, 4.5f, -3, 22.5f, 7.0f, 3, C_TERRA);
	B(m, -22.5f, 4.5f, -3, -21.5f, 7.0f, 3, C_TERRA);
	// Columns around the dais
	for (int i = 0; i < 4; i++) {
		float x = (i & 1) ? 13.0f : -13.0f, z = (i & 2) ? 13.0f : -13.0f;
		RC(m, 1, x, z, 0, 6.0f, 1.0f, C_GOLD, 0, 0.1f);
	}

	BOTH(palaceHalf(m, s);)

	static float ffa[][3] = { { 40, 0.01f, 33 }, { -40, 0.01f, -33 }, { -44, 0.01f, 16 }, { 44, 0.01f, -16 } };
	spawnRows(m, 51.0f, 54.0f, 10.0f, ffa, 4);
}


// ================================================================== block-built maps
// Fridge Factory, Diner Rooftops and Henhouse Hollow are built from whole 1 m blocks (Shell Shockers
// style): every box sits on the metre grid, walls and floors are at least one block thick, stairs are
// half-block slabs, and round things are chunky block shapes. The maps set 'blocky', so surface.c draws
// every face as a grid of blocks.

// ================================================================== Fridge Factory
// A cold-storage depot, small and dense. West half: an outdoor yard with a stepped crate hill (four
// jump-up levels, split by a lane down the middle). East half: a frosty freezer hall with a catwalk
// around three walls, a freezer room in the middle (its roof joined to the catwalk by a bridge) and high
// windows in the dividing wall, so the hill top and the catwalk can see each other.
// Mirrored north/south: red spawns at z > 0, blue at z < 0, and both get the same yard and hall.
static const float C_YARD[3]   = { 0.70f, 0.72f, 0.76f }, C_ICEFLR[3] = { 0.80f, 0.88f, 0.95f },
                   C_HALLW[3]  = { 0.88f, 0.93f, 0.98f }, C_CATW[3]   = { 0.42f, 0.50f, 0.62f },
                   C_FREEZ[3]  = { 0.32f, 0.44f, 0.64f }, C_ICE[3]    = { 0.70f, 0.90f, 1.00f },
                   C_CR_TEAL[3] = { 0.26f, 0.76f, 0.68f }, C_CR_SUN[3] = { 0.98f, 0.80f, 0.24f },
                   C_CR_PINK[3] = { 0.94f, 0.44f, 0.50f };

// Mirrored north/south only (x stays): s = +1 for the north half, -1 for the south half
static void N(Map* m, int s, float x0, float y0, float z0, float x1, float y1, float z1, const float c[3])
{
	boxS(m, 1, s, x0, y0, z0, x1, y1, z1, c);
}

static void fridgeHalf(Map* m, int s)
{
	// Outer walls: brick around the yard, white around the hall
	N(m, s, -24, 0, 17, 0, 7, 18, C_BRICK);
	N(m, s, 0, 0, 17, 24, 8, 18, C_HALLW);

	// Dividing wall (x 0..1): solid up to 3 m with doorways at z 8..12 and in the middle (-2..2),
	// open windows from 3 to 6 m between posts, a solid band on top
	N(m, s, 0, 0, 2, 1, 3, 8, C_HALLW);
	N(m, s, 0, 0, 12, 1, 3, 17, C_HALLW);
	N(m, s, 0, 3, 5, 1, 6, 6, C_HALLW);
	N(m, s, 0, 3, 15, 1, 6, 17, C_HALLW);

	// Hall: catwalk (top at 4 m) along the north/south walls, slab stairs up from the hall floor
	N(m, s, 9, 3, 13, 23, 4, 17, C_CATW);
	stairsS(m, 1, s, 0.0f, 1, 15, 1, 0, 8, 4.0f, 0.5f, 1.0f, C_CATW);

	// Freezer room (x 7..15, z -4..4): a door in the north and south faces
	N(m, s, 7, 0, 3, 10, 3, 4, C_FREEZ);
	N(m, s, 12, 0, 3, 15, 3, 4, C_FREEZ);

	// Hall cover
	N(m, s, 3, 0, 5, 5, 1, 7, C_CR_TEAL);
	N(m, s, 16, 0, 8, 18, 1, 10, C_CR_SUN);
	N(m, s, 20, 0, 5, 22, 1, 7, C_ICE);                                       // ice blocks under the catwalk

	// Yard: the crate hill, two halves with a 2 m lane between them (z -1..1), one block per level
	N(m, s, -18, 0, 1, -6, 1, 6, C_CR_TEAL);
	N(m, s, -16, 1, 1, -8, 2, 5, C_CR_SUN);
	N(m, s, -14, 2, 1, -10, 3, 4, C_CR_PINK);
	N(m, s, -13, 3, 1, -11, 4, 3, C_CR_TEAL);

	// Yard cover: single crates and a two-high stack
	N(m, s, -21, 0, 10, -19, 1, 12, C_CR_PINK);
	N(m, s, -9, 0, 12, -7, 1, 14, C_CR_SUN);
	N(m, s, -4, 0, 4, -2, 1, 6, C_CR_TEAL);
	N(m, s, -14, 0, 10, -12, 1, 12, C_CR_TEAL);
	N(m, s, -13, 1, 10, -12, 2, 11, C_CR_PINK);

	// Spawns: red in the north half, blue in the south, facing the middle
	const int team = s > 0 ? 0 : 1;
	const float yaw = s > 0 ? 0.0f : PI_F;
	static const float sp[8][3] = {
		{ -20, 0.01f, 15 }, { -14, 0.01f, 15.5f }, { -6, 0.01f, 15 }, { -3, 0.01f, 10 },
		{ 4, 0.01f, 11 }, { 12, 0.01f, 10 }, { 17, 0.01f, 11.5f }, { 21, 4.01f, 15 } };
	for (int i = 0; i < 8; i++) addSpawn(m, sp[i][0], sp[i][1], sp[i][2] * s, yaw, team);
	spawnFacing(m, -21, 0.01f, 6.0f * s, 2);
	spawnFacing(m, 4, 0.01f, 2.5f * s, 2);
	spawnFacing(m, 21, 4.01f, 6.0f * s, 2);
	spawnFacing(m, -10, 0.01f, 8.0f * s, 2);
}

static void loadFridge(Map* m)
{
	memset(m, 0, sizeof(*m));
	m->name = "Fridge Factory";
	m->sky = 4;
	m->blocky = true;
	m->killY = -30.0f;
	m->panoRadius = 40.0f;
	m->panoHeight = 22.0f;

	B(m, -24, -1, -18, 0, 0, 18, C_YARD);                                // yard concrete
	B(m, 0, -1, -18, 24, 0, 18, C_ICEFLR);                               // frosty hall floor
	B(m, -24, 0, -17, -23, 7, 17, C_BRICK);                              // west wall (above any jump from the crate hill)
	B(m, 23, 0, -17, 24, 8, 17, C_HALLW);                                // east wall
	B(m, 0, 6, -17, 1, 8, 17, C_HALLW);                                  // top band of the dividing wall

	// East catwalk joining the north and south ones, and a bridge out to the freezer roof
	B(m, 19, 3, -13, 23, 4, 13, C_CATW);
	B(m, 15, 3, -1, 19, 4, 1, C_CATW);

	// Freezer room: side walls, a roof you can stand on (level with the catwalk), an ice pillar inside
	B(m, 7, 0, -3, 8, 3, 3, C_FREEZ);
	B(m, 14, 0, -3, 15, 3, 3, C_FREEZ);
	B(m, 7, 3, -4, 15, 4, 4, C_FREEZ);
	B(m, 10, 0, -1, 12, 3, 1, C_ICE);

	// A block bridge across the lane between the two hill tops, and a crate that breaks the lane's sightline
	B(m, -13, 3, -1, -11, 4, 1, C_WOOD);
	B(m, -22, 0, -1, -20, 1, 1, C_CR_SUN);

	BOTH(fridgeHalf(m, s);)
}

// ================================================================== Diner Rooftops
// Rooftops at dusk around a little diner. Each team has a tall apartment roof (6 m, where it spawns) and a
// lower laundromat roof (3 m); slab stairs and landings join both to the diner roof (5 m) in the middle,
// where a block-built neon frying pan is climbed by jumping up five letter blocks. The diner has a ground
// floor with a counter, doors and windows you can hop through. Falling off a roof drops you in the street,
// and fire-escape stairs lead back up. Point-symmetric like the older maps.
static const float C_ASPH[3]   = { 0.46f, 0.45f, 0.50f }, C_BRICKR[3] = { 0.72f, 0.40f, 0.36f },
                   C_TAR[3]    = { 0.34f, 0.34f, 0.38f }, C_TEALB[3]  = { 0.36f, 0.66f, 0.66f },
                   C_TAR2[3]   = { 0.42f, 0.42f, 0.46f }, C_DINER[3]  = { 0.82f, 0.86f, 0.90f },
                   C_DINRF[3]  = { 0.94f, 0.94f, 0.96f }, C_NPINK[3]  = { 1.00f, 0.40f, 0.70f },
                   C_NCYAN[3]  = { 0.30f, 0.90f, 1.00f }, C_NYEL[3]   = { 1.00f, 0.88f, 0.30f },
                   C_PANI[3]   = { 0.20f, 0.20f, 0.24f }, C_EGGW[3]   = { 0.98f, 0.98f, 0.95f },
                   C_YOLKY[3]  = { 1.00f, 0.78f, 0.20f }, C_STEELD[3] = { 0.38f, 0.40f, 0.46f },
                   C_DUMP[3]   = { 0.30f, 0.55f, 0.38f }, C_ACU[3]    = { 0.75f, 0.77f, 0.80f },
                   C_TANK[3]   = { 0.55f, 0.45f, 0.38f }, C_BRICKD[3] = { 0.55f, 0.34f, 0.32f },
                   C_NLIME[3]  = { 0.55f, 1.00f, 0.40f };

static void dinerHalf(Map* m, int s)
{
	// Apartment block (spawn roof, top 6 m) and laundromat (top 3 m), each with a one-block tar roof
	P(m, s, -20, 0, 10, -6, 5, 20, C_BRICKR);
	P(m, s, -20, 5, 10, -6, 6, 20, C_TAR);
	P(m, s, 8, 0, 8, 20, 2, 20, C_TEALB);
	P(m, s, 8, 2, 8, 20, 3, 20, C_TAR2);

	// Apartment -> diner: a landing level with the diner roof, two slabs up to the apartment roof
	P(m, s, -9, 4, 6, -6, 5, 8, C_STEELD);
	stairsS(m, s, s, 5.0f, -7.5f, 8, 0, 1, 2, 3.0f, 0.5f, 1.0f, C_STEELD);
	P(m, s, -9, 4, 8, -6, 5, 10, C_STEELD);                                // under the slabs (keeps them solid blocks)
	// Laundromat -> diner: four slabs up off its south edge, then a block bridge onto the diner roof
	stairsS(m, s, s, 3.0f, 9, 8, 0, -1, 4, 2.0f, 0.5f, 1.0f, C_STEELD);
	P(m, s, 7, 4, 3, 10, 5, 4, C_STEELD);

	// Fire escapes from the street
	stairsS(m, s, s, 0.0f, 14, 2, 0, 1, 6, 2.0f, 0.5f, 1.0f, C_STEELD);      // up to the laundromat
	stairsS(m, s, s, 0.0f, -5, 8, 0, 1, 12, 2.0f, 0.5f, 1.0f, C_STEELD);     // up to the apartment

	// Diner: north wall with a door in the middle (the turned copy makes the south wall), east wall with
	// a one-block sill you can hop over and windows between posts (the turned copy makes the west wall)
	P(m, s, -7, 0, 5, -1, 4, 6, C_DINER);
	P(m, s, 1, 0, 5, 7, 4, 6, C_DINER);
	P(m, s, 6, 0, -5, 7, 1, 5, C_NPINK);
	P(m, s, 6, 3, -5, 7, 4, 5, C_DINER);
	P(m, s, 6, 1, -5, 7, 3, -4, C_DINER);
	P(m, s, 6, 1, 0, 7, 3, 1, C_DINER);
	P(m, s, 6, 1, 4, 7, 3, 5, C_DINER);
	P(m, s, 3, 0, 2, 5, 1, 4, C_NPINK);                                      // a booth

	// Five letter blocks, each one jump higher, spiralling up to the pan's rim (the turned copy gives the
	// other team its own way up)
	P(m, s, 4, 5, -4, 6, 6, -2, C_NPINK);
	P(m, s, 4, 5, -2, 6, 7, 0, C_NCYAN);
	P(m, s, 4, 5, 0, 6, 8, 2, C_NYEL);
	P(m, s, 4, 5, 2, 6, 9, 4, C_NLIME);
	P(m, s, 1, 5, 3, 4, 10, 5, C_NPINK);

	// Roof clutter for cover
	P(m, s, -17, 6, 13, -15, 7, 15, C_ACU);
	P(m, s, -12, 6, 15, -10, 8, 17, C_TANK);                                   // water tank
	P(m, s, 10, 3, 14, 12, 4, 16, C_ACU);
	P(m, s, 12, 3, 18, 18, 5, 19, C_NCYAN);                                    // billboard along the back edge

	// Street clutter
	P(m, s, -16, 0, 2, -14, 1, 4, C_DUMP);
	P(m, s, -19, 0, -5, -18, 1, -4, C_CRATE);
	P(m, s, 2, 0, 17, 4, 1, 19, C_DUMP);
	P(m, s, -2, 0, 11, -1, 1, 12, C_CRATE);

	// Spawns (facing the middle): three on the apartment roof, three on the laundromat, two in the alley
	const int team = s > 0 ? 0 : 1;
	static const float sp[8][3] = {
		{ -18, 6.01f, 18 }, { -13, 6.01f, 12.5f }, { -8, 6.01f, 18 },
		{ 11, 3.01f, 11 }, { 16, 3.01f, 16.5f }, { 18.5f, 3.01f, 10 },
		{ 1, 0.01f, 15 }, { -3, 0.01f, 19.5f } };
	for (int i = 0; i < 8; i++) spawnFacing(m, sp[i][0] * s, sp[i][1], sp[i][2] * s, team);
	spawnFacing(m, -18.0f * s, 0.01f, 0.0f, 2);
	spawnFacing(m, -3.0f * s, 5.01f, 3.5f * s, 2);
}

static void loadDiner(Map* m)
{
	memset(m, 0, sizeof(*m));
	m->name = "Diner Rooftops";
	m->sky = 5;
	m->blocky = true;
	m->killY = -30.0f;
	m->panoRadius = 40.0f;
	m->panoHeight = 24.0f;

	B(m, -22, -1, -22, 22, 0, 22, C_ASPH);
	perimeter(m, 22, 9.0f, C_BRICKD);                                   // the neighbouring buildings: no roof jump clears them

	// Diner roof (top 5 m) and counter
	B(m, -7, 4, -6, 7, 5, 6, C_DINRF);
	B(m, -4, 0, -1, 4, 1, 1, C_NCYAN);
	// The neon pan: a pole, a 6 x 6 pan with a one-block rim, and a fried egg inside (low slabs you step onto)
	B(m, -1, 5, -1, 1, 9, 1, C_PANI);
	B(m, -3, 9, -3, 3, 10, 3, C_PANI);
	B(m, -3, 10, -3, 3, 11, -2, C_PANI);
	B(m, -3, 10, 2, 3, 11, 3, C_PANI);
	B(m, -3, 10, -2, -2, 11, 2, C_PANI);
	B(m, 2, 10, -2, 3, 11, 2, C_PANI);
	B(m, -2, 10, -2, 1, 10.5f, 1, C_EGGW);
	B(m, -1, 10.5f, -1, 0, 11, 0, C_YOLKY);

	BOTH(dinerHalf(m, s);)
}

// ================================================================== Henhouse Hollow
// A farm in a hedged hollow on a golden afternoon. A big red barn in the middle: ground floor with wide
// doors, a loft along each side wall (slab stairs inside) and hay doors open to the outside, with hay
// blocks to climb in from outside. Each team's half has a chicken coop on stilts, a lookout platform next
// to a silo (the long sightline) and a hay-block maze (close fights). Point-symmetric like the older maps.
static const float C_BARN[3]  = { 0.74f, 0.24f, 0.20f }, C_BROOF[3] = { 0.40f, 0.30f, 0.28f },
                   C_HAY[3]   = { 0.92f, 0.78f, 0.38f }, C_HAY2[3]  = { 0.86f, 0.70f, 0.32f },
                   C_SILO[3]  = { 0.80f, 0.80f, 0.84f }, C_COOP[3]  = { 0.95f, 0.85f, 0.60f },
                   C_FENCE[3] = { 0.80f, 0.66f, 0.46f }, C_LOFT[3]  = { 0.62f, 0.46f, 0.30f };

static void henhouseHalf(Map* m, int s)
{
	// Barn north wall (the turned copy is the south wall): a small door low down, a hay door up high
	P(m, s, -8, 0, 5, -5, 4, 6, C_BARN);
	P(m, s, -3, 0, 5, 8, 4, 6, C_BARN);
	P(m, s, -5, 3, 5, -3, 4, 6, C_BARN);
	P(m, s, -8, 4, 5, -2, 8, 6, C_BARN);
	P(m, s, 2, 4, 5, 8, 8, 6, C_BARN);
	P(m, s, -2, 7, 5, 2, 8, 6, C_BARN);
	// Barn east wall around the big door (the turned copy is the west wall)
	P(m, s, 7, 0, -6, 8, 8, -2, C_BARN);
	P(m, s, 7, 4, -2, 8, 8, 2, C_BARN);
	// Loft (top 4 m) along the north wall, steep slab stairs up to it, a hay block for cover below
	P(m, s, -7, 3, 2, 7, 4, 5, C_LOFT);
	stairsS(m, s, s, 0.0f, -4, -2, 0, 1, 8, 2.0f, 0.5f, 0.5f, C_LOFT);
	P(m, s, -1, 0, -4, 1, 1, -3, C_HAY);
	// Hay blocks outside the hay door: three jumps up and into the loft
	P(m, s, -2, 0, 6, 2, 3, 8, C_HAY2);
	P(m, s, -2, 0, 8, 2, 2, 10, C_HAY);
	P(m, s, -2, 0, 10, 2, 1, 12, C_HAY2);

	// Chicken coop on stilts (you can walk under it), a slab ramp up to its door
	P(m, s, 3, 0, 13, 4, 2, 14, C_WOOD);
	P(m, s, 6, 0, 13, 7, 2, 14, C_WOOD);
	P(m, s, 3, 0, 16, 4, 2, 17, C_WOOD);
	P(m, s, 6, 0, 16, 7, 2, 17, C_WOOD);
	P(m, s, 3, 2, 13, 7, 3, 17, C_WOOD);
	P(m, s, 3, 3, 16, 7, 5, 17, C_COOP);
	P(m, s, 6, 3, 13, 7, 5, 16, C_COOP);
	P(m, s, 3, 3, 13, 4, 5, 16, C_COOP);
	P(m, s, 3, 5, 13, 7, 6, 17, C_ROOF);
	stairsS(m, s, s, 0.0f, 5, 7, 0, 1, 6, 2.0f, 0.5f, 1.0f, C_WOOD);

	// Lookout platform (top 6 m) on four legs, long slab stairs along the edge, and a block silo behind it
	P(m, s, 9, 5, 11, 13, 6, 15, C_WOOD);
	P(m, s, 9, 0, 11, 10, 5, 12, C_TRUNK);
	P(m, s, 12, 0, 11, 13, 5, 12, C_TRUNK);
	P(m, s, 9, 0, 14, 10, 5, 15, C_TRUNK);
	P(m, s, 12, 0, 14, 13, 5, 15, C_TRUNK);
	stairsS(m, s, s, 0.0f, 21, 13, -1, 0, 12, 2.0f, 0.5f, 0.6667f, C_WOOD);
	P(m, s, 15, 0, 16, 19, 10, 20, C_SILO);
	P(m, s, 16, 10, 17, 18, 11, 19, C_IRON);

	// Hay maze in the far corner: walls two blocks high (too high to jump) and a few single blocks to hop
	P(m, s, -21, 0, 10, -15, 2, 11, C_HAY);
	P(m, s, -12, 0, 10, -9, 2, 11, C_HAY2);
	P(m, s, -18, 0, 11, -17, 2, 17, C_HAY2);
	P(m, s, -15, 0, 13, -14, 2, 17, C_HAY);
	P(m, s, -12, 0, 11, -11, 2, 15, C_HAY);
	P(m, s, -21, 0, 17, -17, 2, 18, C_HAY);
	P(m, s, -14, 0, 17, -9, 2, 18, C_HAY2);
	P(m, s, -15, 0, 11, -14, 1, 13, C_HAY);
	P(m, s, -21, 0, 14, -19, 1, 15, C_HAY2);

	// Field: a low fence, a trough and a blocky tree
	P(m, s, 8, 0, 8, 13, 1, 9, C_FENCE);
	P(m, s, -7, 0, 9, -4, 1, 10, C_WOOD);
	P(m, s, 19, 0, 2, 20, 3, 3, C_TRUNK);
	P(m, s, 17, 3, 0, 22, 5, 5, C_LEAF);
	P(m, s, 18, 5, 1, 21, 6, 4, C_LEAF);

	// Spawns (facing the middle) and two free-for-all points
	const int team = s > 0 ? 0 : 1;
	static const float sp[8][3] = {
		{ -6, 0.01f, 20 }, { 0, 0.01f, 20 }, { 5, 0.01f, 19.5f }, { 9, 0.01f, 17 },
		{ -3, 0.01f, 15 }, { -19, 0.01f, 19.5f }, { 11, 0.01f, 20 }, { -8, 0.01f, 13 } };
	for (int i = 0; i < 8; i++) spawnFacing(m, sp[i][0] * s, sp[i][1], sp[i][2] * s, team);
	spawnFacing(m, 20.0f * s, 0.01f, -2.5f * s, 2);
	spawnFacing(m, -3.0f * s, 0.01f, 13.0f * s, 2);
}

static void loadHenhouse(Map* m)
{
	memset(m, 0, sizeof(*m));
	m->name = "Henhouse Hollow";
	m->sky = 6;
	m->blocky = true;
	m->killY = -30.0f;
	m->panoRadius = 42.0f;
	m->panoHeight = 24.0f;

	B(m, -23, -1, -23, 23, 0, 23, C_GRASS);
	perimeter(m, 23, 9.0f, C_HEDGE);                                    // a tall hedgerow: no jump clears it

	// Barn roof: stepped like a gable, ridge running east-west (out of reach from the loft)
	B(m, -8, 8, -6, 8, 9, 6, C_BROOF);
	B(m, -8, 9, -5, 8, 10, 5, C_BROOF);
	B(m, -8, 10, -3, 8, 11, 3, C_BROOF);
	B(m, -8, 11, -1, 8, 12, 1, C_BROOF);

	BOTH(henhouseHalf(m, s);)
}

static const char* const mapNames[MAP_COUNT] = { "Eggworks", "Sunny Side", "Cracked Canyon", "Pan Palace", "Fridge Factory", "Diner Rooftops", "Henhouse Hollow" };
static const char* const mapHelp[MAP_COUNT] = {
	"Sunset factory: reactor pit, towers, catwalk.",
	"Sunny village: brooks, windmill, barn, hills.",
	"Deep canyon: bridges, island, mesas, tunnels.",
	"Night palace: galleries, pool and golden dais.",
	"Cold storage: crate hill, freezer hall, catwalks.",
	"Dusk rooftops: diner, neon pan sign, alleys.",
	"Farm hollow: barn loft, coops, silo, hay maze."
};

const char* map_name(int id)
{
	return (id >= 0 && id < MAP_COUNT) ? mapNames[id] : "?";
}

const char* map_help(int id)
{
	return (id >= 0 && id < MAP_COUNT) ? mapHelp[id] : "";
}

bool map_implemented(int id)
{
	return id >= 0 && id < MAP_COUNT;
}

// ------------------------------------------------------------------ block conversion
// The first four maps were built from free-sized pieces. blockify() turns one into a block map like the
// newer ones: every box snaps to the 1 m grid sideways and to half blocks in height (anything thinner
// becomes one block / one slab thick), round things become chunky block shapes (a plus of two boxes,
// domes as stepped layers), and spawns are set down on whatever surface is now under them.
static float snapH(float v) { return roundf(v * 2.0f) * 0.5f; }

static void snapSpan(float* lo, float* hi, bool half)
{
	float a = half ? snapH(*lo) : roundf(*lo), b = half ? snapH(*hi) : roundf(*hi);
	const float unit = half ? 0.5f : 1.0f;
	if (b - a < unit - 0.01f) {                         // thinner than a block: one block, centred on it
		float c = 0.5f * (*lo + *hi);
		a = half ? floorf(c * 2.0f) * 0.5f : floorf(c);
		b = a + unit;
	}
	*lo = a; *hi = b;
}

// A chunky round footprint: two crossing boxes (or one square column for thin things)
static void blockDisc(Map* m, float x, float z, float y0, float y1, float r, const float c[3])
{
	float cx = roundf(x), cz = roundf(z);
	if (r < 0.75f) {
		float h = r < 0.5f ? 0.5f : 1.0f;
		addBox(m, roundf(x - h), y0, roundf(z - h), roundf(x - h) + 2.0f * h, y1, roundf(z - h) + 2.0f * h, c[0], c[1], c[2]);
		return;
	}
	float R = fmaxf(1.0f, roundf(r)), H = fmaxf(1.0f, roundf(r * 0.6f));
	addBox(m, cx - R, y0, cz - H, cx + R, y1, cz + H, c[0], c[1], c[2]);
	addBox(m, cx - H, y0, cz - R, cx + H, y1, cz + R, c[0], c[1], c[2]);
}

static void blockify(Map* m)
{
	// 1) snap the visible boxes and drop the hidden ones (they only gave round things their collision)
	int n = 0;
	for (int i = 0; i < m->boxCount; i++) {
		Box b = m->boxes[i];
		if (b.hidden) continue;
		snapSpan(&b.min[0], &b.max[0], false);
		snapSpan(&b.min[2], &b.max[2], false);
		snapSpan(&b.min[1], &b.max[1], true);
		m->boxes[n++] = b;
	}
	m->boxCount = n;

	// 2) round things become block shapes
	for (int i = 0; i < m->roundCount; i++) {
		const Round* r = &m->rounds[i];
		float y0 = snapH(r->y0), y1 = snapH(r->y1);
		if (y1 <= y0) y1 = y0 + 0.5f;
		if (r->kind == ROUND_CYL) {
			blockDisc(m, r->x, r->z, y0, y1, 0.5f * (r->r0 + r->r1), r->color);
		} else if (r->kind == ROUND_DOME) {
			int layers = (int)fmaxf(1.0f, fminf(3.0f, roundf((y1 - y0) / 0.5f)));
			float lh = (y1 - y0) / layers;
			for (int k = 0; k < layers; k++)
				blockDisc(m, r->x, r->z, snapH(y0 + k * lh), snapH(y0 + (k + 1) * lh), r->r0 * (1.0f - 0.3f * k), r->color);
		} else {                                         // ball: a fat plus, with a smaller cap on top
			float mid = snapH(0.5f * (y0 + y1));
			if (mid <= y0) mid = y0 + 0.5f;
			blockDisc(m, r->x, r->z, y0, mid, r->r0, r->color);
			if (y1 > mid) blockDisc(m, r->x, r->z, mid, y1, r->r0 * 0.7f, r->color);
		}
	}
	m->roundCount = 0;

	// 3) set spawns down on the surface under them
	for (int s = 0; s < m->spawnCount; s++) {
		Spawn* sp = &m->spawns[s];
		float best = -1.0e9f;
		for (int i = 0; i < m->boxCount; i++) {
			const Box* b = &m->boxes[i];
			if (sp->x < b->min[0] || sp->x > b->max[0] || sp->z < b->min[2] || sp->z > b->max[2]) continue;
			if (b->max[1] <= sp->y + 1.0f && b->max[1] > best) best = b->max[1];
		}
		if (best > -1.0e8f) sp->y = best + 0.01f;
	}
	m->blocky = true;
}

bool map_load(Map* m, int id)
{
	switch (id) {
	case 0: loadEggworks(m); blockify(m); return true;
	case 1: loadSunny(m);   blockify(m); return true;
	case 2: loadCanyon(m);  blockify(m); return true;
	case 3: loadPalace(m);  blockify(m); return true;
	case 4: loadFridge(m);  return true;
	case 5: loadDiner(m);   return true;
	case 6: loadHenhouse(m); return true;
	default: return false;
	}
}
