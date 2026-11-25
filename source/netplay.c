#include "netplay.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "fx.h"
#include "pickups.h"
#include "synth.h"

// =====================================================================================================
//  Wire format helpers. Everything is little-endian. Positions are int16 in 1/64 m, angles are
//  uint16 turns, so a fighter costs about 15 bytes in a snapshot.
// =====================================================================================================
#define PI_F 3.14159265f
#define NP_MAGIC 0xE6

enum {
	P_HELLO = 1,   // client -> host: version, favourite weapon (also the keep-alive in rooms)
	P_ROOM,        // host -> client: room settings and roster
	P_START,       // host -> client: the match begins; map, settings, who is who
	P_READY,       // client -> host: map loaded
	P_INPUT,       // client -> host: where I am, what I did
	P_SNAP,        // host -> client: the match state
	P_EVT,         // host -> client: shots, explosions, hit confirmations
	P_LEAVE,       // either way: goodbye
};

enum { EV_FIRE = 1, EV_GREN, EV_RESPAWN, EV_PICKUP, EV_MELEE };          // client -> host (reliable, numbered)
enum { HE_SHOT = 1, HE_BOOM, HE_HIT, HE_MELEE };                         // host -> client (best effort)
enum { ROOM_OK, ROOM_FULL, ROOM_VERSION, ROOM_BUSY };

typedef struct { unsigned char* b; int n, cap; } W;
typedef struct { const unsigned char* b; int n, pos; bool bad; } R;

static void w8(W* w, int v)  { if (w->n < w->cap) w->b[w->n] = (unsigned char)v; w->n++; }
static void w16(W* w, int v) { w8(w, v & 255); w8(w, (v >> 8) & 255); }
static void w32(W* w, unsigned v) { w16(w, (int)(v & 0xFFFF)); w16(w, (int)(v >> 16)); }
static void wf(W* w, float f) { unsigned u; memcpy(&u, &f, 4); w32(w, u); }
static void wmem(W* w, const void* p, int n) { for (int i = 0; i < n; i++) w8(w, ((const unsigned char*)p)[i]); }
static void wstr16(W* w, const char* s) { char t[16]; memset(t, 0, sizeof(t)); snprintf(t, sizeof(t), "%s", s); wmem(w, t, 16); }

static int r8(R* r)  { if (r->pos >= r->n) { r->bad = true; return 0; } return r->b[r->pos++]; }
static int r16(R* r) { int lo = r8(r); int hi = r8(r); return lo | (hi << 8); }
static int ri16(R* r) { return (short)r16(r); }
static int ri8(R* r) { return (signed char)r8(r); }
static unsigned r32(R* r) { unsigned lo = (unsigned)r16(r); unsigned hi = (unsigned)r16(r); return lo | (hi << 16); }
static float rf(R* r) { unsigned u = r32(r); float f; memcpy(&f, &u, 4); return f; }
static void rmem(R* r, void* p, int n) { for (int i = 0; i < n; i++) ((unsigned char*)p)[i] = (unsigned char)r8(r); }
static void rstr16(R* r, char out[16]) { rmem(r, out, 16); out[15] = '\0'; }

static int   q64(float v)  { float t = v * 64.0f; t = t < -32767.0f ? -32767.0f : (t > 32767.0f ? 32767.0f : t); return (int)(t < 0 ? t - 0.5f : t + 0.5f); }
static float dq64(int v)   { return (float)v / 64.0f; }
static int   q16(float v)  { float t = v * 16.0f; t = t < -32767.0f ? -32767.0f : (t > 32767.0f ? 32767.0f : t); return (int)(t < 0 ? t - 0.5f : t + 0.5f); }
static float dq16(int v)   { return (float)v / 16.0f; }
static int   angU16(float a) { float t = a / (2.0f * PI_F); t -= floorf(t); return (int)(t * 65536.0f) & 65535; }
static float u16Ang(int v) { float a = (float)v * (2.0f * PI_F / 65536.0f); return a > PI_F ? a - 2.0f * PI_F : a; }
static int   clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int   q8(float v, float scale) { return clampi((int)(v * scale + 0.5f), 0, 255); }

static void putHeader(W* w, int type) { w8(w, NP_MAGIC); w8(w, type); }

static void dirFromAngles(float yaw, float pitch, float d[3])
{
	d[0] = sinf(yaw) * cosf(pitch);
	d[1] = sinf(pitch);
	d[2] = -cosf(yaw) * cosf(pitch);
}

static void anglesFromDir(const float d[3], float* yaw, float* pitch)
{
	*yaw = atan2f(d[0], -d[2]);
	*pitch = asinf(clampf(d[1], -1.0f, 1.0f));
}

static void putCfg(W* w, const MatchConfig* c)
{
	w8(w, c->mode); w8(w, c->map); w8(w, c->team); w8(w, c->difficulty);
	w8(w, c->totalBots); w8(w, c->redBots); w8(w, c->blueBots); w8(w, c->balance);
	w8(w, c->durationIdx); w8(w, c->scoreIdx);
}

static void getCfg(R* r, MatchConfig* c)
{
	c->mode = r8(r); c->map = r8(r); c->team = r8(r); c->difficulty = r8(r);
	c->totalBots = r8(r); c->redBots = r8(r); c->blueBots = r8(r); c->balance = r8(r);
	c->durationIdx = r8(r); c->scoreIdx = r8(r);
	match_sanitize(c);
}

static Np* g_np;       // the instance the combat hooks talk to (set at the start of every frame call)

// The transport is local wireless (net.h).
#define nx_active    net_active
#define nx_close     net_close
#define nx_node_mask net_node_mask
#define nx_node_name net_node_name
#define nx_send      net_send
#define nx_recv      net_recv

// =====================================================================================================
//  Rooms
// =====================================================================================================
void np_init(Np* np)
{
	memset(np, 0, sizeof(*np));
	np->state = NP_OFF;
	np->myIdx = -1;
}

const char* np_message(const Np* np) { return np->msg; }
bool np_active(const Np* np) { return np->state != NP_OFF; }

bool np_start_wireless(void) { return net_init(); }

static int humanByNode(const Np* np, int node)
{
	for (int i = 0; i < NP_MAX_HUMANS; i++) if (np->human[i].used && np->human[i].node == node) return i;
	return -1;
}

int np_human_count(const Np* np)
{
	int n = 0;
	for (int i = 0; i < NP_MAX_HUMANS; i++) if (np->human[i].used) n++;
	return n;
}

const NpHuman* np_human(const Np* np, int index)
{
	for (int i = 0; i < NP_MAX_HUMANS; i++) {
		if (!np->human[i].used) continue;
		if (index-- == 0) return &np->human[i];
	}
	return NULL;
}

static int appData(const Np* np, unsigned char* out)
{
	W w = { out, 0, NET_APPDATA_MAX };
	w8(&w, 'E'); w8(&w, 'G'); w8(&w, '3'); w8(&w, 'D');
	w8(&w, NP_VERSION);
	w8(&w, np->cfg.mode); w8(&w, np->cfg.map);
	w8(&w, np_human_count(np)); w8(&w, np->cfg.difficulty);
	w8(&w, np->state == NP_HOST_MATCH ? 1 : 0);
	return w.n;
}

static void publish(Np* np)
{
	unsigned char app[NET_APPDATA_MAX];
	int n = appData(np, app);
	net_host_set_appdata(app, n);
}

