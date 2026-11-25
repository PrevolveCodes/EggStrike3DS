#include "surface.h"
#include "sky.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

// Faces: 0 top (+y), 1 bottom (-y), 2 +z, 3 -z, 4 +x, 5 -x.
// Face coordinates (u, v): top/bottom (x, z); +-z faces (x, y); +-x faces (z, y).
// All patterns work in world coordinates so they line up across neighbouring boxes.

typedef struct {
	SurfVertex* out;
	int n, cap;
	bool countOnly;
	const Box* box;
	int face;
	float u0, u1, v0, v1;       // the face rectangle
	float base[3];
	float detail;               // >= 1, bigger = coarser patterns
	float light[6][3];          // colour multiplier per face direction (sun side warm and bright, shade side cool)
	float amb[3], sunCol[3], L[3];   // the same lighting, for curved surfaces that need it per vertex
} Sf;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static float hash2(int i, int j, int s)
{
	unsigned h = (unsigned)i * 374761393u + (unsigned)j * 668265263u + (unsigned)s * 2246822519u;
	h = (h ^ (h >> 13)) * 1274126177u;
	h ^= h >> 16;
	return (float)(h & 0xFFFFu) / 65535.0f;
}

// ------------------------------------------------------------------ output
// Lighting is baked into the vertex colours from the map's sky: a sun (or moon) in the sky's own
// direction lights the faces turned towards it, and every other face gets a cooler ambient colour.
// That gives each map a bright side and a shadow side, so shapes read as solid instead of flat.
typedef struct { float amb[3]; float sunK; } LightLook;
static const LightLook looks[SKY_PRESETS] = {
	{ { 0.65f, 0.57f, 0.70f }, 0.78f },   // sunset: purple shadows, orange light
	{ { 0.66f, 0.70f, 0.80f }, 0.40f },   // noon
	{ { 0.70f, 0.67f, 0.74f }, 0.58f },   // desert afternoon
	{ { 0.34f, 0.38f, 0.62f }, 0.50f },   // night: blue shadows, pale moonlight
	{ { 0.74f, 0.80f, 0.92f }, 0.42f },   // frosty morning: cool bright shadows, soft low sun
	{ { 0.62f, 0.52f, 0.74f }, 0.62f },   // neon dusk: violet shadows, warm low sun
	{ { 0.70f, 0.66f, 0.66f }, 0.62f },   // harvest afternoon: warm golden light, soft shadows
};

static void setupLight(Sf* s, int skyId)
{
	if (skyId < 0 || skyId >= SKY_PRESETS) skyId = 0;
	const SkyDef* sk = sky_preset(skyId);
	const LightLook* lk = &looks[skyId];
	float ce = cosf(sk->sunEl);
	float L[3] = { sinf(sk->sunAz) * ce, sinf(sk->sunEl), -cosf(sk->sunAz) * ce };   // towards the sun
	for (int c = 0; c < 3; c++) { s->amb[c] = lk->amb[c]; s->sunCol[c] = sk->sunColor[c] * lk->sunK; s->L[c] = L[c]; }
	static const float N[6][3] = { { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 }, { -1, 0, 0 } };
	for (int f = 0; f < 6; f++) {
		float d = N[f][0] * L[0] + N[f][1] * L[1] + N[f][2] * L[2];
		if (d < 0.0f) d = 0.0f;
		float amb = (f == 1) ? 0.55f : 1.0f;                       // undersides are dark
		for (int c = 0; c < 3; c++) s->light[f][c] = clampf(lk->amb[c] * amb + sk->sunColor[c] * lk->sunK * d, 0.0f, 1.12f);
	}
}

static void corner(Sf* s, float u, float v, const float col[3], SurfVertex* o)
{
	float p = 0.0f;
	const Box* b = s->box;
	switch (s->face) {
	case 0: p = b->max[1]; break;
	case 1: p = b->min[1]; break;
	case 2: p = b->max[2]; break;
	case 3: p = b->min[2]; break;
	case 4: p = b->max[0]; break;
	default: p = b->min[0]; break;
	}
	if (s->face < 2)       { o->x = u; o->y = p; o->z = v; }
	else if (s->face < 4)  { o->x = u; o->y = v; o->z = p; }
	else                   { o->x = p; o->y = v; o->z = u; }

	// height of this vertex in the world
	float y = (s->face < 2) ? p : v;
	// things deep below the rim (canyon floor, brook beds) sit in shadow
	float depthK = 1.0f;
	if (y < 0.0f) depthK = 0.62f + 0.38f * clampf(1.0f + y / 9.0f, 0.0f, 1.0f);
	// walls get darker towards their foot (contact shadow), and a touch lighter at the very top edge
	float ao = 1.0f;
	if (s->face >= 2) {
		float rise = v - s->v0;                          // metres above the bottom of this face
		ao = 0.68f + 0.32f * clampf(rise / 2.6f, 0.0f, 1.0f);
		float toTop = s->v1 - v;
		if (toTop < 0.6f) ao *= 1.0f + 0.07f * (1.0f - toTop / 0.6f);
	}
	float k = ao * depthK;
	o->r = clampf(col[0] * s->light[s->face][0] * k, 0.0f, 1.0f);
	o->g = clampf(col[1] * s->light[s->face][1] * k, 0.0f, 1.0f);
	o->b = clampf(col[2] * s->light[s->face][2] * k, 0.0f, 1.0f);
}

