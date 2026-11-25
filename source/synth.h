#ifndef SYNTH_H
#define SYNTH_H

// Procedural sound: every effect and music loop is generated from maths at startup,
// so the game needs no audio files. Mono 16-bit PCM at AUDIO_RATE.
#define AUDIO_RATE 16000

#define SFX_GUN_COUNT 7   // must equal WEAPON_COUNT (checked in combat.c)

typedef enum {
	// Weapon fire: SFX_FIRE_BASE + weapon id (same order as WeaponId, SFX_GUN_COUNT guns)
	SFX_FIRE_BASE = 0,
	SFX_RELOAD = 7, SFX_RELOAD_DONE, SFX_SWITCH, SFX_EMPTY,
	SFX_EXPLOSION, SFX_JUMP, SFX_LAND, SFX_FOOTSTEP, SFX_HURT, SFX_DEATH,
	SFX_HITMARK, SFX_KILL, SFX_EGG_CRACK,
	SFX_MENU_MOVE, SFX_MENU_SELECT, SFX_MENU_BACK, SFX_BUY,
	SFX_COUNT_TICK, SFX_GO, SFX_WIN, SFX_LOSE,
	SFX_UI_TAP, SFX_UI_TAB, SFX_UI_START,
	SFX_MELEE_SWING, SFX_MELEE_HIT,
	SFX_COUNT
} SfxId;

typedef enum { MUSIC_NONE, MUSIC_MENU, MUSIC_MATCH, MUSIC_COUNT } MusicId;

int  synth_sfx_frames(int id);
void synth_sfx_render(int id, short* out, int frames);
int  synth_music_frames(int track);
void synth_music_render(int track, short* out, int frames);

// Pan (-1 left .. 1 right) and loudness (0..1) for a sound at (sx,sz) heard from (lx,lz).
void audio_spatial(float lx, float lz, float listenerYaw, float sx, float sz, float* pan, float* gain);

#endif
