#include "sky.h"
#include <math.h>
#include <string.h>

#define PI_F  3.14159265f
#define TAU_F 6.28318531f
#define DEG   (PI_F / 180.0f)

// ------------------------------------------------------------------ the presets
static const SkyDef presets[SKY_PRESETS] = {
	{ // Eggworks: smoggy sunset behind a factory skyline
		.name = "Sunset smog",
		.zenith = { 0.20f, 0.18f, 0.40f }, .mid = { 0.62f, 0.36f, 0.46f }, .horizon = { 1.00f, 0.66f, 0.38f }, .ground = { 0.50f, 0.34f, 0.32f },
		.sunAz = 2.55f, .sunEl = 0.10f, .sunSize = 0.085f, .sunColor = { 1.00f, 0.86f, 0.55f }, .glowColor = { 1.00f, 0.62f, 0.32f },
		.cloudLit = { 1.00f, 0.66f, 0.52f }, .cloudShade = { 0.50f, 0.28f, 0.42f }, .clouds = 9, .cloudEl0 = 0.18f, .cloudEl1 = 0.65f, .cloudSpeed = 0.00006f,
		.stars = 0,
		.farStyle = RIDGE_HILLS, .farColor = { 0.58f, 0.36f, 0.42f }, .farHeight = 16.0f,
		.nearStyle = RIDGE_SKYLINE, .nearColor = { 0.20f, 0.14f, 0.22f }, .nearHeight = 26.0f, .nearLights = true,
	},
	{ // Sunny Side: bright noon, rolling hills
		.name = "Bright noon",
		.zenith = { 0.14f, 0.40f, 0.86f }, .mid = { 0.36f, 0.64f, 0.95f }, .horizon = { 0.82f, 0.93f, 1.00f }, .ground = { 0.62f, 0.80f, 0.70f },
		.sunAz = 0.55f, .sunEl = 0.95f, .sunSize = 0.055f, .sunColor = { 1.00f, 0.98f, 0.82f }, .glowColor = { 1.00f, 0.96f, 0.78f },
		.cloudLit = { 1.00f, 1.00f, 1.00f }, .cloudShade = { 0.74f, 0.82f, 0.94f }, .clouds = 12, .cloudEl0 = 0.16f, .cloudEl1 = 1.05f, .cloudSpeed = 0.00010f,
		.stars = 0,
		.farStyle = RIDGE_HILLS, .farColor = { 0.52f, 0.70f, 0.78f }, .farHeight = 20.0f,
		.nearStyle = RIDGE_HILLS, .nearColor = { 0.36f, 0.64f, 0.38f }, .nearHeight = 11.0f, .nearLights = false,
	},
	{ // Cracked Canyon: hot afternoon, red mesas on the horizon
		.name = "Desert afternoon",
		.zenith = { 0.22f, 0.46f, 0.80f }, .mid = { 0.55f, 0.70f, 0.88f }, .horizon = { 1.00f, 0.90f, 0.72f }, .ground = { 0.84f, 0.66f, 0.50f },
		.sunAz = 5.60f, .sunEl = 0.62f, .sunSize = 0.08f, .sunColor = { 1.00f, 0.96f, 0.80f }, .glowColor = { 1.00f, 0.90f, 0.66f },
		.cloudLit = { 1.00f, 0.97f, 0.92f }, .cloudShade = { 0.86f, 0.78f, 0.74f }, .clouds = 6, .cloudEl0 = 0.30f, .cloudEl1 = 0.95f, .cloudSpeed = 0.00005f,
		.stars = 0,
		.farStyle = RIDGE_MESAS, .farColor = { 0.80f, 0.56f, 0.46f }, .farHeight = 24.0f,
		.nearStyle = RIDGE_MESAS, .nearColor = { 0.64f, 0.38f, 0.30f }, .nearHeight = 15.0f, .nearLights = false,
	},
	{ // Pan Palace: starry night over distant domes
		.name = "Starry night",
		.zenith = { 0.015f, 0.02f, 0.10f }, .mid = { 0.07f, 0.09f, 0.27f }, .horizon = { 0.32f, 0.26f, 0.48f }, .ground = { 0.10f, 0.08f, 0.20f },
		.sunAz = 0.90f, .sunEl = 0.72f, .sunSize = 0.060f, .sunColor = { 0.96f, 0.96f, 0.86f }, .glowColor = { 0.50f, 0.52f, 0.78f },
		.moon = true,
		.cloudLit = { 0.40f, 0.42f, 0.66f }, .cloudShade = { 0.14f, 0.15f, 0.34f }, .clouds = 5, .cloudEl0 = 0.20f, .cloudEl1 = 0.70f, .cloudSpeed = 0.00004f,
		.stars = 110,
		.farStyle = RIDGE_PEAKS, .farColor = { 0.18f, 0.15f, 0.34f }, .farHeight = 30.0f,
		.nearStyle = RIDGE_PALACE, .nearColor = { 0.07f, 0.06f, 0.16f }, .nearHeight = 26.0f, .nearLights = true,
	},
	{ // Fridge Factory: crisp winter morning, snowy peaks behind a row of warehouses
		.name = "Frosty morning",
		.zenith = { 0.30f, 0.50f, 0.80f }, .mid = { 0.58f, 0.74f, 0.92f }, .horizon = { 0.90f, 0.95f, 1.00f }, .ground = { 0.78f, 0.84f, 0.90f },
		.sunAz = 3.60f, .sunEl = 0.28f, .sunSize = 0.065f, .sunColor = { 1.00f, 0.97f, 0.90f }, .glowColor = { 0.92f, 0.94f, 1.00f },
		.cloudLit = { 1.00f, 1.00f, 1.00f }, .cloudShade = { 0.72f, 0.78f, 0.90f }, .clouds = 8, .cloudEl0 = 0.14f, .cloudEl1 = 0.80f, .cloudSpeed = 0.00005f,
		.stars = 0,
		.farStyle = RIDGE_PEAKS, .farColor = { 0.86f, 0.90f, 0.97f }, .farHeight = 28.0f,
		.nearStyle = RIDGE_SKYLINE, .nearColor = { 0.44f, 0.52f, 0.64f }, .nearHeight = 18.0f, .nearLights = false,
	},
	{ // Diner Rooftops: pink and violet dusk over a city with its lights coming on
		.name = "Neon dusk",
		.zenith = { 0.16f, 0.14f, 0.38f }, .mid = { 0.52f, 0.30f, 0.58f }, .horizon = { 1.00f, 0.56f, 0.52f }, .ground = { 0.36f, 0.26f, 0.40f },
		.sunAz = 4.10f, .sunEl = 0.07f, .sunSize = 0.09f, .sunColor = { 1.00f, 0.80f, 0.60f }, .glowColor = { 1.00f, 0.50f, 0.50f },
		.cloudLit = { 1.00f, 0.62f, 0.66f }, .cloudShade = { 0.40f, 0.26f, 0.48f }, .clouds = 7, .cloudEl0 = 0.16f, .cloudEl1 = 0.70f, .cloudSpeed = 0.00006f,
		.stars = 40,
		.farStyle = RIDGE_HILLS, .farColor = { 0.44f, 0.30f, 0.52f }, .farHeight = 14.0f,
		.nearStyle = RIDGE_SKYLINE, .nearColor = { 0.16f, 0.12f, 0.24f }, .nearHeight = 30.0f, .nearLights = true,
	},
	{ // Henhouse Hollow: golden late afternoon over rolling farmland
		.name = "Harvest afternoon",
		.zenith = { 0.24f, 0.44f, 0.78f }, .mid = { 0.56f, 0.70f, 0.88f }, .horizon = { 1.00f, 0.86f, 0.60f }, .ground = { 0.70f, 0.66f, 0.44f },
		.sunAz = 2.20f, .sunEl = 0.30f, .sunSize = 0.075f, .sunColor = { 1.00f, 0.90f, 0.66f }, .glowColor = { 1.00f, 0.80f, 0.48f },
		.cloudLit = { 1.00f, 0.94f, 0.82f }, .cloudShade = { 0.80f, 0.70f, 0.66f }, .clouds = 9, .cloudEl0 = 0.16f, .cloudEl1 = 0.85f, .cloudSpeed = 0.00006f,
		.stars = 0,
		.farStyle = RIDGE_HILLS, .farColor = { 0.60f, 0.66f, 0.56f }, .farHeight = 18.0f,
		.nearStyle = RIDGE_HILLS, .nearColor = { 0.56f, 0.62f, 0.32f }, .nearHeight = 10.0f, .nearLights = false,
	},
};