int np_scan(NpRoomInfo* out, int max)
{
	NetRoom raw[8];
	int n = net_scan(raw, max < 8 ? max : 8);
	int k = 0;
	for (int i = 0; i < n; i++) {
		const unsigned char* a = raw[i].app;
		if (raw[i].appLen < 10 || a[0] != 'E' || a[1] != 'G' || a[2] != '3' || a[3] != 'D' || a[4] != NP_VERSION) continue;
		NpRoomInfo* r = &out[k++];
		memset(r, 0, sizeof(*r));
		snprintf(r->name, sizeof(r->name), "%s", raw[i].name);
		r->mode = a[5]; r->map = a[6]; r->humans = a[7]; r->difficulty = a[8]; r->inMatch = a[9] != 0;
		r->raw = raw[i];
	}
	return k;
}

static void clearHooks(void);
static void setHooks(bool host);

bool np_host_open(Np* np, const MatchConfig* cfg, int myWeapon)
{
	np_init(np);
	np->cfg = *cfg;
	np->myWeapon = myWeapon;
	unsigned char app[NET_APPDATA_MAX];
	np->state = NP_HOST_ROOM;
	NpHuman* me = &np->human[0];
	me->used = true; me->node = NET_HOST_NODE; me->team = cfg->team; me->weapon = myWeapon; me->fighter = 0;
	snprintf(me->name, sizeof(me->name), "You");
	char nm[16];
	if (nx_node_name(NET_HOST_NODE, nm)) snprintf(me->name, sizeof(me->name), "%s", nm);
	np->myIdx = 0;
	int len = appData(np, app);
	if (!net_host_open(app, len)) { np->state = NP_OFF; snprintf(np->msg, sizeof(np->msg), "Could not open a room"); return false; }
	if (nx_node_name(NET_HOST_NODE, nm)) snprintf(me->name, sizeof(me->name), "%s", nm);
	return true;
}

bool np_join(Np* np, const NpRoomInfo* room, int myWeapon)
{
	np_init(np);
	np->myWeapon = myWeapon;
	if (!net_join(&room->raw)) { snprintf(np->msg, sizeof(np->msg), "Could not join that room"); return false; }
	np->state = NP_CLIENT_ROOM;
	np->helloTimer = 0.0f;
	np->hostSilence = 0.0f;
	return true;
}

static void sendLeave(int node)
{
	unsigned char buf[4];
	W w = { buf, 0, sizeof(buf) };
	putHeader(&w, P_LEAVE);
	nx_send(node, buf, w.n);
}

void np_leave(Np* np, const char* reason)
{
	if (np->state == NP_OFF) return;
	if (nx_active()) {
		if (np->state == NP_HOST_ROOM || np->state == NP_HOST_MATCH) {
			for (int rep = 0; rep < 2; rep++)
				for (int i = 1; i < NP_MAX_HUMANS; i++) if (np->human[i].used) sendLeave(np->human[i].node);
		} else {
			for (int rep = 0; rep < 2; rep++) sendLeave(NET_HOST_NODE);
		}
		nx_close();
	}
	clearHooks();
	NpState was = np->state;
	(void)was;
	np->state = NP_OFF;
	snprintf(np->msg, sizeof(np->msg), "%s", reason ? reason : "");
	for (int i = 0; i < NP_MAX_HUMANS; i++) np->human[i].used = false;
	np->combat = NULL;
	np->player = NULL;
}

void np_host_set_config(Np* np, const MatchConfig* cfg)
{
	if (np->state != NP_HOST_ROOM && np->state != NP_HOST_MATCH) return;
	if (memcmp(&np->cfg, cfg, sizeof(*cfg)) == 0) return;
	np->cfg = *cfg;
	np->human[0].team = cfg->team;
	np->roomTimer = 0.0f;       // tell everybody now
}

// After a match: back to the waiting room. Everybody who is still connected stays in it.
void np_back_to_room(Np* np)
{
	clearHooks();
	np->combat = NULL;
	np->player = NULL;
	np->startPending = false;
	if (np->state == NP_HOST_MATCH) {
		np->state = NP_HOST_ROOM;
		net_host_lock(false);
		for (int i = 0; i < NP_MAX_HUMANS; i++) { np->human[i].fighter = -1; np->human[i].ready = false; np->human[i].silence = 0.0f; }
		np->human[0].fighter = 0;
		np->roomTimer = 0.0f;
	} else if (np->state == NP_CLIENT_MATCH) {
		np->state = NP_CLIENT_ROOM;
		np->helloTimer = 0.0f;
		np->hostSilence = 0.0f;
	}
}

static int smallerTeam(const Np* np)
{
	int cnt[2] = { 0, 0 };
	for (int i = 0; i < NP_MAX_HUMANS; i++) if (np->human[i].used) cnt[np->human[i].team & 1]++;
	return cnt[0] <= cnt[1] ? TEAM_RED : TEAM_BLUE;
}

static void sendRoom(Np* np, int hi, int status)
{
	unsigned char buf[320];
	W w = { buf, 0, sizeof(buf) };
	putHeader(&w, P_ROOM);
	w8(&w, status);
	int yours = 0, k = 0;
	for (int i = 0; i < NP_MAX_HUMANS; i++) { if (!np->human[i].used) continue; if (i == hi) yours = k; k++; }
	w8(&w, yours);
	w8(&w, np->state == NP_HOST_MATCH ? 1 : 0);
	putCfg(&w, &np->cfg);
	w8(&w, k);
	for (int i = 0; i < NP_MAX_HUMANS; i++) {
		if (!np->human[i].used) continue;
		wstr16(&w, np->human[i].name);
		w8(&w, np->human[i].team);
	}
	nx_send(np->human[hi].node, buf, w.n);
}

static void removeHuman(Np* np, int hi)
{
	NpHuman* h = &np->human[hi];
	if (!h->used) return;
	if (np->state == NP_HOST_MATCH && np->combat && h->fighter > 0) combat_drop_remote(np->combat, h->fighter);
	memset(h, 0, sizeof(*h));
	np->roomTimer = 0.0f;
}

static void sendReject(int node, int status)
{
	unsigned char b[8];
	W w = { b, 0, sizeof(b) };
	putHeader(&w, P_ROOM);
	w8(&w, status);
	nx_send(node, b, w.n);
}

static void hostRoomPacket(Np* np, int node, const unsigned char* buf, int len)
{
	R r = { buf, len, 0, false };
	if (r8(&r) != NP_MAGIC) return;
	int type = r8(&r);
	int hi = humanByNode(np, node);
	if (hi >= 0) np->human[hi].silence = 0.0f;

	if (type == P_LEAVE) { if (hi >= 0) removeHuman(np, hi); return; }
	if (type != P_HELLO) return;
	int ver = r8(&r), weapon = r8(&r);
	char hname[16];
	rstr16(&r, hname);
	if (r.bad) return;
	if (hi < 0) {
		if (ver != NP_VERSION) { sendReject(node, ROOM_VERSION); return; }
		int slot = -1;
		for (int i = 1; i < NP_MAX_HUMANS; i++) if (!np->human[i].used) { slot = i; break; }
		if (slot < 0) { sendReject(node, ROOM_FULL); return; }
		NpHuman* h = &np->human[slot];
		memset(h, 0, sizeof(*h));
		h->used = true;
		h->node = node;
		h->fighter = -1;
		h->team = smallerTeam(np);
		snprintf(h->name, sizeof(h->name), "Player %d", node);
		char nm[16];
		if (nx_node_name(node, nm)) snprintf(h->name, sizeof(h->name), "%s", nm);
		hi = slot;
		np->roomTimer = 0.0f;
	}
	np->human[hi].weapon = clampi(weapon, 0, PRIMARY_COUNT - 1);
}