// Clip a rectangle to the face. False when nothing is left.
static bool clip(const Sf* s, float* u0, float* v0, float* u1, float* v1)
{
	if (*u0 < s->u0) *u0 = s->u0;
	if (*v0 < s->v0) *v0 = s->v0;
	if (*u1 > s->u1) *u1 = s->u1;
	if (*v1 > s->v1) *v1 = s->v1;
	return (*u1 - *u0) > 0.003f && (*v1 - *v0) > 0.003f;
}

// Four corner colours: (u0,v0) (u1,v0) (u1,v1) (u0,v1)
static void quad4(Sf* s, float u0, float v0, float u1, float v1, const float c[4][3])
{
	if (s->countOnly) { s->n += 6; return; }
	if (s->n + 6 > s->cap) return;
	SurfVertex q[4];
	corner(s, u0, v0, c[0], &q[0]);
	corner(s, u1, v0, c[1], &q[1]);
	corner(s, u1, v1, c[2], &q[2]);
	corner(s, u0, v1, c[3], &q[3]);
	s->out[s->n++] = q[0]; s->out[s->n++] = q[1]; s->out[s->n++] = q[2];
	s->out[s->n++] = q[0]; s->out[s->n++] = q[2]; s->out[s->n++] = q[3];
}

static void flat(Sf* s, float u0, float v0, float u1, float v1, const float c[3])
{
	if (!clip(s, &u0, &v0, &u1, &v1)) return;
	float cc[4][3];
	for (int i = 0; i < 4; i++) memcpy(cc[i], c, sizeof(float) * 3);
	quad4(s, u0, v0, u1, v1, cc);
}

// base colour scaled by f, with a warm/cool shift (w > 0 makes it warmer/yellower)
static void tone(const Sf* s, float f, float w, float out[3])
{
	out[0] = clampf(s->base[0] * (f + w), 0.0f, 1.0f);
	out[1] = clampf(s->base[1] * f, 0.0f, 1.0f);
	out[2] = clampf(s->base[2] * (f - w), 0.0f, 1.0f);
}

// ------------------------------------------------------------------ patterns
// Flat-coloured cells of cu x cv, each with its own random brightness (and optional checker).
static void gridFlat(Sf* s, float u0, float u1, float v0, float v1, float cu, float cv,
                     float var, float warm, float parity, int seed)
{
	cu *= s->detail; cv *= s->detail;
	int i0 = (int)floorf(u0 / cu), i1 = (int)ceilf(u1 / cu);
	int j0 = (int)floorf(v0 / cv), j1 = (int)ceilf(v1 / cv);
	for (int j = j0; j < j1; j++)
		for (int i = i0; i < i1; i++) {
			float n = hash2(i, j, seed);
			float f = 1.0f + var * (2.0f * n - 1.0f) - parity * (float)((i + j) & 1);
			float c[3];
			tone(s, f, warm * (n - 0.5f), c);
			flat(s, fmaxf(u0, i * cu), fmaxf(v0, j * cv), fminf(u1, (i + 1) * cu), fminf(v1, (j + 1) * cv), c);
		}
}

static float vnoise(float u, float v, float cu, float cv, int seed)
{
	float x = u / cu, y = v / cv;
	int i = (int)floorf(x), j = (int)floorf(y);
	float fx = x - i, fy = y - j;
	float a = hash2(i, j, seed), b = hash2(i + 1, j, seed), c = hash2(i, j + 1, seed), d = hash2(i + 1, j + 1, seed);
	return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}

// Smooth blotchy colour (every corner has its own colour, so cells blend into each other).
static void gridSmooth(Sf* s, float cu, float cv, float var, float warm, int seed)
{
	cu *= s->detail; cv *= s->detail;
	int i0 = (int)floorf(s->u0 / cu), i1 = (int)ceilf(s->u1 / cu);
	int j0 = (int)floorf(s->v0 / cv), j1 = (int)ceilf(s->v1 / cv);
	for (int j = j0; j < j1; j++)
		for (int i = i0; i < i1; i++) {
			float u0 = fmaxf(s->u0, i * cu), u1 = fminf(s->u1, (i + 1) * cu);
			float v0 = fmaxf(s->v0, j * cv), v1 = fminf(s->v1, (j + 1) * cv);
			if (!clip(s, &u0, &v0, &u1, &v1)) continue;
			float c[4][3];
			const float us[4] = { u0, u1, u1, u0 }, vs[4] = { v0, v0, v1, v1 };
			for (int k = 0; k < 4; k++) {
				float n = vnoise(us[k], vs[k], cu, cv, seed);
				tone(s, 1.0f + var * (2.0f * n - 1.0f), warm * (n - 0.5f), c[k]);
			}
			quad4(s, u0, v0, u1, v1, c);
		}
}