const SkyDef* sky_preset(int id)
{
	if (id < 0 || id >= SKY_PRESETS) id = 0;
	return &presets[id];
}

unsigned int sky_clear_rgba(const SkyDef* s)
{
	unsigned r = (unsigned)(s->horizon[0] * 255.0f), g = (unsigned)(s->horizon[1] * 255.0f), b = (unsigned)(s->horizon[2] * 255.0f);
	return (r << 24) | (g << 16) | (b << 8) | 0xFFu;
}

// ------------------------------------------------------------------ helpers
static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static void mix3(const float a[3], const float b[3], float t, float out[3])
{
	t = clamp01(t);
	for (int i = 0; i < 3; i++) out[i] = a[i] + (b[i] - a[i]) * t;
}

static float h01(unsigned n)
{
	n = n * 747796405u + 2891336453u;
	n = ((n >> ((n >> 28) + 4u)) ^ n) * 277803737u;
	n = (n >> 22) ^ n;
	return (float)(n & 0xFFFFu) / 65535.0f;
}

// Dome colour at an elevation (radians)
static void domeColor(const SkyDef* s, float el, float out[3])
{
	if (el <= 0.0f) { mix3(s->horizon, s->ground, -el / 0.45f, out); return; }
	float t = el / (PI_F * 0.5f);
	if (t < 0.38f) mix3(s->horizon, s->mid, t / 0.38f, out);
	else mix3(s->mid, s->zenith, (t - 0.38f) / 0.62f, out);
}

