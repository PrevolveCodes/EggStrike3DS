#include "ui.h"
#include "cosmetics.h"
#include "viewmodel.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define UI_Z 0.5f
#define PI_F_UI 3.14159265f

static C2D_TextBuf textBuf;

bool ui_init(void)
{
	textBuf = C2D_TextBufNew(8192);
	return textBuf != NULL;
}

void ui_shutdown(void)
{
	if (textBuf) C2D_TextBufDelete(textBuf);
	textBuf = NULL;
}

void ui_frame_begin(void)
{
	C2D_TextBufClear(textBuf);
}

void ui_rect(float x, float y, float w, float h, u32 color)
{
	C2D_DrawRectSolid(x, y, UI_Z, w, h, color);
}

void ui_rect_grad(float x, float y, float w, float h, u32 top, u32 bottom)
{
	C2D_DrawRectangle(x, y, UI_Z, w, h, top, top, bottom, bottom);
}

void ui_panel(float x, float y, float w, float h, u32 fill, u32 border)
{
	ui_rect(x, y, w, h, border);
	ui_rect(x + 2, y + 2, w - 4, h - 4, fill);
}

void ui_ellipse(float x, float y, float w, float h, u32 color)
{
	C2D_DrawEllipseSolid(x, y, UI_Z, w, h, color);
}

void ui_triangle(float x0, float y0, float x1, float y1, float x2, float y2, u32 color)
{
	C2D_DrawTriangle(x0, y0, color, x1, y1, color, x2, y2, color, UI_Z);
}

void ui_triangle_grad(float x0, float y0, u32 c0, float x1, float y1, u32 c1, float x2, float y2, u32 c2)
{
	C2D_DrawTriangle(x0, y0, c0, x1, y1, c1, x2, y2, c2, UI_Z);
}

void ui_bar(float x, float y, float w, float h, float frac, u32 fill, u32 back)
{
	if (frac < 0.0f) frac = 0.0f;
	if (frac > 1.0f) frac = 1.0f;
	ui_rect(x, y, w, h, back);
	if (frac > 0.0f) ui_rect(x, y, w * frac, h, fill);
}


// ------------------------------------------------------------------ cartoon theme
u32 ui_shade(u32 c, float k)
{
	int r = (int)((c & 255u) * k), g = (int)(((c >> 8) & 255u) * k), b = (int)(((c >> 16) & 255u) * k);
	if (r > 255) r = 255;
	if (g > 255) g = 255;
	if (b > 255) b = 255;
	return C2D_Color32(r, g, b, (c >> 24) & 255u);
}

u32 ui_alpha(u32 c, int a)
{
	return C2D_Color32(c & 255u, (c >> 8) & 255u, (c >> 16) & 255u, a);
}

// Rounded rectangle. Opaque colours use a cross of rectangles plus four corner discs; translucent ones
// (where overlapping pieces would show) use chamfered corners instead, so nothing is drawn twice.
void ui_round_rect(float x, float y, float w, float h, float r, u32 color)
{
	if (r * 2.0f > w) r = w * 0.5f;
	if (r * 2.0f > h) r = h * 0.5f;
	if (r < 1.0f) { ui_rect(x, y, w, h, color); return; }
	if ((color >> 24) == 255u) {
		ui_rect(x + r, y, w - 2 * r, h, color);
		ui_rect(x, y + r, r, h - 2 * r, color);
		ui_rect(x + w - r, y + r, r, h - 2 * r, color);
		ui_ellipse(x, y, 2 * r, 2 * r, color);
		ui_ellipse(x + w - 2 * r, y, 2 * r, 2 * r, color);
		ui_ellipse(x, y + h - 2 * r, 2 * r, 2 * r, color);
		ui_ellipse(x + w - 2 * r, y + h - 2 * r, 2 * r, 2 * r, color);
	} else {
		ui_rect(x + r, y, w - 2 * r, h, color);
		ui_rect(x, y + r, r, h - 2 * r, color);
		ui_rect(x + w - r, y + r, r, h - 2 * r, color);
		ui_triangle(x, y + r, x + r, y, x + r, y + r, color);
		ui_triangle(x + w, y + r, x + w - r, y, x + w - r, y + r, color);
		ui_triangle(x, y + h - r, x + r, y + h, x + r, y + h - r, color);
		ui_triangle(x + w, y + h - r, x + w - r, y + h, x + w - r, y + h - r, color);
	}
}