// Bricks / blocks / planks / tiles: rows of bw x bh blocks with thin mortar lines in between.
// stag: 0 aligned, 1 every other row shifted by half, 2 random shift per row.
static void blocks(Sf* s, float u0, float u1, float v0, float v1, float ou, float ov,
                   float bw, float bh, float mw, int stag, float var, float mortarF,
                   float parity, int seed)
{
	bw *= s->detail; bh *= s->detail;
	float mortar[3];
	tone(s, mortarF, 0.0f, mortar);
	int j0 = (int)floorf((v0 - ov) / bh), j1 = (int)ceilf((v1 - ov) / bh);
	for (int j = j0; j < j1; j++) {
		float r0 = ov + j * bh, r1 = r0 + bh;
		float off = stag == 0 ? 0.0f : (stag == 1 ? (float)(j & 1) * bw * 0.5f : hash2(j, 3, seed) * bw);
		if (mw > 0.0f) flat(s, u0, fmaxf(v0, r1 - mw), u1, fminf(v1, r1), mortar);
		int i0 = (int)floorf((u0 - ou - off) / bw), i1 = (int)ceilf((u1 - ou - off) / bw);
		for (int i = i0; i < i1; i++) {
			float b0 = ou + off + i * bw, b1 = b0 + bw;
			float n = hash2(i, j, seed + 7);
			float f = 1.0f + var * (2.0f * n - 1.0f) - parity * (float)((i + j) & 1);
			float c[3];
			tone(s, f, 0.0f, c);
			float bu0 = fmaxf(u0, b0), bu1 = fminf(u1, b1 - mw);
			float bv0 = fmaxf(v0, r0), bv1 = fminf(v1, r1 - mw);
			if (bu1 > bu0 && bv1 > bv0) flat(s, bu0, bv0, bu1, bv1, c);
			if (mw > 0.0f) flat(s, fmaxf(u0, b1 - mw), fmaxf(v0, r0), fminf(u1, b1), fminf(v1, r1 - mw), mortar);
		}
	}
}

// Crate: dark frame around the edge, planks inside.
static void crate(Sf* s, float var)
{
	float fw = clampf(0.10f * s->detail, 0.05f, 0.35f);
	float W = s->u1 - s->u0, H = s->v1 - s->v0;
	if (fw * 2.5f > W || fw * 2.5f > H) fw = fminf(W, H) * 0.2f;
	float fc[3];
	tone(s, 0.60f, 0.0f, fc);
	flat(s, s->u0, s->v0, s->u1, s->v0 + fw, fc);
	flat(s, s->u0, s->v1 - fw, s->u1, s->v1, fc);
	flat(s, s->u0, s->v0 + fw, s->u0 + fw, s->v1 - fw, fc);
	flat(s, s->u1 - fw, s->v0 + fw, s->u1, s->v1 - fw, fc);
	float save = s->detail; s->detail = 1.0f;
	blocks(s, s->u0 + fw, s->u1 - fw, s->v0 + fw, s->v1 - fw, s->u0 + fw, s->v0 + fw,
	       W * 2.0f, fmaxf(0.2f, H / floorf(fmaxf(1.0f, H / 0.22f))), 0.014f, 0, var, 0.55f, 0.0f, 11);
	s->detail = save;
}

static bool smallFace(const Sf* s)
{
	float W = s->u1 - s->u0, H = s->v1 - s->v0;
	return W * H < 3.0f && (W < 1.2f || H < 1.2f);
}

