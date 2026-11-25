// Real wireless transport: Nintendo 3DS local wireless (UDS) through libctru.
//
// NOT TESTED ON HARDWARE. It was written from the libctru documentation (uds.h) and checked to compile
// against stand-in headers only. If a room cannot be created, found or joined, this file is the place to look.
#include "net.h"

#include <3ds.h>
#include <stdio.h>
#include <string.h>

#define WLANCOMM_ID   0x45475333u            // "EGS3": identifies EggStrike rooms in scans
#define ID8           0x2A
#define DATA_CHANNEL  1
#define SHARED_MEM    0x3000
#define SCAN_BUF      0x4000
#define MAX_ROOMS     8
static const char PASSPHRASE[] = "EggStrike3DS local play";

static bool  inited;
static bool  active;
static bool  isHost;
static int   myNode;
static udsBindContext  bindCtx;
static udsNetworkStruct hostNet;
static udsNetworkStruct scanNets[MAX_ROOMS];
static int   scanCount;
static u8    scanBuf[SCAN_BUF] __attribute__((aligned(0x1000)));
static u8    rxBuf[UDS_DATAFRAME_MAXSIZE] __attribute__((aligned(4)));

static unsigned maskCache;
static u64      maskStamp;

bool net_init(void)
{
	if (inited) return true;
	Result r = udsInit(SHARED_MEM, NULL);          // NULL: use the console's own user name
	if (R_FAILED(r)) return false;
	inited = true;
	return true;
}

void net_shutdown(void)
{
	if (active) net_close();
	if (inited) { udsExit(); inited = false; }
}

bool net_host_open(const unsigned char* app, int appLen)
{
	if (!inited || active) return false;
	udsGenerateDefaultNetworkStruct(&hostNet, WLANCOMM_ID, ID8, NET_MAX_NODES);
	Result r = udsCreateNetwork(&hostNet, PASSPHRASE, sizeof(PASSPHRASE), &bindCtx, DATA_CHANNEL, UDS_DEFAULT_RECVBUFSIZE);
	if (R_FAILED(r)) return false;
	active = true;
	isHost = true;
	myNode = UDS_HOST_NETWORKNODEID;
	maskCache = 0;
	maskStamp = 0;
	net_host_set_appdata(app, appLen);
	return true;
}

void net_host_set_appdata(const unsigned char* app, int appLen)
{
	if (!active || !isHost) return;
	if (appLen > NET_APPDATA_MAX) appLen = NET_APPDATA_MAX;
	udsSetApplicationData(app, (size_t)appLen);
}

void net_host_lock(bool locked)
{
	if (!active || !isHost) return;
	udsSetNewConnectionsBlocked(locked, true, true);
}

int net_scan(NetRoom* rooms, int max)
{
	if (!inited || active) return 0;
	udsNetworkScanInfo* nets = NULL;
	size_t total = 0;
	Result r = udsScanBeacons(scanBuf, sizeof(scanBuf), &nets, &total, WLANCOMM_ID, ID8, NULL, false);
	if (R_FAILED(r) || !nets) return 0;

	int n = 0;
	scanCount = 0;
	for (size_t i = 0; i < total && n < max && scanCount < MAX_ROOMS; i++) {
		NetRoom* room = &rooms[n];
		memset(room, 0, sizeof(*room));
		size_t actual = 0;
		udsGetNetworkStructApplicationData(&nets[i].network, room->app, sizeof(room->app), &actual);
		room->appLen = (int)actual;
		if (room->appLen < 4) continue;                    // not one of ours
		char nm[32] = { 0 };
		if (udsCheckNodeInfoInitialized(&nets[i].nodes[0])) udsGetNodeInfoUsername(&nets[i].nodes[0], nm);
		snprintf(room->name, sizeof(room->name), "%s", nm[0] ? nm : "Host");
		room->nodes = nets[i].network.total_nodes;
		room->maxNodes = nets[i].network.max_nodes;
		scanNets[scanCount] = nets[i].network;
		room->handle = scanCount++;
		n++;
	}
	return n;
}

bool net_join(const NetRoom* room)
{
	if (!inited || active || !room || room->handle < 0 || room->handle >= scanCount) return false;
	Result r = 0;
	for (int attempt = 0; attempt < 3; attempt++) {
		r = udsConnectNetwork(&scanNets[room->handle], PASSPHRASE, sizeof(PASSPHRASE), &bindCtx,
		                      UDS_BROADCAST_NETWORKNODEID, UDSCONTYPE_Client, DATA_CHANNEL, UDS_DEFAULT_RECVBUFSIZE);
		if (R_SUCCEEDED(r)) break;
		svcSleepThread(250000000LL);
	}
	if (R_FAILED(r)) return false;

	udsConnectionStatus st;
	if (R_FAILED(udsGetConnectionStatus(&st))) { udsDisconnectNetwork(); udsUnbind(&bindCtx); return false; }
	active = true;
	isHost = false;
	myNode = st.cur_NetworkNodeID;
	maskCache = 0;
	maskStamp = 0;
	return true;
}

void net_close(void)
{
	if (!active) return;
	if (isHost) udsDestroyNetwork();
	else        udsDisconnectNetwork();
	udsUnbind(&bindCtx);
	active = false;
	isHost = false;
	myNode = 0;
}

bool net_active(void) { return active; }
int  net_my_node(void) { return myNode; }

unsigned net_node_mask(void)
{
	if (!active) return 0;
	u64 now = osGetTime();
	if (now - maskStamp < 100) return maskCache;          // one status call per 0.1 s is plenty
	maskStamp = now;
	udsConnectionStatus st;
	if (R_FAILED(udsGetConnectionStatus(&st))) { maskCache = 0; return 0; }
	maskCache = st.node_bitmask;
	return maskCache;
}

bool net_node_name(int node, char out[16])
{
	out[0] = '\0';
	if (!active || node < 1 || node > NET_MAX_NODES) return false;
	udsNodeInfo info;
	if (R_FAILED(udsGetNodeInformation((u16)node, &info))) return false;
	char nm[32] = { 0 };
	if (R_FAILED(udsGetNodeInfoUsername(&info, nm))) return false;
	snprintf(out, 16, "%s", nm);
	return out[0] != '\0';
}

bool net_send(int node, const void* data, int len)
{
	if (!active || len <= 0 || len > NET_MAX_PACKET) return false;
	Result r = udsSendTo((u16)node, DATA_CHANNEL, UDS_SENDFLAG_Default, data, (size_t)len);
	return R_SUCCEEDED(r);
}

int net_recv(int* fromNode, void* buf, int max)
{
	if (!active) return 0;
	size_t actual = 0;
	u16 src = 0;
	Result r = udsPullPacket(&bindCtx, rxBuf, sizeof(rxBuf), &actual, &src);
	if (R_FAILED(r) || actual == 0) return 0;
	if ((int)actual > max) actual = (size_t)max;
	memcpy(buf, rxBuf, actual);
	if (fromNode) *fromNode = src;
	return (int)actual;
}