void ui_card(float x, float y, float w, float h, u32 fill)
{
	ui_round_rect(x, y + 2, w, h, 9, UI_COLA(58, 38, 78, 70));      // soft shadow
	ui_round_rect(x, y, w, h, 9, T_INK);
	ui_round_rect(x + 2, y + 2, w - 4, h - 4, 7, fill);
}

void ui_chunky(float x, float y, float w, float h, u32 fill, bool focused, bool pressed)
{
	float off = pressed ? 2.0f : 0.0f;
	if (focused) {
		ui_round_rect(x - 4, y - 3 + off, w + 8, h + 6, 11, T_INK);
		ui_round_rect(x - 2, y - 1 + off, w + 4, h + 2, 9, UI_COL(255, 255, 255));
	}
	if (!pressed) ui_round_rect(x, y + 4, w, h, 8, UI_COLA(58, 38, 78, 90));
	ui_round_rect(x, y + off, w, h, 8, T_INK);
	float lip = pressed ? 2.0f : 4.0f;
	ui_round_rect(x + 2, y + off + 2, w - 4, h - 4, 6, ui_shade(fill, 0.70f));
	ui_round_rect(x + 2, y + off + 2, w - 4, h - 4 - lip, 6, fill);
	ui_round_rect(x + 6, y + off + 4, w - 12, (h - 8) * 0.30f, 3, UI_COLA(255, 255, 255, 85));
}

void ui_pill_bar(float x, float y, float w, float h, float frac, u32 fill, u32 back)
{
	if (frac < 0.0f) frac = 0.0f;
	if (frac > 1.0f) frac = 1.0f;
	float r = h * 0.5f;
	ui_round_rect(x, y, w, h, r, T_INK);
	ui_round_rect(x + 2, y + 2, w - 4, h - 4, r - 2, back);
	if (frac > 0.02f) {
		float fw = (w - 4) * frac;
		if (fw < h - 4) fw = h - 4;
		ui_round_rect(x + 2, y + 2, fw, h - 4, r - 2, fill);
		ui_round_rect(x + 4, y + 3, fw - 4 > 2 ? fw - 4 : 2, (h - 4) * 0.35f, 2, UI_COLA(255, 255, 255, 90));
	}
}

void ui_icon_heart(float cx, float cy, float s, u32 color)
{
	u32 ink = T_INK;
	for (int pass = 0; pass < 2; pass++) {
		float g = pass == 0 ? s * 0.10f : 0.0f;               // first pass: slightly bigger ink copy
		u32 c = pass == 0 ? ink : color;
		ui_ellipse(cx - s * 0.52f - g, cy - s * 0.46f - g, s * 0.58f + 2 * g, s * 0.58f + 2 * g, c);
		ui_ellipse(cx - s * 0.06f - g, cy - s * 0.46f - g, s * 0.58f + 2 * g, s * 0.58f + 2 * g, c);
		ui_triangle(cx - s * 0.50f - g, cy - s * 0.10f, cx + s * 0.50f + g, cy - s * 0.10f, cx, cy + s * 0.52f + g, c);
	}
	ui_ellipse(cx - s * 0.34f, cy - s * 0.34f, s * 0.18f, s * 0.14f, UI_COLA(255, 255, 255, 170));
}

void ui_icon_coin(float cx, float cy, float s)
{
	ui_ellipse(cx - s * 0.5f, cy - s * 0.5f, s, s, T_INK);
	ui_ellipse(cx - s * 0.42f, cy - s * 0.42f, s * 0.84f, s * 0.84f, UI_COL(255, 200, 40));
	ui_ellipse(cx - s * 0.28f, cy - s * 0.28f, s * 0.56f, s * 0.56f, UI_COL(255, 226, 110));
	ui_ellipse(cx - s * 0.20f, cy - s * 0.20f, s * 0.40f, s * 0.40f, UI_COL(240, 170, 30));
}

