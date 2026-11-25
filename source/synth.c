#include "synth.h"
#include <math.h>
#include <string.h>

#define TAU 6.2831853f
#define MAX_SECONDS 2
#define MAX_FRAMES (AUDIO_RATE * MAX_SECONDS)

static float scratch[MAX_FRAMES];

typedef struct { unsigned rng; float lp; float lp2; float ph[4]; } St;

static float noise(St* s)
{
	s->rng ^= s->rng << 13;
	s->rng ^= s->rng >> 17;
	s->rng ^= s->rng << 5;
	return (float)(s->rng >> 8) * (1.0f / 8388608.0f) - 1.0f;
}
static float lowpass(float in, float* state, float a) { *state += a * (in - *state); return *state; }
static float decay(float t, float d) { return expf(-t * d); }
static float sweepF(float f0, float f1, float t, float dur) { return f0 * powf(f1 / f0, t / dur); }
static float osc(St* s, int k, float f, float dt)
{
	s->ph[k] += TAU * f * dt;
	if (s->ph[k] > 1.0e4f) s->ph[k] = fmodf(s->ph[k], TAU);
	return sinf(s->ph[k]);
}
static float square(float f, float t, float duty) { return fmodf(f * t, 1.0f) < duty ? 1.0f : -1.0f; }
static float triangle(float f, float t) { return 2.0f * fabsf(2.0f * fmodf(f * t, 1.0f) - 1.0f) - 1.0f; }

// ------------------------------------------------------------------ gunshots
typedef struct {
	float dur;
	float nDecay, nCut, nMix;           // noise burst (crack)
	float tF0, tF1, tDecay, tMix;       // tonal "pew"
	float bF0, bF1, bDecay, bMix;       // low thump
	float gain, echoT, echoMix;
} Gun;

static const Gun guns[SFX_GUN_COUNT] = {
	/* Splatterpop  */ { 0.38f, 14, 0.55f, 1.0f, 220,  70, 18, 0.35f, 110, 45, 12, 0.9f, 0.9f, 0.11f, 0.25f },
	/* Yolkbuster   */ { 0.70f,  5, 0.12f, 0.8f, 140, 400,  3, 0.35f,  80, 35,  8, 0.8f, 0.9f, 0.15f, 0.25f },
	/* Longbeak     */ { 0.80f, 40, 0.90f, 1.0f, 900, 200, 30, 0.40f,  90, 40,  7, 0.9f, 1.0f, 0.20f, 0.35f },
	/* Peckmatic    */ { 0.16f, 30, 0.70f, 0.9f, 600, 180, 28, 0.35f, 120, 60, 25, 0.5f, 0.75f, 0.0f, 0.0f },
	/* Farmhand     */ { 0.22f, 26, 0.85f, 0.9f,1000, 300, 22, 0.45f, 100, 50, 20, 0.45f, 0.8f, 0.09f, 0.2f },
	/* Triplet      */ { 0.13f, 38, 0.80f, 0.85f, 800, 250, 35, 0.40f, 140, 70, 30, 0.4f, 0.7f, 0.0f, 0.0f },
	/* Eggstol      */ { 0.18f, 32, 0.75f, 0.95f, 700, 220, 30, 0.38f, 130, 60, 26, 0.5f, 0.75f, 0.06f, 0.12f },
};

static void renderGun(const Gun* g, int frames, float* out)
{
	St s = { 12345u, 0, 0, { 0, 0, 0, 0 } };
	float dt = 1.0f / AUDIO_RATE;
	for (int i = 0; i < frames; i++) {
		float t = i * dt;
		float n = lowpass(noise(&s), &s.lp, g->nCut) * decay(t, g->nDecay) * g->nMix;
		float f = sweepF(g->tF0, g->tF1, t, g->dur);
		float tn = (osc(&s, 0, f, dt) + 0.3f * sinf(2.0f * s.ph[0])) * decay(t, g->tDecay) * g->tMix;
		float bf = sweepF(g->bF0, g->bF1, t, g->dur);
		float th = osc(&s, 1, bf, dt) * decay(t, g->bDecay) * g->bMix;
		out[i] = (n * 1.6f + tn + th) * g->gain;
	}
	int d = (int)(g->echoT * AUDIO_RATE);
	if (d > 0)
		for (int i = frames - 1; i >= d; i--) out[i] += out[i - d] * g->echoMix;
}

