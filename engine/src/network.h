#pragma once

/* network.h -- NetworkState (S8.3a): the runtime multiplayer session state
 * that rides on GameState, offline by default -- nothing networked
 * happens unless a mode is explicitly set. Zero-initialized to
 * NET_OFFLINE, and reset back to that same zero value by game_free's
 * blanket `*state = (GameState){0}` -- like MusicState/CameraEffect
 * (game.h), NetworkState is transient session state, not part of
 * GamedataState, so it is never undo-snapshotted.
 *
 * Pairs with net.h's NetTransport (S8.1, the send/recv/poll ops struct)
 * and net_discovery.h (S8.3a, the beacon-send / join-list-collect logic
 * driven over that transport). The pause-menu Host/Join UI and the real
 * UDP broadcast wiring that actually flip `mode` and populate `transport`
 * are S8.3b; the networked game session itself is S8.4. */

#include "net.h"

#include <stdbool.h>

/* Longest advertised peer name (a beacon's host_name, a JoinList entry's
 * name), including the null terminator. Sent on the wire as a length-
 * prefixed string (net_protocol.h's packet_writer_write_string) and
 * displayed as a single fixed-width menu row (S8.3b) -- a bounded display
 * string, not a growth-prone collection, so a fixed char buffer is the
 * right shape here (CLAUDE.md's vec-over-MAX_* rule targets dynamic
 * collections, not this). */
#define NET_NAME_MAX 32

/* Cap on simultaneously discovered LAN hosts. Bounded by how many hosts
 * physically exist on the LAN and choose to beacon, not by frame count or
 * event count -- a justified fixed-cap array, the same exception
 * BindingStore (input_func.h) already relies on. */
#define DISCOVERED_HOSTS_MAX 16

/* One LAN host a client has seen beacon. addr.port is the host's
 * advertised game-session LISTEN port (BeaconMessage.listen_port,
 * net_protocol.h) -- NOT the ephemeral discovery-socket source port a
 * beacon UDP datagram happened to arrive from. See net_discovery.c's
 * discovery_client_tick for where addr is assembled. */
typedef struct {
    NetAddr addr;
    char name[NET_NAME_MAX];
    /* Seconds elapsed since this host's beacon was last seen -- an AGE
     * counter, not an absolute timestamp. join_list_age below adds
     * delta_time to every entry each call; join_list_add_or_refresh
     * resets an entry's age to 0 whenever a fresh beacon arrives. Ageing
     * this way means no caller needs a shared wall-clock reference. */
    float last_seen_seconds;
} DiscoveredHost;

typedef struct {
    DiscoveredHost hosts[DISCOVERED_HOSTS_MAX];
    int count;
} JoinList;

/* Find an already-discovered host by address. Returns nullptr if absent. */
DiscoveredHost *join_list_find(JoinList *list, NetAddr addr);

/* Add addr/name as a newly discovered host, or refresh an existing entry
 * at the same address (updates name, resets last_seen_seconds to 0). name
 * is copied and truncated to NET_NAME_MAX-1 bytes (strv_copy_to_cstr's
 * contract) -- the caller's buffer need not outlive this call. When the
 * list is already at DISCOVERED_HOSTS_MAX and addr is not already
 * present, the new host is silently dropped: documented bounded
 * behavior, not a crash -- sixteen simultaneous LAN hosts is far beyond
 * any expected pre-alpha LAN party size. */
void join_list_add_or_refresh(JoinList *list, NetAddr addr, const char *name);

/* Age every entry by delta_time (adds to last_seen_seconds). A separate
 * function from join_list_evict_timed_out below so neither one takes two
 * adjacent float parameters -- clang-tidy's bugprone-easily-swappable-
 * parameters flags that shape, the same reason hud.h's HudPlayerHealth
 * wraps current/max instead of taking them as separate parameters. */
void join_list_age(JoinList *list, float delta_time);

/* Remove every entry whose last_seen_seconds exceeds timeout_seconds.
 * Does not itself advance time -- call join_list_age first (see
 * net_discovery.c's discovery_client_tick for the intended call order:
 * drain + add/refresh incoming beacons, then age, then evict). */
void join_list_evict_timed_out(JoinList *list, float timeout_seconds);

typedef enum {
    NET_OFFLINE, /* zero-init default: single-player, nothing networked */
    NET_HOSTING,
    NET_DISCOVERING,
    NET_JOINING,
} NetMode;

typedef struct {
    NetMode mode;
    NetTransport transport;
    /* net.h's null-op-safe wrappers (net_send/net_recv/net_poll) already
     * tolerate an all-zero NetTransport, but a real transport (net_udp.h)
     * owns a socket that must be destroyed exactly once. This flag is
     * what future teardown code checks before calling net_udp_destroy, so
     * a zero-initialized (never-created) transport is never double-freed.
     * Nothing sets it yet -- S8.3a never creates a real transport, only
     * the loopback one its tests construct locally. */
    bool transport_initialized;
    JoinList join_list;
    /* The host a client chose to join from the join list (S8.3b UI
     * selection), consumed by S8.4's connect/session logic. Meaningless
     * while mode != NET_JOINING. */
    NetAddr join_target;
    /* This peer's advertised name: sent as BeaconMessage.host_name while
     * NET_HOSTING, shown to clients in their join list. */
    char host_name[NET_NAME_MAX];
    /* Accumulator driving discovery_host_tick's ~1/s beacon cadence
     * (net_discovery.h's DISCOVERY_BEACON_INTERVAL_SECONDS). Meaningless
     * while mode != NET_HOSTING. */
    float beacon_timer;
} NetworkState;