static void dirOf(float az, float el, float out[3])
{
	float ce = cosf(el);
	out[0] = sinf(az) * ce; out[1] = sinf(el); out[2] = -cosf(az) * ce;
}

static void tri(SkyEmit emit, const float a[3], const float b[3], const float c[3],
                const float ca[3], const float cb[3], const float cc[3])
{
	float p[3][3], col[3][3];
	memcpy(p[0], a, 12); memcpy(p[1], b, 12); memcpy(p[2], c, 12);
	memcpy(col[0], ca, 12); memcpy(col[1], cb, 12); memcpy(col[2], cc, 12);
	emit(p, col);
}

static void quad(SkyEmit emit, const float a[3], const float b[3], const float c[3], const float d[3],
                 const float ca[3], const float cb[3], const float cc[3], const float cd[3])
{
	tri(emit, a, b, c, ca, cb, cc);
	tri(emit, a, c, d, ca, cc, cd);
}

// Unit vectors tangent to the sky sphere at direction d (right, up)
static void tangents(const float d[3], float right[3], float up[3])
{
	float rx = -d[2], rz = d[0];                 // d x (0,1,0), flattened
	float l = sqrtf(rx * rx + rz * rz);
	if (l < 1.0e-4f) { rx = 1.0f; rz = 0.0f; l = 1.0f; }
	right[0] = rx / l; right[1] = 0.0f; right[2] = rz / l;
	up[0] = right[1] * d[2] - right[2] * d[1];
	up[1] = right[2] * d[0] - right[0] * d[2];
	up[2] = right[0] * d[1] - right[1] * d[0];
}

// A flat disc facing the camera: centre colour fades to the rim colour (Gouraud shading does the glow)
static void disc(SkyEmit emit, const float eye[3], const float d[3], float dist, float radius, int segs,
                 const float centreCol[3], const float rimCol[3], float shadeBottom)
{
	float right[3], up[3];
	tangents(d, right, up);
	float c[3] = { eye[0] + d[0] * dist, eye[1] + d[1] * dist, eye[2] + d[2] * dist };
	float prev[3], prevCol[3];
	for (int i = 0; i <= segs; i++) {
		float a = TAU_F * i / segs;
		float ca = cosf(a), sa = sinf(a);
		float p[3] = { c[0] + (right[0] * ca + up[0] * sa) * radius, c[1] + (right[1] * ca + up[1] * sa) * radius, c[2] + (right[2] * ca + up[2] * sa) * radius };
		float col[3];
		float k = 1.0f - shadeBottom * clamp01(0.5f - 0.5f * sa);       // underside darker (clouds)
		for (int j = 0; j < 3; j++) col[j] = rimCol[j] * k;
		if (i > 0) tri(emit, c, prev, p, centreCol, prevCol, col);
		memcpy(prev, p, 12); memcpy(prevCol, col, 12);
	}
}

// ------------------------------------------------------------------ dome
#define DOME_R   150.0f
#define DOME_SEG 24