// ------------------------------------------------------------- other effects
static const float sfxDur[SFX_COUNT] = {
	0, 0, 0, 0, 0, 0, 0,             // guns use their own table
	0.22f, 0.30f, 0.15f, 0.08f,      // reload, reload done, switch, empty
	1.40f, 0.18f, 0.15f, 0.09f, 0.25f, 0.80f,   // explosion, jump, land, footstep, hurt, death
	0.06f, 0.35f, 0.30f,             // hit marker, kill, egg crack
	0.05f, 0.14f, 0.12f, 0.40f,      // menu move/select/back, buy
	0.12f, 0.40f, 0.90f, 0.90f,      // countdown tick, go, win, lose
	0.09f, 0.11f, 0.45f,             // ui tap, ui tab, ui start
	0.22f, 0.26f                     // melee swing, melee hit
};

static float noteHz(int midi) { return 440.0f * powf(2.0f, (midi - 69) / 12.0f); }

// One short tone burst starting at 'start' seconds
static float blip(float t, float start, float f, float dec, float len)
{
	float u = t - start;
	if (u < 0.0f || u > len) return 0.0f;
	return sinf(TAU * f * u) * decay(u, dec);
}

static float miscSample(int id, float t, St* s, float dt)
{
	switch (id) {
	case SFX_RELOAD: {
		float c1 = lowpass(noise(s), &s->lp, 0.9f) * decay(t, 90.0f);
		float u = t - 0.12f;
		float c2 = u > 0 ? lowpass(noise(s), &s->lp2, 0.8f) * decay(u, 80.0f) * 0.7f : 0.0f;
		return (c1 + c2) * 0.9f + blip(t, 0.0f, 1500.0f, 60.0f, 0.05f) * 0.3f;
	}
	case SFX_RELOAD_DONE: {
		float c1 = lowpass(noise(s), &s->lp, 0.7f) * decay(t, 70.0f);
		float u = t - 0.10f;
		float c2 = u > 0 ? (lowpass(noise(s), &s->lp2, 0.6f) * decay(u, 55.0f) + blip(u, 0, 220.0f, 35.0f, 0.1f)) : 0.0f;
		return (c1 + c2 * 1.3f) * 1.1f;
	}
	case SFX_SWITCH:
		return lowpass(noise(s), &s->lp, 0.15f + 0.5f * t) * decay(t, 14.0f) * 1.3f + blip(t, 0, 900.0f, 80.0f, 0.03f) * 0.4f;
	case SFX_EMPTY:
		return lowpass(noise(s), &s->lp, 0.95f) * decay(t, 100.0f);
	case SFX_EXPLOSION: {
		float cut = 0.05f + 0.45f * decay(t, 4.0f);
		float n = lowpass(noise(s), &s->lp, cut) * decay(t, 2.6f) * 2.2f;
		float th = osc(s, 0, sweepF(70.0f, 22.0f, t, 1.4f), dt) * decay(t, 3.0f) * 1.2f;
		return n + th;
	}
	case SFX_JUMP:
		return osc(s, 0, sweepF(220.0f, 520.0f, t, 0.18f), dt) * decay(t, 6.0f) * 0.9f;
	case SFX_LAND:
		return osc(s, 0, sweepF(120.0f, 50.0f, t, 0.15f), dt) * decay(t, 25.0f) + lowpass(noise(s), &s->lp, 0.2f) * decay(t, 30.0f) * 0.8f;
	case SFX_FOOTSTEP:
		return lowpass(noise(s), &s->lp, 0.25f) * decay(t, 45.0f) * 1.2f + osc(s, 0, sweepF(110.0f, 70.0f, t, 0.09f), dt) * decay(t, 40.0f) * 0.5f;
	case SFX_HURT: {
		float f = sweepF(320.0f, 150.0f, t, 0.25f);
		s->ph[0] += TAU * f * dt;
		float sq = sinf(s->ph[0]) > 0 ? 1.0f : -1.0f;
		return sq * decay(t, 8.0f) * 0.5f + lowpass(noise(s), &s->lp, 0.6f) * decay(t, 25.0f) * 0.5f;
	}
	case SFX_DEATH: {
		float f = sweepF(420.0f, 50.0f, t, 0.8f);
		s->ph[0] += TAU * f * dt;
		float saw = fmodf(s->ph[0] / TAU, 1.0f) * 2.0f - 1.0f;
		return saw * decay(t, 3.5f) * 0.6f + lowpass(noise(s), &s->lp, 0.3f) * decay(t, 6.0f) * 0.6f;
	}
	case SFX_HITMARK:
		return sinf(TAU * 1800.0f * t) * decay(t, 55.0f) * 0.9f;
	case SFX_KILL:
		return blip(t, 0.0f, 880.0f, 9.0f, 0.2f) + blip(t, 0.12f, 1320.0f, 8.0f, 0.23f);
	case SFX_EGG_CRACK: {
		float c1 = lowpass(noise(s), &s->lp, 0.8f) * decay(t, 22.0f);
		float u = t - 0.08f;
		float c2 = u > 0 ? lowpass(noise(s), &s->lp2, 0.7f) * decay(u, 30.0f) * 0.8f : 0.0f;
		float splat = lowpass(noise(s), &s->lp, 0.25f) * decay(t, 12.0f) * 0.4f;
		return c1 + c2 + splat;
	}
	case SFX_MENU_MOVE:
		return sinf(TAU * 760.0f * t) * decay(t, 60.0f);
	case SFX_MENU_SELECT:
		return blip(t, 0.0f, 620.0f, 20.0f, 0.08f) + blip(t, 0.06f, 930.0f, 20.0f, 0.08f);
	case SFX_MENU_BACK:
		return osc(s, 0, sweepF(520.0f, 340.0f, t, 0.12f), dt) * decay(t, 18.0f);
	case SFX_BUY:
		return blip(t, 0.0f, 1000.0f, 12.0f, 0.2f) + blip(t, 0.09f, 1300.0f, 12.0f, 0.2f) + blip(t, 0.18f, 1700.0f, 9.0f, 0.22f);
	case SFX_COUNT_TICK:
		return sinf(TAU * 800.0f * t) * decay(t, 18.0f);
	case SFX_GO: {
		float f = sweepF(900.0f, 1300.0f, t, 0.4f);
		s->ph[0] += TAU * f * dt;
		return (sinf(s->ph[0]) + 0.5f * sinf(2.0f * s->ph[0])) * decay(t, 7.0f) * 0.7f;
	}
	case SFX_WIN: {
		static const int notes[4] = { 72, 76, 79, 84 };
		float v = 0;
		for (int k = 0; k < 4; k++) v += blip(t, k * 0.15f, noteHz(notes[k]), 6.0f, 0.5f) * (k == 3 ? 1.2f : 0.8f);
		return v * 0.7f;
	}
	case SFX_LOSE: {
		static const int notes[3] = { 67, 64, 60 };
		float v = 0;
		for (int k = 0; k < 3; k++) v += blip(t, k * 0.22f, noteHz(notes[k]), 4.5f, 0.6f);
		return v * 0.7f;
	}
	case SFX_UI_TAP:     // soft bubbly pop for footer buttons and taps
		return osc(s, 0, sweepF(380.0f, 760.0f, t, 0.07f), dt) * decay(t, 32.0f) * 0.9f;
	case SFX_UI_TAB:     // two quick rising notes
		return blip(t, 0.0f, 700.0f, 40.0f, 0.05f) + blip(t, 0.045f, 1050.0f, 40.0f, 0.06f);
	case SFX_UI_START: { // cheerful rising arpeggio for Play / Deploy
		static const int notes[3] = { 72, 76, 84 };
		float v = 0;
		for (int k = 0; k < 3; k++) v += blip(t, k * 0.07f, noteHz(notes[k]), 9.0f, 0.3f);
		return v * 0.6f;
	}
	case SFX_MELEE_SWING: {  // airy whoosh: noise through a filter that opens and closes as the whisk passes
		float env = sinf(3.14159f * fminf(t / 0.22f, 1.0f));
		float cut = 0.05f + 0.35f * env;
		return lowpass(noise(s), &s->lp, cut) * env * env * 1.6f;
	}
	case SFX_MELEE_HIT: {    // a dull thwack and the springy twang of whisk wires
		float thud = osc(s, 0, sweepF(180.0f, 70.0f, t, 0.12f), dt) * decay(t, 28.0f) * 1.1f;
		float slap = lowpass(noise(s), &s->lp, 0.55f) * decay(t, 45.0f) * 1.2f;
		float wob = 1.0f + 0.04f * sinf(TAU * 22.0f * t);
		float twang = (sinf(TAU * 1250.0f * wob * t) + 0.6f * sinf(TAU * 1870.0f * wob * t)) * decay(t, 14.0f) * 0.25f;
		return thud + slap + twang;
	}
	default: return 0.0f;
	}
}