static void clientRoomPacket(Np* np, const unsigned char* buf, int len)
{
	R r = { buf, len, 0, false };
	if (r8(&r) != NP_MAGIC) return;
	int type = r8(&r);
	np->hostSilence = 0.0f;

	if (type == P_LEAVE) { np_leave(np, "The host closed the room"); return; }
	if (type == P_ROOM) {
		int status = r8(&r);
		if (status == ROOM_FULL)    { np_leave(np, "That room is full"); return; }
		if (status == ROOM_VERSION) { np_leave(np, "Different game version"); return; }
		if (status == ROOM_BUSY)    { np_leave(np, "A match is already running"); return; }
		int yours = r8(&r);
		np->inMatchRoom = r8(&r) != 0;
		MatchConfig cfg = np->cfg;
		getCfg(&r, &cfg);
		int n = clampi(r8(&r), 0, NP_MAX_HUMANS);
		NpHuman tmp[NP_MAX_HUMANS];
		memset(tmp, 0, sizeof(tmp));
		for (int i = 0; i < n; i++) {
			tmp[i].used = true;
			rstr16(&r, tmp[i].name);
			tmp[i].team = r8(&r);
			tmp[i].node = i == 0 ? NET_HOST_NODE : i + 1;
		}
		if (r.bad) return;
		if (np->state == NP_CLIENT_ROOM) {
			memcpy(np->human, tmp, sizeof(tmp));
			np->cfg = cfg;
			np->myIdx = yours;
			if (yours >= 0 && yours < n) np->cfg.team = tmp[yours].team;
		}
		return;
	}
	if (type == P_START && np->state == NP_CLIENT_ROOM && !np->startPending) {
		np->seed = r32(&r);
		MatchConfig cfg = np->cfg;
		getCfg(&r, &cfg);
		int k = r8(&r);
		int n = clampi(r8(&r), 2, MAX_FIGHTERS);
		for (int i = 0; i < n; i++) {
			rstr16(&r, np->fighters[i].name);
			np->fighters[i].team = (signed char)r8(&r);
			np->fighters[i].human = r8(&r) != 0;
		}
		if (r.bad || k < 1 || k >= n) return;
		np->cfg = cfg;
		np->myFighter = k;
		np->nFighters = n;
		np->cfg.team = np->fighters[k].team < 0 ? 0 : np->fighters[k].team;
		np->startPending = true;
	}
}

void np_room_update(Np* np, float dt)
{
	if (np->state == NP_OFF) return;
	unsigned char buf[NET_MAX_PACKET];
	int from;
	int len;

	if (np->state == NP_HOST_ROOM || np->state == NP_HOST_MATCH) {
		while ((len = nx_recv(&from, buf, sizeof(buf))) > 0) {
			if (np->state == NP_HOST_ROOM) hostRoomPacket(np, from, buf, len);
			else {                                      // post-match screen: only keep-alives and goodbyes matter
				int hi = humanByNode(np, from);
				if (hi >= 0) np->human[hi].silence = 0.0f;
				if (len >= 2 && buf[0] == NP_MAGIC && buf[1] == P_LEAVE && hi >= 0) removeHuman(np, hi);
			}
		}
		unsigned mask = nx_node_mask();
		const float limit = np->state == NP_HOST_ROOM ? 8.0f : 25.0f;
		for (int i = 1; i < NP_MAX_HUMANS; i++) {
			NpHuman* h = &np->human[i];
			if (!h->used) continue;
			h->silence += dt;
			if (h->silence > limit || (mask && !(mask & (1u << (h->node - 1))))) removeHuman(np, i);
		}
		np->roomTimer -= dt;
		if (np->roomTimer <= 0.0f) {
			np->roomTimer = 0.5f;
			for (int i = 1; i < NP_MAX_HUMANS; i++) if (np->human[i].used) sendRoom(np, i, ROOM_OK);
			publish(np);
		}
		return;
	}

	// client (room, or the post-match screen)
	while ((len = nx_recv(&from, buf, sizeof(buf))) > 0) {
		if (from != NET_HOST_NODE) continue;
		if (np->state == NP_CLIENT_ROOM) clientRoomPacket(np, buf, len);
		else {
			np->hostSilence = 0.0f;
			if (len >= 2 && buf[0] == NP_MAGIC && buf[1] == P_LEAVE) { np_leave(np, "The host closed the room"); return; }
			// Still looking at the results while the host already started the next match: join it
			if (len >= 2 && buf[0] == NP_MAGIC && buf[1] == P_START) {
				np_back_to_room(np);
				clientRoomPacket(np, buf, len);
			}
		}
	}
	np->hostSilence += dt;
	unsigned mask = nx_node_mask();
	if (np->hostSilence > (np->state == NP_CLIENT_ROOM ? 6.0f : 25.0f) || (nx_active() && mask && !(mask & 1u))) {
		np_leave(np, "Lost the connection to the host");
		return;
	}
	np->helloTimer -= dt;
	if (np->helloTimer <= 0.0f) {
		np->helloTimer = 0.5f;
		unsigned char b[32];
		W w = { b, 0, sizeof(b) };
		putHeader(&w, P_HELLO);
		w8(&w, NP_VERSION);
		w8(&w, np->myWeapon);
		wstr16(&w, "");                           // name slot (the host reads names from the wireless network)
		nx_send(NET_HOST_NODE, b, w.n);
	}
}

// =====================================================================================================
//  Host: starting
// =====================================================================================================
static void sendStart(Np* np, int hi)
{
	unsigned char buf[NET_MAX_PACKET];
	W w = { buf, 0, sizeof(buf) };
	putHeader(&w, P_START);
	w32(&w, np->seed);
	putCfg(&w, &np->cfg);
	w8(&w, np->human[hi].fighter);
	w8(&w, np->nFighters);
	for (int i = 0; i < np->nFighters; i++) {
		wstr16(&w, np->fighters[i].name);
		w8(&w, np->fighters[i].team);
		w8(&w, np->fighters[i].human ? 1 : 0);
	}
	nx_send(np->human[hi].node, buf, w.n);
}

static void refreshFighters(Np* np, Combat* c)
{
	np->nFighters = c->botCount + 1;
	const bool team = combat_is_team_mode(c);
	snprintf(np->fighters[0].name, 16, "%s", np->human[0].name);
	np->fighters[0].team = team ? c->playerTeam : -1;
	np->fighters[0].human = true;
	for (int i = 0; i < c->botCount; i++) {
		snprintf(np->fighters[i + 1].name, 16, "%s", c->bots[i].name);
		np->fighters[i + 1].team = team ? c->bots[i].team : -1;
		np->fighters[i + 1].human = c->bots[i].remote;
	}
}

void np_host_begin(Np* np, Combat* c, int hostTeam)
{
	np->combat = c;
	np->state = NP_HOST_MATCH;
	np->tick = 0;
	np->snapSeq = 0;
	np->startAge = 0.0f;
	np->startTimer = 0.0f;
	np->snapAcc = 0.0f;
	c->netRole = NET_HOST;
	c->playerTeam = hostTeam;
	np->cfg.team = hostTeam;
	np->human[0].team = hostTeam;
	np->human[0].fighter = 0;
	np->human[0].ready = true;

	for (int i = 1; i < NP_MAX_HUMANS; i++) {
		NpHuman* h = &np->human[i];
		if (!h->used) continue;
		h->ready = false;
		h->silence = 0.0f;
		h->evSeq = 0;
		h->inSeq = 0;
		h->fighter = combat_add_remote(c, h->name, h->team, h->node, h->weapon);
		if (h->fighter < 0) { sendLeave(h->node); removeHuman(np, i); }
	}

	refreshFighters(np, c);
	memset(np->evcount, 0, sizeof(np->evcount));
	memset(np->evlen, 0, sizeof(np->evlen));
	net_host_lock(true);
	publish(np);
	setHooks(true);
	g_np = np;
	for (int i = 1; i < NP_MAX_HUMANS; i++) if (np->human[i].used) sendStart(np, i);
}

