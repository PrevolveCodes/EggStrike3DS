#ifndef BOTS_H
#define BOTS_H

#include <stdbool.h>
#include "map.h"
#include "nav.h"
#include "player.h"
#include "weapons.h"

#define MAX_BOTS      14
#define MAX_FIGHTERS  (MAX_BOTS + 1)    // fighter 0 is the human player, bots are 1..N
#define BOT_MAX_HP    100.0f
#define BOT_RESPAWN   3.0f

typedef enum { BS_RESPAWN, BS_IDLE, BS_PATROL, BS_SEARCH, BS_CHASE, BS_ATTACK, BS_RETREAT } BotState;
typedef enum { ROLE_ATTACKER, ROLE_DEFENDER, ROLE_SUPPORT, ROLE_ROAMER, ROLE_OBJECTIVE } BotRole;

// A bot's personality, fixed for the whole match, so each bot is recognisable by how it plays:
//   RUSHER  closes in fast, strafes and hops, likes shotguns and SMGs, takes more risks
//   HOLDER  picks spots with long sightlines, waits there and scans, likes the long guns
//   FLANKER takes the side routes and comes at the enemy from an angle
//   BALANCED plays as before
typedef enum { STYLE_BALANCED, STYLE_RUSHER, STYLE_HOLDER, STYLE_FLANKER, STYLE_COUNT } BotStyle;

// Difficulty changes how well a bot perceives and aims, never what it knows.
typedef struct {
	float vision;        // sight range (m)
	float fovCos;        // cosine of half the field of view
	float reactMin, reactMax;   // delay before firing at a newly seen enemy (s)
	float turnRate;      // max turning speed (rad/s)
	float trackRate;     // how quickly the aim point follows a moving target
	float settle;        // seconds to settle onto a target
	float errMax;        // extra aim error right after acquiring a target (rad)
	float spreadMul;     // multiplier on weapon spread
	float tolerance;     // aim error under which it opens fire (rad)
	float speedMul;      // movement speed
	float retreatHp;     // retreat below this health fraction
	float offMin, offMax;   // pauses between bursts (s)
	float strafe;        // strafing amount in fights
	float aimNoise;      // per-shot aim error (std dev, radians): hands are never steady
	float leadMul;       // how much of a target's sideways motion it leads with slow shots (rockets); 0 = none
} BotSkill;

typedef struct {
	bool  active;
	int   id;                 // fighter id (1..MAX_BOTS)
	int   team;               // 0 red, 1 blue (team modes only)
	char  name[16];

	float x, y, z, vy;
	bool  onGround;
	float yaw, pitch;

	float hp;
	bool  alive;
	float respawnTimer;
	float lastHitTimer;
	float hitFlash, flashT;
	float invuln;             // spawn protection time left
	int   kills, deaths;
	Perks perks;              // kill streak and its perks
	float grenadeCd;          // bots throw one now and then (they carry plenty)
	float meleeT;             // whisk swing time left (drawn in the hand instead of the gun)
	float meleeCd;            // time until the next swing
	bool  meleeHitDone;

	int         weapon;
	WeaponState ws;

	// --- AI ---
	BotState state;
	BotRole  role;
	BotStyle style;
	int      flankLeg;        // flankers alternate: 0 = swing out to the side, 1 = strike
	float    holdLeft;        // holders wait this long at their spot before moving on
	bool     holdPending;     // set when a hold spot was chosen: wait there once the bot arrives
	float    holdDir, holdFlip;
	float stateTime, roleTimer;
	float retreatHp;

	int   target;             // fighter id currently being fought, or -1
	int   lastTarget;
	float reactLeft;          // reaction delay still to run
	float onTarget;           // time spent on the current target
	float aimPos[3];
	bool  aimValid;
	float seenTimer[MAX_FIGHTERS];       // seconds since each fighter was last seen
	float seenPos[MAX_FIGHTERS][3];      // where each fighter was last seen
	float seenVel[MAX_FIGHTERS][3];      // how fast it was moving between sightings (m/s, horizontal)
	float targetBias[MAX_FIGHTERS];      // per-bot taste so bots do not all pick the same victim

	short path[NAV_MAX_PATH];
	int   pathLen, pathIdx;
	float repathTimer;

	float strafeDir, strafeTimer;
	float stuckTimer, progX, progZ, progTimer;
	float jumpTimer;

	float searchPos[3];
	float searchTimer, lookTimer;
	float retreatCool;
	float idleTime;
	bool  alertValid;         // hurt by someone unseen: only the rough direction is known
	bool  alertUsed;
	float alertYaw;
	float moveYaw;            // direction of travel, used for facing when nothing is in sight

	float burstTimer;
	bool  burstOn;
	float shareTimer;
	float objTimer;           // re-think the objective goal when this runs out
	bool  visibleToPlayer;
	unsigned frame;

	// --- local multiplayer ---
	bool  remote;             // host: a person on another console (no AI; position comes from the network)
	int   node;               // host: network node of that person
	int   spawnSeq;           // bumped at every spawn; the owner teleports when it changes
	int   lastAttacker;       // fighter id of whoever hurt this fighter last (-1 none); sent to remote owners
	int   ackSpawn;           // host: the spawnSeq the owner last confirmed
	float netPos[3];          // client mirror: where the last snapshot put this fighter
	float netYaw, netPitch;
} Bot;

const BotSkill* bot_skill(int difficulty);
void bot_weapon_range(int weapon, float* prefMin, float* prefMax);
const char* bot_name(int index);
const char* bot_style_name(BotStyle s);

struct Combat;

void bot_create(struct Combat* c, int index, int team);
void bot_respawn(struct Combat* c, const Player* p, Bot* b);
void bot_update(struct Combat* c, Player* p, const Map* map, Bot* b, float dt);

// Used by the decision code
bool bot_goto_pos(struct Combat* c, Bot* b, const float pos[3]);
bool bot_goto_node(struct Combat* c, Bot* b, int node);
void bot_stop_path(Bot* b);
bool bot_path_done(const Bot* b);
bool bot_line_clear(const Map* map, const float a[3], const float b[3]);   // no wall between two points
void bot_decide(struct Combat* c, Player* p, Bot* b, float dt);

#endif
