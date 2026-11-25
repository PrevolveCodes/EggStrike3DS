#ifndef PROFILE_H
#define PROFILE_H

#include "match.h"
#include "settings.h"

typedef struct {
	unsigned int kills, deaths;
	unsigned int matchesPlayed, matchesWon;
	unsigned int flagsCaptured;
	unsigned int coins;
	unsigned int playSeconds;
} Stats;

enum { COS_SHELL, COS_HAT, COS_ACC, COS_SKIN, COS_CATEGORIES };

typedef struct {
	int sel[COS_CATEGORIES];                 // equipped item per category
	unsigned char unlocked[COS_CATEGORIES];  // bit per item
} Cosmetics;

typedef struct {
	Settings    settings;
	Stats       stats;
	Cosmetics   cos;
	int         favoriteWeapon;    // primary weapon
	int         secondaryWeapon;
	MatchConfig lobby;
} PlayerProfile;

void profile_defaults(PlayerProfile* p);
void profile_sanitize(PlayerProfile* p);
// Set the primary weapon (slot 0). Slot 1 is ignored: the secondary is always the Eggstol pistol.
void profile_set_weapon(PlayerProfile* p, int slot, int w);

#endif
