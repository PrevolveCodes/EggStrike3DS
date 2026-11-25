#ifndef NETPLAY_H
#define NETPLAY_H

#include <stdbool.h>
#include "combat.h"
#include "input.h"
#include "match.h"
#include "net.h"

// Local multiplayer: one console hosts, up to three friends join over local wireless.
//
// The host runs the whole match (bots, damage, objectives, scores). Each friend's console moves its own
// egg and sends where it is plus what it did (shots, grenades, "I want to respawn"). The host sends
// everybody a snapshot of the match about 15 times a second. A client keeps a mirror of the match for
// drawing and the HUD. Ids are swapped on a client so that its own egg is always fighter 0, like offline.

#define NP_MAX_HUMANS NET_MAX_NODES   // slot 0 is the host
#define NP_VERSION    7

typedef enum { NP_OFF, NP_HOST_ROOM, NP_CLIENT_ROOM, NP_HOST_MATCH, NP_CLIENT_MATCH } NpState;

typedef struct {
	bool  used;
	int   node;           // network node (1 = host)
	char  name[16];
	int   team;           // 0 red, 1 blue (team modes)
	int   weapon;         // primary weapon chosen in the menu
	int   fighter;        // host: fighter id in the match (-1 in the room)
	bool  ready;          // host: has loaded the match
	float silence;        // host: seconds since this friend last sent anything
	int   evSeq;          // host: last event applied
	int   inSeq;          // host: last input packet applied
	int   lastSpawnSeen;
} NpHuman;

typedef struct {
	char name[16];        // host's name
	int  mode, map, humans, difficulty;
	bool inMatch;
	NetRoom raw;
} NpRoomInfo;

typedef struct {
	char name[16];
	int  team;
	bool human;
} NpFighterInfo;

typedef struct { unsigned char type, seq, len, data[10]; } NpPendingEvent;

typedef struct Np {
	NpState state;
	char    msg[48];                  // why the room ended, for the menu

	NpHuman human[NP_MAX_HUMANS];     // slot 0 is the host; on a client this mirrors the host's roster
	int     myIdx;
	MatchConfig cfg;                  // room settings (the host's; a client shows them)
	int     myWeapon;
	float   roomTimer, helloTimer;
	bool    startPending;             // client: START arrived, the game should build the match
	bool    inMatchRoom;              // client: host says a match is running

	// ---- match
	unsigned seed;
	int      myFighter;               // client: this console's fighter id on the host (swapped with 0 locally)
	int      nFighters;
	NpFighterInfo fighters[MAX_FIGHTERS];   // indexed by host fighter id
	Combat*  combat;
	Player*  player;
	unsigned tick;
	float    startTimer, readyTimer, startAge;
	bool     worldSeen;               // client: got the first world block
	bool     seenSnap;
	unsigned lastSnapSeq;             // client
	unsigned lastSnapFrame;           // client: host frame of the newest snapshot (for lag compensation)
	unsigned snapSeq;                 // host: next snapshot number
	float    hostSilence;             // client: seconds since the host last sent anything
	int      lastSpawnSeq;            // client
	bool     spawnPending;
	Spawn    spawn;
	int      evAck;                   // client: newest event the host confirmed
	int      nextEvSeq;               // client
	NpPendingEvent pending[16];
	int      pendingN;
	unsigned inSeq;                   // client
	float    prevHp;                  // client: for the damage flash
	int      prevKills;
	int      prevStreak;
	bool     hostLeft;

	unsigned char evbuf[NP_MAX_HUMANS][700];   // host: events waiting to go out this frame
	int      evlen[NP_MAX_HUMANS];
	int      evcount[NP_MAX_HUMANS];
	float    snapAcc;
} Np;

void np_init(Np* np);
const char* np_message(const Np* np);          // last "left the room" reason, "" when none
bool np_active(const Np* np);                  // anywhere in a room or a match

// ---- finding and joining rooms
bool np_start_wireless(void);                  // false when wireless cannot be used (airplane mode, no hardware)
int  np_scan(NpRoomInfo* out, int max);        // blocks about a second
bool np_host_open(Np* np, const MatchConfig* cfg, int myWeapon);
bool np_join(Np* np, const NpRoomInfo* room, int myWeapon);
void np_leave(Np* np, const char* reason);


// ---- the waiting room
void np_host_set_config(Np* np, const MatchConfig* cfg);
void np_back_to_room(Np* np);                  // after a match: both host and clients return to the waiting room
void np_room_update(Np* np, float dt);         // call every frame in a room
int  np_human_count(const Np* np);
const NpHuman* np_human(const Np* np, int index);   // index among the used slots, 0 = host

// ---- starting a match
// Host: the combat has just been created from np->cfg. Moves friends into their eggs and sends START.
void np_host_begin(Np* np, Combat* c, int hostTeam);
bool np_host_all_ready(const Np* np);          // everyone loaded, or the wait ran out
// Client: START arrived; the game loads the map, then calls np_client_build.
bool np_client_take_start(Np* np);
void np_client_build(Np* np, Combat* c, const Map* map, Match* m);

// ---- during a match
void np_host_frame_pre(Np* np, Combat* c, Player* p, Match* m, float dt);
void np_host_frame_post(Np* np, Combat* c, Player* p, Match* m, float dt);
void np_client_frame_pre(Np* np, Combat* c, Player* p, Match* m, float dt);
void np_client_frame_post(Np* np, Combat* c, Player* p, const PlayerInput* in, float dt);
bool np_client_take_spawn(Np* np, Spawn* out); // true once per (re)spawn the host ordered
void np_client_request_respawn(Np* np, int weapon);

#endif
