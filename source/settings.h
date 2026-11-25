#ifndef SETTINGS_H
#define SETTINGS_H

typedef struct {
	int lookSens;      // 1..10 (5 = default)
	int invertY;       // 0/1
	int touchLook;     // 0/1 drag the bottom screen to look
	int swapTriggers;  // 0 = R fire / L aim, 1 = L fire / R aim
	int musicVol;      // 0..10
	int sfxVol;        // 0..10
} Settings;

void settings_defaults(Settings* s);
void settings_clamp(Settings* s);

#endif
