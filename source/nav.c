#include "nav.h"
#include "collision.h"
#include "config.h"
#include "rng.h"
#include <math.h>
#include <string.h>

#define MAX_DROP    5.5f
#define MAX_JUMP_UP 1.25f

static int cellIndex(const Nav* n, int cx, int cz) { return cz * n->gridW + cx; }

static bool pathClear(const Map* map, const NavNode* a, const NavNode* b)
{
	float dh = b->y - a->y;
	bool jump = dh > STEP_HEIGHT + 0.01f;
	const float ts[3] = { 0.25f, 0.5f, 0.75f };
	for (int k = 0; k < 3; k++) {
		float t = ts[k];
		float x = a->x + (b->x - a->x) * t, z = a->z + (b->z - a->z) * t;
		float y;
		if (t < 0.4f) y = a->y;
		else if (t > 0.6f) y = b->y;
		else {
			if (jump) continue;                       // the jump arcs over the obstacle
			y = dh >= 0.0f ? b->y : a->y;
		}
		if (collision_blocked(map, x, y + 0.02f, z)) return false;
	}
	return true;
}

static void addLink(NavNode* a, int target, bool jump)
{
	if (a->nbrCount >= NAV_MAX_LINKS) return;
	if (jump) a->jumpMask |= (unsigned char)(1u << a->nbrCount);
	a->nbr[a->nbrCount++] = (short)target;
}

void nav_build(Nav* nav, const Map* map)
{
	memset(nav, 0, sizeof(*nav));

	float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
	for (int i = 0; i < map->boxCount; i++) {
		const Box* b = &map->boxes[i];
		if (b->min[0] < minX) minX = b->min[0];
		if (b->max[0] > maxX) maxX = b->max[0];
		if (b->min[2] < minZ) minZ = b->min[2];
		if (b->max[2] > maxZ) maxZ = b->max[2];
	}
	nav->minX = minX;
	nav->minZ = minZ;
	nav->gridW = (int)ceilf((maxX - minX) / NAV_CELL);
	nav->gridH = (int)ceilf((maxZ - minZ) / NAV_CELL);
	if (nav->gridW > NAV_GRID_MAX) nav->gridW = NAV_GRID_MAX;
	if (nav->gridH > NAV_GRID_MAX) nav->gridH = NAV_GRID_MAX;

	// Standable points: the top of every box under the cell centre with room above it
	for (int cz = 0; cz < nav->gridH; cz++) {
		for (int cx = 0; cx < nav->gridW; cx++) {
			int cell = cellIndex(nav, cx, cz);
			nav->cellStart[cell] = (short)nav->nodeCount;
			float x = minX + (cx + 0.5f) * NAV_CELL, z = minZ + (cz + 0.5f) * NAV_CELL;

			float tops[8];
			int nt = 0;
			for (int i = 0; i < map->boxCount; i++) {
				const Box* b = &map->boxes[i];
				if (x < b->min[0] || x > b->max[0] || z < b->min[2] || z > b->max[2]) continue;
				float top = b->max[1];
				if (top < -8.0f || top > 12.0f) continue;
				bool dup = false;
				for (int k = 0; k < nt; k++) if (fabsf(tops[k] - top) < 0.05f) dup = true;
				if (!dup && nt < 8) tops[nt++] = top;
			}
			for (int k = 0; k < nt && nav->nodeCount < NAV_MAX_NODES; k++) {
				float y = tops[k];
				if (collision_blocked(map, x, y + 0.02f, z)) continue;
				if (collision_ground_height(map, x, z, y + 0.02f) < y - 0.05f) continue;
				NavNode* n = &nav->nodes[nav->nodeCount++];
				n->x = x; n->y = y; n->z = z;
				nav->cellCount[cell]++;
			}
		}
	}

	// Links to the 8 surrounding cells
	for (int cz = 0; cz < nav->gridH; cz++) {
		for (int cx = 0; cx < nav->gridW; cx++) {
			int cell = cellIndex(nav, cx, cz);
			for (int a = 0; a < nav->cellCount[cell]; a++) {
				int ai = nav->cellStart[cell] + a;
				NavNode* na = &nav->nodes[ai];
				for (int dz = -1; dz <= 1; dz++) {
					for (int dx = -1; dx <= 1; dx++) {
						if (!dx && !dz) continue;
						int ncx = cx + dx, ncz = cz + dz;
						if (ncx < 0 || ncz < 0 || ncx >= nav->gridW || ncz >= nav->gridH) continue;
						int ncell = cellIndex(nav, ncx, ncz);
						for (int b = 0; b < nav->cellCount[ncell]; b++) {
							int bi = nav->cellStart[ncell] + b;
							const NavNode* nb = &nav->nodes[bi];
							float dh = nb->y - na->y;
							if (dh > MAX_JUMP_UP || dh < -MAX_DROP) continue;
							// diagonal moves must also have both side cells passable
							if (dx && dz) {
								NavNode s1 = *na, s2 = *na;
								s1.x = na->x + dx * NAV_CELL; s2.z = na->z + dz * NAV_CELL;
								if (collision_blocked(map, s1.x, na->y + 0.02f, s1.z) ||
								    collision_blocked(map, s2.x, na->y + 0.02f, s2.z)) continue;
							}
							if (pathClear(map, na, nb)) addLink(na, bi, dh > STEP_HEIGHT + 0.01f);
						}
					}
				}
			}
		}
	}

	// Mark everything reachable from the spawn points
	static short queue[NAV_MAX_NODES];
	int qh = 0, qt = 0;
	for (int s = 0; s < map->spawnCount; s++) {
		int best = -1;
		float bd = 1e9f;
		for (int i = 0; i < nav->nodeCount; i++) {
			const NavNode* n = &nav->nodes[i];
			float d = fabsf(n->x - map->spawns[s].x) + fabsf(n->z - map->spawns[s].z) + fabsf(n->y - map->spawns[s].y) * 4.0f;
			if (d < bd) { bd = d; best = i; }
		}
		if (best >= 0 && !nav->nodes[best].reachable) { nav->nodes[best].reachable = true; queue[qt++] = (short)best; }
	}
	while (qh < qt) {
		const NavNode* n = &nav->nodes[queue[qh++]];
		for (int k = 0; k < n->nbrCount; k++) {
			NavNode* m = &nav->nodes[n->nbr[k]];
			if (!m->reachable) { m->reachable = true; queue[qt++] = n->nbr[k]; }
		}
	}
}

