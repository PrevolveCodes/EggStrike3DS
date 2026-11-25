#include "cosmetics.h"

#define MAX_ITEMS 8

static const char* const catNames[COS_CATEGORIES] = { "Shell", "Hat", "Accessory", "Weapon Skin" };

static const char* const names[COS_CATEGORIES][MAX_ITEMS] = {
	{ "Classic", "Hazel", "Sky", "Bubblegum", "Mint", "Sunny", "Gold", "Midnight" },
	{ "None", "Cap", "Top Hat", "Party Hat", "Chef Hat", "Propeller", "Crown", 0 },
	{ "None", "Shades", "Mustache", "Bow Tie", "Scarf", "Eye Patch", 0, 0 },
	{ "Default", "Gold", "Neon", "Frost", "Crimson", 0, 0, 0 },
};
static const int prices[COS_CATEGORIES][MAX_ITEMS] = {
	{ 0, 0, 0, 30, 30, 60, 150, 150 },
	{ 0, 40, 80, 60, 100, 120, 250, 0 },
	{ 0, 50, 50, 70, 90, 60, 0, 0 },
	{ 0, 200, 120, 120, 120, 0, 0, 0 },
};
static const int counts[COS_CATEGORIES] = { 8, 7, 6, 5 };

static const float shellRgb[8][3] = {
	{ 0.98f, 0.95f, 0.85f }, { 0.76f, 0.55f, 0.35f }, { 0.45f, 0.72f, 0.95f }, { 0.98f, 0.62f, 0.78f },
	{ 0.55f, 0.88f, 0.70f }, { 1.00f, 0.88f, 0.30f }, { 0.95f, 0.75f, 0.15f }, { 0.18f, 0.20f, 0.40f }
};
static const float skinRgb[5][3] = {
	{ 0, 0, 0 }, { 1.00f, 0.82f, 0.20f }, { 0.25f, 1.00f, 0.45f }, { 0.60f, 0.85f, 1.00f }, { 0.80f, 0.12f, 0.18f }
};

void cos_defaults(Cosmetics* c)
{
	for (int cat = 0; cat < COS_CATEGORIES; cat++) {
		c->sel[cat] = 0;
		c->unlocked[cat] = 0;
		for (int i = 0; i < counts[cat]; i++)
			if (prices[cat][i] == 0) c->unlocked[cat] |= (unsigned char)(1u << i);
	}
}

int cos_count(int cat)
{
	return (cat >= 0 && cat < COS_CATEGORIES) ? counts[cat] : 0;
}

const char* cos_category_name(int cat)
{
	return (cat >= 0 && cat < COS_CATEGORIES) ? catNames[cat] : "?";
}

const char* cos_name(int cat, int idx)
{
	if (cat < 0 || cat >= COS_CATEGORIES || idx < 0 || idx >= counts[cat]) return "?";
	return names[cat][idx];
}

int cos_price(int cat, int idx)
{
	if (cat < 0 || cat >= COS_CATEGORIES || idx < 0 || idx >= counts[cat]) return 0;
	return prices[cat][idx];
}

bool cos_unlocked(const Cosmetics* c, int cat, int idx)
{
	if (cat < 0 || cat >= COS_CATEGORIES || idx < 0 || idx >= counts[cat]) return false;
	return (c->unlocked[cat] >> idx) & 1u;
}

CosResult cos_try_buy(Stats* stats, Cosmetics* c, int cat, int idx)
{
	if (cos_unlocked(c, cat, idx)) return COS_OWNED;
	unsigned int price = (unsigned int)cos_price(cat, idx);
	if (stats->coins < price) return COS_NEED_COINS;
	stats->coins -= price;
	c->unlocked[cat] |= (unsigned char)(1u << idx);
	return COS_BOUGHT;
}

void cos_shell_rgb(int idx, float out[3])
{
	if (idx < 0 || idx >= 8) idx = 0;
	for (int i = 0; i < 3; i++) out[i] = shellRgb[idx][i];
}

void cos_skin_tint(int idx, const float base[3], float out[3])
{
	if (idx <= 0 || idx >= 5) {
		for (int i = 0; i < 3; i++) out[i] = base[i];
		return;
	}
	float lum = (base[0] + base[1] + base[2]) / 3.0f;
	float k = 0.7f + 0.3f * lum;
	for (int i = 0; i < 3; i++) out[i] = skinRgb[idx][i] * k;
}
