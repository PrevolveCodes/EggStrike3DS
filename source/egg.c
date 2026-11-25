#include "egg.h"
#include "config.h"
#include "renderer.h"
#include <math.h>

#define RINGS 13
#define SEGS  12
#define TRI_COUNT (SEGS * (2 + (RINGS - 2) * 2))
#define PI_F 3.14159265f

static float grid[RINGS + 1][SEGS][3];
static float gridShade[RINGS + 1][SEGS];
static float gridSpec[RINGS + 1][SEGS];

static float triPos[TRI_COUNT][3][3];
static float triShade[TRI_COUNT][3];
static float triSpec[TRI_COUNT][3];
static int built;

// Egg outline measured from a Shell Shockers egg: a flat-ish belly, widest a bit below the middle,
// narrowing to a rounded top. t = height from the bottom (0..1), hw = half width / widest half width.
static const float ringT[RINGS + 1]  = { 0.00f, 0.02f, 0.05f, 0.12f, 0.22f, 0.35f, 0.47f, 0.58f, 0.70f, 0.80f, 0.88f, 0.95f, 0.98f, 1.00f };
static const float ringHW[RINGS + 1] = { 0.00f, 0.33f, 0.55f, 0.77f, 0.92f, 0.98f, 1.00f, 0.975f, 0.86f, 0.71f, 0.56f, 0.34f, 0.21f, 0.00f };

static void normalize3(float v[3])
{
	float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (l > 1.0e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

static void buildGrid(void)
{
	for (int i = 0; i <= RINGS; i++) {
		float y = TARGET_HEIGHT * ringT[i];
		float r = ringHW[i] * EGG_HALF_W;
		for (int j = 0; j < SEGS; j++) {
			float a = 2.0f * PI_F * (float)j / SEGS;
			grid[i][j][0] = r * cosf(a);
			grid[i][j][1] = y;
			grid[i][j][2] = r * sinf(a);
		}
	}

	// Light from the upper left front; viewer roughly in front and slightly above
	float L[3] = { -0.45f, 0.75f, 0.50f };
	normalize3(L);
	float V[3] = { 0.0f, 0.35f, 1.0f };
	normalize3(V);
	float H[3] = { L[0] + V[0], L[1] + V[1], L[2] + V[2] };
	normalize3(H);

	for (int i = 0; i <= RINGS; i++) {
		for (int j = 0; j < SEGS; j++) {
			float n[3];
			if (i == 0)          { n[0] = 0; n[1] = -1; n[2] = 0; }
			else if (i == RINGS) { n[0] = 0; n[1] =  1; n[2] = 0; }
			else {
				int jp = (j + 1) % SEGS, jm = (j + SEGS - 1) % SEGS;
				float ts[3], tr[3];
				for (int k = 0; k < 3; k++) {
					ts[k] = grid[i][jp][k] - grid[i][jm][k];
					tr[k] = grid[i + 1][j][k] - grid[i - 1][j][k];
				}
				n[0] = tr[1] * ts[2] - tr[2] * ts[1];
				n[1] = tr[2] * ts[0] - tr[0] * ts[2];
				n[2] = tr[0] * ts[1] - tr[1] * ts[0];
				normalize3(n);
				if (n[0] * grid[i][j][0] + n[2] * grid[i][j][2] < 0.0f) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }
			}
			float d = n[0] * L[0] + n[1] * L[1] + n[2] * L[2];
			if (d < 0.0f) d = 0.0f;
			float shade = 0.46f + 0.54f * d;
			if (n[1] < 0.0f) shade *= 0.85f + 0.15f * (1.0f + n[1]);   // slightly darker underneath
			float h = n[0] * H[0] + n[1] * H[1] + n[2] * H[2];
			float spec = h > 0.0f ? powf(h, 28.0f) * 0.30f : 0.0f;
			gridShade[i][j] = shade;
			gridSpec[i][j] = spec;
		}
	}
}

static void build(void)
{
	buildGrid();
	int n = 0;

	#define CORNER(slot, I, J) do { \
		int jj = (J) % SEGS; \
		for (int k = 0; k < 3; k++) triPos[n][slot][k] = grid[I][jj][k]; \
		triShade[n][slot] = gridShade[I][jj]; \
		triSpec[n][slot] = gridSpec[I][jj]; } while (0)

	for (int i = 0; i < RINGS; i++) {
		for (int j = 0; j < SEGS; j++) {
			if (i == 0) {
				CORNER(0, 0, j); CORNER(1, 1, j); CORNER(2, 1, j + 1); n++;
			} else if (i == RINGS - 1) {
				CORNER(0, i, j); CORNER(1, i, j + 1); CORNER(2, i + 1, j); n++;
			} else {
				CORNER(0, i, j); CORNER(1, i, j + 1); CORNER(2, i + 1, j + 1); n++;
				CORNER(0, i, j); CORNER(1, i + 1, j + 1); CORNER(2, i + 1, j); n++;
			}
		}
	}
	#undef CORNER
	built = n;
}

int egg_triangle_count(void)
{
	if (!built) build();
	return built;
}

void egg_vertex(int tri, int corner, float pos[3], float* shade, float* spec)
{
	if (!built) build();
	for (int k = 0; k < 3; k++) pos[k] = triPos[tri][corner][k];
	*shade = triShade[tri][corner];
	*spec = triSpec[tri][corner];
}

void egg_draw(float x, float y, float z, float r, float g, float b)
{
	if (!built) build();

	for (int t = 0; t < built; t++) {
		float p[3][3], c[3][3];
		for (int v = 0; v < 3; v++) {
			p[v][0] = x + triPos[t][v][0];
			p[v][1] = y + triPos[t][v][1];
			p[v][2] = z + triPos[t][v][2];
			float s = triShade[t][v], sp = triSpec[t][v];
			float cr = r * s + sp, cg = g * s + sp, cb = b * s + sp;
			c[v][0] = cr > 1.0f ? 1.0f : cr;
			c[v][1] = cg > 1.0f ? 1.0f : cg;
			c[v][2] = cb > 1.0f ? 1.0f : cb;
		}
		renderer_world_tri(p, c);
	}
}

void egg_outline(float x, float y, float z, float thick, float r, float g, float b)
{
	if (!built) build();
	const float cy = TARGET_HEIGHT * 0.5f;                // scale about the middle of the egg
	const float k = 1.0f + thick / EGG_HALF_W;
	const float col[3][3] = { { r, g, b }, { r, g, b }, { r, g, b } };
	for (int t = 0; t < built; t++) {
		float p[3][3];
		for (int v = 0; v < 3; v++) {
			p[v][0] = x + triPos[t][v][0] * k;
			p[v][1] = y + cy + (triPos[t][v][1] - cy) * k;
			p[v][2] = z + triPos[t][v][2] * k;
		}
		renderer_outline_tri(p, col);
	}
}
