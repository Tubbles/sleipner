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
 * itself is S8.4, starting with S8.4a's JOIN/INPUT flow (NetClient,
 * network_client_send_join/_send_input, network_host_receive below).
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
#include "input.h" // InputState
#include "net.h"
#include "net_protocol.h" // EventRecord, AckMessage
#include "net_reliable.h" // ReliableChannel

#include <stdbool.h>
#include <stdint.h>

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
    /* S8.4a: a client that has sent MSG_JOIN and is being treated as
     * connected to a host. Lifecycle: NET_JOINING (join_target chosen via
     * the discovery screen or set directly, nothing sent yet) ->
     * NET_CLIENT (net_session.c's network_client_tick sent MSG_JOIN and
     * flipped the mode itself, in the same call). Acceptance in S8.4a is
     * IMPLICIT: the client assumes it's in as soon as it sends JOIN, and
     * the host separately accepts or refuses by hash-verifying the
     * message (network_host_receive below) without replying either way. A
     * real accept/reject handshake (the client only advancing to
     * NET_CLIENT once the host confirms) remains open work, tracked in
     * TODO.md -- S8.4c turned out to be the reliable event sub-channel
     * (net_reliable.h) instead, per the open-work master plan's own S8.4
     * definition. Distinct from NET_JOINING: NET_JOINING is "picked a
     * host, about to connect", NET_CLIENT is "connected, sending input
     * every tick". */
    NET_CLIENT,
} NetMode;

/* Cap on clients simultaneously connected to one hosted session. Bounded by
 * how many players choose to join a LAN party, not by frame count or event
 * count -- the same justified fixed-cap-array exception JoinList/
 * BindingStore already rely on. */
#define NET_MAX_CLIENTS 4

/* One client connected to a hosted session -- host-side only, populated by
 * network_host_receive below (a client's own NetworkState never fills out
 * its own `clients`). player_id is assigned 1..NET_MAX_CLIENTS in join
 * order; id 0 is reserved for the host's own local player
 * (input_source = "local:0"). See input_for_entity's "network:<id>"
 * resolution (game.c) for the read side: it looks a player_id up here and
 * returns last_input, or idle input if no client currently holds that id.
 * last_input is overwritten every time network_host_receive decodes a
 * MSG_INPUT from this client's addr; a client that stops sending simply
 * leaves it at whatever it last was -- no timeout/eviction in S8.4a.
 * active distinguishes a live slot from an unused tail entry in the
 * fixed-cap array below. */
typedef struct {
    NetAddr addr;
    int player_id;
    InputState last_input;
    bool active;
    /* S8.4c: HOST->this-client reliable event channel (net_reliable.h) --
     * assigns/resends MSG_EVENT packets this host sends to this specific
     * client and applies its ACKs. See net_session.h's own doc comment for
     * what actually flows over it (NETWORK_EVENT_PLAYER_JOINED). */
    ReliableChannel event_channel;
} NetClient;

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
     * NET_HOSTING, shown to clients in their join list. Doubles as the
     * client_name a client sends in its own MSG_JOIN while NET_JOINING/
     * NET_CLIENT -- one "this peer's display name" field rather than a
     * second one, since only one of the two roles is ever meaningful at a
     * time for a single NetworkState. */
    char host_name[NET_NAME_MAX];
    /* Accumulator driving discovery_host_tick's ~1/s beacon cadence
     * (net_discovery.h's DISCOVERY_BEACON_INTERVAL_SECONDS). Meaningless
     * while mode != NET_HOSTING. */
    float beacon_timer;
    /* HOSTING only: every client that has successfully JOINed this
     * session (gamedata hash matched network_host_receive's local hash).
     * Meaningless while mode != NET_HOSTING. */
    NetClient clients[NET_MAX_CLIENTS];
    int client_count;
    /* S8.4c: CLIENT side only -- this client's own receive-side reliable
     * channel for events the HOST sends it. network_client_send_ack below
     * acks whatever it has received so far; network_client_tick
     * (net_session.c) applies each newly-delivered MSG_EVENT via
     * reliable_on_receive (net_reliable.h). Meaningless on the HOST's own
     * NetworkState -- a host's per-client event_channel (NetClient, above)
     * is what matters there. v1 only wires the host->client direction, see
     * net_reliable.h's own top doc comment for why. */
    ReliableChannel host_event_channel;
    /* S8.4c: CLIENT side only -- bookkeeping for the last reliable event
     * this client actually delivered (network_client_tick's
     * network_client_apply_event, net_session.c), plus a running count of
     * how many have been delivered in total. Not gameplay state -- see
     * net_session.h's NETWORK_EVENT_PLAYER_JOINED doc comment for why
     * "applying" an event is currently just this bookkeeping rather than a
     * game-state mutation. */
    int32_t last_delivered_event_type;
    int32_t last_delivered_event_entity_id;
    int delivered_event_count;
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
 * value -- mode, join_list, join_target, beacon_timer, host_name,
 * clients/client_count, host_event_channel, and the delivered-event
 * bookkeeping (S8.4c). Idempotent: safe to call on an already-OFFLINE
 * NetworkState. */
