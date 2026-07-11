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

#include "raylib.h" // Vector2

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
    /* S8.4a/S8.6: a client that has received a MSG_JOIN_ACCEPT reply to its
     * own MSG_JOIN and is now connected to a host. Lifecycle: NET_JOINING
     * (join_target chosen via the discovery screen or set directly,
     * resending MSG_JOIN every tick via net_session.c's network_client_tick)
     * -> NET_CLIENT (a MSG_JOIN_ACCEPT arrived, network_client_tick decoded
     * it, stored local_player_id below, and flipped the mode itself, in the
     * same call). Acceptance is EXPLICIT as of S8.6: the host hash-verifies
     * every MSG_JOIN (network_host_receive below) and, on a match, replies
     * with MSG_JOIN_ACCEPT (network.c's handle_join_datagram) carrying the
     * player_id it just assigned -- resent on every re-JOIN, so a client
     * that never sees the first accept (dropped packet) still converges
     * once it resends. A hash-mismatched JOIN gets no reply at all: a
     * refused client simply never leaves NET_JOINING. Distinct from
     * NET_JOINING: NET_JOINING is "picked a host, about to connect",
     * NET_CLIENT is "connected, sending input every tick". */
    NET_CLIENT,
} NetMode;

/* Cap on clients simultaneously connected to one hosted session. Bounded by
 * how many players choose to join a LAN party, not by frame count or event
 * count -- the same justified fixed-cap-array exception JoinList/
 * BindingStore already rely on. */
#define NET_MAX_CLIENTS 4

/* Prefix an entity's `input_source` attr uses to name a network player:
 * "network:<player_id>", player_id matching a NetClient.player_id below.
 * Shared between game.c's input_for_entity/game_get_local_player (the read
 * side: resolve THIS attr value to an input provider or "is this MY
 * player") and net_session.c's S8.6 per-join spawn (the write side: stamp
 * a freshly spawned player entity's input_source with this prefix plus the
 * id register_client just assigned). Lives here, not game.c, so both
 * files -- neither of which includes the other -- can share one literal
 * rather than risking two copies drifting apart. */
#define NETWORK_INPUT_SOURCE_PREFIX "network:"

/* Reliable-event type ids exchanged over the event sub-channel (net_session.h's
 * "reliable event sub-channel" note has the end-to-end wiring). They live here,
 * not net_session.h, for the same cross-layer-sharing reason NETWORK_INPUT_SOURCE_PREFIX
 * does: network.c must react to some of them (NETWORK_EVENT_RESYNC_COMPLETE and
 * NETWORK_EVENT_SAVE_REQUEST, in handle_event_datagram) while net_session.c
 * originates the others, and network.h is the header both include.
 *  - NETWORK_EVENT_PLAYER_JOINED: HOST->client, a new player connected (S8.4c);
 *    carries the joined player_id as EventRecord.entity_id.
 *  - NETWORK_EVENT_RESYNC_COMPLETE (S8.7f1): client->host, "I finished applying
 *    resync generation N"; the generation rides EventRecord.entity_id (reused as
 *    a plain u32 carrier, not an actual entity id), and handle_event_datagram
 *    records it onto that client's resync_confirmed_generation so the host stops
 *    re-sending that generation's chunks to it.
 *  - NETWORK_EVENT_SAVE_REQUEST (S8.7g): client->host, "please save the canonical
 *    gamedata". entity_id/argument unused (zero). The host is the single writer of
 *    gamedata.toml (which also keeps the Syncthing copy race-free), so a client
 *    never writes its own file -- it asks the host, which runs its normal save
 *    path. handle_event_datagram flags save_request_pending for the frame layer.
 *  - NETWORK_EVENT_SAVED (S8.7g): host->clients, "the canonical gamedata was
 *    saved". entity_id/argument unused (zero). Each client clears its own dirty
 *    indicator and shows the "Saved" toast on delivery (network_client_apply_event
 *    flags saved_ack_pending for the frame layer). */
#define NETWORK_EVENT_PLAYER_JOINED 1
#define NETWORK_EVENT_RESYNC_COMPLETE 2
#define NETWORK_EVENT_SAVE_REQUEST 3
#define NETWORK_EVENT_SAVED 4

/* S8.7f1: full-gamedata structural resync sizing.
 *  - NETWORK_RESYNC_MAX_BYTES caps the emitted TOML a single resync can carry;
 *    the client's reassembly buffer and the host's send source are both this
 *    size. total_bytes is enforced STRICTLY below this cap so the one-byte null
 *    terminator the reload needs always fits (network_resync_accept_chunk).
 *  - NETWORK_RESYNC_MAX_CHUNKS bounds the per-generation chunk bitmap.
 *  - NETWORK_RESYNC_LEVEL_NAME_MAX caps the current-level name a resync names;
 *    a longer name marks the generation failed (a pre-alpha ceiling).
 *  - NETWORK_RESYNC_SEND_INTERVAL_SECONDS is the host's re-send sweep cadence. */
