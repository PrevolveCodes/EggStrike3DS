#ifndef HUD_H
#define HUD_H

#include <stdbool.h>
#include "combat.h"
#include "map.h"
#include "match.h"
#include "player.h"
#include "profile.h"

// Bottom-screen layout (320 x 240). Shared with input so touch-look ignores buttons.
#define HUD_STRIP_Y   204
#define HUD_STRIP_H   36
#define HUD_PAUSE_X   4
#define HUD_PAUSE_Y   4
#define HUD_PAUSE_W   44
#define HUD_PAUSE_H   24

bool hud_touch_is_ui(int x, int y);
int  hud_weapon_at(int x, int y);      // loadout slot (0/1) under the touch, or -1
bool hud_pause_hit(int x, int y);

// Top screen: crosshair, scope, damage/death overlays (3D boxes in view space).
void hud_draw_view(const Player* p);
void hud_draw_leaderboard(const Player* p, const Combat* c);   // top-left list, teams apart (2D, after renderer_top_overlay)
void hud_draw_objective(const Player* p, const Combat* c);   // top screen: flag / pan / round status and event messages
void hud_draw_streak(const Player* p, const Combat* c);   // top screen: streak, shield, perk timers, new-perk banner
void hud_draw_respawn(const Player* p);   // top screen: "Respawning in 1.4" while the pause-menu Respawn counts down
void hud_draw_status(const Player* p, const Combat* c, const Match* m);   // top screen: health bar and ammo (2D)
void hud_draw_death(const Player* p, const Combat* c);   // top screen: who got you and the respawn countdown
void hud_draw_joining_top(float frac);   // top screen: 'Joining match...' bar over the flyover
void hud_draw_joining_bottom(float frac, const Match* m);
void hud_draw_intro_view(void);   // letterbox bars during the map flyover

// Bottom screen: timer, scores, health, ammo, minimap, weapon strip.
void hud_draw_bottom(const Player* p, const Combat* c, const Map* map, const Match* m,
                     const PlayerProfile* prof, bool showScoreboard);

#endif
