#include "match.h"
#include <string.h>
#include "map.h"

static const char* modeNames[MODE_COUNT] = {
	"Free For All", "Team Deathmatch", "Capture the Flag",
	"King of the Pan", "Team Elimination", "Bot Practice"
};
static const char* modeHelp[MODE_COUNT] = {
	"Everyone fights alone. First to the score limit wins.",
	"Red vs Blue. Most kills wins.",
	"Steal the enemy flag and bring it home.",
	"Hold the central pan to earn points.",
	"No respawns. Last team standing wins the round.",
	"Shoot targets with no match pressure."
};
static const int durations[DURATION_COUNT] = { 2, 3, 5, 7, 10, 15 };
static const int killLimits[SCORE_COUNT]   = { 10, 20, 30, 50 };
static const int flagLimits[SCORE_COUNT]   = { 1, 3, 5, 7 };
static const int pointLimits[SCORE_COUNT]  = { 100, 200, 300, 500 };
static const int roundLimits[SCORE_COUNT]  = { 3, 5, 7, 9 };

static int clampi(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

const char* mode_name(int mode)
{
	return (mode >= 0 && mode < MODE_COUNT) ? modeNames[mode] : "?";
}

const char* mode_help(int mode)
{
	return (mode >= 0 && mode < MODE_COUNT) ? modeHelp[mode] : "";
}

bool mode_is_team(int mode)
{
	return mode == MODE_TDM || mode == MODE_CTF || mode == MODE_KOTP || mode == MODE_ELIM;
}

bool mode_available(int mode)
{
	return mode >= 0 && mode < MODE_COUNT;
}

void match_defaults(MatchConfig* c)
{
	c->mode = MODE_PRACTICE;
	c->map = 0;
	c->team = TEAM_RED;
	c->difficulty = DIFF_NORMAL;
	c->totalBots = 6;
	c->redBots = 3;
	c->blueBots = 3;
	c->balance = 1;
	c->durationIdx = 2;
	c->scoreIdx = 1;
}

void match_sanitize(MatchConfig* c)
{
	c->mode = clampi(c->mode, 0, MODE_COUNT - 1);
	c->map = clampi(c->map, 0, MAP_COUNT - 1);
	c->team = clampi(c->team, 0, 1);
	c->difficulty = clampi(c->difficulty, 0, 2);
	c->redBots = clampi(c->redBots, 0, MAX_BOTS_PER_TEAM);
	c->blueBots = clampi(c->blueBots, 0, MAX_BOTS_PER_TEAM);
	c->totalBots = clampi(c->totalBots, 0, MAX_BOTS_TOTAL);
	if (c->mode == MODE_PRACTICE && c->totalBots > 7) c->totalBots = 7;
	c->balance = c->balance ? 1 : 0;
	c->durationIdx = clampi(c->durationIdx, 0, DURATION_COUNT - 1);
	c->scoreIdx = clampi(c->scoreIdx, 0, SCORE_COUNT - 1);
}

void match_apply_total(MatchConfig* c)
{
	int t = clampi(c->totalBots, 0, MAX_BOTS_TOTAL);
	c->totalBots = t;
	if (c->balance) { match_apply_balance(c); return; }
	c->redBots = (t + 1) / 2;
	c->blueBots = t / 2;
}

void match_apply_balance(MatchConfig* c)
{
	// Total players = bots + the human; each team gets half of them.
	int t = clampi(c->totalBots, 0, MAX_BOTS_TOTAL);
	int playersOwn = (t + 2) / 2;
	int own = playersOwn - 1;
	int other = t - own;
	if (c->team == TEAM_RED) { c->redBots = own; c->blueBots = other; }
	else                     { c->blueBots = own; c->redBots = other; }
}

int match_duration_minutes_at(int idx)
{
	return durations[clampi(idx, 0, DURATION_COUNT - 1)];
}

int match_score_at(int mode, int idx)
{
	idx = clampi(idx, 0, SCORE_COUNT - 1);
	switch (mode) {
	case MODE_CTF:  return flagLimits[idx];
	case MODE_KOTP: return pointLimits[idx];
	case MODE_ELIM: return roundLimits[idx];
	case MODE_PRACTICE: return 0;
	default:        return killLimits[idx];
	}
}

const char* match_score_unit(int mode)
{
	switch (mode) {
	case MODE_CTF:  return "captures";
	case MODE_KOTP: return "points";
	case MODE_ELIM: return "rounds";
	default:        return "kills";
	}
}

int match_duration_seconds(const MatchConfig* c)
{
	return c->mode == MODE_PRACTICE ? 0 : durations[clampi(c->durationIdx, 0, DURATION_COUNT - 1)] * 60;
}

int match_score_limit(const MatchConfig* c)
{
	return match_score_at(c->mode, c->scoreIdx);
}

void match_start(Match* m, const MatchConfig* c)
{
	memset(m, 0, sizeof(*m));
	m->cfg = *c;
	m->phase = PH_INTRO;
	m->countdown = 3.0f;
	m->timeLeft = (float)match_duration_seconds(c);
	m->winner = -1;
}

static void finish(Match* m, bool timeUp)
{
	m->phase = PH_ENDED;
	m->timeUp = timeUp;
	int mode = m->cfg.mode;
	if (mode == MODE_FFA) {
		int limit = match_score_limit(&m->cfg);
		if (limit > 0 && m->playerKills >= limit) m->winner = 2;
		else if (limit > 0 && m->botBestKills >= limit) m->winner = 3;
		else if (m->playerKills > m->botBestKills) m->winner = 2;       // time ran out: most kills wins
		else if (m->botBestKills > m->playerKills) m->winner = 3;
		else m->winner = -1;
	} else if (mode_is_team(mode)) {
		m->winner = m->redScore > m->blueScore ? TEAM_RED :
		            (m->blueScore > m->redScore ? TEAM_BLUE : -1);
	} else {
		m->winner = -1;
	}
}

void match_join(Match* m, float elapsed)
{
	m->phase = PH_ACTIVE;
	m->countdown = 0.0f;
	m->elapsed = elapsed;
	int dur = match_duration_seconds(&m->cfg);
	if (dur > 0) {
		m->timeLeft = (float)dur - elapsed;
		if (m->timeLeft < 15.0f) m->timeLeft = 15.0f;       // never join a match that is about to end
	}
}

void match_begin(Match* m)
{
	if (m->phase == PH_INTRO) { m->phase = PH_COUNTDOWN; m->countdown = 3.0f; }
}

void match_update(Match* m, float dt)
{
	if (m->phase == PH_INTRO) return;
	if (m->phase == PH_COUNTDOWN) {
		m->countdown -= dt;
		if (m->countdown <= 0.0f) { m->countdown = 0.0f; m->phase = PH_ACTIVE; }
		return;
	}
	if (m->phase != PH_ACTIVE) return;

	m->elapsed += dt;

	int limit = match_score_limit(&m->cfg);
	if (limit > 0) {
		if (m->cfg.mode == MODE_FFA && (m->playerKills >= limit || m->botBestKills >= limit)) { finish(m, false); return; }
		if (mode_is_team(m->cfg.mode) && (m->redScore >= limit || m->blueScore >= limit)) {
			finish(m, false);
			return;
		}
	}

	if (m->timeLeft > 0.0f || match_duration_seconds(&m->cfg) > 0) {
		m->timeLeft -= dt;
		if (m->timeLeft <= 0.0f) { m->timeLeft = 0.0f; finish(m, true); }
	}
}

bool match_player_won(const Match* m)
{
	if (m->cfg.mode == MODE_FFA) return m->winner == 2;
	if (mode_is_team(m->cfg.mode)) return m->winner == m->cfg.team;
	return false;
}

int match_coins_earned(const Match* m, bool won)
{
	if (m->cfg.mode == MODE_PRACTICE) return m->playerKills;
	return m->playerKills * 5 + 25 + (won ? 50 : 0);
}