#define NETWORK_RESYNC_MAX_BYTES 262144
#define NETWORK_RESYNC_MAX_CHUNKS (NETWORK_RESYNC_MAX_BYTES / NETWORK_RESYNC_CHUNK_BYTES)
#define NETWORK_RESYNC_LEVEL_NAME_MAX 64
#define NETWORK_RESYNC_SEND_INTERVAL_SECONDS 0.1F

/* S8.7f2: seconds the host's editor-driven structural-resync trigger waits
 * before it fires, restarted on every new structural mutation. Debouncing
 * coalesces an edit burst -- e.g. a multi-cell tile-paint stroke, each cell a
 * separate undo entry -- into a SINGLE resync barrier rather than one full
 * gamedata re-emit + stream per cell. */
#define NETWORK_STRUCTURAL_RESYNC_DEBOUNCE_SECONDS 0.5F

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
    /* S8.4c: HOST<->this-client reliable event channel (net_reliable.h) --
     * its send-side assigns/resends MSG_EVENT packets this host sends to
     * this specific client and applies its ACKs (see net_session.h's own
     * doc comment for what actually flows over it,
     * NETWORK_EVENT_PLAYER_JOINED). S8.7a: its receive-side dedups MSG_EVENT
     * packets THIS client sends the host (network_host_receive below),
     * keyed by this client's own sequence space -- entirely independent of
     * the send-side's sequence space, see net_reliable.h's own top doc
     * comment. network_host_send_acks (below) acks whatever this client has
     * sent so far. */
    ReliableChannel event_channel;
    /* S8.7f1: the highest structural-resync generation this client has
     * confirmed it fully applied (via a NETWORK_EVENT_RESYNC_COMPLETE reliable
     * event, recorded in handle_event_datagram). 0 = confirmed nothing yet, so
     * a freshly-registered client automatically trails any armed generation and
     * gets streamed on the next host sweep -- this is also the late-editing-join
     * path. The host stops re-sending a generation's chunks to a client once
     * this catches up to network->resync_generation. */
    uint32_t resync_confirmed_generation;
    /* S8.7f1: true while this client owes itself a post-resync entity SNAPSHOT.
     * The streamed resync buffer is frozen at the emit instant, so a client
     * that applies it lands on emit-time entity positions -- stale if the host
     * kept simulating or editing after the emit, and with deltas suspended
     * during an editor session nothing else would heal it. Set whenever the
     * host streams this client a chunk set (host_tick_resync, net_session.c);
     * once this client's resync_confirmed_generation catches the armed
     * generation, the host sends it ONE fresh snapshot (the same join-time
     * network_host_send_snapshot) to overlay live positions on the
     * just-applied structural base, and clears the flag. Reset with the rest
     * of NetClient (network_stop's and register_client's whole-struct init). */
    bool resync_snapshot_owed;
} NetClient;

/* Cap on client-side out-of-order editor-op echoes held back waiting for
 * their in-order turn (S8.7c). Bounded by how many op_seqs can be in
 * flight ahead of next_expected_op_seq at once on a LAN -- a small burst,
 * not frame-count-proportional -- and drained every client tick as the gap
 * fills. A holdback overflow is lossless: the offending packet is skipped
 * WITHOUT being dedup-marked, so the sender's reliable resend redelivers it
 * once a slot frees. Same justified fixed-cap exception BindingStore and
 * NETWORK_PENDING_OPS_MAX already rely on. */
#define NETWORK_OP_HOLDBACK_MAX 16

/* S8.7c: one client-side held-back editor-op echo whose op_seq arrived
 * ahead of next_expected_op_seq. The reliable channel delivers
 * out-of-order arrivals immediately (net_reliable.h), but ops must apply in
 * op_seq order or two moves of the same entity could finish in the wrong
 * final state -- so a not-yet-applicable echo is parked here (raw packet
 * bytes, decoded only when its turn comes, same lifetime reasoning as
 * PendingEditorOps below) until the gap ahead of it fills.
 * occupied=false marks a free slot. */
typedef struct {
    uint8_t packet[RELIABLE_SLOT_PACKET_MAX];
    size_t length;
    uint32_t op_seq;
    bool occupied;
} HeldEditorOp;

/* S8.7d1: cap on entity locks held at once across the whole session.
 * Bounded by peers times the editor multi-select cap (5 peers x 32
 * multiselect ids = 160), a user-action bound rather than a
 * frame-count-proportional one -- the same justified fixed-cap exception
 * BindingStore/NetClient/JoinList already rely on. An acquire that finds
 * the table full is DENIED (network_lock_acquire returns false), never
 * silently dropped -- a safe fallback. */
