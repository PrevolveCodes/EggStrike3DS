#ifndef NET_H
#define NET_H

#include <stdbool.h>
#include <stddef.h>

// Transport layer for local multiplayer. Two implementations:
//   net_uds.c  - real 3DS-to-3DS local wireless (libctru UDS). Needs two consoles to try.
//   net_sim.c  - in-process fake network used by the PC tests (adds delay and packet loss).
//
// The network has one host (node 1) and up to NET_MAX_NODES-1 clients (nodes 2, 3, 4).
// Packets are small datagrams. Nothing here is reliable: netplay.c repeats what matters.

#define NET_MAX_NODES   4
#define NET_MAX_PACKET  1400      // below UDS_DATAFRAME_MAXSIZE (1478)
#define NET_APPDATA_MAX 120
#define NET_HOST_NODE   1

typedef struct {
	char          name[16];                 // the host's console name
	int           nodes;                    // people in the room now
	int           maxNodes;
	unsigned char app[NET_APPDATA_MAX];     // what the host published with net_host_set_appdata
	int           appLen;
	int           handle;                   // opaque: index into the last scan
} NetRoom;

bool net_init(void);                        // false: wireless is unavailable
void net_shutdown(void);

// Hosting. The room is visible to scans until net_host_lock(true).
bool net_host_open(const unsigned char* app, int appLen);
void net_host_set_appdata(const unsigned char* app, int appLen);
void net_host_lock(bool locked);            // true: no new players may join

// Joining. net_scan blocks for about a second; draw a "Searching..." frame first.
int  net_scan(NetRoom* rooms, int max);
bool net_join(const NetRoom* room);

void net_close(void);                       // leave or close the room
bool net_active(void);                      // in a room (hosting or joined)

int      net_my_node(void);                 // 1 for the host, 2..4 for clients
unsigned net_node_mask(void);               // bit (n-1) set for every node present; 0 if the link is gone
bool     net_node_name(int node, char out[16]);

bool net_send(int node, const void* data, int len);      // true if handed to the radio
int  net_recv(int* fromNode, void* buf, int max);        // returns the length, 0 when nothing is waiting

#endif
