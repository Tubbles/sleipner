#pragma once

/* network.h -- NetworkState (S8.3a) plus the lifecycle helpers that
 * actually create/destroy a real transport (S8.3b): the runtime
 * multiplayer session state that rides on GameState, offline by default
 * -- nothing networked happens unless a mode is explicitly set.
 * Zero-initialized to NET_OFFLINE, and reset back to that same zero
 * value by game_free's blanket `*state = (GameState){0}` -- like
 * MusicState/CameraEffect (game.h), NetworkState is transient session
 * state, not part of GamedataState, so it is never undo-snapshotted.
 *
 * Pairs with net.h's NetTransport (S8.1, the send/recv/poll ops struct)
 * and net_discovery.h (S8.3a, the beacon-send / join-list-collect logic
 * driven over that transport, plus DISCOVERY_PORT). The pause-menu
 * Host/Join UI (menu.h/frame.c) calls network_start_hosting /
 * network_start_discovering below to flip `mode` and populate
 * `transport` with a real net_udp.h socket; the networked game session
 * itself is S8.4.
 *
 * Deliberately decoupled from GameState/Diag (network.c has no
 * dependency on game.h) -- same boundary save_screen.h documents for
 * itself, and for the same reason: game.h already includes this header
 * (GameState.network), so the reverse include would cycle. Callers that
 * need to log a network_start_* failure (frame.c's dispatch_menu_action)
 * do so themselves via the returned ErrorState, the same pattern
 * frame.c's open_save_screen already uses for platform_saves_dir. */

#include "alloc.h"
#include "error.h"
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
     * what network_stop checks before calling net_udp_destroy, so a
     * zero-initialized (never-started) transport is never double-freed. */
    bool transport_initialized;
    JoinList join_list;
    /* The host a client chose to join from the join list (discovery_screen.c's
     * CONFIRM handling, frame.c), consumed by S8.4's connect/session logic.
     * Meaningless while mode != NET_JOINING. */
    NetAddr join_target;
    /* This peer's advertised name: sent as BeaconMessage.host_name while
     * NET_HOSTING, shown to clients in their join list. */
    char host_name[NET_NAME_MAX];
    /* Accumulator driving discovery_host_tick's ~1/s beacon cadence
     * (net_discovery.h's DISCOVERY_BEACON_INTERVAL_SECONDS). Meaningless
     * while mode != NET_HOSTING. */
    float beacon_timer;
} NetworkState;

/* Placeholder advertised name until a real player-name setting exists
 * (no such field in preferences.h yet) -- passed by frame.c's
 * dispatch_menu_action as network_start_hosting's host_name. */
#define NETWORK_DEFAULT_HOST_NAME "Sleipner Host"

/* Create a real UDP socket (net_udp.h) bound to DISCOVERY_PORT with
 * broadcast enabled, and set `network` to NET_HOSTING beaconing under
 * `host_name` (truncated to NET_NAME_MAX-1 bytes). `alloc` backs the
 * transport's internal state -- production passes an arena Allocator
 * over GameState.progression_arena specifically because that arena
 * survives level transitions/hot-reloads (game_load_gamedata's
 * arena_restore only ever touches gamedata_arena), so hosting keeps
 * beaconing across a `transition:` the same way it does across ordinary
 * gameplay frames. See game_reset_progression's network_stop call
 * (game.c) for the one existing path that DOES wipe progression_arena
 * (the pause-menu RESTORE action) and why it must close the socket
 * first.
 *
 * Best-effort: socket creation can fail (no permission, port already
 * bound with no working SO_REUSEPORT, sandboxed environment). On
 * failure this returns false with `err` populated and leaves `network`
 * completely untouched (still whatever it was before the call, normally
 * NET_OFFLINE) -- the caller logs and the pause menu simply stays
 * usable, matching every other best-effort I/O path in this codebase
 * (platform_saves_dir, save_write, autosave). Never crashes. */
[[nodiscard]] bool
network_start_hosting(NetworkState *network, Allocator *alloc, const char *host_name, ErrorState *err);

/* Create a real UDP socket bound to DISCOVERY_PORT (no broadcast needed
 * -- this socket only listens for beacons, never sends to one) and set
 * `network` to NET_DISCOVERING with a freshly cleared join_list. Same
 * best-effort/never-crashes contract as network_start_hosting: a
 * failure returns false with `err` populated and leaves `network`
 * untouched. */
[[nodiscard]] bool network_start_discovering(NetworkState *network, Allocator *alloc, ErrorState *err);

/* Destroy the transport if one was ever created (safe no-op otherwise),
 * and reset every NetworkState field back to its NET_OFFLINE zero
 * value -- mode, join_list, join_target, beacon_timer, host_name.
 * Idempotent: safe to call on an already-OFFLINE NetworkState. */
void network_stop(NetworkState *network);
