#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include "settings.h"

typedef struct {
	float moveX, moveY;       // -1..1 (strafe right, forward)
	float lookYaw, lookPitch; // radians to add this frame
	bool  jump;
	bool  fire;        // trigger held
	bool  fireDown;    // trigger pressed this frame
	bool  aim;         // aim held
	bool  reload;      // pressed
	bool  grenade;     // throw a grenade (Y)
	bool  melee;       // whisk whack (D-Pad Down, ZL, ZR)
	bool  nextWeapon;  // X or D-Pad right
	bool  prevWeapon;  // D-Pad left
	int   selectWeapon;// -1, or loadout slot 0/1 (tapped on the bottom screen)
} PlayerInput;

typedef struct {
	bool up, down, left, right;   // D-Pad with key repeat
	bool accept, back, start;     // A, B, START
	bool alt, extra;              // Y, X
	bool pageL, pageR;            // L and R shoulder buttons (switch pages / tabs)
	bool selectHeld;              // SELECT held: show the scoreboard
	bool touchPressed, touchHeld, touchReleased;
	int  tx, ty;                  // touch position on the bottom screen
} UiInput;

void input_init(void);
void input_update(void);                       // call once per frame
void input_get_ui(UiInput* out);
void input_get_player(PlayerInput* out, float dt, const Settings* s);

#endif