void ui_icon_star(float cx, float cy, float s, u32 color)
{
	float px[10], py[10];
	for (int i = 0; i < 10; i++) {
		float a = -1.5707963f + (float)i * 0.6283185f;
		float r = (i & 1) ? s * 0.22f : s * 0.5f;
		px[i] = cx + cosf(a) * r; py[i] = cy + sinf(a) * r;
	}
	for (int i = 0; i < 10; i++) ui_triangle(cx, cy, px[i], py[i], px[(i + 1) % 10], py[(i + 1) % 10], color);
}

void ui_icon_egg_small(float cx, float cy, float s, u32 color)
{
	ui_ellipse(cx - s * 0.43f, cy - s * 0.53f, s * 0.86f, s * 1.06f, T_INK);
	ui_ellipse(cx - s * 0.37f, cy - s * 0.47f, s * 0.74f, s * 0.94f, color);
	ui_ellipse(cx - s * 0.22f, cy - s * 0.32f, s * 0.20f, s * 0.30f, UI_COLA(255, 255, 255, 150));
}

void ui_backdrop_sky(float time)
{
	ui_rect_grad(0, 0, 400, 240, UI_COL(84, 176, 255), UI_COL(206, 240, 255));
	// sun with a soft halo
	ui_ellipse(300, -10, 150, 150, UI_COLA(255, 240, 150, 70));
	ui_ellipse(322, 12, 106, 106, UI_COLA(255, 236, 130, 120));
	ui_ellipse(338, 28, 74, 74, UI_COL(255, 224, 90));
	// drifting clouds
	for (int i = 0; i < 3; i++) {
		float x = fmodf(time * (5.0f + i * 2.0f) + i * 150.0f, 520.0f) - 90.0f;
		float y = 36.0f + i * 34.0f + (i == 1 ? 30.0f : 0.0f);
		ui_ellipse(x, y + 6, 70, 22, UI_COLA(255, 255, 255, 235));
		ui_ellipse(x + 14, y - 4, 40, 26, UI_COLA(255, 255, 255, 235));
		ui_ellipse(x + 36, y + 2, 44, 24, UI_COLA(255, 255, 255, 235));
	}
	// hills and a grass strip
	ui_ellipse(-70, 170, 330, 140, UI_COL(120, 205, 100));
	ui_ellipse(180, 188, 330, 120, UI_COL(96, 188, 92));
	ui_rect(0, 214, 400, 26, UI_COL(88, 178, 82));
	ui_rect(0, 214, 400, 3, UI_COL(120, 214, 104));
	for (int i = 0; i < 9; i++) {                           // little flowers
		float fx = 18.0f + i * 46.0f + (i & 1) * 10.0f, fy = 224.0f + (i % 3) * 4.0f;
		ui_ellipse(fx, fy, 5, 5, (i % 3 == 0) ? UI_COL(255, 255, 255) : ((i % 3 == 1) ? UI_COL(255, 210, 70) : UI_COL(255, 150, 190)));
	}
}

void ui_backdrop_panel(void)
{
	ui_rect_grad(0, 0, 320, 240, UI_COL(255, 244, 210), UI_COL(255, 224, 168));
	for (int r = 0; r < 8; r++)
		for (int c = 0; c < 11; c++) {
			float x = c * 30.0f + ((r & 1) ? 15.0f : 0.0f) - 6.0f, y = r * 30.0f + 12.0f;
			ui_ellipse(x, y, 9, 9, UI_COLA(255, 205, 120, 70));
		}
}

void ui_title_ribbon(const char* title, bool withBack, unsigned int coins, bool showCoins)
{
	ui_rect_grad(0, 0, 320, 31, UI_COL(255, 168, 66), UI_COL(255, 128, 50));
	ui_rect(0, 31, 320, 3, T_INK);
	ui_rect(0, 28, 320, 3, UI_COL(255, 214, 120));
	float tx = withBack ? 168.0f : 160.0f;
	if (showCoins) tx -= 16.0f;
	ui_text(tx + 1, 6, 0.72f, T_INK, UI_CENTER, title);
	ui_text(tx, 4, 0.72f, UI_COL(255, 255, 255), UI_CENTER, title);
	if (withBack) {
		ui_chunky(6, 3, 38, 23, T_YOLK, false, false);
		ui_triangle(30, 8, 30, 21, 18, 14.5f, T_INK);                 // back arrow
	}
	if (showCoins) {
		ui_round_rect(246, 5, 68, 20, 9, T_INK);
		ui_round_rect(248, 7, 64, 16, 7, T_CREAM);
		ui_icon_coin(259, 15, 12);
		char buf[16];
		snprintf(buf, sizeof(buf), "%u", coins);
		ui_text(306, 7, 0.46f, T_INK, UI_RIGHT, buf);
	}
}