#define NETWORK_LOCKS_MAX 160

/* S8.7d1: seconds of host-side silence (no datagram of any kind from a
 * lock's holder) after which the host force-releases that holder's locks.
 * Connected clients send MSG_INPUT every tick, so an active peer's locks
 * are refreshed continuously (network_lock_refresh_holder, network.c) and
 * never age; only true silence (disconnect, crash, cable pull) lets one
 * expire. See host_age_and_expire_locks (net_session.c). */
#define NETWORK_LOCK_TIMEOUT_SECONDS 5.0F

/* One entity lock. On the HOST this table is the authority: holder_player_id
 * names the peer that owns the edit lock, seconds_since_refresh is the
 * silence-timeout accumulator (zeroed whenever a datagram from the holder
 * arrives, aged each host tick). On a CLIENT the identical struct is a
 * REPLICA maintained purely from host echoes -- seconds_since_refresh is
 * unused there (a client never times a lock out; it only mirrors the host's
 * grant/release decisions). active distinguishes a live entry from an unused
 * slot in the fixed-cap array below. */
typedef struct {
    int entity_id;
    int holder_player_id;
    float seconds_since_refresh;
    bool active;
} EntityLock;

/* S8.7d1: cap on entity ids parked in lock_denied_entity_ids per client for
 * the editor to drain (S8.7d2). One editor gesture can request at most the
 * editor's 32-entry multiselect worth of locks, so 32 is the natural bound;
 * an overflow beyond it is dropped silently. Same justified fixed-cap
 * exception as the locks table above. */
#define NETWORK_LOCK_DENIED_MAX 32

/* S8.7e: cap on presence entries held at once -- every connected peer plus
 * this peer's own entry. Bounded by NET_MAX_CLIENTS + 1 (the host and up to
 * NET_MAX_CLIENTS clients), a peer-count bound, not a frame-count one -- the
 * same justified fixed-cap exception BindingStore/NetClient/JoinList rely on. */
#define NETWORK_PRESENCE_MAX (NET_MAX_CLIENTS + 1)

/* S8.7e: seconds of silence after which a presence entry fades out. Presence
 * flows only while a peer is actively editing (frame.c gates the outgoing
 * bridge on editor_mode), so a peer that leaves editor mode, goes quiet, or
 * disconnects stops refreshing its entry and is deactivated once its age
 * crosses this. One second at the presence tick rate is many missed packets --
 * a single dropped datagram never fades an active peer. */
#define NETWORK_PRESENCE_TIMEOUT_SECONDS 1.0F

/* S8.7e: one peer's editor presence -- its cursor, current selection, and
 * display name. This table holds ALL known peers' presence, INCLUDING this
 * peer's own entry (so rendering can treat every cursor uniformly and simply
 * skip its own id). cursor is a world position (the editor camera target,
 * frame.c). seconds_since_seen is a silence-age accumulator: reset to 0 by
 * network_presence_upsert whenever a fresh record arrives, aged each tick by
 * network_presence_age, and deactivated past NETWORK_PRESENCE_TIMEOUT_SECONDS.
 * active distinguishes a live entry from an unused slot in the fixed-cap array. */
