#ifndef COMBAT_H
#define COMBAT_H

#include <stdbool.h>
#include "bots.h"
#include "config.h"
#include "input.h"
#include "map.h"
#include "match.h"
#include "nav.h"
#include "objective.h"
#include "pickups.h"
#include "player.h"
#include "scoreboard.h"

#define FIGHTER_PLAYER 0
#define HIST_N 32            // frames of fighter positions kept for lag compensation (host)

// Local multiplayer role of this console. Offline play and the host run the whole simulation;
// a client only mirrors what the host reports and plays its own egg.
typedef enum { NET_OFFLINE, NET_HOST, NET_CLIENT } NetRole;

typedef struct {
	bool  active;
	float pos[3];
	float vel[3];
	int   weapon;
	int   owner;     // fighter id that fired it
	float life;
} Projectile;

typedef struct {
	bool  active;
	float pos[3];
	float vel[3];
	int   owner;
	float fuse;
} Grenade;

typedef struct Combat {
	Bot        bots[MAX_BOTS];
	int        botCount;
	Nav        nav;
	const Map* map;
	Projectile proj[MAX_PROJECTILES];
	Grenade    gren[MAX_GRENADES];

	int   mode;
	int   playerTeam;
	int   difficulty;
	int   teamScore[2];     // TDM kills, CTF captures, KOTP points, ELIM round wins
	Objective obj;
	AmmoBox ammoBox[AMMO_BOX_MAX];   // ammo crates around the map (see pickups.h)
	int   ammoBoxCount;
	char  perkMsg[32];       // banner for the human's newest perk
	float perkMsgT;
	float base[2][3];       // centre of each team's spawns
	bool  hasBase;

	bool  frozen;           // bots hold still (intro, countdown, match over)
	int   frame;
	int   pathBudget;       // path searches allowed this frame

	// local multiplayer (see netplay.c). combat_init clears these; netplay sets them afterwards.
	int   netRole;                         // NetRole
	bool  noPlayer;                        // dedicated server: fighter 0 is an empty seat
	bool  rewindOn;                        // host: shots from a remote player are tested against the past
	int   rewindSlot, rewindSelf;
	float hist[HIST_N][MAX_FIGHTERS][3];   // host: where every fighter was in each recent frame
} Combat;

// Things the multiplayer layer wants to hear about. All are optional (NULL when offline).
typedef struct {
	void (*onShot)(Combat* c, int shooter, int weapon, const float eye[3], const float dir[3], float spread);  // host: a fighter fired
	void (*onBoom)(const float pos[3], float radius);                    // host: an explosion
	void (*onHit)(int attacker, int victim);                             // host: damage was dealt
	void (*onMelee)(int attacker, bool hit);                             // host: a fighter swung the whisk
	void (*onLocalFire)(int weapon, float spread, const float dir[3]);   // client: my own shot
	void (*onLocalGrenade)(const float dir[3]);                          // client: my own grenade
	void (*onLocalMelee)(float yaw);                                     // client: my own whisk whack landed (or missed)
} CombatHooks;
extern CombatHooks combat_hooks;

// A client's mirror of a match (no bots, no pathfinding); netplay.c fills it from the host's snapshots.
void combat_init_server(Combat* c, const Map* map, const MatchConfig* cfg);   // dedicated server: nobody plays fighter 0 (it stays down and is left out of lists)
void combat_init_mirror(Combat* c, const Map* map, const MatchConfig* cfg);
// Creates the bots for a match config and spawns them. The player is placed afterwards.
void combat_init(Combat* c, const Map* map, const MatchConfig* cfg);
// Player weapon handling, bots, projectiles, damage, death and regeneration.
void combat_update(Combat* c, Player* p, const PlayerInput* in, const Map* map, float dt);
void combat_draw(const Combat* c, const float cam[3]);
// Bot names as 2D text over the 3D view (call after renderer_top_overlay).
void combat_draw_names(const Combat* c, const Player* cam, float zoom);   // cam = camera position (for outline thickness)

// Lobs a grenade from 'eye' along 'dir' (speed in m/s). Used by the player and by bots.
void combat_throw_grenade(Combat* c, int owner, const float eye[3], const float dir[3], float speed);

// Melee (the whisk whack). combat_melee hits the nearest enemy in front of 'eye' within MELEE_RANGE,
// plays the hit, and deals the damage (not on a client). Returns the fighter hit or -1; *back is set
// when it struck from behind. combat_melee_target only looks (used for the lunge and by bots).
int  combat_melee(Combat* c, Player* p, const Map* map, int attacker, const float eye[3], float yaw, bool* back);
int  combat_melee_target(const Combat* c, const Player* p, const Map* map, int attacker,
                         const float eye[3], float yaw, float range, float* dist);

int  combat_choose_spawn(const Combat* c, const Player* p, const Map* map, int team, int selfId);

bool combat_is_team_mode(const Combat* c);
int  combat_team_of(const Combat* c, const Player* p, int id);   // -1 in free-for-all
bool combat_is_enemy(const Combat* c, const Player* p, int a, int b);
bool combat_alive(const Combat* c, const Player* p, int id);
void combat_pos(const Combat* c, const Player* p, int id, float out[3]);   // feet position
Perks* combat_perks(Combat* c, Player* p, int id);                          // kill streak and perks of a fighter

void combat_damage(Combat* c, Player* p, int attacker, int victim, float dmg);
void combat_fire(Combat* c, Player* p, const Map* map, int shooter, const float eye[3],
                 const float dir[3], float spread, int weapon);

// ---- local multiplayer
// Turns an AI slot into a person on another console (or adds a slot). Returns the fighter id, -1 if full.
int  combat_add_remote(Combat* c, const char* name, int team, int node, int weapon);
void combat_drop_remote(Combat* c, int id);                        // the person left: the AI takes the egg back
void combat_remote_update(Combat* c, Player* p, Bot* b, float dt); // host: timers and regeneration for a remote egg
void combat_remote_respawn(Combat* c, Player* p, int id, int weapon);   // host: the owner asked to come back
void combat_update_client(Combat* c, Player* p, const PlayerInput* in, const Map* map, float dt);
void combat_record_history(Combat* c, const Player* p);            // host: remember this frame's positions
void combat_rewind_begin(Combat* c, int frameThen, int selfId);    // host: test shots against where eggs were
void combat_rewind_end(Combat* c);
void combat_boom_fx(Combat* c, const Player* p, const float pos[3], float radius);   // client: show an explosion
const char* combat_streak_banner(int streak);                      // text for a kill streak that just started a perk, or NULL
void combat_banner(Combat* c, const char* text);                   // the big banner for the human's perks

int  combat_scoreboard(const Combat* c, const Player* p, ScoreRow* rows, int max);
int  combat_bots_alive(const Combat* c);
int  combat_best_bot_kills(const Combat* c);

#endif
