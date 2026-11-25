#ifndef COSMETICS_H
#define COSMETICS_H

#include <stdbool.h>
#include "profile.h"

typedef enum { COS_OWNED, COS_BOUGHT, COS_NEED_COINS } CosResult;

void        cos_defaults(Cosmetics* c);
int         cos_count(int cat);
const char* cos_category_name(int cat);
const char* cos_name(int cat, int idx);
int         cos_price(int cat, int idx);
bool        cos_unlocked(const Cosmetics* c, int cat, int idx);
CosResult   cos_try_buy(Stats* stats, Cosmetics* c, int cat, int idx);
void        cos_shell_rgb(int idx, float out[3]);
void        cos_skin_tint(int idx, const float base[3], float out[3]);

#endif