int nav_nearest(const Nav* nav, float x, float y, float z)
{
	int cx = (int)floorf((x - nav->minX) / NAV_CELL);
	int cz = (int)floorf((z - nav->minZ) / NAV_CELL);

	// First pass only accepts nodes at a height the body can actually reach (not the top of
	// a staircase it is standing beside); the second pass accepts anything.
	for (int pass = 0; pass < 2; pass++) {
		float maxDy = pass == 0 ? 1.3f : 1.0e9f;
		int best = -1;
		float bd = 1.0e9f;
		for (int r = 0; r <= 3 && best < 0; r++) {
			for (int dz = -r; dz <= r; dz++) {
				for (int dx = -r; dx <= r; dx++) {
					int ncx = cx + dx, ncz = cz + dz;
					if (ncx < 0 || ncz < 0 || ncx >= nav->gridW || ncz >= nav->gridH) continue;
					int cell = cellIndex(nav, ncx, ncz);
					for (int k = 0; k < nav->cellCount[cell]; k++) {
						int i = nav->cellStart[cell] + k;
						const NavNode* n = &nav->nodes[i];
						if (!n->reachable) continue;
						if (fabsf(n->y - y) > maxDy) continue;
						float ex = n->x - x, ez = n->z - z, ey = (n->y - y) * 2.5f;
						float d = ex * ex + ez * ez + ey * ey;
						if (d < bd) { bd = d; best = i; }
					}
				}
			}
		}
		if (best >= 0) return best;
	}
	return -1;
}