static void paint(Sf* s, int mat, bool top)
{
	const Box* b = s->box;
	float W = s->u1 - s->u0, H = s->v1 - s->v0;
	float dx = b->max[0] - b->min[0], dy = b->max[1] - b->min[1], dz = b->max[2] - b->min[2];

	if (mat == MAT_WOOD && fmaxf(dx, fmaxf(dy, dz)) <= 3.6f) mat = MAT_CRATE;
	bool tiny = smallFace(s);

	switch (mat) {
	case MAT_LAWN:
		if (top) gridSmooth(s, 2.2f, 2.2f, 0.12f, 0.10f, 5);
		else     gridFlat(s, s->u0, s->u1, s->v0, s->v1, 1.0f, 0.7f, 0.10f, 0.0f, 0.0f, 6);
		break;
	case MAT_SAND:
		if (top) gridSmooth(s, 2.6f, 1.9f, 0.07f, 0.06f, 8);
		else     gridFlat(s, s->u0, s->u1, s->v0, s->v1, 1.5f, 0.8f, 0.07f, 0.0f, 0.0f, 9);
		break;
	case MAT_ROCK:
		if (top) gridSmooth(s, 1.6f, 1.6f, 0.11f, 0.08f, 12);
		else     blocks(s, s->u0, s->u1, s->v0, s->v1, 0.0f, 0.0f, 2.6f, 0.75f, 0.0f, 2, 0.11f, 1.0f, 0.0f, 13);
		break;
	case MAT_PLASTER:
		if (top || tiny) { gridFlat(s, s->u0, s->u1, s->v0, s->v1, 2.0f, 2.0f, 0.03f, 0.0f, 0.0f, 3); break; }
		{
			float v0 = s->v0, v1 = s->v1, c[3];
			if (H > 1.6f) {
				if (v0 <= 0.05f) { tone(s, 0.70f, 0.0f, c); flat(s, s->u0, v0, s->u1, v0 + 0.35f, c); v0 += 0.35f; }
				tone(s, 1.07f, 0.0f, c); flat(s, s->u0, v1 - 0.18f, s->u1, v1, c); v1 -= 0.18f;
			}
			gridFlat(s, s->u0, s->u1, v0, v1, 1.3f, 1.1f, 0.04f, 0.0f, 0.0f, 4);
		}
		break;
	case MAT_PANEL:
		if (top || tiny) gridFlat(s, s->u0, s->u1, s->v0, s->v1, 2.0f, 2.0f, 0.04f, 0.0f, 0.0f, 14);
		else blocks(s, s->u0, s->u1, s->v0, s->v1, 0.0f, 0.0f, 4.0f, 2.0f, 0.07f, 1, 0.05f, 0.70f, 0.0f, 15);
		break;
	case MAT_STONE:
		if (tiny) gridFlat(s, s->u0, s->u1, s->v0, s->v1, 1.0f, 1.0f, 0.06f, 0.0f, 0.0f, 16);
		else if (top) blocks(s, s->u0, s->u1, s->v0, s->v1, 0.0f, 0.0f, 1.5f, 1.5f, 0.05f, 0, 0.05f, 0.76f, 0.05f, 17);
		else blocks(s, s->u0, s->u1, s->v0, s->v1, 0.0f, 0.0f, 1.2f, 0.6f, 0.04f, 1, 0.07f, 0.74f, 0.0f, 18);
		break;
	case MAT_WOOD:
		if (tiny) gridFlat(s, s->u0, s->u1, s->v0, s->v1, 1.0f, 0.3f, 0.07f, 0.0f, 0.0f, 19);
		else if (top && dx > dz) blocks(s, s->u0, s->u1, s->v0, s->v1, 0.0f, 0.0f, 2.4f, 0.28f, 0.02f, 2, 0.08f, 0.55f, 0.0f, 20);
		else blocks(s, s->u0, s->u1, s->v0, s->v1, 0.0f, 0.0f, 2.4f, 0.28f, 0.02f, 2, 0.08f, 0.55f, 0.0f, 21);
		break;
	case MAT_CRATE:
		crate(s, 0.07f);
		break;
	case MAT_ROOF:
		if (top) blocks(s, s->u0, s->u1, s->v0, s->v1, 0.0f, 0.0f, 1.0f, 0.55f, 0.05f, 1, 0.09f, 0.62f, 0.0f, 22);
		else gridFlat(s, s->u0, s->u1, s->v0, s->v1, 2.0f, 0.4f, 0.04f, 0.0f, 0.0f, 23);
		break;
	case MAT_HEDGE:
		gridFlat(s, s->u0, s->u1, s->v0, s->v1, 0.5f, 0.5f, 0.20f, 0.10f, 0.0f, 24);
		break;
	case MAT_BARK:
		if (top) gridFlat(s, s->u0, s->u1, s->v0, s->v1, 1.0f, 1.0f, 0.05f, 0.0f, 0.0f, 25);
		else gridFlat(s, s->u0, s->u1, s->v0, s->v1, 0.14f, 1.2f, 0.22f, 0.0f, 0.0f, 26);
		break;
	case MAT_WATER:
		if (top) blocks(s, s->u0, s->u1, s->v0, s->v1, 0.0f, 0.0f, 1.3f, 0.5f, 0.0f, 2, 0.07f, 1.0f, 0.0f, 27);
		else gridFlat(s, s->u0, s->u1, s->v0, s->v1, 1.0f, 1.0f, 0.04f, 0.0f, 0.0f, 28);
		break;
	case MAT_MARBLE:
		if (top) {
			blocks(s, s->u0, s->u1, s->v0, s->v1, 0.0f, 0.0f, 3.0f, 3.0f, 0.0f, 0, 0.02f, 1.0f, 0.075f, 29);
		} else if (tiny) gridFlat(s, s->u0, s->u1, s->v0, s->v1, 1.0f, 1.0f, 0.03f, 0.0f, 0.0f, 30);
		else blocks(s, s->u0, s->u1, s->v0, s->v1, 0.0f, 0.0f, 1.5f, 0.75f, 0.03f, 1, 0.03f, 0.82f, 0.0f, 31);
		break;
	case MAT_PILLAR:
		if (top) gridFlat(s, s->u0, s->u1, s->v0, s->v1, 2.0f, 2.0f, 0.02f, 0.0f, 0.0f, 32);
		else gridFlat(s, s->u0, s->u1, s->v0, s->v1, 0.28f, 3.5f, 0.0f, 0.0f, 0.10f, 33);
		break;
	case MAT_GOLD:
		if (tiny) gridFlat(s, s->u0, s->u1, s->v0, s->v1, 1.0f, 1.0f, 0.04f, 0.0f, 0.0f, 34);
		else blocks(s, s->u0, s->u1, s->v0, s->v1, 0.0f, 0.0f, 2.0f, 2.0f, 0.04f, 0, 0.03f, 0.62f, 0.07f, 35);
		break;
	case MAT_METAL:
		if (tiny) gridFlat(s, s->u0, s->u1, s->v0, s->v1, 1.0f, 1.0f, 0.04f, 0.0f, 0.0f, 36);
		else blocks(s, s->u0, s->u1, s->v0, s->v1, 0.0f, 0.0f, 1.5f, 1.5f, 0.05f, 0, 0.03f, 0.58f, 0.05f, 37);
		break;
	case MAT_FLOOR:
		if (top) blocks(s, s->u0, s->u1, s->v0, s->v1, 0.0f, 0.0f, 3.0f, 3.0f, 0.06f, 0, 0.04f, 0.78f, 0.05f, 38);
		else gridFlat(s, s->u0, s->u1, s->v0, s->v1, 2.0f, 1.0f, 0.04f, 0.0f, 0.0f, 39);
		break;
	default:
		gridFlat(s, s->u0, s->u1, s->v0, s->v1, 2.0f, 2.0f, 0.03f, 0.0f, 0.0f, 40);
		break;
	}
	(void)W;
}