// -------------------------------------------------------------------- output
static short toPcm(float v, int i, int frames)
{
	float fade = 1.0f;
	int tail = 96;
	if (i > frames - tail) fade = (float)(frames - i) / (float)tail;   // avoid a click at the end
	return (short)(tanhf(v) * 30000.0f * fade);
}

int synth_sfx_frames(int id)
{
	if (id < 0 || id >= SFX_COUNT) return 0;
	float d = id < SFX_GUN_COUNT ? guns[id].dur : sfxDur[id];
	int n = (int)(d * AUDIO_RATE);
	return n > MAX_FRAMES ? MAX_FRAMES : n;
}

void synth_sfx_render(int id, short* out, int frames)
{
	int n = synth_sfx_frames(id);
	if (n <= 0) return;
	if (frames < n) n = frames;

	if (id < SFX_GUN_COUNT) {
		renderGun(&guns[id], n, scratch);
	} else {
		St s = { 777u + (unsigned)id * 31u, 0, 0, { 0, 0, 0, 0 } };
		float dt = 1.0f / AUDIO_RATE;
		for (int i = 0; i < n; i++) scratch[i] = miscSample(id, i * dt, &s, dt);
	}
	for (int i = 0; i < n; i++) out[i] = toPcm(scratch[i], i, n);
}