bool np_host_all_ready(const Np* np)
{
	if (np->startAge > 10.0f) return true;
	for (int i = 1; i < NP_MAX_HUMANS; i++) if (np->human[i].used && !np->human[i].ready) return false;
	return true;
}

// =====================================================================================================
//  Host: event queue to the clients (best effort)
// =====================================================================================================
static bool evBegin(Np* np, int hi, int bytes)
{
	if (np->evlen[hi] + bytes > (int)sizeof(np->evbuf[hi]) - 4) return false;
	np->evcount[hi]++;
	return true;
}

static void hookShot(Combat* c, int shooter, int weapon, const float eye[3], const float dir[3], float spread)
{
	Np* np = g_np;
	if (!np || np->state != NP_HOST_MATCH || c != np->combat) return;
	float yaw, pitch;
	anglesFromDir(dir, &yaw, &pitch);
	for (int hi = 1; hi < NP_MAX_HUMANS; hi++) {
		if (!np->human[hi].used || np->human[hi].fighter == shooter) continue;   // the shooter already shows its own shot
		if (!evBegin(np, hi, 17)) continue;
		W w = { np->evbuf[hi] + np->evlen[hi], 0, 17 };
		w8(&w, HE_SHOT); w8(&w, shooter); w8(&w, weapon);
		w16(&w, q64(eye[0])); w16(&w, q64(eye[1])); w16(&w, q64(eye[2]));
		w16(&w, angU16(yaw)); w16(&w, (int)(pitch * 10000.0f)); w8(&w, q8(spread, 1000.0f));
		np->evlen[hi] += w.n;
	}
}

static void hookBoom(const float pos[3], float radius)
{
	Np* np = g_np;
	if (!np || np->state != NP_HOST_MATCH) return;
	for (int hi = 1; hi < NP_MAX_HUMANS; hi++) {
		if (!np->human[hi].used || !evBegin(np, hi, 9)) continue;
		W w = { np->evbuf[hi] + np->evlen[hi], 0, 9 };
		w8(&w, HE_BOOM); w16(&w, q64(pos[0])); w16(&w, q64(pos[1])); w16(&w, q64(pos[2])); w8(&w, q8(radius, 10.0f));
		np->evlen[hi] += w.n;
	}
}

static void hookHit(int attacker, int victim)
{
	(void)victim;
	Np* np = g_np;
	if (!np || np->state != NP_HOST_MATCH || attacker < 1) return;
	for (int hi = 1; hi < NP_MAX_HUMANS; hi++) {
		if (!np->human[hi].used || np->human[hi].fighter != attacker || !evBegin(np, hi, 1)) continue;
		np->evbuf[hi][np->evlen[hi]++] = HE_HIT;
	}
}

static void hookMelee(int attacker, bool hit)
{
	Np* np = g_np;
	if (!np || np->state != NP_HOST_MATCH || attacker < 0) return;
	for (int hi = 1; hi < NP_MAX_HUMANS; hi++) {
		if (!np->human[hi].used || np->human[hi].fighter == attacker || !evBegin(np, hi, 3)) continue;   // the swinger shows its own
		W w = { np->evbuf[hi] + np->evlen[hi], 0, 3 };
		w8(&w, HE_MELEE); w8(&w, attacker); w8(&w, hit ? 1 : 0);
		np->evlen[hi] += w.n;
	}
}

// ---- client side hooks: queue numbered events for the host
static void queueEvent(Np* np, int type, const unsigned char* data, int len)
{
	if (np->pendingN >= 16) {                               // too many unconfirmed: drop the oldest
		memmove(&np->pending[0], &np->pending[1], sizeof(np->pending[0]) * 15);
		np->pendingN = 15;
	}
	NpPendingEvent* e = &np->pending[np->pendingN++];
	np->nextEvSeq = (np->nextEvSeq + 1) & 255;
	e->type = (unsigned char)type;
	e->seq = (unsigned char)np->nextEvSeq;
	e->len = (unsigned char)len;
	memcpy(e->data, data, (size_t)len);
}

static void hookLocalFire(int weapon, float spread, const float dir[3])
{
	Np* np = g_np;
	if (!np || np->state != NP_CLIENT_MATCH) return;
	float yaw, pitch;
	anglesFromDir(dir, &yaw, &pitch);
	unsigned char d[8];
	W w = { d, 0, sizeof(d) };
	w8(&w, weapon); w8(&w, q8(spread, 1000.0f)); w16(&w, angU16(yaw)); w16(&w, (int)(pitch * 10000.0f)); w16(&w, (int)(np->lastSnapFrame & 0xFFFF));
	queueEvent(np, EV_FIRE, d, w.n);
}

static void hookLocalGrenade(const float dir[3])
{
	Np* np = g_np;
	if (!np || np->state != NP_CLIENT_MATCH) return;
	float yaw, pitch;
	anglesFromDir(dir, &yaw, &pitch);
	unsigned char d[4];
	W w = { d, 0, sizeof(d) };
	w16(&w, angU16(yaw)); w16(&w, (int)(pitch * 10000.0f));
	queueEvent(np, EV_GREN, d, w.n);
}

static void hookLocalMelee(float yaw)
{
	Np* np = g_np;
	if (!np || np->state != NP_CLIENT_MATCH) return;
	unsigned char d[4];
	W w = { d, 0, sizeof(d) };
	w16(&w, angU16(yaw)); w16(&w, (int)(np->lastSnapFrame & 0xFFFF));
	queueEvent(np, EV_MELEE, d, w.n);
}

void np_client_request_respawn(Np* np, int weapon)
{
	unsigned char d[1] = { (unsigned char)clampi(weapon, 0, PRIMARY_COUNT - 1) };
	queueEvent(np, EV_RESPAWN, d, 1);
}

static void setHooks(bool host)
{
	memset(&combat_hooks, 0, sizeof(combat_hooks));
	if (host) {
		combat_hooks.onShot = hookShot;
		combat_hooks.onBoom = hookBoom;
		combat_hooks.onHit = hookHit;
		combat_hooks.onMelee = hookMelee;
	} else {
		combat_hooks.onLocalFire = hookLocalFire;
		combat_hooks.onLocalGrenade = hookLocalGrenade;
		combat_hooks.onLocalMelee = hookLocalMelee;
	}
}

static void clearHooks(void) { memset(&combat_hooks, 0, sizeof(combat_hooks)); }

static int evPayloadSize(int type)
{
	switch (type) {
	case EV_FIRE:    return 8;
	case EV_GREN:    return 4;
	case EV_RESPAWN: return 1;
	case EV_PICKUP:  return 1;
	case EV_MELEE:   return 4;
	default:         return -1;
	}
}

