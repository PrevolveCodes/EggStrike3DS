#include "game.h"

#include <3ds.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "combat.h"
#include "config.h"
#include "fx.h"
#include "hud.h"
#include "input.h"
#include "loadout.h"
#include "map.h"
#include "match.h"
#include "menu.h"
#include "net.h"
#include "netplay.h"
#include "player.h"
#include "profile.h"
#include "renderer.h"
#include "rng.h"
#include "save.h"
#include "ui.h"
#include "viewmodel.h"

typedef enum { GS_MENU, GS_PLAYING, GS_PAUSED, GS_POSTMATCH } GameState;

static GameState state;
static PlayerProfile profile;
static Map map;        // static: too big for the stack
static Combat combat;
static Player player;
static Match match;
static float playSecondsAcc;
static bool wasAlive;
static float panoAngle;
static int lastCount;
static MatchPhase prevPhase;

static bool showScores;

// Joining a match "already in progress": the bots play on their own for a while before you spawn.
// It is simulated in slices so the loading screen keeps moving, with a hard wall-clock cap.
#define WARM_DT        (1.0f / 30.0f)
#define WARM_SLICE_MS  22
#define WARM_MAX_MS    5000
static float warmTarget, warmSim;
static u64 warmStartMs;

// ---- local multiplayer
#define MP_END_HOLD 1.5f          // the host keeps sending for a moment after the final whistle so everybody sees it
static Np np;
static NpRoomInfo scanned[5];
static int scanDelay;             // frames to wait so the 'Searching...' screen is drawn before the scan blocks
static bool wantRespawn;
static float respawnReqT, mpIntroT, mpEndT;
static const UiInput noUi = { 0 };

static bool mpMatch(void) { return np.state == NP_HOST_MATCH || np.state == NP_CLIENT_MATCH; }

// (Re)spawn the human with the chosen loadout at a spawn point away from enemies
static void spawnPlayer(void)
{
	player.loadout[0] = profile.favoriteWeapon;
	player.loadout[1] = W_EGGSTOL;
	int s = combat_choose_spawn(&combat, &player, &map, profile.lobby.team, FIGHTER_PLAYER);
	player_spawn(&player, &map.spawns[s]);
}

static void resetPlayer(void)
{
	memset(&player, 0, sizeof(player));
	player_init(&player);
	player.loadout[0] = profile.favoriteWeapon;
	player.loadout[1] = W_EGGSTOL;
	player.shell = profile.cos.sel[COS_SHELL];
	player.skin = profile.cos.sel[COS_SKIN];
	player.alive = false;         // nobody home until the match lets you in
	wasAlive = false;
}

bool game_init(void)
{
	profile_defaults(&profile);
	save_load(&profile);          // keeps defaults if there is no usable save
	menu_init(&profile);
	np_init(&np);
	menu_set_net(&np);
	input_init();
	rng_seed((unsigned int)osGetTime());
	state = GS_MENU;
	if (!audio_init()) menu_notify("No sound: DSP firmware not found");
	return true;
}

void game_shutdown(void)
{
	save_write(&profile);
	if (np_active(&np)) np_leave(&np, "");
	net_shutdown();
	audio_shutdown();
}

static void joinMatch(void);

static void startMatch(void)
{
#ifdef FORCE_MAP
	profile.lobby.map = FORCE_MAP;                // test build: always this map, Free for All with bots
	profile.lobby.mode = MODE_FFA;
	profile.lobby.totalBots = 5;
	match_sanitize(&profile.lobby);
#endif
#ifdef MELEE_TEST
	profile.lobby.mode = MODE_FFA;                // test build: a Free for All with several bots
	profile.lobby.totalBots = 5;
	match_sanitize(&profile.lobby);
#endif
	const MatchConfig* cfg = &profile.lobby;
	if (!map_load(&map, cfg->map)) map_load(&map, 0);
	renderer_load_map(&map);

	combat_init(&combat, &map, cfg);
	fx_init();

	resetPlayer();

	match_start(&match, cfg);     // PH_INTRO = still joining; the flyover plays behind the loading bar
	panoAngle = 0.0f;
	loadout_open(&profile);
	save_write(&profile);         // remember the lobby setup
	state = GS_PLAYING;

	// Modes with a running clock start "already underway"; rounds and practice start fresh
	bool underway = cfg->mode == MODE_FFA || cfg->mode == MODE_TDM || cfg->mode == MODE_CTF || cfg->mode == MODE_KOTP;
	warmSim = 0.0f;
	warmTarget = underway ? 20.0f + 15.0f * rng_float() : 0.0f;
	warmStartMs = osGetTime();
	combat.frozen = false;
	audio_set_muted(underway);
	if (!underway) joinMatch();
}

