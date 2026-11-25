#ifndef GAME_H
#define GAME_H

#include <stdbool.h>

bool game_init(void);
bool game_update(float dt);   // false = quit the application
void game_draw(void);
void game_shutdown(void);

#endif