static void drawDome(const SkyDef* s, const float eye[3], SkyEmit emit)
{
	static const float elDeg[] = { -90, -40, -12, 0, 6, 14, 26, 42, 62, 90 };
	const int rings = (int)(sizeof(elDeg) / sizeof(elDeg[0]));
	for (int r = 0; r + 1 < rings; r++) {
		float e0 = elDeg[r] * DEG, e1 = elDeg[r + 1] * DEG;
		float c0[3], c1[3];
		domeColor(s, e0, c0); domeColor(s, e1, c1);
		for (int i = 0; i < DOME_SEG; i++) {
			float a0 = TAU_F * i / DOME_SEG, a1 = TAU_F * (i + 1) / DOME_SEG;
			float p00[3], p10[3], p01[3], p11[3], d[3];
			dirOf(a0, e0, d); for (int k = 0; k < 3; k++) p00[k] = eye[k] + d[k] * DOME_R;
			dirOf(a1, e0, d); for (int k = 0; k < 3; k++) p10[k] = eye[k] + d[k] * DOME_R;
			dirOf(a0, e1, d); for (int k = 0; k < 3; k++) p01[k] = eye[k] + d[k] * DOME_R;
			dirOf(a1, e1, d); for (int k = 0; k < 3; k++) p11[k] = eye[k] + d[k] * DOME_R;
			if (r == 0) tri(emit, p00, p11, p01, c0, c1, c1);          // bottom pole: p00 and p10 coincide there
			else if (r + 2 == rings) tri(emit, p00, p10, p01, c0, c0, c1);   // top pole: p01 and p11 coincide
			else quad(emit, p00, p10, p11, p01, c0, c0, c1, c1);
		}
	}
}

// ------------------------------------------------------------------ sun, moon, stars
static void drawSun(const SkyDef* s, const float eye[3], SkyEmit emit)
{
	float d[3];
	dirOf(s->sunAz, s->sunEl, d);
	float sky[3];
	domeColor(s, s->sunEl, sky);
	float glowC[3];
	mix3(sky, s->glowColor, s->moon ? 0.35f : 0.75f, glowC);
	const float R = 140.0f;
	float r = tanf(s->sunSize) * R;
	{   // soft glow: the rim of the disc takes the dome colour at each rim vertex, so it melts into the sky
		float right[3], up[3];
		tangents(d, right, up);
		float c[3] = { eye[0] + d[0] * R, eye[1] + d[1] * R, eye[2] + d[2] * R };
		float gr = r * (s->moon ? 4.0f : 6.0f);
		float prev[3] = { 0, 0, 0 }, prevCol[3] = { 0, 0, 0 };
		for (int i = 0; i <= 24; i++) {
			float a = TAU_F * i / 24;
			float p[3] = { c[0] + (right[0] * cosf(a) + up[0] * sinf(a)) * gr, c[1] + (right[1] * cosf(a) + up[1] * sinf(a)) * gr, c[2] + (right[2] * cosf(a) + up[2] * sinf(a)) * gr };
			float dx = p[0] - eye[0], dy = p[1] - eye[1], dz = p[2] - eye[2];
			float col[3];
			domeColor(s, asinf(clamp01(fabsf(dy) / sqrtf(dx * dx + dy * dy + dz * dz)) * (dy < 0.0f ? -1.0f : 1.0f)), col);
			if (i > 0) tri(emit, c, prev, p, glowC, prevCol, col);
			memcpy(prev, p, 12); memcpy(prevCol, col, 12);
		}
	}
	float white[3] = { 1.0f, 1.0f, 1.0f };
	float core[3];
	mix3(s->sunColor, white, 0.55f, core);
	float dd[3] = { d[0] * 0.9999f, d[1] * 0.9999f, d[2] * 0.9999f };
	disc(emit, eye, dd, R - 0.5f, r, 20, core, s->sunColor, 0.0f);
	if (s->moon) {                                                                    // craters
		float right[3], up[3];
		tangents(d, right, up);
		static const float cr[4][3] = { { -0.35f, 0.25f, 0.20f }, { 0.30f, 0.10f, 0.28f }, { 0.05f, -0.42f, 0.22f }, { -0.15f, -0.10f, 0.14f } };
		float dark[3];
		mix3(s->sunColor, s->glowColor, 0.45f, dark);
		for (int i = 0; i < 4; i++) {
			float cd[3];
			for (int k = 0; k < 3; k++) cd[k] = d[k] + (right[k] * cr[i][0] + up[k] * cr[i][1]) * s->sunSize;
			float l = sqrtf(cd[0] * cd[0] + cd[1] * cd[1] + cd[2] * cd[2]);
			for (int k = 0; k < 3; k++) cd[k] /= l;
			disc(emit, eye, cd, R - 1.0f, r * cr[i][2], 10, dark, dark, 0.0f);
		}
	}
}

