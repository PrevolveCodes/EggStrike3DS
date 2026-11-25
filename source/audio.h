#ifndef AUDIO_H
#define AUDIO_H

#include <stdbool.h>
#include "synth.h"

bool audio_init(void);         // false if the DSP is unavailable: the game just runs silent
void audio_shutdown(void);
bool audio_available(void);
void audio_set_volume(int music, int sfx);   // 0..10 each
void audio_play(int sfx, float pan, float gain);
void audio_set_muted(bool muted);   // silences effects (music keeps playing); used while a match warms up
void audio_play_at(int sfx, float lx, float lz, float listenerYaw, float sx, float sz, float gain);
void audio_music(int track);   // MusicId; MUSIC_NONE stops

#endif