static void joinMatch(void)
{
	match_join(&match, warmSim);
	match.redScore = combat.teamScore[0];
	match.blueScore = combat.teamScore[1];
	match.botBestKills = combat_best_bot_kills(&combat);
	fx_init();
	combat.obj.msgT = 0.0f;
	combat.perkMsgT = 0.0f;
	audio_set_muted(false);
	spawnPlayer();
	wasAlive = true;
	lastCount = -1;
	audio_play(SFX_GO, 0.0f, 1.0f);
}

// One slice of the silent warm-up between the bots
static void warmUp(void)
{
	PlayerInput idle;
	memset(&idle, 0, sizeof(idle));
	idle.selectWeapon = -1;
	combat.frozen = false;
	u64 t0 = osGetTime();
	while (warmSim < warmTarget && osGetTime() - t0 < WARM_SLICE_MS) {
		combat_update(&combat, &player, &idle, &map, WARM_DT);
		warmSim += WARM_DT;
	}
	if (warmSim >= warmTarget || osGetTime() - warmStartMs > WARM_MAX_MS) joinMatch();
}

static void finishMatch(bool showResults)
{
	match.playerKills = player.kills;
	match.playerDeaths = player.deaths;
	bool won = match_player_won(&match);
	int coins = match_coins_earned(&match, won);

	Stats* s = &profile.stats;
	s->kills += (unsigned int)player.kills;
	s->deaths += (unsigned int)player.deaths;
	s->matchesPlayed++;
	if (won) s->matchesWon++;
	s->coins += (unsigned int)coins;
	save_write(&profile);

	if (showResults) {
		if (match.cfg.mode != MODE_PRACTICE) audio_play(won ? SFX_WIN : SFX_LOSE, 0.0f, 1.0f);
		ScoreRow rows[MAX_FIGHTERS];
		int nrows = combat_scoreboard(&combat, &player, rows, MAX_FIGHTERS);
		menu_show_post(&match, coins, won, rows, nrows);
		state = GS_POSTMATCH;
	}
	else             { menu_show_main(); state = GS_MENU; }
}

// ---------------------------------------------------------------- multiplayer flow
static void showMpHome(void) { menu_show_mp(); }

static void leaveToMpMenu(const char* msg)
{
	audio_set_muted(false);
	if (msg && msg[0]) menu_notify(msg);
	showMpHome();
	state = GS_MENU;
}

static void openRoom(void)
{
	if (!np_start_wireless()) { menu_notify("Wireless is not available"); return; }
	MatchConfig* l = &profile.lobby;
	if (l->mode == MODE_PRACTICE) l->mode = MODE_FFA;       // no Practice in multiplayer
	match_sanitize(l);
	if (!np_host_open(&np, l, profile.favoriteWeapon)) {
		menu_notify(np_message(&np)[0] ? np_message(&np) : "Could not open a room");
		return;
	}
	menu_show_room();
}

static void scanRooms(void)
{
	int n = 0;
	if (np_start_wireless()) n = np_scan(scanned, 5);
	else menu_notify("Wireless is not available");
	menu_set_rooms(scanned, n);
}

static void joinRoom(void)
{
	int i = menu_selected_room();
	if (np_join(&np, &scanned[i], profile.favoriteWeapon)) menu_show_room();
	else menu_notify(np_message(&np)[0] ? np_message(&np) : "Could not join that room");
}

