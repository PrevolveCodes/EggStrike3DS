#include "input.h"
#include "config.h"
#include "hud.h"
#include <3ds.h>
#include <math.h>
#include <string.h>

static u32 kDown, kHeld, kUp;
static int touchX, touchY;
static int repeatFrames[4];   // up, down, left, right
static bool touchLookActive;
static int lastTouchX, lastTouchY;

void input_init(void)
{
	kDown = kHeld = kUp = 0;
	touchX = touchY = 0;
	memset(repeatFrames, 0, sizeof(repeatFrames));
	touchLookActive = false;
}

void input_update(void)
{
	hidScanInput();
	kDown = hidKeysDown();
	kHeld = hidKeysHeld();
	kUp   = hidKeysUp();

	if (kHeld & KEY_TOUCH) {
		touchPosition t;
		hidTouchRead(&t);
		touchX = t.px;
		touchY = t.py;
	}

	const u32 dirKeys[4] = { KEY_DUP, KEY_DDOWN, KEY_DLEFT, KEY_DRIGHT };
	for (int i = 0; i < 4; i++) {
		if (kHeld & dirKeys[i]) repeatFrames[i]++;
		else repeatFrames[i] = 0;
	}
}

static bool repeatFire(int frames)
{
	// fires on the first frame, then repeats while held
	return frames == 1 || (frames > 14 && (frames % 3) == 0);
}

void input_get_ui(UiInput* out)
{
	out->up     = repeatFire(repeatFrames[0]);
	out->down   = repeatFire(repeatFrames[1]);
	out->left   = repeatFire(repeatFrames[2]);
	out->right  = repeatFire(repeatFrames[3]);
	out->accept = (kDown & KEY_A) != 0;
	out->back   = (kDown & KEY_B) != 0;
	out->start  = (kDown & KEY_START) != 0;
	out->alt    = (kDown & KEY_Y) != 0;
	out->extra  = (kDown & KEY_X) != 0;
	out->pageL  = (kDown & KEY_L) != 0;
	out->pageR  = (kDown & KEY_R) != 0;
	out->selectHeld = (kHeld & KEY_SELECT) != 0;
	out->touchPressed  = (kDown & KEY_TOUCH) != 0;
	out->touchHeld     = (kHeld & KEY_TOUCH) != 0;
	out->touchReleased = (kUp & KEY_TOUCH) != 0;
	out->tx = touchX;
	out->ty = touchY;
}

static void applyDeadzone(float* x, float* y)
{
	float mag = sqrtf((*x) * (*x) + (*y) * (*y));
	if (mag < 0.12f) { *x = 0.0f; *y = 0.0f; return; }
	if (mag > 1.0f)  { *x /= mag; *y /= mag; }
}

void input_get_player(PlayerInput* out, float dt, const Settings* s)
{
	memset(out, 0, sizeof(*out));
	out->selectWeapon = -1;

	float sens = 0.2f * (float)s->lookSens;   // 5 = 1.0
	float ySign = s->invertY ? -1.0f : 1.0f;

	circlePosition cp, cs;
	hidCircleRead(&cp);
	hidCstickRead(&cs); // zero on original 3DS

	float mx = cp.dx / 156.0f, my = cp.dy / 156.0f;
	applyDeadzone(&mx, &my);
	out->moveX = mx;
	out->moveY = my;

	float lx = cs.dx / 156.0f, ly = cs.dy / 156.0f;
	applyDeadzone(&lx, &ly);
	out->lookYaw   = lx * LOOK_STICK_RATE * sens * dt;
	out->lookPitch = ly * LOOK_STICK_RATE * sens * dt * ySign;

	// Touch-drag look: only for touches that did not start on a HUD button
	if (s->touchLook) {
		if (kDown & KEY_TOUCH) {
			touchLookActive = !hud_touch_is_ui(touchX, touchY);
			lastTouchX = touchX;
			lastTouchY = touchY;
		} else if ((kHeld & KEY_TOUCH) && touchLookActive) {
			out->lookYaw   += (float)(touchX - lastTouchX) * LOOK_TOUCH_SENS * sens;
			out->lookPitch -= (float)(touchY - lastTouchY) * LOOK_TOUCH_SENS * sens * ySign;
			lastTouchX = touchX;
			lastTouchY = touchY;
		}
	}
	if (kUp & KEY_TOUCH) touchLookActive = false;

	u32 fireKey = s->swapTriggers ? KEY_L : KEY_R;
	u32 aimKey  = s->swapTriggers ? KEY_R : KEY_L;

	out->jump       = (kHeld & KEY_A) != 0;
	out->fire       = (kHeld & fireKey) != 0;
	out->fireDown   = (kDown & fireKey) != 0;
	out->aim        = (kHeld & aimKey) != 0;
#ifdef MELEE_TEST
	{ static bool latch; if (kDown & KEY_DUP) latch = !latch; if (latch) out->aim = true; }   /* test build: D-Pad Up latches aiming */
#endif
	out->reload     = (kDown & KEY_B) != 0;
	out->grenade    = (kDown & KEY_Y) != 0;
	out->melee      = (kDown & (KEY_DDOWN | KEY_ZL | KEY_ZR)) != 0;
	out->nextWeapon = (kDown & (KEY_X | KEY_DRIGHT)) != 0;
	out->prevWeapon = (kDown & KEY_DLEFT) != 0;
}