// =====================================================================================================
//  Host: frame
// =====================================================================================================
static void hostApplyEvent(Np* np, Combat* c, Player* p, NpHuman* h, Bot* b, int type, R* r)
{
	switch (type) {
	case EV_FIRE: {
		int weapon = r8(r), spread = r8(r), yaw = r16(r), pitch = ri16(r), snap16 = r16(r);
		if (!b->alive || c->frozen || weapon < 0 || weapon >= WEAPON_COUNT) break;
		float dir[3];
		dirFromAngles(u16Ang(yaw), (float)pitch / 10000.0f, dir);
		float eye[3] = { b->x, b->y + EYE_HEIGHT, b->z };
		b->invuln = 0.0f;
		b->flashT = 0.06f;
		b->weapon = weapon;
		int frameThen = (int)c->frame - (int)(((unsigned)c->frame - (unsigned)snap16) & 0xFFFF);
		combat_rewind_begin(c, frameThen, b->id);
		combat_fire(c, p, c->map, b->id, eye, dir, (float)spread / 1000.0f, weapon);
		combat_rewind_end(c);
		audio_play_at(SFX_FIRE_BASE + weapon, p->x, p->z, p->yaw, b->x, b->z, 0.8f);
		break;
	}
	case EV_GREN: {
		int yaw = r16(r), pitch = ri16(r);
		if (!b->alive || c->frozen || b->grenadeCd > 0.0f) break;
		float dir[3];
		dirFromAngles(u16Ang(yaw), (float)pitch / 10000.0f, dir);
		float eye[3] = { b->x, b->y + EYE_HEIGHT, b->z };
		combat_throw_grenade(c, b->id, eye, dir, GRENADE_SPEED);
		b->grenadeCd = GRENADE_COOLDOWN;
		b->invuln = 0.0f;
		break;
	}
	case EV_MELEE: {
		int yaw = r16(r), snap16 = r16(r);
		if (!b->alive || c->frozen || b->meleeCd > 0.0f) break;
		b->meleeT = MELEE_TIME - MELEE_HIT_AT;             // the swing is already at the moment it lands
		b->meleeCd = MELEE_COOLDOWN * 0.8f;                // a little slack for packets arriving in bunches
		b->invuln = 0.0f;
		b->ws.reloadLeft = 0.0f;
		float eye[3] = { b->x, b->y + EYE_HEIGHT, b->z };
		audio_play_at(SFX_MELEE_SWING, p->x, p->z, p->yaw, b->x, b->z, 0.8f);
		int frameThen = (int)c->frame - (int)(((unsigned)c->frame - (unsigned)snap16) & 0xFFFF);
		combat_rewind_begin(c, frameThen, b->id);
		combat_melee(c, p, c->map, b->id, eye, u16Ang(yaw), NULL);
		combat_rewind_end(c);
		break;
	}
	case EV_RESPAWN: {
		int weapon = r8(r);
		combat_remote_respawn(c, p, b->id, weapon);
		break;
	}
	case EV_PICKUP: {
		int idx = r8(r);
		ammo_remote_pickup(c, b, idx);
		break;
	}
	default: break;
	}
	(void)np; (void)h;
}

static void hostInput(Np* np, Combat* c, Player* p, int hi, R* r)
{
	NpHuman* h = &np->human[hi];
	if (h->fighter < 1 || h->fighter > c->botCount) return;
	Bot* b = &c->bots[h->fighter - 1];

	int seq = r8(r), ack = r8(r);
	(void)r16(r);                                      // snapshot frame the client was showing (events carry their own)
	float x = dq64(ri16(r)), y = dq64(ri16(r)), z = dq64(ri16(r));
	float yaw = u16Ang(r16(r));
	float pitch = (float)ri8(r) / 80.0f;
	int flags = r8(r);
	int weapon = r8(r);
	int nev = r8(r);
	if (r->bad) return;

	int d = (seq - h->inSeq) & 255;
	if (d != 0 && d < 128) {                           // newer than the last one applied
		h->inSeq = seq;
		if (weapon >= 0 && weapon < WEAPON_COUNT) b->weapon = weapon;
		b->ackSpawn = ack;
		if (b->alive && ack == (b->spawnSeq & 255)) {   // ignore positions from before the client saw its respawn
			b->x = x; b->y = y; b->z = z;
			b->yaw = yaw; b->pitch = pitch;
			b->onGround = (flags & 1) != 0;
		}
	}

	for (int i = 0; i < nev; i++) {
		int type = r8(r), eseq = r8(r);
		int size = evPayloadSize(type);
		if (size < 0 || r->bad) return;
		int de = (eseq - h->evSeq) & 255;
		if (de == 1) {
			h->evSeq = eseq;
			hostApplyEvent(np, c, p, h, b, type, r);
		} else {
			for (int k = 0; k < size; k++) r8(r);      // already applied (a resend) or an earlier one is missing
			if (de >= 2 && de < 128) return;           // gap: wait for the resend of the missing one
		}
	}
}

void np_host_frame_pre(Np* np, Combat* c, Player* p, Match* m, float dt)
{
	(void)m;
	g_np = np;
	np->combat = c;
	np->player = p;
	np->startAge += dt;

	unsigned char buf[NET_MAX_PACKET];
	int from, len;
	while ((len = nx_recv(&from, buf, sizeof(buf))) > 0) {
		int hi = humanByNode(np, from);
		if (hi < 1) {
			if (len >= 2 && buf[0] == NP_MAGIC && buf[1] == P_HELLO) {
				sendReject(from, ROOM_BUSY);                                               // a late joiner
			}
			continue;
		}
		NpHuman* h = &np->human[hi];
		h->silence = 0.0f;
		R r = { buf, len, 0, false };
		if (r8(&r) != NP_MAGIC) continue;
		int type = r8(&r);
		switch (type) {
		case P_INPUT: hostInput(np, c, p, hi, &r); break;
		case P_READY: h->ready = true; break;
		case P_LEAVE: removeHuman(np, hi); break;
		default: break;
		}
	}

	unsigned mask = nx_node_mask();
	for (int i = 1; i < NP_MAX_HUMANS; i++) {
		NpHuman* h = &np->human[i];
		if (!h->used) continue;
		h->silence += dt;
		if (h->silence > 6.0f || (mask && !(mask & (1u << (h->node - 1))))) removeHuman(np, i);
	}

	np->startTimer -= dt;
	if (np->startTimer <= 0.0f) {
		np->startTimer = 0.25f;
		for (int i = 1; i < NP_MAX_HUMANS; i++) if (np->human[i].used && !np->human[i].ready) sendStart(np, i);
	}
}

static void putFighter(W* w, const Combat* c, const Player* p, int id)
{
	float pos[3], yaw, pitch, hp;
	bool alive, ground, inv;
	int weapon, kills, deaths;
	if (id == FIGHTER_PLAYER) {
		pos[0] = p->x; pos[1] = p->y; pos[2] = p->z; yaw = p->yaw; pitch = p->pitch; hp = p->hp;
		alive = p->alive; ground = p->onGround; inv = p->invuln > 0.0f; weapon = p->cur; kills = p->kills; deaths = p->deaths;
	} else {
		const Bot* b = &c->bots[id - 1];
		pos[0] = b->x; pos[1] = b->y; pos[2] = b->z; yaw = b->yaw; pitch = b->pitch; hp = b->hp;
		alive = b->alive; ground = b->onGround; inv = b->invuln > 0.0f; weapon = b->weapon; kills = b->kills; deaths = b->deaths;
	}
	w8(w, (alive ? 1 : 0) | (ground ? 2 : 0) | (inv ? 4 : 0));
	w8(w, weapon);
	w16(w, q64(pos[0])); w16(w, q64(pos[1])); w16(w, q64(pos[2]));
	w16(w, angU16(yaw));
	w8(w, clampi((int)(pitch * 80.0f), -127, 127));
	w8(w, clampi((int)ceilf(hp), 0, 255));
	w8(w, clampi(kills, 0, 255));
	w8(w, clampi(deaths, 0, 255));
}

