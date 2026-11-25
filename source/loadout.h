#ifndef LOADOUT_H
#define LOADOUT_H

#include "input.h"
#include "profile.h"

typedef enum { LO_NONE, LO_SPAWN } LoadoutAction;

// "Change your weapon?" screen on the bottom screen: shown during the map flyover
// before a match and again when you are eliminated.
// waitTime > 0 means GET READY is not available yet (respawn delay).
void          loadout_open(const PlayerProfile* prof);
LoadoutAction loadout_update(const UiInput* in, PlayerProfile* prof, float waitTime);
void          loadout_draw(const PlayerProfile* prof, float waitTime);
// Same, but with your own text on the greyed-out button ("NEXT ROUND") while waitTime > 0.
void          loadout_draw_wait(const PlayerProfile* prof, float waitTime, const char* waitText);

#endif