static void drawStars(const SkyDef* s, const float eye[3], int frame, SkyEmit emit)
{
	for (int i = 0; i < s->stars; i++) {
		float az = h01(i * 3u + 1u) * TAU_F;
		float el = (6.0f + h01(i * 3u + 2u) * 80.0f) * DEG;
		float d[3];
		dirOf(az, el, d);
		float tw = 0.65f + 0.35f * sinf((frame + i * 37) * 0.045f + i);
		if (h01(i * 3u + 3u) < 0.7f) tw = 0.9f;                                      // most stars shine steadily
		float size = (h01(i * 5u + 7u) < 0.15f) ? 0.0062f : 0.0040f;
		float c[3] = { tw, tw, tw * 0.92f };
		float right[3], up[3];
		tangents(d, right, up);
		const float R = 145.0f, r = size * R;
		float p[4][3];
		for (int k = 0; k < 3; k++) {
			float cc = eye[k] + d[k] * R;
			p[0][k] = cc - right[k] * r; p[1][k] = cc + up[k] * r; p[2][k] = cc + right[k] * r; p[3][k] = cc - up[k] * r;
		}
		quad(emit, p[0], p[1], p[2], p[3], c, c, c, c);
	}
}

// ------------------------------------------------------------------ silhouettes
#define RIDGE_SEG 72
#define RIDGE_BASE (-30.0f)

static void ridgePt(const float eye[3], float az, float R, float y, float out[3])
{
	out[0] = eye[0] + sinf(az) * R; out[1] = y; out[2] = eye[2] - cosf(az) * R;
}

// A vertical block between two azimuths with a flat top
static void ridgeBlock(SkyEmit emit, const float eye[3], float a0, float a1, float R, float top,
                       const float cTop[3], const float cBot[3])
{
	float p0[3], p1[3], p2[3], p3[3];
	ridgePt(eye, a0, R, RIDGE_BASE, p0); ridgePt(eye, a1, R, RIDGE_BASE, p1);
	ridgePt(eye, a1, R, top, p2); ridgePt(eye, a0, R, top, p3);
	quad(emit, p0, p1, p2, p3, cBot, cBot, cTop, cTop);
}

static void ridgeLight(SkyEmit emit, const float eye[3], float a, float R, float y, float w)
{
	float c[3] = { 1.0f, 0.82f, 0.45f };
	float p0[3], p1[3], p2[3], p3[3];
	ridgePt(eye, a - w, R - 0.3f, y, p0); ridgePt(eye, a + w, R - 0.3f, y, p1);
	ridgePt(eye, a + w, R - 0.3f, y + 1.1f, p2); ridgePt(eye, a - w, R - 0.3f, y + 1.1f, p3);
	quad(emit, p0, p1, p2, p3, c, c, c, c);
}