// Host: everybody in the room is in; build the match, then wait in the flyover until the friends have loaded
static void startHostMatch(void)
{
	MatchConfig cfg = profile.lobby;
	if (cfg.mode == MODE_PRACTICE) cfg.mode = MODE_FFA;
	match_sanitize(&cfg);
	if (!map_load(&map, cfg.map)) { cfg.map = 0; map_load(&map, 0); }
	renderer_load_map(&map);
	np_host_set_config(&np, &cfg);

	combat_init(&combat, &map, &cfg);
	fx_init();
	np.seed = (unsigned)osGetTime();
	np_host_begin(&np, &combat, cfg.team);
	resetPlayer();
	match_start(&match, &cfg);
	panoAngle = 0.0f;
	warmTarget = 1.0f;
	warmSim = 0.0f;
	mpIntroT = 0.0f;
	mpEndT = 0.0f;
	wantRespawn = false;
	showScores = false;
	lastCount = -1;
	combat.frozen = false;
	audio_set_muted(false);
	save_write(&profile);
	state = GS_PLAYING;
}

// Client: START arrived from the host
static void startClientMatch(void)
{
	if (!map_load(&map, np.cfg.map)) {
		np_leave(&np, "That map is not available");
		leaveToMpMenu(np_message(&np));
		return;
	}
	renderer_load_map(&map);
	np_client_build(&np, &combat, &map, &match);
	fx_init();
	resetPlayer();
	panoAngle = 0.0f;
	warmTarget = 1.0f;
	warmSim = 0.0f;
	mpIntroT = 0.0f;
	mpEndT = 0.0f;
	wantRespawn = false;
	respawnReqT = 0.0f;
	showScores = false;
	lastCount = -1;
	audio_set_muted(false);
	state = GS_PLAYING;
}

// Waiting room and results screen: keep the connection alive, notice who left, follow the host into a match
static void mpMenuFrame(float dt)
{
	np.myWeapon = profile.favoriteWeapon;
	np_room_update(&np, dt);
	if (!np_active(&np)) {                       // the host closed the room, or the link dropped
		const char* m = np_message(&np);
		leaveToMpMenu(m[0] ? m : "The room was closed");
		return;
	}
	if (np.state == NP_HOST_ROOM) np_host_set_config(&np, &profile.lobby);
	if ((np.state == NP_CLIENT_ROOM || np.state == NP_CLIENT_MATCH) && np_client_take_start(&np)) startClientMatch();
}

// Leave a running match for good (pause menu)
static void quitMpMatch(void)
{
	finishMatch(false);
	np_leave(&np, "");
	showMpHome();
}