typedef struct {
    int player_id;
    Vector2 cursor;
    int selected_entity_id;
    char name[NET_NAME_MAX];
    float seconds_since_seen;
    bool active;
} PresenceEntry;

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
    /* S8.6: CLIENT side only -- the player_id this client learned from the
     * host's MSG_JOIN_ACCEPT (net_session.c's network_client_tick sets this
     * the moment one arrives, in the same call that flips mode to
     * NET_CLIENT). Meaningless on the HOST's own NetworkState, and on a
     * client still NET_JOINING (no accept received yet) -- 0 is the
     * zero-init "not yet assigned" value, which also happens to be the id
     * reserved for a host's own local player, but that overlap is harmless
     * since this field is never read except on a NET_CLIENT NetworkState.
     * game.c's game_get_local_player resolves this peer's own player
     * entity via NETWORK_INPUT_SOURCE_PREFIX + this id. */
    int local_player_id;
    /* S8.4c: CLIENT side only -- this client's own reliable channel for
     * events exchanged with the HOST. Its receive-side dedups events the
     * HOST sends it: network_client_send_ack below acks whatever it has
     * received so far; network_client_tick (net_session.c) applies each
     * newly-delivered MSG_EVENT via reliable_on_receive (net_reliable.h).
     * S8.7a: its send-side is what network_client_send_reliable_event
     * (below) assigns/resends under, for an event THIS client originates --
     * network_client_tick_reliable_channel (below) ages/resends it every
     * client tick, and reliable_on_ack (net_reliable.h) applied to it (via
     * the client's own MSG_ACK drain, net_session.c) clears whichever slots
     * the host's ack covers. Meaningless on the HOST's own NetworkState --
     * a host's per-client event_channel (NetClient, above) is what matters
     * there. */
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
    /* S8.7a: HOST side only -- the mirror of the CLIENT-side trio above,
     * for a reliable event a CLIENT delivered to this host (network_host_
     * receive below, via a client's own event_channel receive-side). Also
     * records which client it came from (last_client_event_player_id, the
     * matched NetClient.player_id) since a host has more than one connected
     * client where a client only ever has one host. Not gameplay state,
     * same as the client-side trio -- S8.7b+ layers real op handling on
     * top. Meaningless on a CLIENT's own NetworkState. */
    int32_t last_client_event_type;
    int32_t last_client_event_entity_id;
    int last_client_event_player_id;
    int client_event_delivered_count;
    /* S8.7g: host-only save handshake, drained by the frame layer (frame.c's
     * run_active_frame) because net code cannot reach the save function or the
     * UndoHistory -- both frame-owned -- the same net/frame split the resync
     * apply already uses. save_request_pending is HOST side: handle_event_datagram
     * sets it when a client's NETWORK_EVENT_SAVE_REQUEST is delivered; the drain
     * runs the host's save wrapper and broadcasts NETWORK_EVENT_SAVED. It is a
     * bool, not a counter, so several requests that arrive before one drain
     * coalesce into a single save. saved_ack_pending is CLIENT side:
     * network_client_apply_event sets it when the host's NETWORK_EVENT_SAVED is
     * delivered; the drain clears this client's dirty indicator and toasts
     * "Saved". Both reset by network_stop. */
    bool save_request_pending;
    bool saved_ack_pending;
    /* S8.7c: HOST side only -- the global editor-op serialization counter.
     * 0 while offline (zero-init default, and network_stop's reset);
     * network_apply_hosting initializes it to 1 at hosting start, so the
     * first stamped op gets 1 -- never 0, the "unstamped request" sentinel
     * EditorOp (net_protocol.h) documents for a client's op REQUEST.
     * net_session.c's network_host_tick stamps each applied move op
     * op_seq = next_op_seq++ before echoing it, so every client replays the
     * one same total order. Advertised to each joining client as
     * JoinAcceptMessage.op_seq_baseline (network.c's handle_join_datagram).
     * Meaningless on a CLIENT's own NetworkState. */
    uint32_t next_op_seq;
    /* S8.7c: CLIENT side only -- strict in-order application of host-stamped
     * op echoes. 0 while offline (zero-init default, and network_stop's
     * reset); seeded from the host's JoinAcceptMessage.op_seq_baseline
     * exactly once, on the JOINING->CLIENT flip edge (net_session.c's
     * accept branch, which clears held_ops in the same step), so the client
     * starts in sync with the host's counter no matter how many ops the
     * session stamped before this join. An echo whose op_seq equals it is
     * applied immediately (then held_ops is scanned for the next
     * consecutive op_seqs to apply too), one whose op_seq is ahead is
     * parked in held_ops, one behind is a duplicate and dropped.
     * Meaningless on the HOST's own NetworkState. */
    HeldEditorOp held_ops[NETWORK_OP_HOLDBACK_MAX];
    uint32_t next_expected_op_seq;
    /* S8.7d1: the entity lock table. On the HOST it is the authority
     * (network_lock_acquire/_release/_refresh_holder mutate it, the timeout
     * ages it); on a CLIENT it is a replica maintained purely from LOCK_*
     * echoes. Session state, never undo-snapshotted -- reset by network_stop
     * like every other field here. */
    EntityLock locks[NETWORK_LOCKS_MAX];
    /* S8.7d1: CLIENT side only -- entity ids from LOCK_DENY echoes addressed
     * to this peer (echo author == local_player_id), appended here for the
     * editor to drain each frame (the drain itself is S8.7d2; this slice
     * only populates). Overflow past NETWORK_LOCK_DENIED_MAX is dropped
     * silently. Meaningless on the HOST's own NetworkState. */
    int lock_denied_entity_ids[NETWORK_LOCK_DENIED_MAX];
    int lock_denied_count;
    /* S8.7e: the editor-presence table -- every known peer's cursor,
     * selection, and name, INCLUDING this peer's own entry (see PresenceEntry
     * above). On a CLIENT it holds this client's own entry (from its local
     * bridge) plus the host and every other peer relayed by the host; on the
     * HOST it holds the host's own entry plus each client's latest. Session
     * state, never undo-snapshotted -- reset by network_stop like every other
     * field here. */
    PresenceEntry presence[NETWORK_PRESENCE_MAX];
    /* S8.7e: the outgoing presence bridge. frame.c writes these each editor
     * frame (local_cursor = the editor camera target, a main.c local; the
     * selection = editor_state->selected_entity_id) and sets local_cursor_valid;
     * the session tick (net_session.c) consumes them and clears the flag. This
     * decouples the editor-owned camera/selection -- main.c locals -- from the
     * net layer, which must stay editor-free (network.h has no dependency on
     * the editor). Meaningless unless local_cursor_valid is set this frame. */
    Vector2 local_cursor;
    int local_cursor_selected_entity_id;
    bool local_cursor_valid;
    /* S8.7f1: full-gamedata structural resync buffer. Dual role, and a peer is
     * only ever one role at a time (a host never reassembles, a client never
     * emits): on the HOST it holds the emitted TOML being streamed -- the stable
     * send source that stays fixed across every re-send sweep of one generation;
     * on a CLIENT it is the reassembly target the incoming chunks are copied
     * into. Session state, never undo-snapshotted -- reset by network_stop. */
    uint8_t resync_buffer[NETWORK_RESYNC_MAX_BYTES];
    /* S8.7f1 HOST side. resync_generation is the structural-edit counter: 0
     * means no structural edit has happened this session, the first edit bumps
     * it to 1. A client whose resync_confirmed_generation trails this is streamed
     * the full chunk set every send sweep. resync_total_bytes is the emitted
     * TOML length in resync_buffer, resync_level_name the level the emit was
     * taken on (clients reload onto it), resync_send_timer the sweep-cadence
     * accumulator. Meaningless on a CLIENT's own NetworkState. */
    uint32_t resync_generation;
    size_t resync_total_bytes;
    char resync_level_name[NETWORK_RESYNC_LEVEL_NAME_MAX];
    float resync_send_timer;
    /* S8.7f2 HOST side. The editor-driven structural-resync debounce. 0 = idle;
     * the frame layer (run_active_frame) sets it to
     * NETWORK_STRUCTURAL_RESYNC_DEBOUNCE_SECONDS whenever the host's editor
     * commits a structural mutation or performs an undo/redo restore, counts it
     * down by delta_time each frame, and fires network_host_begin_structural_resync
     * when it reaches 0 (or immediately if the host leaves editor mode while it is
     * still armed -- leaving editor mode resumes the entity delta stream, which
     * cannot carry structural changes). Re-arming while already armed just resets
     * the timer, which is how the burst coalescing happens. Meaningless on a
     * CLIENT's own NetworkState; reset to 0 by network_stop. */
    float structural_resync_debounce_timer;
    /* S8.7f1 CLIENT side reassembly bookkeeping. resync_incoming_generation is
     * the generation currently being reassembled (a strictly newer one
     * supersedes it mid-stream, resetting the bitmap). resync_failed_generation
     * is a generation whose reload failed or whose header was out of bounds --
     * never retried, only a newer generation supersedes it (leaves this client
     * diverged until the next structural edit, a bounded pre-alpha compromise).
     * resync_incoming_total_bytes/_chunk_count/_level_name describe the in-flight
     * generation; resync_chunk_bitmap marks which chunk indices have arrived;
     * resync_ready flips true once every chunk 0..chunk_count-1 is present, and
     * the frame layer then applies the reassembled buffer. Meaningless on the
     * HOST's own NetworkState. */
    uint32_t resync_incoming_generation;
    uint32_t resync_failed_generation;
    size_t resync_incoming_total_bytes;
    uint16_t resync_incoming_chunk_count;
    uint8_t resync_chunk_bitmap[NETWORK_RESYNC_MAX_CHUNKS / 8];
    char resync_incoming_level_name[NETWORK_RESYNC_LEVEL_NAME_MAX];
    bool resync_ready;
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
 * clients/client_count, local_player_id (S8.6), host_event_channel, the
 * delivered-event bookkeeping (S8.4c), the client-event bookkeeping
 * (S8.7a), (S8.7d1) the lock table plus the client deny list/count,
 * (S8.7e) the presence table plus the outgoing presence bridge fields, and
 * (S8.7g) the save handshake pending flags.
 * Idempotent: safe to call on an already-OFFLINE NetworkState. */
