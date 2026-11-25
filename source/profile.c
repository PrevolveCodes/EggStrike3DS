#include "profile.h"
#include "cosmetics.h"
#include "weapons.h"
#include <string.h>

void profile_defaults(PlayerProfile* p)
{
	memset(p, 0, sizeof(*p));
	settings_defaults(&p->settings);
	cos_defaults(&p->cos);
	p->favoriteWeapon = W_PECKMATIC;
	p->secondaryWeapon = W_EGGSTOL;
	match_defaults(&p->lobby);
}

void profile_sanitize(PlayerProfile* p)
{
	settings_clamp(&p->settings);
	match_sanitize(&p->lobby);
	if (p->favoriteWeapon < 0 || p->favoriteWeapon >= PRIMARY_COUNT) p->favoriteWeapon = W_PECKMATIC;
	p->secondaryWeapon = W_EGGSTOL;   // fixed; older saves with another secondary are corrected here
	for (int c = 0; c < COS_CATEGORIES; c++) {
		int n = cos_count(c);
		if (p->cos.sel[c] < 0 || p->cos.sel[c] >= n) p->cos.sel[c] = 0;
		p->cos.unlocked[c] |= 1; // the first item of each category is always owned
		if (!cos_unlocked(&p->cos, c, p->cos.sel[c])) p->cos.sel[c] = 0;
	}
}

void profile_set_weapon(PlayerProfile* p, int slot, int w)
{
	if (slot != 0) return;                    // the secondary is always the Eggstol
	if (w < 0 || w >= PRIMARY_COUNT) return;  // and the Eggstol is never a primary
	p->favoriteWeapon = w;
	p->secondaryWeapon = W_EGGSTOL;
}