static void clientFrame(float dt, const UiInput* ui, bool paused)
{
	prevPhase = match.phase;
	np_client_frame_pre(&np, &combat, &player, &match, dt);
	if (!np_active(&np)) { leaveToMpMenu(np_message(&np)); return; }

	Spawn sp;
	if (np_client_take_spawn(&np, &sp)) {                // the host put us somewhere: first spawn, respawn or a new round
		player.loadout[0] = profile.favoriteWeapon;
		player.loadout[1] = W_EGGSTOL;
		player.shell = profile.cos.sel[COS_SHELL];
		player.skin = profile.cos.sel[COS_SKIN];
		player_spawn(&player, &sp);
		wantRespawn = false;
		wasAlive = true;
	}

	PlayerInput in;
	memset(&in, 0, sizeof(in));
	in.selectWeapon = -1;

	if (match.phase == PH_INTRO) {                       // loading: the flyover plays until the host starts the countdown
		panoAngle += PANO_SPEED * dt;
		mpIntroT += dt;
		warmSim = fminf(0.95f, mpIntroT / 4.0f);
		np_client_frame_post(&np, &combat, &player, &in, dt);
		return;
	}

	if (match.phase == PH_COUNTDOWN) {
		int c = (int)(match.countdown + 0.999f);
		if (c != lastCount) { lastCount = c; audio_play(SFX_COUNT_TICK, 0.0f, 1.0f); }
	}
	if (prevPhase == PH_COUNTDOWN && match.phase == PH_ACTIVE) audio_play(SFX_GO, 0.0f, 1.0f);

	if (!paused) {
		input_get_player(&in, dt, &profile.settings);
		if (ui->touchPressed && player.alive) {
			int w = hud_weapon_at(ui->tx, ui->ty);
			if (w >= 0) in.selectWeapon = w;
		}
	}
	if (match.phase != PH_ACTIVE || !player.alive) {
		int sel = in.selectWeapon;
		memset(&in, 0, sizeof(in));
		in.selectWeapon = (player.alive && match.phase == PH_ACTIVE) ? sel : -1;
	}
	in.lookYaw   /= player.zoom;
	in.lookPitch /= player.zoom;

	combat.frozen = (match.phase != PH_ACTIVE);
	if (player.alive) player_update(&player, &in, &map, dt);
	combat_update_client(&combat, &player, &in, &map, dt);
	fx_update(dt);

	if (!player.alive) {
		panoAngle += PANO_SPEED * dt;
		if (wasAlive) { loadout_open(&profile); wantRespawn = false; respawnReqT = 0.0f; }
		bool waitRound = combat.mode == MODE_ELIM;
		LoadoutAction la = loadout_update(ui, &profile, waitRound ? 1.0f : player.deadTimer);
		bool autoSpawn = !waitRound && player.deadTimer <= -(AUTO_RESPAWN - RESPAWN_DELAY);
		if (match.phase == PH_ACTIVE && !waitRound && ((la == LO_SPAWN && player.deadTimer <= 0.0f) || autoSpawn)) wantRespawn = true;
		if (wantRespawn && match.phase == PH_ACTIVE) {   // ask until the host's snapshot puts us back on the map
			respawnReqT -= dt;
			if (respawnReqT <= 0.0f) { respawnReqT = 0.7f; np_client_request_respawn(&np, profile.favoriteWeapon); }
		}
	} else {
		wantRespawn = false;
	}
	wasAlive = player.alive;

	np_client_frame_post(&np, &combat, &player, &in, dt);

	match.playerKills = player.kills;
	match.playerDeaths = player.deaths;
	match.redScore = combat.teamScore[0];
	match.blueScore = combat.teamScore[1];
	match.botBestKills = combat_best_bot_kills(&combat);
	showScores = ui->selectHeld;
	if (match.phase == PH_ENDED) {
		if (match.cfg.mode == MODE_FFA)               // the host's 'winner' only knows about the host's own egg
			match.winner = match.playerKills > match.botBestKills ? 2 : (match.botBestKills > match.playerKills ? 3 : -1);
		combat.frozen = true;
		finishMatch(true);
	}
}