void network_stop(NetworkState *network);

/* ---- S8.7d1: entity lock table helpers (host authority + client replica).
 * Pure state mutators on network->locks, unit-tested directly in
 * network_test.c. On the HOST they enforce the lock protocol; on a CLIENT a
 * subset (acquire/find) is reused to maintain the echo-driven replica. ---- */

/* Grant/re-grant entity_id to holder_player_id. Returns true when the lock
 * is (or becomes) held by holder_player_id: the entity was free (claims a
 * slot -- a full table returns false instead) or already held by the SAME
 * holder (idempotent re-grant, its silence age reset to 0). Returns false
 * when the entity is held by a DIFFERENT holder, or the table is full. */
[[nodiscard]] bool network_lock_acquire(NetworkState *network, int entity_id, int holder_player_id);

/* Release entity_id, but only if it is currently held by exactly
 * holder_player_id -- returns true and clears the slot in that case, false
 * otherwise (releasing something you do not hold is a no-op). */
[[nodiscard]] bool network_lock_release(NetworkState *network, int entity_id, int holder_player_id);

/* The active lock on entity_id, or nullptr if the entity is unlocked. */
EntityLock *network_lock_find(NetworkState *network, int entity_id);

/* Zero seconds_since_refresh on every active lock held by holder_player_id --
 * the silence-timeout refresh the host applies whenever a datagram from that
 * holder arrives (network_host_receive, network.c). No-op if the holder owns
 * no locks. */