static void drawText(float x, float y, float scale, u32 color, int align, const char* str)
{
	C2D_Text t;
	C2D_TextParse(&t, textBuf, str);
	C2D_TextOptimize(&t);
	float w, h;
	C2D_TextGetDimensions(&t, scale, scale, &w, &h);
	float px = x;
	if (align == UI_CENTER) px = x - w * 0.5f;
	else if (align == UI_RIGHT) px = x - w;
	C2D_DrawText(&t, C2D_WithColor, px, y, UI_Z, scale, scale, color);
}

void ui_text(float x, float y, float scale, u32 color, int align, const char* str)
{
	drawText(x, y, scale, color, align, str);
}

void ui_text_shadow(float x, float y, float scale, u32 color, int align, const char* str)
{
	drawText(x + 1.0f, y + 2.0f, scale, UI_COLA(58, 38, 78, 210), align, str);
	drawText(x, y, scale, color, align, str);
}

void ui_textf(float x, float y, float scale, u32 color, int align, const char* fmt, ...)
{
	char buf[128];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	ui_text_shadow(x, y, scale, color, align, buf);
}

float ui_text_width(const char* str, float scale)
{
	C2D_Text t;
	C2D_TextParse(&t, textBuf, str);
	float w, h;
	C2D_TextGetDimensions(&t, scale, scale, &w, &h);
	return w;
}

bool ui_hit(float x, float y, float w, float h, int tx, int ty)
{
	return tx >= x && tx < x + w && ty >= y && ty < y + h;
}

// ---------------------------------------------------------------- map drawing

void ui_map_view(UiMapView* v, const Map* m, float x, float y, float w, float h)
{
	float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
	for (int i = 0; i < m->boxCount; i++) {
		const Box* b = &m->boxes[i];
		if (b->min[0] < minX) minX = b->min[0];
		if (b->max[0] > maxX) maxX = b->max[0];
		if (b->min[2] < minZ) minZ = b->min[2];
		if (b->max[2] > maxZ) maxZ = b->max[2];
	}
	if (maxX <= minX || maxZ <= minZ) { minX = minZ = -30.0f; maxX = maxZ = 30.0f; }

	float sx = w / (maxX - minX), sz = h / (maxZ - minZ);
	v->scale = sx < sz ? sx : sz;
	v->cx = (minX + maxX) * 0.5f;
	v->cz = (minZ + maxZ) * 0.5f;
	v->x = x; v->y = y; v->w = w; v->h = h;
}

void ui_map_point(const UiMapView* v, float wx, float wz, float* sx, float* sy)
{
	*sx = v->x + v->w * 0.5f + (wx - v->cx) * v->scale;
	*sy = v->y + v->h * 0.5f + (wz - v->cz) * v->scale;
}

