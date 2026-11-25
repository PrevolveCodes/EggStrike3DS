#ifndef SCOREBOARD_H
#define SCOREBOARD_H

#include <stdbool.h>

typedef struct {
	char name[16];
	int  team;       // 0 red, 1 blue, -1 none (free for all)
	int  kills, deaths;
	bool isPlayer;
	bool alive;
} ScoreRow;

#endif