void network_lock_refresh_holder(NetworkState *network, int holder_player_id);

/* ---- S8.7e: editor presence table helpers. Pure state mutators on
 * network->presence, unit-tested directly in network_test.c. Both host and
 * client maintain the table the same way -- the host relays the aggregate,
 * each peer renders every entry but its own. ---- */

/* Insert or refresh `record`: find the entry for record.player_id (or claim a
 * free slot), copy its fields in (name truncated to NET_NAME_MAX-1 bytes, the
 * same truncating copy join_list_add_or_refresh uses), reset seconds_since_seen
 * to 0, and mark it active. A full table silently drops the record -- the cap
 * equals the max peer count, so a full table means a duplicate-id anomaly, not
 * a real new peer. */
void network_presence_upsert(NetworkState *network, PresenceRecord record);

/* Age every active entry by delta_time and deactivate any past
 * NETWORK_PRESENCE_TIMEOUT_SECONDS. A peer that left editor mode, went silent,
 * or disconnected stops refreshing its entry (frame.c only bridges presence
 * while editing) and fades out here. */
void network_presence_age(NetworkState *network, float delta_time);

/* The active presence entry for player_id, or nullptr if absent or inactive. */
PresenceEntry *network_presence_find(NetworkState *network, int player_id);

/* ---- S8.7f1: full-gamedata structural resync (CLIENT reassembly). Pure state
 * mutator on network's resync bookkeeping, unit-tested directly in
 * network_test.c. The host-side emit/self-reload/stream + client-side apply
 * live in net_session.c (they need GameState); this is the transport-free
 * reassembly step. ---- */

/* CLIENT side: fold one decoded MSG_RESYNC chunk into the reassembly state.
 * Fail-closed, never overruns resync_buffer:
 *  - chunk->generation == resync_failed_generation: dropped (a parse-failed
 *    generation is never retried; only a strictly newer one supersedes it).
 *  - chunk->generation  < resync_incoming_generation: dropped (stale).
 *  - chunk->generation  > resync_incoming_generation: adopted as the new
 *    in-flight generation (bitmap reset; generation/total/chunk_count/level_name
 *    stored). A level name longer than NETWORK_RESYNC_LEVEL_NAME_MAX-1, a
 *    total_bytes >= NETWORK_RESYNC_MAX_BYTES, or a chunk_count exceeding
 *    NETWORK_RESYNC_MAX_CHUNKS marks the generation failed and drops it.
 * For any accepted (non-superseded, non-stale) chunk the geometry is then
 * validated -- chunk_index < chunk_count, chunk_count matches ceil(total/CHUNK),
 * and the payload length equals CHUNK_BYTES for every chunk but the last (which
 * must equal the remainder). Any violation drops the packet without touching
 * the buffer. A valid chunk is copied to resync_buffer at chunk_index * CHUNK_BYTES
 * and its bitmap bit set; resync_ready flips true once every bit is present. */
void network_resync_accept_chunk(NetworkState *network, const ResyncChunk *chunk);

/* S8.7f2/f3b: arm the host's structural-resync debounce (set
 * structural_resync_debounce_timer to NETWORK_STRUCTURAL_RESYNC_DEBOUNCE_SECONDS).
 * No-op unless NET_HOSTING. frame.c's per-frame structural trigger arms via
 * this, and the scene ATTR panel's blueprint-section edit sites (editor/core.c)
 * call it directly: those run in EDITOR_TOP_SCENE and so bypass frame.c's
 * top-mode-gated arm, which would otherwise leave a scene-panel blueprint
 * default edit un-resynced (the S8.7f2 gap this closes). */
void network_structural_mark_dirty(NetworkState *network);

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
 * NOT change network->mode itself -- as of S8.6, net_session.c's
 * network_client_tick calls this every tick while still NET_JOINING (a
 * resend, not a one-shot) and only advances to NET_CLIENT once a
 * MSG_JOIN_ACCEPT reply actually arrives, see NetMode's own doc comment
 * (above) for the full handshake. */
void network_client_send_join(NetworkState *network, uint64_t local_gamedata_hash);

/* CLIENT side: encode and send this frame's local_input as MSG_INPUT to
 * network->join_target. Same fire-and-forget contract as
 * network_client_send_join. Call every tick while NET_CLIENT. */