static void updatePlaying(float dt, const UiInput* uiIn, bool paused)
{
	const bool mpH = np.state == NP_HOST_MATCH;
	const bool mpC = np.state == NP_CLIENT_MATCH;
	const UiInput* ui = paused ? &noUi : uiIn;

	if (!paused && match.phase != PH_INTRO && (ui->start || (ui->touchPressed && hud_pause_hit(ui->tx, ui->ty)))) {
		state = GS_PAUSED;
		menu_show_pause();
		audio_play(SFX_UI_TAP, 0.0f, 1.0f);
		if (!mpH && !mpC) return;
		paused = true;                 // in multiplayer the match does not wait for you
		ui = &noUi;
	}
	if (mpC) { clientFrame(dt, ui, paused); return; }
	if (mpH) np_host_frame_pre(&np, &combat, &player, &match, dt);

	// Joining: the map flies by while the bots play on without you (multiplayer: while everybody loads)
	if (match.phase == PH_INTRO) {
		panoAngle += PANO_SPEED * dt;
		if (mpH) {
			mpIntroT += dt;
			warmSim = fminf(0.95f, mpIntroT / 4.0f);
			if (np_host_all_ready(&np)) {
				match_begin(&match);
				spawnPlayer();
				wasAlive = true;
				lastCount = -1;
			}
			np_host_frame_post(&np, &combat, &player, &match, dt);
		} else {
			warmUp();
		}
		return;
	}

	PlayerInput in;
	if (paused) {
		memset(&in, 0, sizeof(in));
		in.selectWeapon = -1;
	} else {
		input_get_player(&in, dt, &profile.settings);
		if (ui->touchPressed && player.alive) {
			int w = hud_weapon_at(ui->tx, ui->ty);
			if (w >= 0) in.selectWeapon = w;
		}
	}

	prevPhase = match.phase;
	match_update(&match, dt);
	if (match.phase == PH_COUNTDOWN) {
		int c = (int)(match.countdown + 0.999f);
		if (c != lastCount) { lastCount = c; audio_play(SFX_COUNT_TICK, 0.0f, 1.0f); }
	}
	if (prevPhase == PH_COUNTDOWN && match.phase == PH_ACTIVE) audio_play(SFX_GO, 0.0f, 1.0f);

	// Frozen during the countdown and while eliminated (weapon taps still count)
	if (match.phase != PH_ACTIVE || !player.alive) {
		int sel = in.selectWeapon;
		memset(&in, 0, sizeof(in));
		in.selectWeapon = (player.alive && match.phase == PH_ACTIVE) ? sel : -1;
	}

	in.lookYaw   /= player.zoom;
	in.lookPitch /= player.zoom;

	combat.frozen = (match.phase != PH_ACTIVE);
	player_update(&player, &in, &map, dt);
	combat_update(&combat, &player, &in, &map, dt);
	fx_update(dt);

	if (player.alive && player.y < map.killY) { player.lastAttacker = -1; player.hp = 0.0f; }   // fell out of the map

	// Team Elimination: a new round puts everybody back, the human too (even if they were still standing)
	if (combat.obj.newRound) {
		combat.obj.newRound = false;
		spawnPlayer();
		wasAlive = true;
	}

	// Eliminated: choose weapons on the bottom screen, then spawn. The top screen flies around the map.
	if (!player.alive) {
		panoAngle += PANO_SPEED * dt;
		if (wasAlive) loadout_open(&profile);
		bool waitRound = combat.mode == MODE_ELIM;       // no respawn until the next round
		LoadoutAction la = loadout_update(ui, &profile, waitRound ? 1.0f : player.deadTimer);
		bool autoSpawn = !waitRound && player.deadTimer <= -(AUTO_RESPAWN - RESPAWN_DELAY);
		if (match.phase == PH_ACTIVE && !waitRound && ((la == LO_SPAWN && player.deadTimer <= 0.0f) || autoSpawn))
			spawnPlayer();
	}
	wasAlive = player.alive;

	match.playerKills = player.kills;
	match.playerDeaths = player.deaths;
	match.redScore = combat.teamScore[0];
	match.blueScore = combat.teamScore[1];
	match.botBestKills = combat_best_bot_kills(&combat);
	showScores = ui->selectHeld;
	if (match.phase == PH_ENDED) {
		combat.frozen = true;
		if (mpH) {
			np_host_frame_post(&np, &combat, &player, &match, dt);
			mpEndT += dt;
			if (mpEndT < MP_END_HOLD) return;
		}
		finishMatch(true);
		return;
	}
	if (mpH) np_host_frame_post(&np, &combat, &player, &match, dt);
}