int nav_random_node(const Nav* nav)
{
	if (nav->nodeCount <= 0) return -1;
	for (int tries = 0; tries < 24; tries++) {
		int i = (int)(rng_float() * nav->nodeCount);
		if (i >= nav->nodeCount) i = nav->nodeCount - 1;
		if (nav->nodes[i].reachable) return i;
	}
	return -1;
}

// ---- A* ----
typedef struct { float f; short node; } HeapItem;

static float gScore[NAV_MAX_NODES];
static short parentOf[NAV_MAX_NODES];
static unsigned char nodeState[NAV_MAX_NODES];   // 0 unseen, 1 open, 2 closed
#define HEAP_CAP (NAV_MAX_NODES * 4)
static HeapItem heap[HEAP_CAP];
static int heapN;

static void heapPush(float f, int node)
{
	if (heapN >= HEAP_CAP) return;
	int i = heapN++;
	heap[i].f = f; heap[i].node = (short)node;
	while (i > 0) {
		int p = (i - 1) / 2;
		if (heap[p].f <= heap[i].f) break;
		HeapItem t = heap[p]; heap[p] = heap[i]; heap[i] = t;
		i = p;
	}
}

static HeapItem heapPop(void)
{
	HeapItem top = heap[0];
	heap[0] = heap[--heapN];
	int i = 0;
	for (;;) {
		int l = 2 * i + 1, r = l + 1, m = i;
		if (l < heapN && heap[l].f < heap[m].f) m = l;
		if (r < heapN && heap[r].f < heap[m].f) m = r;
		if (m == i) break;
		HeapItem t = heap[m]; heap[m] = heap[i]; heap[i] = t;
		i = m;
	}
	return top;
}

int nav_path(const Nav* nav, int start, int goal, short* out, int maxLen)
{
	if (start < 0 || goal < 0 || start >= nav->nodeCount || goal >= nav->nodeCount) return 0;
	if (start == goal) { out[0] = (short)start; return 1; }

	memset(nodeState, 0, (size_t)nav->nodeCount);
	heapN = 0;
	gScore[start] = 0.0f;
	parentOf[start] = -1;
	nodeState[start] = 1;
	const NavNode* gn = &nav->nodes[goal];
	{
		const NavNode* s = &nav->nodes[start];
		heapPush(sqrtf((s->x - gn->x) * (s->x - gn->x) + (s->z - gn->z) * (s->z - gn->z)), start);
	}

	bool found = false;
	while (heapN > 0) {
		HeapItem it = heapPop();
		int cur = it.node;
		if (nodeState[cur] == 2) continue;
		nodeState[cur] = 2;
		if (cur == goal) { found = true; break; }

		const NavNode* cn = &nav->nodes[cur];
		for (int k = 0; k < cn->nbrCount; k++) {
			int nb = cn->nbr[k];
			if (nodeState[nb] == 2) continue;
			const NavNode* nn = &nav->nodes[nb];
			float dx = nn->x - cn->x, dz = nn->z - cn->z, dy = nn->y - cn->y;
			float cost = sqrtf(dx * dx + dz * dz);
			if (cn->jumpMask & (1u << k)) cost += 2.0f;
			if (dy < -1.0f) cost += 0.5f;
			float g = gScore[cur] + cost;
			if (nodeState[nb] == 0 || g < gScore[nb]) {
				gScore[nb] = g;
				parentOf[nb] = (short)cur;
				nodeState[nb] = 1;
				float hx = nn->x - gn->x, hz = nn->z - gn->z;
				heapPush(g + sqrtf(hx * hx + hz * hz), nb);
			}
		}
	}
	if (!found) return 0;

	// Walk back from the goal, then reverse into out
	static short rev[NAV_MAX_NODES];
	int n = 0;
	for (int c = goal; c >= 0 && n < NAV_MAX_NODES; c = parentOf[c]) rev[n++] = (short)c;
	if (n > maxLen) return 0;
	for (int i = 0; i < n; i++) out[i] = rev[n - 1 - i];
	return n;
}