void network_client_send_input(NetworkState *network, const InputState *local_input);

/* S8.7c: out-parameter for network_host_receive's MSG_OP drain. network.c
 * must stay free of game.h (this file's own top doc comment), but applying
 * an op needs GameState -- so network_host_receive copies each
 * newly-delivered op datagram's RAW BYTES here and net_session.c's
 * network_host_tick decodes + applies them after the drain returns.
 *
 * Raw bytes, not decoded EditorOps: a decoded op's Strv fields (level_name)
 * view into network_host_receive's own stack receive buffer, which dies the
 * moment the drain loop reuses it for the next datagram; copying the packet
 * bytes and decoding at apply time keeps those views alive with zero
 * allocator involvement.
 *
 * The fixed cap is justified: op arrivals per tick are bounded (a client
 * sends at most a handful of discrete edits per tick), the buffer is
 * drained every tick, and overflow is lossless -- once count reaches
 * NETWORK_PENDING_OPS_MAX, handle_op_datagram returns WITHOUT dedup-marking
 * the packet, so the client's reliable resend redelivers it next tick.
 * Same bounded-cap exception NetClient/JoinList already rely on. */
#define NETWORK_PENDING_OPS_MAX 16
typedef struct {
    uint8_t packets[NETWORK_PENDING_OPS_MAX][RELIABLE_SLOT_PACKET_MAX];
    size_t lengths[NETWORK_PENDING_OPS_MAX];
    int sender_player_ids[NETWORK_PENDING_OPS_MAX];
    int count;
} PendingEditorOps;

/* HOST side: drain every packet currently waiting on network->transport.
 * A decoded MSG_JOIN is hash-verified against local_gamedata_hash
 * (net_protocol.h's protocol_join_verify) -- a match registers the
 * sender's NetAddr in `clients` with the next player_id (a no-op, not a
 * duplicate, if that addr is already registered: re-JOIN refreshes rather
 * than adding a second entry) and replies with a MSG_JOIN_ACCEPT carrying
 * that player_id (S8.6, sent on every accepted JOIN including a refresh,
 * so a client whose first accept was lost still converges once it
 * resends), a mismatch is silently refused (no client registered, no
 * reply sent) -- UNLESS (S8.7f1) a structural resync generation is armed
 * (resync_generation > 0), in which case the mismatched join is accepted
 * anyway: the fresh NetClient trails the generation, so the next resync
 * sweep streams it the host's full gamedata and convergence is
 * guaranteed. Host always wins -- the accepted mismatched joiner is
 * force-converted to the host's gamedata, the intended host-authoritative
 * behavior for editing sessions (see handle_join_datagram, network.c).
 * A decoded MSG_INPUT from an address already present in
 * `clients` overwrites that client's last_input; MSG_INPUT from an
 * unregistered address is ignored. A decoded MSG_ACK (S8.4c) from an
 * address already present in `clients` is applied to that client's own
 * event_channel (net_reliable.h's reliable_on_ack), clearing whichever
 * send-window slots it covers; MSG_ACK from an unregistered address is
 * ignored, same as MSG_INPUT. A decoded MSG_EVENT (S8.7a) from an address
 * already present in `clients` is dedup'd via that client's own
 * event_channel (net_reliable.h's reliable_on_receive, keyed by the
 * packet header's seq); a newly-delivered one updates
 * last_client_event_type/_entity_id/_player_id and increments
 * client_event_delivered_count, a duplicate is silently dropped, same
 * exactly-once contract network_client_receive_event (net_session.c)
 * already gives the reverse direction; MSG_EVENT from an unregistered
 * address is ignored, same as MSG_INPUT/MSG_ACK. Anything that fails to
 * decode (wrong magic/version, truncated, an unrelated message type) is
 * silently ignored, same as discovery_client_tick's own drain loop.
 *
 * A decoded MSG_OP (S8.7c) from a registered client is copied -- raw
 * datagram bytes, length, and the sender's player_id -- into `out_ops` for
 * net_session.c's network_host_tick to decode and apply after the drain
 * (network.c cannot include game.h). Backpressure: if out_ops is already
 * full (count == NETWORK_PENDING_OPS_MAX) the datagram is left on the wire
 * unmarked (NOT passed to reliable_on_receive), so the client's resend
 * redelivers it next tick -- overflow is lossless. Otherwise it is
 * deduped via the sending client's own event_channel (reliable_on_receive,
 * keyed by the packet header's seq) and, if newly delivered, appended to
 * out_ops. MSG_OP from an unregistered address is ignored, same as
 * MSG_INPUT/MSG_ACK/MSG_EVENT. A decoded MSG_CURSOR (S8.7e) from a
 * registered client upserts ONLY the records whose player_id matches the
 * sending client's player_id into network->presence (one peer, one voice --
 * the wire is never trusted to speak for another peer, mismatched records
 * dropped); CURSOR from an unregistered address is ignored, same as the
 * others. out_ops must be non-null (callers that do not consume ops pass one
 * they ignore). Call once per tick, before simulating, so the freshest input
 * reaches this tick's game_update. */
