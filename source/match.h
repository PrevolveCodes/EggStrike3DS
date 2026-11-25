#ifndef MATCH_H
#define MATCH_H

#include <stdbool.h>

typedef enum {
	MODE_FFA, MODE_TDM, MODE_CTF, MODE_KOTP, MODE_ELIM, MODE_PRACTICE, MODE_COUNT
} GameMode;

#define TEAM_RED  0
#define TEAM_BLUE 1
#define DIFF_EASY 0
#define DIFF_NORMAL 1
#define DIFF_HARD 2
#define DURATION_COUNT 6
#define SCORE_COUNT 4
#define MAX_BOTS_PER_TEAM 7
#define MAX_BOTS_TOTAL 14

// All fields are ints so the menu can edit them directly.
typedef struct {
	int mode;
	int map;
	int team;         // player's team (TEAM_RED / TEAM_BLUE)
	int difficulty;
	int totalBots;    // FFA / Practice bot count, mirrors red+blue in team modes
	int redBots;
	int blueBots;
	int balance;      // keep team sizes even
	int durationIdx;
	int scoreIdx;
} MatchConfig;

typedef enum { PH_INTRO, PH_COUNTDOWN, PH_ACTIVE, PH_ENDED } MatchPhase;

typedef struct {
	MatchConfig cfg;
	MatchPhase  phase;
	float countdown;
	float elapsed;
	float timeLeft;      // 0 when the match has no time limit
	int   redScore, blueScore;
	int   playerKills, playerDeaths;
	int   botBestKills;  // highest kill count among bots (free for all)
	int   winner;        // -1 none/draw, 0 red, 1 blue, 2 player, 3 a bot (free for all)
	bool  timeUp;
} Match;

const char* mode_name(int mode);
const char* mode_help(int mode);
bool  mode_is_team(int mode);
bool  mode_available(int mode);   // modes whose gameplay is implemented

void  match_defaults(MatchConfig* c);
void  match_sanitize(MatchConfig* c);
void  match_apply_total(MatchConfig* c);    // split totalBots between teams
void  match_apply_balance(MatchConfig* c);  // even teams including the player
int   match_duration_seconds(const MatchConfig* c); // 0 = unlimited
int   match_score_limit(const MatchConfig* c);      // 0 = none
int   match_duration_minutes_at(int idx);
int   match_score_at(int mode, int idx);
const char* match_score_unit(int mode);

void  match_start(Match* m, const MatchConfig* c);   // begins in PH_INTRO (map flyover + weapon pick)
void  match_join(Match* m, float elapsed);           // go live right away, as if 'elapsed' seconds were already played
void  match_begin(Match* m);                         // GET READY pressed: start the countdown
void  match_update(Match* m, float dt);
bool  match_player_won(const Match* m);
int   match_coins_earned(const Match* m, bool won);

#endif