void ui_map_draw(const UiMapView* v, const Map* m)
{
	ui_rect(v->x, v->y, v->w, v->h, UI_COL(34, 40, 52));

	// Lower geometry first so taller blocks draw on top of it
	const float limits[4] = { 0.5f, 1.8f, 4.2f, 1000.0f };
	float lo = -1000.0f;
	for (int pass = 0; pass < 4; pass++) {
		float hi = limits[pass];
		for (int i = 0; i < m->boxCount; i++) {
			const Box* b = &m->boxes[i];
			if (b->hidden) continue;                          // collision-only (a round thing is drawn instead)
			float top = b->max[1];
			if (top < lo || top >= hi) continue;
			float x0, y0, x1, y1;
			ui_map_point(v, b->min[0], b->min[2], &x0, &y0);
			ui_map_point(v, b->max[0], b->max[2], &x1, &y1);
			float k = 0.55f + 0.45f * (top > 6.0f ? 1.0f : top / 6.0f);
			if (top <= 0.5f) k = 0.45f;
			ui_rect(x0, y0, x1 - x0, y1 - y0,
			        UI_COL((int)(b->color[0] * 255 * k), (int)(b->color[1] * 255 * k), (int)(b->color[2] * 255 * k)));
		}
		lo = hi;
	}
	for (int i = 0; i < m->roundCount; i++) {                 // round things as little discs
		const Round* r = &m->rounds[i];
		if (r->kind == ROUND_DOME || r->y1 < 0.0f) continue;
		float cx, cy;
		ui_map_point(v, r->x, r->z, &cx, &cy);
		float rr = r->r0 * v->scale;
		if (rr < 1.2f) rr = 1.2f;
		float k = 0.55f + 0.45f * (r->y1 > 6.0f ? 1.0f : r->y1 / 6.0f);
		ui_ellipse(cx - rr, cy - rr, rr * 2, rr * 2, UI_COL((int)(r->color[0] * 255 * k), (int)(r->color[1] * 255 * k), (int)(r->color[2] * 255 * k)));
	}
}

// ------------------------------------------------------------------- egg art