void network_host_receive(NetworkState *network, uint64_t local_gamedata_hash, PendingEditorOps *out_ops);

/* Look up a connected client by player_id (1..NET_MAX_CLIENTS). Returns
 * nullptr if no active client currently holds that id -- the "unknown/
 * absent id" case game.c's input_for_entity resolves to idle input. */
[[nodiscard]] const NetClient *network_find_client_by_player_id(const NetworkState *network, int player_id);

/* ---- S8.4c/S8.7a: reliable event sub-channel primitives, layered on
 * net_reliable.h's ReliableChannel over the same `network->transport`
 * S8.4a's JOIN/INPUT flow already uses. S8.4c wired the host->client
 * direction -- see net_session.h's own doc comment for the one concrete
 * event it wires end-to-end (NETWORK_EVENT_PLAYER_JOINED). S8.7a adds the
 * reverse, client->host, direction: network_client_send_reliable_event/
 * network_client_tick_reliable_channel below are its send-side primitives
 * (mirroring network_broadcast_reliable_event/
 * network_host_tick_reliable_channels), network_host_receive above dedups
 * and delivers incoming client events, and network_host_send_acks below
 * acks them back -- see net_reliable.h's own top doc comment for why the
 * same ReliableChannel struct safely carries both directions at once. ---- */

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

/* S8.7a. CLIENT side. Reliable-send `event` (net_reliable.h's
 * reliable_send) to network->join_target under host_event_channel's own
 * send-side sequence -- the mirror of network_broadcast_reliable_event
 * above, one direction reversed: a single host to send to (not a fan-out
 * over `clients`), so no per-client loop is needed. */
void network_client_send_reliable_event(NetworkState *network, EventRecord event);

/* S8.7a. CLIENT side. Age host_event_channel's send-side by delta_time and
 * resend anything still unacked past RELIABLE_RESEND_SECONDS
 * (net_reliable.h's reliable_tick), toward network->join_target -- the
 * mirror of network_host_tick_reliable_channels above. Call once per
 * client tick while NET_CLIENT. */
void network_client_tick_reliable_channel(NetworkState *network, float delta_time);

/* S8.7a. HOST side. For every currently active client whose own
 * event_channel has received anything (has_received_any, net_reliable.h),
 * encode its current ack (reliable_make_ack) and send it as MSG_ACK to
 * that client's addr -- the mirror of network_client_send_ack above, one
 * per connected client rather than one for the single host. A client
 * whose event_channel has never received anything gets no ack this call --
 * nothing to ack yet, same gate network_client_send_ack itself uses. Same
 * fire-and-forget contract as every other network_* send in this file. */
void network_host_send_acks(NetworkState *network);

/* ---- S8.7b: editor operations on the reliable sub-channel (send side).
 * Editor ops (net_protocol.h's EditorOp, MSG_OP) ride the very same
 * per-direction ReliableChannel MSG_EVENT already uses -- one shared
 * next_send_sequence, one shared send/dedup window per direction (see
 * net_reliable.h's own "Shared sequence space" note). These two are the
 * op-carrying twins of network_client_send_reliable_event /
 * network_broadcast_reliable_event, calling reliable_send_op in place of
 * reliable_send. The resend ticks (network_client_tick_reliable_channel /
 * network_host_tick_reliable_channels above) need no op-specific variant --
 * slots hold pre-encoded bytes, so reliable_tick resends a MSG_OP exactly
 * as it resends a MSG_EVENT. The receive/apply side (network_host_receive's
 * MSG_OP branch, net_session's op application) lands in a later slice. ---- */

/* S8.7b. CLIENT side. Reliable-send `operation` (net_reliable.h's
 * reliable_send_op) to network->join_target under host_event_channel's own
 * send-side sequence -- the op twin of network_client_send_reliable_event
 * above. This is an op REQUEST: the caller passes op_seq 0, since the host
 * stamps the real serialization number on its echo (a later slice); this
 * function does not enforce that, it just sends whatever op it is given. */
void network_client_send_reliable_op(NetworkState *network, const EditorOp *operation);

/* S8.7b. HOST side. Reliable-send `operation` (net_reliable.h's
 * reliable_send_op) to every currently active client's own event_channel --
 * the op twin of network_broadcast_reliable_event above: each client gets
 * its own sequence number and resend timer, so one client's dropped packet
 * never delays or duplicates delivery to another. */
void network_host_broadcast_reliable_op(NetworkState *network, const EditorOp *operation);
