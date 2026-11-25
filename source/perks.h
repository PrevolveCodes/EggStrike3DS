#ifndef PERKS_H
#define PERKS_H

// Kill streaks. Anyone (you or a bot) who keeps getting kills without being cracked earns perks:
//    3 kills  SHELL SHIELD  +40 shield that soaks up damage before your health (up to 80)
//    5 kills  HOT YOLK      10 s of faster shooting and reloading
//    7 kills  CRACK SHOT    8 s of 25% more damage
//   10, 15, 20 ...  GOLDEN EGG  full health and a fresh shield
// A real death ends the streak and every perk. The pause-menu Respawn keeps the streak count but
// the shield and timed perks do not come along.
#define STREAK_SHIELD_AT   3
#define STREAK_HOT_AT      5
#define STREAK_CRACK_AT    7
#define STREAK_GOLDEN_AT   10
#define STREAK_GOLDEN_STEP 5

#define PERK_SHIELD_GAIN   40.0f
#define PERK_SHIELD_MAX    80.0f
#define PERK_HOT_TIME      10.0f
#define PERK_HOT_SPEED     1.35f   // weapon clock runs this much faster
#define PERK_CRACK_TIME    8.0f
#define PERK_CRACK_DAMAGE  1.25f   // was 1.5: with Shell Shockers damage it made every sniper hit a kill

typedef struct {
	int   streak;     // kills since the last death
	float shield;     // damage soaked before health
	float hotT;       // Hot Yolk time left
	float crackT;     // Crack Shot time left
} Perks;

#endif