void ui_draw_egg(float cx, float cy, float s, int shell, int hat, int acc, float time)
{
	float rgb[3];
	cos_shell_rgb(shell, rgb);
	u32 light = UI_COL((int)(rgb[0] * 120 + 135), (int)(rgb[1] * 120 + 135), (int)(rgb[2] * 120 + 135));
	u32 black = UI_COL(25, 25, 35);
	u32 white = UI_COL(255, 255, 255);
	(void)white;

	float top = cy - 40.0f * s;

	// Ground shadow
	ui_ellipse(cx - 34 * s, cy + 36 * s, 68 * s, 14 * s, UI_COLA(0, 0, 0, 70));

	// Egg body: an egg-shaped fan of triangles, lit from the upper left
	{
		const int N = 40;
		const float a = 30.0f * s, b = 40.0f * s;           // half width, half height
		int cr = (int)(rgb[0] * 255), cg = (int)(rgb[1] * 255), cb = (int)(rgb[2] * 255);
		u32 edge = UI_COL((int)(cr * 0.70f), (int)(cg * 0.70f), (int)(cb * 0.70f));
		float hx = cx - 0.25f * a, hy = cy - 0.22f * b;     // highlight centre
		float px0 = 0, py0 = 0;
		for (int i = 0; i <= N; i++) {
			float th = 2.0f * PI_F_UI * (float)i / N;
			float x = cx + a * cosf(th) * (1.0f - 0.16f * sinf(th));   // narrower toward the top
			float y = cy - b * sinf(th);
			if (i > 0) ui_triangle_grad(hx, hy, light, px0, py0, edge, x, y, edge);
			px0 = x; py0 = y;
		}
		ui_ellipse(cx - 17 * s, cy - 30 * s, 12 * s, 22 * s, UI_COLA(255, 255, 255, 90));   // soft gloss
	}

	// accessories
	switch (acc) {
	case 1: // shades
		ui_rect(cx - 20 * s, cy - 15 * s, 18 * s, 11 * s, black);
		ui_rect(cx + 2 * s,  cy - 15 * s, 18 * s, 11 * s, black);
		ui_rect(cx - 3 * s,  cy - 13 * s, 6 * s, 3 * s, black);
		break;
	case 2: // mustache
		ui_ellipse(cx - 17 * s, cy + 6 * s, 18 * s, 8 * s, UI_COL(70, 40, 25));
		ui_ellipse(cx - 1 * s,  cy + 6 * s, 18 * s, 8 * s, UI_COL(70, 40, 25));
		break;
	case 3: // bow tie
		ui_triangle(cx, cy + 26 * s, cx - 15 * s, cy + 17 * s, cx - 15 * s, cy + 35 * s, UI_COL(210, 40, 50));
		ui_triangle(cx, cy + 26 * s, cx + 15 * s, cy + 17 * s, cx + 15 * s, cy + 35 * s, UI_COL(210, 40, 50));
		ui_ellipse(cx - 4 * s, cy + 22 * s, 8 * s, 8 * s, UI_COL(150, 20, 30));
		break;
	case 4: // scarf
		ui_rect(cx - 27 * s, cy + 18 * s, 54 * s, 10 * s, UI_COL(60, 130, 210));
		ui_rect(cx + 8 * s,  cy + 26 * s, 10 * s, 18 * s, UI_COL(60, 130, 210));
		break;
	case 5: // eye patch
		ui_rect(cx - 19 * s, cy - 16 * s, 16 * s, 13 * s, black);
		ui_rect(cx - 30 * s, cy - 24 * s, 60 * s, 2 * s, black);
		break;
	default: break;
	}

	// hats
	float wob = sinf(time * 3.0f);
	switch (hat) {
	case 1: // cap
		ui_rect(cx - 24 * s, top - 2 * s, 48 * s, 14 * s, UI_COL(200, 40, 40));
		ui_rect(cx - 4 * s,  top + 8 * s, 34 * s, 5 * s, UI_COL(150, 25, 25));
		break;
	case 2: // top hat
		ui_rect(cx - 17 * s, top - 28 * s, 34 * s, 30 * s, black);
		ui_rect(cx - 17 * s, top - 6 * s, 34 * s, 5 * s, UI_COL(190, 40, 50));
		ui_rect(cx - 28 * s, top + 1 * s, 56 * s, 6 * s, black);
		break;
	case 3: // party hat
		ui_triangle(cx - 20 * s, top + 8 * s, cx + 20 * s, top + 8 * s, cx, top - 36 * s, UI_COL(240, 80, 160));
		ui_ellipse(cx - 6 * s, top - 42 * s, 12 * s, 12 * s, UI_COL(255, 230, 80));
		break;
	case 4: // chef hat
		ui_ellipse(cx - 28 * s, top - 30 * s, 30 * s, 30 * s, white);
		ui_ellipse(cx - 10 * s, top - 38 * s, 30 * s, 32 * s, white);
		ui_ellipse(cx +  0 * s, top - 28 * s, 30 * s, 30 * s, white);
		ui_rect(cx - 20 * s, top - 8 * s, 40 * s, 16 * s, UI_COL(240, 240, 240));
		break;
	case 5: // propeller cap
		ui_rect(cx - 22 * s, top - 2 * s, 44 * s, 12 * s, UI_COL(60, 110, 220));
		ui_rect(cx - 2 * s,  top - 14 * s, 4 * s, 12 * s, UI_COL(90, 90, 100));
		ui_rect(cx - (22 * wob) * s, top - 17 * s, (44 * wob) * s, 4 * s, UI_COL(240, 200, 60));
		break;
	case 6: // crown
		ui_rect(cx - 22 * s, top - 14 * s, 44 * s, 18 * s, UI_COL(245, 200, 40));
		ui_triangle(cx - 22 * s, top - 14 * s, cx - 12 * s, top - 14 * s, cx - 17 * s, top - 28 * s, UI_COL(245, 200, 40));
		ui_triangle(cx - 6 * s,  top - 14 * s, cx + 6 * s,  top - 14 * s, cx,          top - 32 * s, UI_COL(245, 200, 40));
		ui_triangle(cx + 12 * s, top - 14 * s, cx + 22 * s, top - 14 * s, cx + 17 * s, top - 28 * s, UI_COL(245, 200, 40));
		ui_ellipse(cx - 4 * s, top - 8 * s, 8 * s, 8 * s, UI_COL(220, 40, 60));
		break;
	default: break;
	}
}

// ---- weapon icons
static int colByte(float v) { return v < 0.0f ? 0 : (v > 1.0f ? 255 : (int)(v * 255.0f)); }

static void iconQuad(const float q[4][2], const float rgb[3], void* user)
{
	(void)user;
	u32 c = UI_COL(colByte(rgb[0]), colByte(rgb[1]), colByte(rgb[2]));
	ui_triangle(q[0][0], q[0][1], q[1][0], q[1][1], q[2][0], q[2][1], c);
	ui_triangle(q[0][0], q[0][1], q[2][0], q[2][1], q[3][0], q[3][1], c);
}

void ui_weapon_icon(int weapon, float x, float y, float w, float h)
{
	viewmodel_icon(weapon, x, y, w, h, iconQuad, 0);
}