static void putSelf(W* w, const Bot* b)
{
	w8(w, clampi((int)ceilf(b->hp), 0, 100));
	w8(w, (b->alive ? 1 : 0) | (b->invuln > 0.0f ? 2 : 0));
	w8(w, q8(b->invuln, 10.0f));
	w8(w, clampi(b->perks.streak, 0, 255));
	w8(w, q8(b->perks.shield, 1.0f));
	w8(w, q8(b->perks.hotT, 10.0f));
	w8(w, q8(b->perks.crackT, 10.0f));
	w8(w, b->spawnSeq & 255);
	w16(w, q64(b->x)); w16(w, q64(b->y)); w16(w, q64(b->z));
	w16(w, angU16(b->yaw));
	w8(w, clampi((int)(b->respawnTimer * 10.0f), -127, 127));
	w8(w, b->lastAttacker);
}

void np_host_frame_post(Np* np, Combat* c, Player* p, Match* m, float dt)
{
	g_np = np;
	(void)dt;
	combat_record_history(c, p);
	np->tick++;

	if ((np->tick & 1u) == 0u) {                       // snapshots go out every second frame (about 15 per second)
		unsigned char common[1100];
		W cw = { common, 0, sizeof(common) };
		const bool world = (np->tick % 12u) == 0u || np->tick < 8u;

		w8(&cw, m->phase);
		w8(&cw, q8(m->countdown, 10.0f));
		wf(&cw, m->elapsed);
		wf(&cw, m->timeLeft);
		w16(&cw, c->teamScore[0]); w16(&cw, c->teamScore[1]);
		w8(&cw, m->timeUp ? 1 : 0);
		w8(&cw, m->winner);

		w8(&cw, c->botCount + 1);
		for (int id = 0; id <= c->botCount; id++) putFighter(&cw, c, p, id);

		int np_proj = 0, np_gren = 0;
		for (int i = 0; i < MAX_PROJECTILES; i++) if (c->proj[i].active) np_proj++;
		for (int i = 0; i < MAX_GRENADES; i++) if (c->gren[i].active) np_gren++;
		w8(&cw, np_proj);
		for (int i = 0; i < MAX_PROJECTILES; i++) {
			const Projectile* pr = &c->proj[i];
			if (!pr->active) continue;
			w8(&cw, pr->weapon); w8(&cw, pr->owner);
			for (int a = 0; a < 3; a++) w16(&cw, q64(pr->pos[a]));
			for (int a = 0; a < 3; a++) w16(&cw, q16(pr->vel[a]));
		}
		w8(&cw, np_gren);
		for (int i = 0; i < MAX_GRENADES; i++) {
			const Grenade* g = &c->gren[i];
			if (!g->active) continue;
			w8(&cw, g->owner); w8(&cw, q8(g->fuse, 50.0f));
			for (int a = 0; a < 3; a++) w16(&cw, q64(g->pos[a]));
			for (int a = 0; a < 3; a++) w16(&cw, q16(g->vel[a]));
		}
		if (world) {
			w16(&cw, (int)sizeof(Objective));
			wmem(&cw, &c->obj, (int)sizeof(Objective));
			w8(&cw, c->ammoBoxCount);
			for (int i = 0; i < c->ammoBoxCount; i++) {
				for (int a = 0; a < 3; a++) w16(&cw, q64(c->ammoBox[i].pos[a]));
				w8(&cw, q8(c->ammoBox[i].timer, 10.0f));
			}
		}

		if (cw.n <= (int)sizeof(common)) {
			np->snapSeq++;
			for (int hi = 1; hi < NP_MAX_HUMANS; hi++) {
				NpHuman* h = &np->human[hi];
				if (!h->used || h->fighter < 1) continue;
				unsigned char buf[NET_MAX_PACKET];
				W w = { buf, 0, sizeof(buf) };
				putHeader(&w, P_SNAP);
				w16(&w, (int)(np->snapSeq & 0xFFFF));
				w32(&w, (unsigned)c->frame);
				w8(&w, world ? 1 : 0);
				putSelf(&w, &c->bots[h->fighter - 1]);
				w8(&w, h->evSeq);
				wmem(&w, common, cw.n);
				if (w.n <= (int)sizeof(buf)) nx_send(h->node, buf, w.n);
			}
		}
	}

	for (int hi = 1; hi < NP_MAX_HUMANS; hi++) {
		if (!np->human[hi].used || np->evcount[hi] == 0) { np->evcount[hi] = 0; np->evlen[hi] = 0; continue; }
		unsigned char buf[NET_MAX_PACKET];
		W w = { buf, 0, sizeof(buf) };
		putHeader(&w, P_EVT);
		w8(&w, np->evcount[hi]);
		wmem(&w, np->evbuf[hi], np->evlen[hi]);
		nx_send(np->human[hi].node, buf, w.n);
		np->evcount[hi] = 0;
		np->evlen[hi] = 0;
	}
}

// =====================================================================================================
//  Client
// =====================================================================================================
static int mapId(const Np* np, int id)
{
	if (id < 0) return id;
	if (id == 0) return np->myFighter;
	if (id == np->myFighter) return 0;
	return id;
}

bool np_client_take_start(Np* np)
{
	if (!np->startPending) return false;
	np->startPending = false;
	return true;
}

void np_client_build(Np* np, Combat* c, const Map* map, Match* m)
{
	MatchConfig cfg = np->cfg;
	combat_init_mirror(c, map, &cfg);
	c->botCount = np->nFighters - 1;
	for (int L = 1; L <= c->botCount; L++) {
		Bot* b = &c->bots[L - 1];
		memset(b, 0, sizeof(*b));
		const NpFighterInfo* f = &np->fighters[mapId(np, L)];
		b->active = true;
		b->id = L;
		b->team = f->team < 0 ? 0 : f->team;
		snprintf(b->name, sizeof(b->name), "%s", f->name);
		b->weapon = W_PECKMATIC;
		b->hp = BOT_MAX_HP;
		b->lastAttacker = -1;
	}
	match_start(m, &cfg);
	np->state = NP_CLIENT_MATCH;
	np->combat = c;
	np->seenSnap = false;
	np->worldSeen = false;
	np->lastSnapSeq = 0;
	np->lastSnapFrame = 0;
	np->hostSilence = 0.0f;
	np->lastSpawnSeq = -1;
	np->spawnPending = false;
	np->evAck = 0;
	np->nextEvSeq = 0;
	np->pendingN = 0;
	np->inSeq = 0;
	np->prevHp = 100.0f;
	np->prevKills = 0;
	np->prevStreak = 0;
	np->readyTimer = 0.0f;
	g_np = np;
	setHooks(false);
}

bool np_client_take_spawn(Np* np, Spawn* out)
{
	if (!np->spawnPending) return false;
	np->spawnPending = false;
	*out = np->spawn;
	return true;
}