static void drawRidge(const SkyDef* s, const float eye[3], int style, const float col[3], float H, float R,
                      unsigned seed, bool lights, SkyEmit emit)
{
	if (style == RIDGE_NONE) return;
	float top[3], bot[3], hz[3];
	for (int k = 0; k < 3; k++) { top[k] = col[k] * 1.12f; if (top[k] > 1.0f) top[k] = 1.0f; hz[k] = s->horizon[k]; }
	mix3(col, hz, 0.55f, bot);
	const float dA = TAU_F / RIDGE_SEG;

	if (style == RIDGE_HILLS) {
		for (int i = 0; i < RIDGE_SEG; i++) {
			float h0 = H * (0.55f + 0.30f * sinf(i * 0.27f + seed) + 0.22f * sinf(i * 0.61f + seed * 2.0f) + 0.10f * sinf(i * 1.3f));
			float h1 = H * (0.55f + 0.30f * sinf((i + 1) * 0.27f + seed) + 0.22f * sinf((i + 1) * 0.61f + seed * 2.0f) + 0.10f * sinf((i + 1) * 1.3f));
			if (h0 < 0.1f * H) h0 = 0.1f * H;
			if (h1 < 0.1f * H) h1 = 0.1f * H;
			float p0[3], p1[3], p2[3], p3[3];
			ridgePt(eye, i * dA, R, RIDGE_BASE, p0); ridgePt(eye, (i + 1) * dA, R, RIDGE_BASE, p1);
			ridgePt(eye, (i + 1) * dA, R, h1, p2); ridgePt(eye, i * dA, R, h0, p3);
			quad(emit, p0, p1, p2, p3, bot, bot, top, top);
		}
	} else if (style == RIDGE_PEAKS) {
		for (int i = 0; i < RIDGE_SEG; i++) {
			float hh = H * (0.35f + 0.65f * h01(i * 7u + seed));
			float a0 = (i - 0.6f) * dA, a1 = (i + 1.6f) * dA;
			float p0[3], p1[3], apex[3];
			ridgePt(eye, a0, R, RIDGE_BASE, p0); ridgePt(eye, a1, R, RIDGE_BASE, p1);
			ridgePt(eye, (i + 0.5f) * dA, R, hh, apex);
			float cap[3], white[3] = { 0.92f, 0.92f, 1.0f };
			mix3(top, white, hh > 0.78f * H ? 0.55f : 0.0f, cap);
			tri(emit, p0, p1, apex, bot, bot, cap);
		}
	} else {
		// Blocky styles: walk round the horizon placing buildings or mesas of random width
		int i = 0, n = 0;
		while (i < RIDGE_SEG) {
			float r1 = h01(n * 11u + seed), r2 = h01(n * 13u + seed + 5u), r3 = h01(n * 17u + seed + 9u);
			int w = 1 + (int)(r1 * 3.0f);
			if (style == RIDGE_MESAS) w = 2 + (int)(r1 * 4.0f);
			if (i + w > RIDGE_SEG) w = RIDGE_SEG - i;
			float a0 = i * dA, a1 = (i + w) * dA;
			float hh = H * (0.28f + 0.55f * r2);
			if (style == RIDGE_MESAS) {
				ridgeBlock(emit, eye, a0, a1, R, hh, top, bot);
				if (r3 > 0.45f && w >= 3) ridgeBlock(emit, eye, a0 + dA * 0.6f, a1 - dA * 0.6f, R - 0.2f, hh * 1.22f, top, bot);   // a second tier
			} else if (style == RIDGE_SKYLINE) {
				ridgeBlock(emit, eye, a0, a1, R, hh, col, bot);
				if (n % 5 == 2) {                                                   // chimney
					float am = (a0 + a1) * 0.5f, cw = dA * 0.28f;
					ridgeBlock(emit, eye, am - cw, am + cw, R - 0.2f, H * 1.45f, col, col);
					float red[3] = { 1.0f, 0.35f, 0.25f };
					ridgeLight(emit, eye, am, R - 0.4f, H * 1.45f, cw * 0.8f);
					(void)red;
				} else if (n % 7 == 3 && w >= 2) {                                  // cooling tower: wide base, narrow top
					float p0[3], p1[3], p2[3], p3[3];
					float am = (a0 + a1) * 0.5f, wb = (a1 - a0) * 0.5f, wt = wb * 0.62f;
					ridgePt(eye, am - wb, R - 0.2f, hh, p0); ridgePt(eye, am + wb, R - 0.2f, hh, p1);
					ridgePt(eye, am + wt, R - 0.2f, hh + H * 0.55f, p2); ridgePt(eye, am - wt, R - 0.2f, hh + H * 0.55f, p3);
					quad(emit, p0, p1, p2, p3, col, col, col, col);
				}
			} else {   // RIDGE_PALACE: halls with domes and minarets
				ridgeBlock(emit, eye, a0, a1, R, hh * 0.8f, col, bot);
				float am = (a0 + a1) * 0.5f, hw = (a1 - a0) * 0.5f;
				if (n % 3 == 1) {                                                   // dome
					float hw2 = hw * 0.85f, rr = hw2 * R * 0.9f;
					float prev[3] = { 0, 0, 0 }, cur[3];
					int steps = 6;
					for (int k = 0; k <= steps; k++) {
						float t = (float)k / steps;
						float ang = am - hw2 + 2.0f * hw2 * t;
						float hgt = hh * 0.8f + sqrtf(fmaxf(0.0f, 1.0f - (2.0f * t - 1.0f) * (2.0f * t - 1.0f))) * rr * 0.9f;
						ridgePt(eye, ang, R - 0.2f, hgt, cur);
						if (k > 0) {
							float b0[3], b1[3];
							ridgePt(eye, ang - 2.0f * hw2 / steps, R - 0.2f, hh * 0.8f, b0);
							ridgePt(eye, ang, R - 0.2f, hh * 0.8f, b1);
							quad(emit, b0, b1, cur, prev, col, col, col, col);
						}
						memcpy(prev, cur, 12);
					}
				} else if (n % 3 == 2) {                                            // minaret
					float p0[3], p1[3], apex[3];
					float mw = hw * 0.30f;
					ridgeBlock(emit, eye, am - mw, am + mw, R - 0.2f, hh * 1.5f, col, col);
					ridgePt(eye, am - mw * 1.8f, R - 0.2f, hh * 1.5f, p0); ridgePt(eye, am + mw * 1.8f, R - 0.2f, hh * 1.5f, p1);
					ridgePt(eye, am, R - 0.2f, hh * 2.0f, apex);
					tri(emit, p0, p1, apex, col, col, col);
				}
			}
			if (lights && h01(n * 19u + seed + 3u) < 0.55f)
				ridgeLight(emit, eye, (a0 + a1) * 0.5f + dA * 0.15f * (r3 - 0.5f), R - 0.5f, 2.0f + hh * 0.5f * r3, dA * 0.10f);
			i += w;
			n++;
		}
	}
}