void network_stop(NetworkState *network);

/* ---- S8.4a: session JOIN + INPUT flow, over an already-established
 * `network->transport`/`network->join_target` (net.h/net_loopback.h/
 * net_udp.h; net_protocol.h encodes/decodes the wire bytes). Driven every
 * tick by net_session.h's network_host_tick/network_client_tick, which add
 * the GameState-level mode gating and gamedata-hash plumbing these
 * transport-level primitives deliberately don't know about (network.h has
 * no dependency on game.h -- see this file's own top doc comment). ---- */

/* CLIENT side: encode and send one MSG_JOIN carrying local_gamedata_hash
 * and network->host_name (as JoinMessage.client_name) to network->join_target.
 * Fire-and-forget, like discovery_host_tick's beacon send -- a failed
 * encode/send is silently dropped, there is no error channel here. Does
 * NOT change network->mode itself; net_session.c's network_client_tick
 * does that immediately after calling this. */
void network_client_send_join(NetworkState *network, uint64_t local_gamedata_hash);

/* CLIENT side: encode and send this frame's local_input as MSG_INPUT to
 * network->join_target. Same fire-and-forget contract as
 * network_client_send_join. Call every tick while NET_CLIENT. */
void network_client_send_input(NetworkState *network, const InputState *local_input);

/* HOST side: drain every packet currently waiting on network->transport.
 * A decoded MSG_JOIN is hash-verified against local_gamedata_hash
 * (net_protocol.h's protocol_join_verify) -- a match registers the
 * sender's NetAddr in `clients` with the next player_id (a no-op, not a
 * duplicate, if that addr is already registered: re-JOIN refreshes rather
 * than adding a second entry), a mismatch is silently refused (no client
 * registered). A decoded MSG_INPUT from an address already present in
 * `clients` overwrites that client's last_input; MSG_INPUT from an
 * unregistered address is ignored. A decoded MSG_ACK (S8.4c) from an
 * address already present in `clients` is applied to that client's own
 * event_channel (net_reliable.h's reliable_on_ack), clearing whichever
 * send-window slots it covers; MSG_ACK from an unregistered address is
 * ignored, same as MSG_INPUT. Anything that fails to decode (wrong
 * magic/version, truncated, an unrelated message type) is silently
 * ignored, same as discovery_client_tick's own drain loop. Call once per
 * tick, before simulating, so the freshest input reaches this tick's
 * game_update. */
void network_host_receive(NetworkState *network, uint64_t local_gamedata_hash);

/* Look up a connected client by player_id (1..NET_MAX_CLIENTS). Returns
 * nullptr if no active client currently holds that id -- the "unknown/
 * absent id" case game.c's input_for_entity resolves to idle input. */
[[nodiscard]] const NetClient *network_find_client_by_player_id(const NetworkState *network, int player_id);

/* ---- S8.4c: reliable event sub-channel primitives, layered on
 * net_reliable.h's ReliableChannel over the same `network->transport`
 * S8.4a's JOIN/INPUT flow already uses. See net_session.h's own doc
 * comment for the one concrete event this wires end-to-end
 * (NETWORK_EVENT_PLAYER_JOINED) and net_reliable.h's top doc comment for
 * why only the host->client direction exists in v1. ---- */

/* HOST side. Reliable-send `event` (net_reliable.h's reliable_send) to
 * every currently active client's own event_channel -- each client gets
 * its own sequence number and resend timer, so one client's dropped
 * packet never delays or duplicates delivery to another. */
void network_broadcast_reliable_event(NetworkState *network, EventRecord event);

/* HOST side. Age every active client's send-side event_channel by
 * delta_time and resend anything still unacked past
 * RELIABLE_RESEND_SECONDS (net_reliable.h's reliable_tick). Call once per
 * hosting tick, after network_host_receive so this tick's incoming ACKs
 * (handled inline by network_host_receive above) are already applied
 * before ageing -- an event acked this same tick should not also be
 * resent this same tick. */
void network_host_tick_reliable_channels(NetworkState *network, float delta_time);

/* CLIENT side. If host_event_channel has ever received anything
 * (has_received_any, net_reliable.h), encode the current ack
 * (reliable_make_ack) and send it as MSG_ACK to join_target. A no-op
 * before any reliable event has ever arrived -- nothing to ack yet. Same
 * fire-and-forget contract as network_client_send_join/_send_input above. */
void network_client_send_ack(NetworkState *network);
