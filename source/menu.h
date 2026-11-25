#ifndef MENU_H
#define MENU_H

#include <stdbool.h>
#include "input.h"
#include "match.h"
#include "netplay.h"
#include "profile.h"
#include "scoreboard.h"

typedef enum {
	MENU_NONE, MENU_START_MATCH, MENU_QUIT_APP, MENU_RESUME,
	MENU_REMATCH, MENU_TO_MENU, MENU_SAVE, MENU_RESPAWN,
	// multiplayer
	MENU_MP_HOST,        // open a room
	MENU_MP_SCAN,        // look for rooms (the game answers with menu_set_rooms)
	MENU_MP_JOINROOM,    // join menu_selected_room()
	MENU_MP_START,       // host: start the match
	MENU_MP_LEAVE        // leave or close the room
} MenuAction;

void menu_init(PlayerProfile* prof);
void menu_notify(const char* msg);   // short message in the title bar
void menu_show_main(void);
void menu_show_pause(void);
void menu_show_post(const Match* m, int coinsEarned, bool won, const ScoreRow* rows, int rowCount);

// ---- multiplayer screens (np may be NULL until the game has one)
void menu_set_net(Np* np);
bool menu_multiplayer(void);                  // inside a room or a multiplayer match
void menu_show_mp(void);                      // Host / Join choice
void menu_show_room(void);                    // the waiting room
void menu_set_rooms(const NpRoomInfo* rooms, int count);   // answer to MENU_MP_SCAN
int  menu_selected_room(void);

MenuAction menu_update(const UiInput* in, float dt);
void menu_draw_top(void);     // call inside a top 2D scene
void menu_draw_bottom(void);  // call inside a bottom 2D scene

#endif