// ------------------------------------------------------------------ clouds
static void drawClouds(const SkyDef* s, const float eye[3], int frame, SkyEmit emit)
{
	for (int k = 0; k < s->clouds; k++) {
		float az = h01(k * 31u + 11u) * TAU_F + frame * s->cloudSpeed * (0.7f + 0.6f * h01(k * 5u + 2u));
		float el = s->cloudEl0 + (s->cloudEl1 - s->cloudEl0) * h01(k * 31u + 17u);
		float scale = 0.7f + 0.9f * h01(k * 31u + 23u);
		int blobs = 4 + (int)(h01(k * 31u + 29u) * 3.0f);
		for (int b = 0; b < blobs; b++) {
			float u = ((float)b - (blobs - 1) * 0.5f) * 0.075f * scale;                        // along the horizon
			float v = (h01(k * 97u + b * 7u + 1u) - 0.35f) * 0.025f * scale + 0.015f * sinf(b * 1.9f);
			float rad = (0.055f + 0.040f * h01(k * 97u + b * 7u + 2u)) * scale;                    // angular radius
			float d[3];
			dirOf(az + u / cosf(el), el + v, d);
			float lit[3], shade[3];
			for (int j = 0; j < 3; j++) { lit[j] = s->cloudLit[j]; shade[j] = s->cloudShade[j]; }
			float rim[3];
			mix3(lit, shade, 0.35f, rim);
			const float R = 112.0f;
			// wide and flat: squash vertically by building the disc from stretched tangents
			float right[3], up[3];
			tangents(d, right, up);
			float c[3] = { eye[0] + d[0] * R, eye[1] + d[1] * R, eye[2] + d[2] * R };
			float prev[3], prevCol[3];
			const int segs = 10;
			for (int i = 0; i <= segs; i++) {
				float a = TAU_F * i / segs;
				float ca = cosf(a), sa = sinf(a);
				float rr = rad * R;
				float p[3] = { c[0] + (right[0] * ca * 1.35f + up[0] * sa * 0.62f) * rr, c[1] + (right[1] * ca * 1.35f + up[1] * sa * 0.62f) * rr, c[2] + (right[2] * ca * 1.35f + up[2] * sa * 0.62f) * rr };
				float col[3];
				mix3(rim, shade, clamp01(0.5f - 0.5f * sa) * 0.9f, col);                       // underside darker
				if (i > 0) tri(emit, c, prev, p, lit, prevCol, col);
				memcpy(prev, p, 12); memcpy(prevCol, col, 12);
			}
		}
	}
}

// ------------------------------------------------------------------ whole sky
void sky_build(const SkyDef* s, const float eye[3], int frame, SkyEmit emit)
{
	drawDome(s, eye, emit);
	drawStars(s, eye, frame, emit);
	drawSun(s, eye, emit);
	drawRidge(s, eye, s->farStyle, s->farColor, s->farHeight, 128.0f, 3u, false, emit);
	drawRidge(s, eye, s->nearStyle, s->nearColor, s->nearHeight, 118.0f, 8u, s->nearLights, emit);
	drawClouds(s, eye, frame, emit);
}