static void clientApplySnap(Np* np, Combat* c, Player* p, Match* m, R* r)
{
	unsigned seq = (unsigned)r16(r);
	unsigned frame = r32(r);
	int flags = r8(r);

	// ---- my own egg
	int hp = r8(r), sflags = r8(r);
	float invuln = (float)r8(r) / 10.0f;
	int streak = r8(r);
	float shield = (float)r8(r);
	float hot = (float)r8(r) / 10.0f, crack = (float)r8(r) / 10.0f;
	int spawnSeq = r8(r);
	float sx = dq64(ri16(r)), sy = dq64(ri16(r)), sz = dq64(ri16(r));
	float syaw = u16Ang(r16(r));
	float deadT = (float)ri8(r) / 10.0f;
	int lastAtt = ri8(r);
	int evAck = r8(r);
	if (r->bad) return;

	if (np->seenSnap) {
		unsigned d = (seq - np->lastSnapSeq) & 0xFFFF;
		if (d == 0 || d >= 0x8000) return;              // old or repeated
	}

	// ---- match and fighters are parsed into temporaries first so a damaged packet changes nothing
	int phase = r8(r);
	float countdown = (float)r8(r) / 10.0f;
	float elapsed = rf(r), timeLeft = rf(r);
	int red = ri16(r), blue = ri16(r);
	int timeUp = r8(r);
	int winner = (signed char)r8(r);

	typedef struct { int flags, weapon; float pos[3], yaw, pitch; int hp, kills, deaths; } Rec;
	Rec rec[MAX_FIGHTERS];
	int nf = r8(r);
	if (nf > MAX_FIGHTERS || r->bad) return;
	for (int i = 0; i < nf; i++) {
		Rec* f = &rec[i];
		f->flags = r8(r); f->weapon = r8(r);
		for (int a = 0; a < 3; a++) f->pos[a] = dq64(ri16(r));
		f->yaw = u16Ang(r16(r));
		f->pitch = (float)ri8(r) / 80.0f;
		f->hp = r8(r); f->kills = r8(r); f->deaths = r8(r);
	}
	Projectile pj[MAX_PROJECTILES];
	Grenade gr[MAX_GRENADES];
	memset(pj, 0, sizeof(pj));
	memset(gr, 0, sizeof(gr));
	int npj = r8(r);
	if (npj > MAX_PROJECTILES) return;
	for (int i = 0; i < npj; i++) {
		pj[i].active = true; pj[i].weapon = clampi(r8(r), 0, WEAPON_COUNT - 1); pj[i].owner = mapId(np, r8(r)); pj[i].life = 4.0f;
		for (int a = 0; a < 3; a++) pj[i].pos[a] = dq64(ri16(r));
		for (int a = 0; a < 3; a++) pj[i].vel[a] = dq16(ri16(r));
	}
	int ngr = r8(r);
	if (ngr > MAX_GRENADES) return;
	for (int i = 0; i < ngr; i++) {
		gr[i].active = true; gr[i].owner = mapId(np, r8(r)); gr[i].fuse = (float)r8(r) / 50.0f;
		for (int a = 0; a < 3; a++) gr[i].pos[a] = dq64(ri16(r));
		for (int a = 0; a < 3; a++) gr[i].vel[a] = dq16(ri16(r));
	}
	Objective obj;
	AmmoBox boxes[AMMO_BOX_MAX];
	int nbox = 0;
	bool haveWorld = (flags & 1) != 0;
	if (haveWorld) {
		int olen = r16(r);
		if (olen != (int)sizeof(Objective)) return;
		rmem(r, &obj, olen);
		nbox = r8(r);
		if (nbox > AMMO_BOX_MAX) return;
		for (int i = 0; i < nbox; i++) {
			for (int a = 0; a < 3; a++) boxes[i].pos[a] = dq64(ri16(r));
			boxes[i].timer = (float)r8(r) / 10.0f;
		}
	}
	if (r->bad) return;

	// ---- the packet is whole: apply it
	np->seenSnap = true;
	np->lastSnapSeq = seq;
	np->lastSnapFrame = frame;
	np->hostSilence = 0.0f;
	np->evAck = evAck;
	{
		int keep = 0;
		for (int i = 0; i < np->pendingN; i++) {
			int d = (evAck - np->pending[i].seq) & 255;
			if (d >= 128) np->pending[keep++] = np->pending[i];      // newer than the acknowledged one
		}
		np->pendingN = keep;
	}

	m->phase = (MatchPhase)clampi(phase, PH_INTRO, PH_ENDED);
	m->countdown = countdown;
	m->elapsed = elapsed;
	m->timeLeft = timeLeft;
	m->timeUp = timeUp != 0;
	m->winner = winner;
	c->teamScore[0] = red;
	c->teamScore[1] = blue;
	m->redScore = red;
	m->blueScore = blue;

	// my egg
	if (spawnSeq != np->lastSpawnSeq) {
		np->lastSpawnSeq = spawnSeq;
		if (sflags & 1) {
			np->spawnPending = true;
			memset(&np->spawn, 0, sizeof(np->spawn));
			np->spawn.x = sx; np->spawn.y = sy; np->spawn.z = sz; np->spawn.yaw = syaw;
			np->spawn.team = c->playerTeam;
			np->prevHp = 100.0f;
		}
	}
	if (!np->spawnPending) {
		if (!(sflags & 1) && p->alive) {                    // cracked
			p->alive = false;
			p->hp = 0.0f;
			p->deadTimer = deadT > 0.0f ? deadT : RESPAWN_DELAY;
			p->aiming = false;
			p->zoom = 1.0f;
			audio_play(SFX_DEATH, 0.0f, 1.0f);
		}
		if (p->alive) {
			if ((float)hp < np->prevHp - 0.5f) { p->damageFlashT = 0.25f; p->lastHitTimer = 0.0f; }
			p->hp = (float)hp;
			np->prevHp = (float)hp;
		}
	}
	p->invuln = invuln;
	p->perks.streak = streak;
	p->perks.shield = shield;
	p->perks.hotT = hot;
	p->perks.crackT = crack;
	if (lastAtt >= 0) p->lastAttacker = mapId(np, lastAtt);
	else p->lastAttacker = -1;
	if (streak > np->prevStreak && p->alive) {
		const char* t = combat_streak_banner(streak);
		if (t) combat_banner(c, t);
	}
	np->prevStreak = streak;

	// everybody else
	for (int j = 0; j < nf; j++) {
		int L = mapId(np, j);
		const Rec* f = &rec[j];
		if (L == 0) {
			if (f->kills > np->prevKills && p->alive) audio_play(SFX_KILL, 0.0f, 1.0f);
			np->prevKills = f->kills;
			p->kills = f->kills;
			p->deaths = f->deaths;
			continue;
		}
		if (L < 1 || L > c->botCount) continue;
		Bot* b = &c->bots[L - 1];
		bool wasAlive = b->alive;
		bool alive = (f->flags & 1) != 0;
		if (wasAlive && !alive) {                           // it cracked: a puff and a sound where it stood
			float fp[3] = { b->x, b->y + 0.8f, b->z };
			fx_burst(fp, 10, 5.0f, 0.25f, 0.7f, 1.0f, 0.9f, 0.3f);
			audio_play_at(SFX_EGG_CRACK, p->x, p->z, p->yaw, b->x, b->z, 1.0f);
		}
		if (alive && (float)f->hp < b->hp - 0.5f) b->hitFlash = 0.12f;
		b->alive = alive;
		b->hp = (float)f->hp;
		b->onGround = (f->flags & 2) != 0;
		b->invuln = (f->flags & 4) ? 0.5f : 0.0f;
		if (f->weapon >= 0 && f->weapon < WEAPON_COUNT) b->weapon = f->weapon;
		b->kills = f->kills;
		b->deaths = f->deaths;
		for (int a = 0; a < 3; a++) b->netPos[a] = f->pos[a];
		b->netYaw = f->yaw;
		b->netPitch = f->pitch;
		if (!alive || !wasAlive) {                          // appeared or died: no gliding
			b->x = f->pos[0]; b->y = f->pos[1]; b->z = f->pos[2];
			b->yaw = f->yaw; b->pitch = f->pitch;
		}
	}

	memcpy(c->proj, pj, sizeof(pj));
	memcpy(c->gren, gr, sizeof(gr));

	if (haveWorld) {
		c->obj = obj;
		c->obj.newRound = false;
		for (int t = 0; t < 2; t++) if (c->obj.flag[t].carrier >= 0) c->obj.flag[t].carrier = mapId(np, c->obj.flag[t].carrier);
		c->ammoBoxCount = nbox;
		for (int i = 0; i < nbox; i++) c->ammoBox[i] = boxes[i];
		np->worldSeen = true;
	}
}