// ------------------------------------------------------------------ materials by colour
static const struct { float r, g, b; int mat; } matTable[] = {
	// Sunny Side
	{ 0.52f, 0.78f, 0.36f, MAT_LAWN },   { 0.96f, 0.92f, 0.80f, MAT_PLASTER }, { 0.86f, 0.36f, 0.30f, MAT_ROOF },
	{ 0.72f, 0.72f, 0.74f, MAT_STONE },  { 0.72f, 0.54f, 0.34f, MAT_WOOD },    { 0.28f, 0.60f, 0.30f, MAT_HEDGE },
	{ 0.45f, 0.75f, 0.95f, MAT_WATER },  { 0.50f, 0.34f, 0.20f, MAT_BARK },    { 0.30f, 0.66f, 0.32f, MAT_HEDGE },
	{ 0.86f, 0.82f, 0.70f, MAT_PANEL },
	// Cracked Canyon
	{ 0.86f, 0.70f, 0.46f, MAT_SAND },   { 0.70f, 0.46f, 0.32f, MAT_ROCK },    { 0.78f, 0.55f, 0.38f, MAT_ROCK },
	{ 0.55f, 0.38f, 0.28f, MAT_ROCK },   { 0.66f, 0.50f, 0.32f, MAT_WOOD },
	// Pan Palace
	{ 0.93f, 0.90f, 0.82f, MAT_MARBLE }, { 0.84f, 0.56f, 0.46f, MAT_PLASTER }, { 0.92f, 0.76f, 0.34f, MAT_GOLD },
	{ 0.80f, 0.74f, 0.64f, MAT_PILLAR }, { 0.62f, 0.28f, 0.30f, MAT_CRATE },
	// Eggworks
	{ 0.80f, 0.75f, 0.55f, MAT_FLOOR },  { 0.85f, 0.45f, 0.35f, MAT_PANEL },  { 0.50f, 0.60f, 0.75f, MAT_METAL },
	{ 0.90f, 0.70f, 0.30f, MAT_CRATE },  { 0.75f, 0.55f, 0.25f, MAT_CRATE },  { 0.60f, 0.60f, 0.65f, MAT_METAL },
	{ 0.45f, 0.45f, 0.50f, MAT_METAL },  { 0.70f, 0.50f, 0.40f, MAT_STONE },
};

int surface_material_for(float r, float g, float b)
{
	for (unsigned i = 0; i < sizeof(matTable) / sizeof(matTable[0]); i++)
		if (fabsf(matTable[i].r - r) < 0.012f && fabsf(matTable[i].g - g) < 0.012f && fabsf(matTable[i].b - b) < 0.012f)
			return matTable[i].mat;
	return MAT_PLAIN;
}

// ------------------------------------------------------------------ face visibility
static void faceRect(const Box* b, int f, float* p, float r[4])
{
	switch (f) {
	case 0: *p = b->max[1]; r[0] = b->min[0]; r[1] = b->max[0]; r[2] = b->min[2]; r[3] = b->max[2]; break;
	case 1: *p = b->min[1]; r[0] = b->min[0]; r[1] = b->max[0]; r[2] = b->min[2]; r[3] = b->max[2]; break;
	case 2: *p = b->max[2]; r[0] = b->min[0]; r[1] = b->max[0]; r[2] = b->min[1]; r[3] = b->max[1]; break;
	case 3: *p = b->min[2]; r[0] = b->min[0]; r[1] = b->max[0]; r[2] = b->min[1]; r[3] = b->max[1]; break;
	case 4: *p = b->max[0]; r[0] = b->min[2]; r[1] = b->max[2]; r[2] = b->min[1]; r[3] = b->max[1]; break;
	default: *p = b->min[0]; r[0] = b->min[2]; r[1] = b->max[2]; r[2] = b->min[1]; r[3] = b->max[1]; break;
	}
}