bool game_update(float dt)
{
	UiInput ui;
	input_get_ui(&ui);
	audio_set_volume(profile.settings.musicVol, profile.settings.sfxVol);

	playSecondsAcc += dt;
	if (playSecondsAcc >= 1.0f) {
		profile.stats.playSeconds += (unsigned int)playSecondsAcc;
		playSecondsAcc -= (float)(int)playSecondsAcc;
	}

	MenuAction a = MENU_NONE;
	if ((state == GS_MENU || state == GS_POSTMATCH) && np_active(&np)) mpMenuFrame(dt);
	if (scanDelay > 0 && --scanDelay == 0) scanRooms();

	switch (state) {
	case GS_MENU:
		a = menu_update(&ui, dt);
		if (a == MENU_QUIT_APP) return false;
		if (a == MENU_START_MATCH) startMatch();
		else if (a == MENU_MP_HOST) openRoom();
		else if (a == MENU_MP_SCAN) scanDelay = 3;
		else if (a == MENU_MP_JOINROOM) joinRoom();
		else if (a == MENU_MP_START) startHostMatch();
		else if (a == MENU_MP_LEAVE) { np_leave(&np, ""); showMpHome(); }
		break;
	case GS_PLAYING:
		updatePlaying(dt, &ui, false);
		break;
	case GS_PAUSED:
		if (mpMatch()) updatePlaying(dt, &ui, true);      // the match goes on while the menu is open
		if (state != GS_PAUSED) break;                    // it ended, or the connection dropped
		a = menu_update(&ui, dt);
		if (a == MENU_RESUME) state = GS_PLAYING;
		else if (a == MENU_RESPAWN) {
			// Only while alive in a live match; the egg stays in play for DESPAWN_TIME seconds
			if (combat.mode == MODE_ELIM)                    menu_notify("No respawn in Team Elimination");
			else if (match.phase != PH_ACTIVE || !player.alive) menu_notify("Respawn works during a live match");
			else if (player.despawnT > 0.0f)               menu_notify("Already respawning");
			else {
				player.despawnT = DESPAWN_TIME;
				audio_play(SFX_MENU_SELECT, 0.0f, 1.0f);
				state = GS_PLAYING;
			}
		}
		else if (a == MENU_TO_MENU) { if (mpMatch()) quitMpMatch(); else finishMatch(false); }
		break;
	case GS_POSTMATCH:
		a = menu_update(&ui, dt);
		if (np_active(&np)) {
			if (a == MENU_REMATCH) { np_back_to_room(&np); menu_show_room(); state = GS_MENU; }       // back to the waiting room
			else if (a == MENU_TO_MENU) { np_leave(&np, ""); showMpHome(); state = GS_MENU; }
		} else {
			if (a == MENU_REMATCH) startMatch();
			else if (a == MENU_TO_MENU) { menu_show_main(); state = GS_MENU; }
		}
		break;
	}
	if (a == MENU_SAVE) save_write(&profile);

	return true;
}

void game_draw(void)
{
	ui_frame_begin();
	renderer_frame_begin();

	switch (state) {
	case GS_MENU:
	case GS_POSTMATCH:
		renderer_top_2d(UI_COL(110, 185, 255));
		menu_draw_top();
		renderer_bottom_2d(UI_COL(30, 36, 60));
		menu_draw_bottom();
		break;
	case GS_PLAYING:
	case GS_PAUSED:
		renderer_dyn_begin();
		Player cam;
		memset(&cam, 0, sizeof(cam));
		// The camera orbits the map while joining and while you are down; otherwise it is your own eyes
		const bool flyover = match.phase == PH_INTRO || !player.alive;
		if (flyover) player_panorama_pose(&cam, panoAngle, 0.0f, 0.0f, map.panoRadius, map.panoHeight);
		const float camPos[3] = { flyover ? cam.x : player.x,
		                          (flyover ? cam.y : player.y) + EYE_HEIGHT,
		                          flyover ? cam.z : player.z };
		combat_draw(&combat, camPos);
		fx_draw();
		if (flyover) {
			hud_draw_view(&player);                      // cinema bars
			renderer_top_3d(&cam, 1.0f);
			renderer_top_overlay();
			if (match.phase == PH_INTRO) {
				hud_draw_joining_top(warmTarget > 0.0f ? warmSim / warmTarget : 1.0f);
			} else {
				hud_draw_leaderboard(&player, &combat);
				hud_draw_objective(&player, &combat);
				hud_draw_death(&player, &combat);
			}
		} else {
			viewmodel_draw(&player);
			hud_draw_view(&player);
			renderer_top_3d(&player, player.zoom);
			renderer_top_overlay();
			combat_draw_names(&combat, &player, player.zoom);
			if (!(player.aiming && weapon_def(player.cur)->scoped)) hud_draw_leaderboard(&player, &combat);   // hidden inside the scope view
			hud_draw_respawn(&player);
			hud_draw_objective(&player, &combat);
			hud_draw_streak(&player, &combat);
			hud_draw_status(&player, &combat, &match);
		}
		renderer_bottom_2d(UI_COL(30, 36, 60));
		if (state == GS_PAUSED) menu_draw_bottom();
		else if (match.phase == PH_INTRO) hud_draw_joining_bottom(warmTarget > 0.0f ? warmSim / warmTarget : 1.0f, &match);
		else hud_draw_bottom(&player, &combat, &map, &match, &profile, showScores);
		break;
	}

	renderer_frame_end();
}