static void clientApplyEvents(Np* np, Combat* c, Player* p, R* r)
{
	int n = r8(r);
	for (int i = 0; i < n && !r->bad; i++) {
		int kind = r8(r);
		if (kind == HE_SHOT) {
			int shooter = r8(r), weapon = r8(r);
			float eye[3];
			for (int a = 0; a < 3; a++) eye[a] = dq64(ri16(r));
			float yaw = u16Ang(r16(r));
			float pitch = (float)ri16(r) / 10000.0f;
			float spread = (float)r8(r) / 1000.0f;
			if (r->bad || weapon < 0 || weapon >= WEAPON_COUNT) return;
			int L = mapId(np, shooter);
			if (L < 1 || L > c->botCount) continue;
			float dir[3];
			dirFromAngles(yaw, pitch, dir);
			c->bots[L - 1].flashT = 0.06f;
			audio_play_at(SFX_FIRE_BASE + weapon, p->x, p->z, p->yaw, eye[0], eye[2], 0.8f);
			combat_fire(c, p, c->map, L, eye, dir, spread, weapon);
		} else if (kind == HE_BOOM) {
			float pos[3];
			for (int a = 0; a < 3; a++) pos[a] = dq64(ri16(r));
			float radius = (float)r8(r) / 10.0f;
			if (r->bad) return;
			combat_boom_fx(c, p, pos, radius);
		} else if (kind == HE_HIT) {
			p->hitMarkerT = 0.15f;
			audio_play(SFX_HITMARK, 0.0f, 1.0f);
		} else if (kind == HE_MELEE) {
			int who = r8(r), hit = r8(r);
			if (r->bad) return;
			int L = mapId(np, who);
			if (L < 1 || L > c->botCount) continue;
			Bot* b = &c->bots[L - 1];
			b->meleeT = MELEE_TIME - MELEE_HIT_AT;
			audio_play_at(hit ? SFX_MELEE_HIT : SFX_MELEE_SWING, p->x, p->z, p->yaw, b->x, b->z, 0.9f);
		} else {
			return;
		}
	}
}

static float wrapAngle(float a)
{
	while (a > PI_F) a -= 2.0f * PI_F;
	while (a < -PI_F) a += 2.0f * PI_F;
	return a;
}

void np_client_frame_pre(Np* np, Combat* c, Player* p, Match* m, float dt)
{
	g_np = np;
	np->combat = c;
	np->player = p;

	unsigned char buf[NET_MAX_PACKET];
	int from, len;
	while ((len = nx_recv(&from, buf, sizeof(buf))) > 0) {
		if (from != NET_HOST_NODE) continue;
		R r = { buf, len, 0, false };
		if (r8(&r) != NP_MAGIC) continue;
		int type = r8(&r);
		np->hostSilence = 0.0f;
		if (type == P_SNAP) clientApplySnap(np, c, p, m, &r);
		else if (type == P_EVT) clientApplyEvents(np, c, p, &r);
		else if (type == P_LEAVE) { np->hostLeft = true; np_leave(np, "The host left the match"); return; }
	}
	np->hostSilence += dt;
	unsigned mask = nx_node_mask();
	if (np->hostSilence > 6.0f || (nx_active() && mask && !(mask & 1u))) {
		np->hostLeft = true;
		np_leave(np, "Lost the connection to the host");
		return;
	}

	// glide the other eggs toward where the host says they are, and work out who the radar may show
	const float k = 1.0f - expf(-14.0f * dt);
	for (int i = 0; i < c->botCount; i++) {
		Bot* b = &c->bots[i];
		if (!b->alive) continue;
		float dx = b->netPos[0] - b->x, dy = b->netPos[1] - b->y, dz = b->netPos[2] - b->z;
		if (dx * dx + dy * dy + dz * dz > 36.0f) { b->x = b->netPos[0]; b->y = b->netPos[1]; b->z = b->netPos[2]; }
		else { b->x += dx * k; b->y += dy * k; b->z += dz * k; }
		b->yaw += wrapAngle(b->netYaw - b->yaw) * k;
		b->pitch += (b->netPitch - b->pitch) * k;
		b->frame++;
		if (p->alive && ((unsigned)b->id + b->frame) % 3u == 0u) {
			float eye[3] = { p->x, p->y + EYE_HEIGHT, p->z };
			float chest[3] = { b->x, b->y + 0.9f, b->z };
			float ex = chest[0] - eye[0], ez = chest[2] - eye[2];
			b->visibleToPlayer = (ex * ex + ez * ez < 70.0f * 70.0f) && bot_line_clear(c->map, eye, chest);
		} else if (!p->alive) {
			b->visibleToPlayer = false;
		}
	}
}

void np_client_frame_post(Np* np, Combat* c, Player* p, const PlayerInput* in, float dt)
{
	g_np = np;
	(void)in;
	if (np->state != NP_CLIENT_MATCH) return;

	int crate = ammo_client_pickup(c, p);
	if (crate >= 0) { unsigned char d[1] = { (unsigned char)crate }; queueEvent(np, EV_PICKUP, d, 1); }

	if (!np->seenSnap) {                                    // tell the host the map is loaded
		np->readyTimer -= dt;
		if (np->readyTimer <= 0.0f) {
			np->readyTimer = 0.25f;
			unsigned char b[4];
			W w = { b, 0, sizeof(b) };
			putHeader(&w, P_READY);
			nx_send(NET_HOST_NODE, b, w.n);
		}
	}

	unsigned char buf[NET_MAX_PACKET];
	W w = { buf, 0, sizeof(buf) };
	putHeader(&w, P_INPUT);
	np->inSeq = (np->inSeq + 1) & 255;
	w8(&w, (int)np->inSeq);
	w8(&w, np->lastSpawnSeq & 255);
	w16(&w, (int)(np->lastSnapFrame & 0xFFFF));
	w16(&w, q64(p->x)); w16(&w, q64(p->y)); w16(&w, q64(p->z));
	w16(&w, angU16(p->yaw));
	w8(&w, clampi((int)(p->pitch * 80.0f), -127, 127));
	w8(&w, (p->onGround ? 1 : 0) | (p->alive ? 2 : 0));
	w8(&w, p->cur);
	int nev = np->pendingN < 8 ? np->pendingN : 8;
	w8(&w, nev);
	for (int i = 0; i < nev; i++) {
		const NpPendingEvent* e = &np->pending[i];
		w8(&w, e->type); w8(&w, e->seq);
		wmem(&w, e->data, e->len);
	}
	nx_send(NET_HOST_NODE, buf, w.n);
}
