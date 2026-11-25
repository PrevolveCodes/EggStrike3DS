#ifndef OBJECTIVE_H
#define OBJECTIVE_H

#include <stdbool.h>
#include "player.h"

// Rules for the team modes that have an objective:
//   King of the Pan  - the team alone inside the central pan earns a point every second.
//   Capture the Flag - steal the enemy flag and bring it to your own flag stand while yours is home.
//   Team Elimination - no respawns inside a round; the last team with an egg standing wins the round.
// The score limit of each mode is in match.c (points, captures, rounds); combat.teamScore holds the score.

#define PAN_RADIUS        4.5f
#define FLAG_TOUCH        1.5f    // how close an egg must be to take, return or capture a flag
#define FLAG_RETURN_TIME  15.0f   // a dropped flag goes home by itself after this long
#define ROUND_PAUSE       4.0f    // Team Elimination: pause between rounds

typedef struct {
	float home[3];      // the flag stand
	float pos[3];       // where the flag is now (follows the carrier)
	int   carrier;      // fighter id carrying it, or -1
	bool  atHome;
	float returnTimer;  // counts down while the flag lies dropped
} Flag;

typedef struct {
	// King of the Pan
	float panPos[3];
	int   holder;           // -1 nobody inside, 0 red, 1 blue, 2 contested
	float pointAcc[2];

	// Capture the Flag (flag[t] is team t's own flag)
	Flag  flag[2];

	// Team Elimination
	int   round;            // 1, 2, 3 ...
	int   roundSize[2];     // how many eggs each team started the round with
	float roundPause;       // > 0 between rounds (nobody can be hurt, bots hold still)
	float roundTime;        // seconds the current round has been running (bots push forward when it drags on)
	int   roundWinner;      // 0 red, 1 blue, 2 draw, -1 round in progress
	bool  newRound;         // set when a round starts: the game puts the human back in
	int   aliveCount[2];

	// A short message for the top of the screen ("BLUE TEAM CAPTURED THE FLAG!")
	char  msg[56];
	float msgT;
	int   msgTeam;          // 0 red, 1 blue, -1 neutral
} Objective;

struct Combat;

void obj_init(struct Combat* c);
void obj_recount(struct Combat* c);       // Team Elimination: count the eggs on each team again (a person joined)
void obj_update(struct Combat* c, Player* p, float dt);
void obj_on_death(struct Combat* c, int fighterId, const float pos[3]);   // drops a carried flag
void obj_draw(const struct Combat* c);                                   // pan, flags (3D, world space)
// One line of text describing the objective right now ("RED FLAG: HOME   BLUE FLAG: TAKEN"). Returns the length.
int  obj_status(const struct Combat* c, char* out, int n);

#endif