// Axis of the face normal and which two axes make up (u, v)
static void faceAxes(int f, int* k, int* au, int* av)
{
	if (f < 2)      { *k = 1; *au = 0; *av = 2; }
	else if (f < 4) { *k = 2; *au = 0; *av = 1; }
	else            { *k = 0; *au = 2; *av = 1; }
}

// A face is hidden when another box covers all of it: either the space just outside the face is
// inside that box (faces glued together, or buried), or that box has an identical coplanar face.
static bool faceHidden(const Map* m, int a, int f)
{
	const Box* A = &m->boxes[a];
	float p, r[4];
	faceRect(A, f, &p, r);
	int k, au, av;
	faceAxes(f, &k, &au, &av);
	float outside = p + ((f & 1) ? -0.002f : 0.002f);
	const float e = 0.002f;
	for (int i = 0; i < m->boxCount; i++) {
		if (i == a) continue;
		const Box* B = &m->boxes[i];
		if (B->hidden) continue;                          // collision-only boxes are not drawn, so they cover nothing
		if (B->min[au] > r[0] + e || B->max[au] < r[1] - e || B->min[av] > r[2] + e || B->max[av] < r[3] - e) continue;
		if (outside >= B->min[k] && outside <= B->max[k]) return true;
		float edge = (f & 1) ? B->min[k] : B->max[k];     // B's face with the same normal
		if (fabsf(edge - p) < 0.002f) {
			bool bInsideA = B->min[au] >= r[0] - e && B->max[au] <= r[1] + e && B->min[av] >= r[2] - e && B->max[av] <= r[3] + e;
			if (!bInsideA || i < a) return true;
		}
	}
	return false;
}


// ------------------------------------------------------------------ round things
static void pushTri(Sf* s, const SurfVertex* a, const SurfVertex* b, const SurfVertex* c)
{
	if (s->countOnly) { s->n += 3; return; }
	if (s->n + 3 > s->cap) return;
	s->out[s->n++] = *a; s->out[s->n++] = *b; s->out[s->n++] = *c;
}

// One vertex of a curved surface, lit with its own normal (smooth shading from the sky's sun)
static SurfVertex curvedVertex(const Sf* s, float x, float y, float z, float nx, float ny, float nz,
                               const float col[3], float k)
{
	float len = sqrtf(nx * nx + ny * ny + nz * nz) + 1.0e-6f;
	nx /= len; ny /= len; nz /= len;
	float d = nx * s->L[0] + ny * s->L[1] + nz * s->L[2];
	if (d < 0.0f) d = 0.0f;
	float ambK = ny < 0.0f ? 1.0f + (0.55f - 1.0f) * (-ny) : 1.0f;      // undersides are dark, like on boxes
	float depthK = 1.0f;
	if (y < 0.0f) depthK = 0.62f + 0.38f * clampf(1.0f + y / 9.0f, 0.0f, 1.0f);
	SurfVertex o;
	o.x = x; o.y = y; o.z = z;
	float* rgb[3] = { &o.r, &o.g, &o.b };
	for (int c = 0; c < 3; c++) {
		float l = clampf(s->amb[c] * ambK + s->sunCol[c] * d, 0.0f, 1.12f);
		*rgb[c] = clampf(col[c] * l * k * depthK, 0.0f, 1.0f);
	}
	return o;
}