// --------------------------------------------------------------------- music
typedef struct {
	int bpm;
	int roots[4];      // bass root (MIDI) per chord
	int thirds[4];     // 3 = minor, 4 = major
	int pattern[8];    // chord-tone index per eighth note for the lead
	int drums;         // 1 = kick and snare
	float leadGain, bassGain;
} Track;

static const Track tracks[MUSIC_COUNT] = {
	{ 0, { 0 }, { 0 }, { 0 }, 0, 0, 0 },
	/* menu  */ { 104, { 36, 33, 41, 43 }, { 4, 3, 4, 4 }, { 0, 1, 2, 3, 2, 1, 2, 1 }, 0, 0.14f, 0.22f },
	/* match */ { 140, { 45, 41, 36, 43 }, { 3, 4, 4, 4 }, { 0, 2, 1, 2, 3, 2, 1, 0 }, 1, 0.16f, 0.24f },
};

#define MUSIC_STEPS 64   // 8 bars of eighth notes

static int stepFrames(const Track* t) { return (int)(AUDIO_RATE * 60.0f / t->bpm / 2.0f); }

int synth_music_frames(int track)
{
	if (track <= MUSIC_NONE || track >= MUSIC_COUNT) return 0;
	return stepFrames(&tracks[track]) * MUSIC_STEPS;
}

void synth_music_render(int track, short* out, int frames)
{
	int total = synth_music_frames(track);
	if (total <= 0) return;
	if (frames < total) total = frames;

	const Track* tr = &tracks[track];
	int sf = stepFrames(tr);
	St s = { 4242u, 0, 0, { 0, 0, 0, 0 } };
	float dt = 1.0f / AUDIO_RATE;
	float hatLp = 0.0f;

	for (int i = 0; i < total; i++) {
		int step = i / sf;
		int pos = step % 8;
		int chord = (step / 8) % 4;
		float tn = (float)(i % sf) * dt;     // time inside the current eighth note

		int root = tr->roots[chord];
		int tones[4] = { 0, tr->thirds[chord], 7, 12 };

		float lead = square(noteHz(root + 24 + tones[tr->pattern[pos]]), tn, 0.25f) * decay(tn, 6.0f) * tr->leadGain;
		float bass = 0.0f;
		if (pos == 0 || pos == 4 || (tr->drums && (pos == 2 || pos == 6)))
			bass = triangle(noteHz(root), tn) * decay(tn, tr->drums ? 4.5f : 2.5f) * tr->bassGain;

		float x = noise(&s);
		float hat = (x - lowpass(x, &hatLp, 0.35f)) * decay(tn, 60.0f) * ((pos & 1) ? 0.10f : 0.05f);

		float drum = 0.0f;
		if (tr->drums) {
			if (pos == 0 || pos == 4)
				drum += sinf(TAU * sweepF(130.0f, 45.0f, tn < 0.12f ? tn : 0.12f, 0.12f) * tn) * decay(tn, 16.0f) * 0.32f;
			if (pos == 2 || pos == 6)
				drum += lowpass(noise(&s), &s.lp, 0.7f) * decay(tn, 22.0f) * 0.22f;
		}

		float v = lead + bass + hat + drum;
		float fade = 1.0f;
		if (i > total - 200) fade = (float)(total - i) / 200.0f;   // smooth loop seam
		out[i] = (short)(tanhf(v * 1.2f) * 26000.0f * fade);
	}
}

// ------------------------------------------------------------------- spatial
void audio_spatial(float lx, float lz, float listenerYaw, float sx, float sz, float* pan, float* gain)
{
	float dx = sx - lx, dz = sz - lz;
	float dist = sqrtf(dx * dx + dz * dz);
	float p = 0.0f;
	if (dist > 0.5f) {
		float rx = cosf(listenerYaw), rz = sinf(listenerYaw);
		p = (dx * rx + dz * rz) / dist * 0.8f;
	}
	float g = 1.0f - dist / 70.0f;
	if (g < 0.15f) g = 0.15f;
	*pan = p;
	*gain = g;
}
