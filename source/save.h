#ifndef SAVE_H
#define SAVE_H

#include <stdbool.h>
#include "profile.h"

void save_set_path(const char* path);   // default: sdmc:/3ds/EggStrike3DS/save.bin
bool save_load(PlayerProfile* p);             // false: no usable save (profile untouched)
bool save_write(const PlayerProfile* p);

#endif