static void buildRound(Sf* s, const Round* r)
{
	const int N = r->sides < 6 ? 6 : (r->sides > 24 ? 24 : r->sides);
	const float TWO_PI = 6.2831853f;
	float h = r->y1 - r->y0;
	if (h < 0.01f) return;
	int seed = (int)(r->x * 7.0f) * 131 + (int)(r->z * 7.0f);

	if (r->kind == ROUND_CYL) {
		// rows from bottom to top: bands if asked, otherwise one extra row so the foot shadow has room
		float ys[16]; int nr = 0;
		ys[nr++] = r->y0;
		if (r->bandH > 0.05f) {
			for (float y = r->y0 + r->bandH; y < r->y1 - 0.05f && nr < 14; y += r->bandH) ys[nr++] = y;
		} else if (h > 4.0f) {
			ys[nr++] = r->y0 + 2.4f;
		}
		ys[nr++] = r->y1;
		float slope = (r->r0 - r->r1) / h;                    // how much the wall leans in per metre
		for (int row = 0; row + 1 < nr; row++) {
			float bandMul = (r->bandH > 0.05f && (row & 1)) ? 0.82f : 1.0f;
			for (int j = 0; j < N; j++) {
				float a0 = TWO_PI * (float)j / (float)N, a1 = TWO_PI * (float)(j + 1) / (float)N;
				float fl = (r->flute > 0.0f && (j & 1)) ? 1.0f - r->flute : 1.0f;
				SurfVertex q[4]; float yy[2] = { ys[row], ys[row + 1] };
				for (int e = 0; e < 4; e++) {
					float a = (e == 0 || e == 3) ? a0 : a1;
					int top = (e >= 2);
					float y = yy[top];
					float rad = r->r0 + (r->r1 - r->r0) * (y - r->y0) / h;
					float ao = 0.68f + 0.32f * clampf((y - r->y0) / 2.6f, 0.0f, 1.0f);
					float toTop = r->y1 - y;
					if (toTop < 0.6f) ao *= 1.0f + 0.07f * (1.0f - toTop / 0.6f);
					q[e] = curvedVertex(s, r->x + rad * sinf(a), y, r->z - rad * cosf(a),
					                    sinf(a), slope, -cosf(a), r->color, ao * bandMul * fl);
				}
				pushTri(s, &q[0], &q[1], &q[2]);
				pushTri(s, &q[0], &q[2], &q[3]);
			}
		}
		if (r->cap && r->r1 > 0.05f) {
			float k = 1.0f + 0.05f;
			SurfVertex c0 = curvedVertex(s, r->x, r->y1, r->z, 0, 1, 0, r->color, k);
			for (int j = 0; j < N; j++) {
				float a0 = TWO_PI * (float)j / (float)N, a1 = TWO_PI * (float)(j + 1) / (float)N;
				float fl = (r->flute > 0.0f && (j & 1)) ? 1.0f - r->flute * 0.5f : 1.0f;
				SurfVertex p0 = curvedVertex(s, r->x + r->r1 * sinf(a0), r->y1, r->z - r->r1 * cosf(a0), 0, 1, 0, r->color, 1.0f * fl);
				SurfVertex p1 = curvedVertex(s, r->x + r->r1 * sinf(a1), r->y1, r->z - r->r1 * cosf(a1), 0, 1, 0, r->color, 1.0f * fl);
				pushTri(s, &c0, &p0, &p1);
			}
		}
		return;
	}

	// Domes and balls: rings of latitude from the bottom pole / equator up to the top pole
	static const float domeAng[4] = { 0.0f, 0.55f, 1.08f, 1.5707963f };
	static const float ballAng[5] = { -1.5707963f, -0.84f, 0.0f, 0.84f, 1.5707963f };
	const float* ang = r->kind == ROUND_DOME ? domeAng : ballAng;
	int nl = r->kind == ROUND_DOME ? 4 : 5;
	float cy = r->kind == ROUND_DOME ? r->y0 : 0.5f * (r->y0 + r->y1);
	float ry = r->kind == ROUND_DOME ? h : 0.5f * h;            // vertical semi-axis
	float rx = r->r0;
	for (int l = 0; l + 1 < nl; l++) {
		for (int j = 0; j < N; j++) {
			float a0 = TWO_PI * (float)j / (float)N, a1 = TWO_PI * (float)(j + 1) / (float)N;
			float tint = 1.0f;
			if (r->flute > 0.0f) tint = 1.0f + r->flute * (hash2(j, l, seed) - 0.5f) * 2.0f;
			SurfVertex q[4];
			for (int e = 0; e < 4; e++) {
				float a = (e == 0 || e == 3) ? a0 : a1;
				float phi = ang[l + (e >= 2 ? 1 : 0)];
				float cp = cosf(phi), sp = sinf(phi);
				float px = rx * cp * sinf(a), pz = -rx * cp * cosf(a), py = cy + ry * sp;
				// normal of an ellipsoid: (x / rx^2, y / ry^2, z / rx^2) with the point relative to the centre
				float nx = px / (rx * rx), ny = (ry * sp) / (ry * ry), nz = pz / (rx * rx);
				float k = (r->kind == ROUND_BALL) ? (0.74f + 0.26f * (sp * 0.5f + 0.5f)) : (0.86f + 0.14f * sp);
				q[e] = curvedVertex(s, r->x + px, py, r->z + pz, nx, ny, nz, r->color, k * tint);
			}
			bool poleLo = (l == 0 && r->kind == ROUND_BALL), poleHi = (l + 2 == nl);
			if (poleLo) pushTri(s, &q[0], &q[2], &q[3]);          // bottom ring collapses to a point
			else if (poleHi) pushTri(s, &q[0], &q[1], &q[2]);       // so does the top one
			else { pushTri(s, &q[0], &q[1], &q[2]); pushTri(s, &q[0], &q[2], &q[3]); }
		}
	}
}

