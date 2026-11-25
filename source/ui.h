#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <citro2d.h>
#include "map.h"

// Colours (call only from code, not static initialisers)
#define UI_COL(r, g, b)      C2D_Color32((r), (g), (b), 255)
#define UI_COLA(r, g, b, a)  C2D_Color32((r), (g), (b), (a))

enum { UI_LEFT = 0, UI_CENTER = 1, UI_RIGHT = 2 };

// Bright cartoony theme: warm cream, sunny yolk, chunky dark-plum outlines
#define T_INK     UI_COL(58, 38, 78)
#define T_CREAM   UI_COL(255, 248, 228)
#define T_YOLK    UI_COL(255, 208, 64)
#define T_ORANGE  UI_COL(255, 142, 58)
#define T_CORAL   UI_COL(255, 98, 92)
#define T_GRASS   UI_COL(98, 206, 102)
#define T_SKY     UI_COL(92, 190, 255)
#define T_TEAL    UI_COL(50, 204, 190)
#define T_GRAPE   UI_COL(158, 116, 242)
#define T_PINK    UI_COL(255, 124, 178)
#define T_RED_TEAM  UI_COL(240, 84, 78)
#define T_BLUE_TEAM UI_COL(70, 150, 255)

u32  ui_shade(u32 color, float k);     // multiply the colour (k < 1 darker, > 1 lighter)
u32  ui_alpha(u32 color, int a);       // same colour with another alpha (0..255)
void ui_round_rect(float x, float y, float w, float h, float r, u32 color);
// Chunky cartoon button: shadow, ink outline, face with a lip underneath, gloss on top. 'press' sinks it a little.
void ui_chunky(float x, float y, float w, float h, u32 fill, bool focused, bool pressed);
// Rounded white-ish card with an ink outline
void ui_card(float x, float y, float w, float h, u32 fill);
// Rounded progress bar with an ink outline
void ui_pill_bar(float x, float y, float w, float h, float frac, u32 fill, u32 back);
// Heart, coin, star, egg icons drawn from shapes
void ui_icon_heart(float cx, float cy, float s, u32 color);
void ui_icon_coin(float cx, float cy, float s);
void ui_icon_star(float cx, float cy, float s, u32 color);
void ui_icon_egg_small(float cx, float cy, float s, u32 color);
// Sunny backdrops: sky with sun, clouds and grass (top screen) and a warm polka-dot panel (bottom screen)
void ui_backdrop_sky(float time);
void ui_backdrop_panel(void);
// Title ribbon across the top of a bottom-screen page; returns nothing, draws a tappable-looking back arrow when withBack
void ui_title_ribbon(const char* title, bool withBack, unsigned int coins, bool showCoins);

bool ui_init(void);
void ui_shutdown(void);
void ui_frame_begin(void);     // call once per frame before drawing text

void ui_rect(float x, float y, float w, float h, u32 color);
void ui_rect_grad(float x, float y, float w, float h, u32 top, u32 bottom);
void ui_panel(float x, float y, float w, float h, u32 fill, u32 border);
void ui_ellipse(float x, float y, float w, float h, u32 color);
void ui_triangle(float x0, float y0, float x1, float y1, float x2, float y2, u32 color);
void ui_triangle_grad(float x0, float y0, u32 c0, float x1, float y1, u32 c1, float x2, float y2, u32 c2);
void ui_bar(float x, float y, float w, float h, float frac, u32 fill, u32 back);

void  ui_text(float x, float y, float scale, u32 color, int align, const char* str);
void  ui_text_shadow(float x, float y, float scale, u32 color, int align, const char* str);
void  ui_textf(float x, float y, float scale, u32 color, int align, const char* fmt, ...);
float ui_text_width(const char* str, float scale);

bool ui_hit(float x, float y, float w, float h, int tx, int ty);

// Side-view picture of a weapon (muzzle to the left) fitted into the box.
void ui_weapon_icon(int weapon, float x, float y, float w, float h);

// Top-down map drawing (minimap and previews)
typedef struct { float x, y, w, h, scale, cx, cz; } UiMapView;
void ui_map_view(UiMapView* v, const Map* m, float x, float y, float w, float h);
void ui_map_draw(const UiMapView* v, const Map* m);
void ui_map_point(const UiMapView* v, float wx, float wz, float* sx, float* sy);

// Egg character with cosmetics (shell colour index, hat index, accessory index)
void ui_draw_egg(float cx, float cy, float scale, int shell, int hat, int acc, float time);

#endif
