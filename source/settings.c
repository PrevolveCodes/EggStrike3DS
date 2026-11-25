#include "settings.h"

static int clampi(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

void settings_defaults(Settings* s)
{
	s->lookSens = 5;
	s->invertY = 0;
	s->touchLook = 1;
	s->swapTriggers = 0;
	s->musicVol = 7;
	s->sfxVol = 8;
}

void settings_clamp(Settings* s)
{
	s->lookSens = clampi(s->lookSens, 1, 10);
	s->invertY = s->invertY ? 1 : 0;
	s->touchLook = s->touchLook ? 1 : 0;
	s->swapTriggers = s->swapTriggers ? 1 : 0;
	s->musicVol = clampi(s->musicVol, 0, 10);
	s->sfxVol = clampi(s->sfxVol, 0, 10);
}