// Blocky maps: every face is cut into world-aligned 1 m cells (2 m when the vertex budget is tight). Each
// cell gets its own slight shade, lit from one corner and darker at the opposite one, so each cube reads
// as a separate block with a soft bevel - at the cost of one quad per cell, the same as a plain tile.
static void voxel(Sf* s, int seed)
{
	float cell = s->detail > 2.2f ? 4.0f : (s->detail > 1.3f ? 2.0f : 1.0f);   // bigger blocks only when a big map would not fit
	// Huge open floors take most of the budget: they get blocks twice as big, so walls and buildings keep small ones
	if (s->face == 0 && (s->u1 - s->u0) * (s->v1 - s->v0) > 300.0f && cell < 4.0f) cell *= 2.0f;
	int i0 = (int)floorf(s->u0 / cell + 0.001f), i1 = (int)ceilf(s->u1 / cell - 0.001f);
	int j0 = (int)floorf(s->v0 / cell + 0.001f), j1 = (int)ceilf(s->v1 / cell - 0.001f);
	for (int j = j0; j < j1; j++)
		for (int i = i0; i < i1; i++) {
			float u0 = fmaxf(s->u0, i * cell), u1 = fminf(s->u1, (i + 1) * cell);
			float v0 = fmaxf(s->v0, j * cell), v1 = fminf(s->v1, (j + 1) * cell);
			if (!clip(s, &u0, &v0, &u1, &v1)) continue;
			float n = hash2(i, j, seed);
			float f = 1.0f + 0.07f * (2.0f * n - 1.0f) - 0.03f * (float)((i + j) & 1);
			static const float bevel[4] = { 0.90f, 0.97f, 1.07f, 0.97f };    // (u0,v0) dark .. (u1,v1) bright
			float c[4][3];
			for (int k = 0; k < 4; k++) tone(s, f * bevel[k], 0.0f, c[k]);
			quad4(s, u0, v0, u1, v1, c);
		}
}

// ------------------------------------------------------------------ whole map
static void buildAll(const Map* m, Sf* s, float detail, const float bounds[6], const unsigned char* hidden)
{
	s->detail = detail;
	for (int bi = 0; bi < m->boxCount; bi++) {
		const Box* b = &m->boxes[bi];
		if (b->hidden) continue;
		int mat = surface_material_for(b->color[0], b->color[1], b->color[2]);
		for (int f = 0; f < 6; f++) {
			if (f == 1 && b->min[1] <= 0.001f) continue;       // underside resting on / below the ground
			if (hidden[bi] & (1u << f)) continue;

			float p, r[4];
			faceRect(b, f, &p, r);
			s->box = b; s->face = f;
			s->u0 = r[0]; s->u1 = r[1]; s->v0 = r[2]; s->v1 = r[3];
			memcpy(s->base, b->color, sizeof(float) * 3);

			bool outer = (f == 2 && b->max[2] >= bounds[5] - 0.01f) || (f == 3 && b->min[2] <= bounds[2] + 0.01f) ||
			             (f == 4 && b->max[0] >= bounds[3] - 0.01f) || (f == 5 && b->min[0] <= bounds[0] + 0.01f);
			if (outer || (f == 1 && !m->blocky)) flat(s, s->u0, s->v0, s->u1, s->v1, b->color);   // never seen up close (blocky maps show catwalk undersides as blocks)
			else if (m->blocky) voxel(s, f * 17 + 3);
			else paint(s, mat, f == 0);
		}
	}
	for (int ri = 0; ri < m->roundCount; ri++) buildRound(s, &m->rounds[ri]);
}

int surface_build_map(const Map* m, SurfVertex* out, int cap, float* detailUsed)
{
	float bounds[6] = { 1e9f, 1e9f, 1e9f, -1e9f, -1e9f, -1e9f };
	for (int i = 0; i < m->boxCount; i++)
		for (int k = 0; k < 3; k++) {
			if (m->boxes[i].min[k] < bounds[k])     bounds[k] = m->boxes[i].min[k];
			if (m->boxes[i].max[k] > bounds[3 + k]) bounds[3 + k] = m->boxes[i].max[k];
		}

	// Which faces are hidden never changes with the detail level, so work it out once.
	static unsigned char hidden[MAX_BOXES];
	for (int i = 0; i < m->boxCount && i < MAX_BOXES; i++) {
		hidden[i] = 0;
		for (int f = 0; f < 6; f++)
			if (faceHidden(m, i, f)) hidden[i] |= (unsigned char)(1u << f);
	}

	// Find the finest detail level that fits the vertex budget.
	float detail = 1.0f;
	Sf s;
	for (int tries = 0; tries < 24; tries++) {
		memset(&s, 0, sizeof s);
		setupLight(&s, m->sky);
		s.countOnly = true;
		buildAll(m, &s, detail, bounds, hidden);
		if (s.n <= cap) break;
		detail *= 1.18f;
	}
	if (detailUsed) *detailUsed = detail;

	memset(&s, 0, sizeof s);
	setupLight(&s, m->sky);
	s.out = out; s.cap = cap;
	buildAll(m, &s, detail, bounds, hidden);
	return s.n;
}
