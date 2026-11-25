#include "audio.h"
#include <3ds.h>
#include <math.h>
#include <string.h>

#define SFX_CHANNELS 14        // DSP channels 1..14 for effects, channel 0 for music
#define MUSIC_GAIN   0.55f

static bool ready;
static bool muted;      // true while a match is being warmed up silently
static short* sfxData[SFX_COUNT];
static int    sfxFrames[SFX_COUNT];
static short* musicData[MUSIC_COUNT];
static int    musicFrames[MUSIC_COUNT];
static ndspWaveBuf sfxBuf[SFX_CHANNELS + 1];
static ndspWaveBuf musicBuf;

static int musicVol = 7, sfxVol = 8;
static int curTrack = MUSIC_NONE;
static int nextChan = 1;
static unsigned rngState = 2463534242u;

static float rand01(void)
{
	rngState ^= rngState << 13;
	rngState ^= rngState >> 17;
	rngState ^= rngState << 5;
	return (float)(rngState >> 8) * (1.0f / 16777216.0f);
}

static void setMix(int ch, float left, float right)
{
	float mix[12];
	memset(mix, 0, sizeof(mix));
	mix[0] = left;
	mix[1] = right;
	ndspChnSetMix(ch, mix);
}

static void configureChannel(int ch, float rate)
{
	ndspChnReset(ch);
	ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
	ndspChnSetRate(ch, rate);
	ndspChnSetFormat(ch, NDSP_FORMAT_MONO_PCM16);
}

bool audio_init(void)
{
	if (R_FAILED(ndspInit())) return false;
	ndspSetOutputMode(NDSP_OUTPUT_STEREO);

	for (int id = 0; id < SFX_COUNT; id++) {
		int n = synth_sfx_frames(id);
		if (n <= 0) continue;
		short* d = (short*)linearAlloc((size_t)n * sizeof(short));
		if (!d) continue;
		synth_sfx_render(id, d, n);
		DSP_FlushDataCache(d, (u32)(n * sizeof(short)));
		sfxData[id] = d;
		sfxFrames[id] = n;
	}
	// Music was removed: the tracks are no longer generated, so audio_music() never has anything to play

	for (int ch = 0; ch <= SFX_CHANNELS; ch++) configureChannel(ch, (float)AUDIO_RATE);
	ready = true;
	return true;
}

void audio_shutdown(void)
{
	if (!ready) return;
	for (int ch = 0; ch <= SFX_CHANNELS; ch++) ndspChnReset(ch);
	ndspExit();
	for (int i = 0; i < SFX_COUNT; i++) { if (sfxData[i]) linearFree(sfxData[i]); sfxData[i] = NULL; }
	for (int i = 0; i < MUSIC_COUNT; i++) { if (musicData[i]) linearFree(musicData[i]); musicData[i] = NULL; }
	ready = false;
}

bool audio_available(void) { return ready; }

void audio_set_volume(int music, int sfx)
{
	if (music < 0) music = 0;
	if (music > 10) music = 10;
	if (sfx < 0) sfx = 0;
	if (sfx > 10) sfx = 10;
	if (music == musicVol && sfx == sfxVol) return;
	musicVol = music;
	sfxVol = sfx;
	if (ready && curTrack != MUSIC_NONE) {
		float v = (musicVol / 10.0f) * MUSIC_GAIN;
		setMix(0, v, v);
	}
}

void audio_play(int sfx, float pan, float gain)
{
	if (!ready || muted || sfx < 0 || sfx >= SFX_COUNT || !sfxData[sfx]) return;
	float vol = (sfxVol / 10.0f) * gain;
	if (vol < 0.01f) return;

	// Find a free channel, otherwise steal the next one in rotation
	int ch = -1;
	for (int k = 0; k < SFX_CHANNELS; k++) {
		int c = 1 + (nextChan - 1 + k) % SFX_CHANNELS;
		if (!ndspChnIsPlaying(c)) { ch = c; break; }
	}
	if (ch < 0) ch = nextChan;
	nextChan = ch % SFX_CHANNELS + 1;

	// A little pitch variation keeps repeated shots from sounding identical
	float pitch = sfx < SFX_MENU_MOVE ? 0.94f + 0.12f * rand01() : 1.0f;
	configureChannel(ch, AUDIO_RATE * pitch);

	if (pan < -1.0f) pan = -1.0f;
	if (pan > 1.0f) pan = 1.0f;
	float a = (pan + 1.0f) * 0.7853982f;   // constant-power pan
	setMix(ch, vol * cosf(a) * 1.414f, vol * sinf(a) * 1.414f);

	memset(&sfxBuf[ch], 0, sizeof(ndspWaveBuf));
	sfxBuf[ch].data_vaddr = sfxData[sfx];
	sfxBuf[ch].nsamples = (u32)sfxFrames[sfx];
	sfxBuf[ch].looping = false;
	ndspChnWaveBufAdd(ch, &sfxBuf[ch]);
}

void audio_set_muted(bool m) { muted = m; }

void audio_play_at(int sfx, float lx, float lz, float listenerYaw, float sx, float sz, float gain)
{
	float pan, g;
	audio_spatial(lx, lz, listenerYaw, sx, sz, &pan, &g);
	audio_play(sfx, pan, g * gain);
}

void audio_music(int track)
{
	if (!ready || track == curTrack) return;
	curTrack = track;

	configureChannel(0, (float)AUDIO_RATE);
	if (track <= MUSIC_NONE || track >= MUSIC_COUNT || !musicData[track]) { curTrack = MUSIC_NONE; return; }

	float v = (musicVol / 10.0f) * MUSIC_GAIN;
	setMix(0, v, v);
	memset(&musicBuf, 0, sizeof(musicBuf));
	musicBuf.data_vaddr = musicData[track];
	musicBuf.nsamples = (u32)musicFrames[track];
	musicBuf.looping = true;
	ndspChnWaveBufAdd(0, &musicBuf);
}
