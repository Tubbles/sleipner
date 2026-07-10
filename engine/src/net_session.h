#pragma once

/* net_session.h -- host-authoritative session tick hooks (S8.4a) plus
 * state sync (S8.4b): the per-frame glue between NetworkState (network.h's
 * JOIN/INPUT primitives) and GameState. Split out from network.h/network.c
 * because network.h is deliberately decoupled from game.h (game.h
 * includes network.h for GameState.network, so the reverse include would
 * cycle -- see network.h's own top doc comment); net_session.c is the
 * file that's allowed to know about both.
 *
 * Both ticks are self-guarding on state->network.mode, mirroring game.c's
 * tick_network (S8.3b): a caller can call both unconditionally every
 * frame and each is a no-op under any mode it doesn't apply to. frame.c's
 * run_active_frame calls both, unconditionally of state->editor_mode, right
 * before game_update -- same "doesn't care whether the player happens to be
 * in the level editor" reasoning tick_network's own doc comment gives.
 * Exposed here (rather than file-local to frame.c) specifically so a
 * headless test can drive a HOST GameState's and a CLIENT GameState's
 * session ticks directly against a net_loopback.h transport, without
 * needing the full frame_update/menu apparatus.
 *
 * ---- S8.4b: state sync ----
 *
 * What's synced: every entity in the host's current level, its
 * entity->position (a Vector2 FIELD, not an attr) plus every entry in its
 * entity->attrs (instance attrs -- health, state, direction, and any other
 * mutable gameplay attr; NOT entity->persisted_attrs, which is the
 * pre-play-session authored baseline, not live gameplay state). Runtime-
 * only render fields (anim_row, frame_index, ...) are NOT synced -- the
 * client re-derives them locally via the existing generic animation pass
 * (advance_entity_animation, game.c), driven by the just-synced
 * `state`/`direction` attrs, same as the host's own render path.
 *
 * How position rides the AttrRecord stream: net_protocol.h's AttrRecord
 * (S8.2) already carries entity_id + name + typed value, built for
 * attribute-shaped data. Rather than widen the wire format with a second,
 * position-specific record shape, position rides the SAME stream under two
 * RESERVED attr names (NETWORK_ATTR_POS_X/_Y below, both ATTR_FLOAT) --
 * network_client_apply_state recognizes them and writes entity->position
 * instead of calling attr_set_float. This is a documented convention, not
 * a wire-format change: zero cost against S8.2, and no gamedata author is
 * expected to ever name a real attr "pos_x"/"pos_y" (none do today).
 *
 * SNAPSHOT vs DELTA (v1 simplification): both message types carry the
 * exact same "synced state" content -- every entity's position + attrs,
 * full, not a diff. network_host_send_snapshot fires once, to one client,
 * when that client's JOIN is accepted (network_host_tick, below).
 * network_host_broadcast_delta fires every hosting tick, to every
 * registered client. Sending the FULL state every tick rather than only
 * changed attrs is deliberately simple and correct (a client's view can
 * never drift, it always converges within a tick or two) at the cost of
 * bandwidth that scales with level size regardless of how much actually
 * changed -- real per-attr diffing (send only attrs that changed since the
 * last tick) is a bandwidth optimization deferred to a later pass
 * (TODO.md), fine for the LAN-only pre-alpha this targets. */

#include "game.h"
#include "input.h"
#include "net_protocol.h" // AttrRecord, EventRecord
#include "net_reliable.h" // ReliableChannel
#include "undo.h"         // UndoHistory

#include <stddef.h> // size_t

/* Reserved AttrRecord names carrying entity->position over the attr-record
 * stream -- see this header's own "How position rides the AttrRecord
 * stream" note above. Both always ATTR_FLOAT. */
#define NETWORK_ATTR_POS_X "pos_x"
#define NETWORK_ATTR_POS_Y "pos_y"

/* S8.6: a THIRD reserved AttrRecord name, always ATTR_STRING, pushed FIRST
 * for every entity (push_entity_sync_records, net_session.c) -- ahead of
 * POS_X/POS_Y and every real attr -- carrying entity->blueprint_name over
 * the exact same stream. Where POS_X/POS_Y let an ALREADY-KNOWN entity's
 * position ride the attr stream instead of a dedicated wire shape, this
 * lets a client MATERIALIZE an entity it doesn't have yet: a client's own
 * level.entities is fixed at its own load-time parse, so a player the host
 * spawns mid-session (net_session.c's per-join spawn) has no local
 * counterpart until a sync record for its id arrives. network_client_apply_
 * state's ensure_synced_entity_exists recognizes this record, and if
 * level_find_entity_by_id already fails for that id, spawns a placeholder
 * via level_spawn_entity (the SAME "add an entity at runtime and fix up
 * tracking" primitive S6.6's editor spawn and S8.6's host-side per-join
 * spawn both use) then FORCES its id to match the record's entity_id --
 * necessary, not optional: level_spawn_entity's own id auto-assignment has
 * no cross-peer coordination, so only an explicit force keeps the new
 * entity resolvable by every later record naming the same entity_id. Every
 * later record for that same entity_id in this and future syncs (POS_X/
 * POS_Y, input_source, health, ...) then resolves and applies exactly like
 * an entity the client had from its own initial parse -- no further special
 * casing needed anywhere else in this file. A record with this name for an
 * entity_id the client ALREADY has is a no-op (already exists, nothing to
 * spawn) -- sent unconditionally every sync alongside POS_X/POS_Y rather
 * than only once at an entity's first appearance, the same "full state
 * every tick, simple over bandwidth-optimal" v1 tradeoff this header's own
 * "SNAPSHOT vs DELTA" note already documents. */
#define NETWORK_ATTR_BLUEPRINT_NAME "blueprint_name"

/* ---- S8.4c/S8.7a: reliable event sub-channel ----
 *
 * net_reliable.h's ReliableChannel (assign/resend on the send side,
 * dedup via a highest-seq + bitfield window on the receive side) is what
 * finally lets MSG_EVENT be delivered reliably and exactly once, unlike
 * S8.4a/b's fire-and-forget INPUT/SNAPSHOT/DELTA. network.h's
 * NetClient.event_channel/NetworkState.host_event_channel carry the actual
 * per-connection state; network_broadcast_reliable_event/
 * network_host_tick_reliable_channels/network_client_send_ack (network.h)
 * are the host->client transport-level primitives, network_client_send_
 * reliable_event/network_client_tick_reliable_channel/network_host_send_acks
 * (network.h, S8.7a) are the client->host mirror; network_host_tick/
 * network_client_tick below are what call all of them at the right point
 * in the tick.
 *
 * NETWORK_EVENT_PLAYER_JOINED (and the S8.7f1 resync-complete confirmation
 * NETWORK_EVENT_RESYNC_COMPLETE) are defined in network.h -- moved there so
 * network.c can react to a client's resync confirmation (handle_event_datagram)
 * without depending on this header. NETWORK_EVENT_PLAYER_JOINED is the one
 * concrete event S8.4c wires end-to-end, host->client: network_host_tick reliable-sends it to every
 * active client (including the one that just joined) whenever a new client
 * registers, carrying the newly-joined player's player_id as
 * EventRecord.entity_id (argument unused, an empty Strv). A client's
 * network_client_tick applies each newly-delivered copy (dedup'd via
 * reliable_on_receive) by recording it on NetworkState
 * (last_delivered_event_type/_entity_id, delivered_event_count) --
 * currently session-level bookkeeping, not a gameplay mutation.
 * client->host (S8.7a) has no concrete event wired yet -- network_host_tick
 * below dedups and records whatever a client sends
 * (last_client_event_type/_entity_id/_player_id, client_event_delivered_
 * count, network.h), the same bookkeeping-only posture, ready for a real
 * caller (the collaborative editor's discrete ops, S8.7b+) to build on.
 *
 * Deliberately NOT rule.h's TriggerType/TriggerEvent pipeline (the obvious
 * "real game event" candidates -- TRIGGER_DEFEAT, a transition
 * notification): game_update's own trigger_events vec (game.c) is
 * scratch-arena-scoped and fully drained by rules_evaluate_batch inside
 * that same call, with no hook point any caller outside game.c can
 * currently observe. Routing an actual TriggerEvent over this reliable
 * channel needs that hook added first (e.g. an out-parameter or a
 * GameState-level accumulator game_update appends fired events into) --
 * broader than S8.4c's own reliable-channel brief, tracked in TODO.md as a
 * follow-up. */

/* HOSTING only: network_host_receive (network.h) drains every pending
 * JOIN/INPUT/ACK/EVENT/JOIN_ACCEPT-triggering packet on
 * state->network.transport, hash-verifying JOINs against
 * state->gamedata_hash (replying with MSG_JOIN_ACCEPT on a match,
 * network.c's handle_join_datagram), storing the latest InputState for
 * every already-registered client, (S8.4c) applying any incoming MSG_ACK
 * to the acking client's own event_channel, and (S8.7a) dedup'ing and
 * recording any incoming MSG_EVENT via the sending client's own
 * event_channel (see this header's own "S8.4c/S8.7a: reliable event
 * sub-channel" note). Call BEFORE game_update so this tick's behavior
 * dispatch (game.c's input_for_entity) sees the freshest input a client
 * sent. Any client newly registered by this call gets (S8.6) a player
 * entity spawned for it -- the same blueprint as this host's own local
 * player, input_source stamped "network:<its id>" (see
 * spawn_network_player below) -- BEFORE (S8.4b) a full SNAPSHOT of the
 * current level (network_host_send_snapshot below), so a joining client's
 * very first received packet already includes its own freshly spawned
 * player, never a partial SNAPSHOT missing it. Also (S8.4c) a
 * NETWORK_EVENT_PLAYER_JOINED is reliable-sent to every active client (see
 * this header's own "S8.4c/S8.7a: reliable event sub-channel" note). Every
 * active client's send-side event_channel is aged/resent
 * (network_host_tick_reliable_channels, network.h) by delta_time. Finally
 * (S8.7a) an ack for whatever each active client has sent so far is sent
 * back to it (network_host_send_acks, network.h) -- a no-op per client
 * before that client's first reliable event ever arrives. (S8.7c) Any
 * MSG_OP network_host_receive drained into its PendingEditorOps this tick
 * is decoded and dispatched by kind. An EDITOR_OP_MOVE_ENTITY naming the
 * current level and a known entity id is applied authoritatively
 * (entity->position set), then stamped op_seq = next_op_seq++ (author
 * overwritten with the sender's player_id) and echoed to every client via
 * network_host_broadcast_reliable_op -- the echo is what makes the move
 * authoritative on every replica, including the originator's. (S8.7d1) A move
 * of an entity locked by someone OTHER than the sender is silently dropped
 * (no apply, no echo); unlocked or held-by-sender moves apply as above. An
 * EDITOR_OP_LOCK_ACQUIRE is granted (echoed as LOCK_ACQUIRE) when it names a
 * known entity in the current level and network_lock_acquire claims it for
 * the sender, or DENIED (echoed as LOCK_DENY) otherwise; an
 * EDITOR_OP_LOCK_RELEASE the sender actually holds is echoed as LOCK_RELEASE.
 * (S8.7f3a) An EDITOR_OP_DELETE_ENTITY follows the same level/entity/lock
 * rules as a move (locked by someone other than the sender is silently
 * dropped); an applied delete cascades through descendants
 * (game_delete_entity_cascade, game.h), clears any lock entry on the entity
 * WITHOUT a release echo (the DELETE echo is itself the "gone" signal every
 * replica clears its lock on), then is stamped + echoed. An
 * EDITOR_OP_PLACE_ENTITY naming the current level and a resolvable blueprint
 * spawns it at the op's position, and the echo's entity_id is stamped with
 * the host-assigned ROOT entity id (the request carried -1) -- no locks
 * involved, a brand-new entity cannot be contended. Cross-level, unknown-id,
 * or out-of-scope ops (SET_ATTR, and a client-originated LOCK_DENY) are
 * silently dropped (tolerant-skip, same convention as the state appliers).
 * Finally (S8.7d1) the host ages its lock
 * table by delta_time and force-releases any lock whose holder has gone
 * silent past NETWORK_LOCK_TIMEOUT_SECONDS, echoing a LOCK_RELEASE authored by
 * the former holder -- ageing runs AFTER network_host_receive so this tick's
 * incoming refreshes land first. (S8.7e) Finally the presence table is
 * serviced: this frame's bridged cursor (frame.c) is folded into the host's
 * own entry (player_id 0), the table is aged so a silent peer drops out, and
 * the aggregate of every active entry is relayed as one MSG_CURSOR to every
 * client -- the host is the star-topology relay. No-op under any mode other
 * than NET_HOSTING. */
void network_host_tick(GameState *state, float delta_time);

/* JOINING or NET_CLIENT only. NET_JOINING: (re)sends this session's
 * MSG_JOIN (network_client_send_join, state->gamedata_hash) -- every tick,
 * not just once, since a dropped first JOIN or its accept reply must not
 * strand the client -- then drains for a MSG_JOIN_ACCEPT (S8.6): a decoded
 * one stores its player_id onto state->network.local_player_id and
 * advances state->network.mode to NET_CLIENT, in the same call, so the
 * NET_CLIENT branch below also runs THIS tick once accepted (see NetMode's
 * doc comment, network.h, for the full handshake). NET_CLIENT (including
 * the NET_CLIENT this call itself just entered): sends local_input as this
 * tick's MSG_INPUT (network_client_send_input, skipped the very tick the
 * accept arrived -- input starts flowing the tick after), then drains and
 * applies every pending SNAPSHOT/DELTA packet (network_client_apply_state
 * below) -- S8.4b's sync-back, S8.6's own entity-materializing extension
 * included -- every pending MSG_EVENT (S8.4c: dedup'd via
 * reliable_on_receive, net_reliable.h, and applied exactly once per
 * distinct sequence number), and (S8.7a) every pending MSG_ACK (applied to
 * host_event_channel's own send-side via reliable_on_ack, net_reliable.h,
 * clearing whichever of THIS client's own outgoing reliable events it
 * covers). Then (S8.7a) ages/resends host_event_channel's send-side by
 * delta_time (network_client_tick_reliable_channel, network.h). Finally
 * sends the current ack for whatever has been received from the host so
 * far (network_client_send_ack, network.h) -- a no-op before the first
 * reliable event from the host ever arrives. (S8.7c) Every pending MSG_OP
 * echo drained here is applied in strict op_seq order (network_client_receive_state
 * below): one whose op_seq equals next_expected_op_seq applies immediately
 * and then drains any consecutive op_seqs parked in held_ops, one ahead is
 * held, one behind is a duplicate and dropped. (S8.7d1) An applied echo is
 * dispatched by kind: MOVE writes the entity position, LOCK_ACQUIRE/RELEASE
 * maintain this client's replica lock table, and a LOCK_DENY addressed to
 * this peer appends its entity id to lock_denied_entity_ids for the editor to
 * drain (S8.7d2). (S8.7f3a) A DELETE echo cascade-deletes the entity if it
 * still exists locally (the originator already deleted its own copy as a
 * preview -- a missing id is the expected no-op) and drops any replica lock
 * on it; a PLACE echo materializes the named blueprint at the echoed
 * position with the echoed host-assigned id forced onto the spawned root
 * (an id that already resolves is a tolerant dedup no-op). (S8.7e) Any
 * host-relayed MSG_CURSOR drained here upserts
 * every peer's presence into this client's table (skipping its own echoed
 * entry, the local bridge copy is fresher); then this client's own bridged
 * cursor (frame.c) is sent to the host and mirrored locally, and the table is
 * aged so a peer the host stops relaying fades out. No-op under any other
 * mode. */
void network_client_tick(GameState *state, const InputState *local_input, float delta_time);

/* HOST side (S8.4b): encode the current synced state of state's whole
 * current level (see this header's own "What's synced" note) as one or
 * more MSG_SNAPSHOT packets (split if the record count would overflow one
 * packet, never overflowing a send buffer) and send them to client->addr.
 * Fire-and-forget, like every other network_* send in this file -- no
 * error channel, a failed encode/send is silently dropped. */
void network_host_send_snapshot(GameState *state, const NetClient *client);

/* HOST side (S8.4b): same synced-state content as network_host_send_snapshot,
 * as one or more MSG_DELTA packets, sent to every currently active client
 * in state->network.clients. Call once per hosting tick, AFTER game_update
 * has run, so the broadcast reflects this tick's freshest simulated state
 * (see run_active_frame, frame.c). v1 sends the full synced state every
 * tick -- see this header's own "SNAPSHOT vs DELTA" note. */
void network_host_broadcast_delta(GameState *state);

/* CLIENT side (S8.4b): apply a decoded batch of AttrRecords (from either a
 * SNAPSHOT or a DELTA -- identical handling, see this header's own
 * "SNAPSHOT vs DELTA" note) onto state->gamedata.current_level. A
 * NETWORK_ATTR_BLUEPRINT_NAME record (S8.6, see that macro's own doc
 * comment above) is handled first and specially: it materializes a
 * placeholder entity for a not-yet-seen entity_id, or is a no-op if that id
 * already resolves. Every other record's entity_id is resolved via
 * level_find_entity_by_id; one for an entity id the client's level still
 * doesn't have (its BLUEPRINT_NAME record hasn't arrived yet, or genuinely
 * never will -- a level the client hasn't loaded, or an id mismatch) is
 * silently skipped -- v1 requires both sides to already be on the same
 * level/gamedata for ids to line up (verified once at JOIN via the
 * gamedata hash, network_host_receive). NETWORK_ATTR_POS_X/_Y write
 * entity->position; every other record calls
 * attr_set_float/_int/_bool/_string on entity->attrs. Every record's Strv
 * name/value is a view into the caller's own receive buffer -- this
 * function copies each one out (via attr_set_*'s own internal
 * str_from_cstr) into state->gamedata_arena before returning, so nothing
 * borrowed from the packet buffer is ever retained. As of S8.5, a
 * NETWORK_ATTR_POS_Y record also shifts entity->interp_from/interp_to/
 * interp_elapsed (net_session.c's shift_interp_window, entity.h's own
 * doc comments) so entity_render_position can smoothly lerp a NET_CLIENT's
 * rendering toward each freshly synced position instead of snapping --
 * entity->position itself is unaffected, it is written on every call same
 * as before S8.5. */
void network_client_apply_state(GameState *state, const AttrRecord *records, size_t count);

/* S8.7c: called by the editor at a move-commit boundary (drag release,
 * editor/core.c's handle_drag_input) to propagate an entity move over the
 * network. The local preview mutation already happened during the gesture
 * (the shipped drag code moves the entity in place every frame); this only
 * sends the COMMIT.
 *
 * NET_OFFLINE: no-op (single-player).
 * NET_CLIENT: builds an EDITOR_OP_MOVE_ENTITY op REQUEST (op_seq 0,
 *   author_player_id = local_player_id, level_name = current level) and
 *   network_client_send_reliable_op's it to the host; the host's echo is
 *   what authoritatively confirms the move on every replica.
 * NET_HOSTING: the local mutation is already the authoritative apply, so
 *   this stamps op_seq = next_op_seq++ (author_player_id 0, the host's own
 *   player id) and network_host_broadcast_reliable_op's it to every client.
 *
 * new_position is the entity's final committed position (post grid-snap). */
void network_editor_commit_move(GameState *state, int entity_id, Vector2 new_position);

/* S8.7d2: called by the editor at grab time (both grab entry points,
 * editor/core.c) with the full multiselect id set, BEFORE entering
 * EDITOR_SUB_DRAG. Returns whether the grab may proceed.
 *
 * NET_OFFLINE (and any non-hosting/non-client mode): return true (single-player
 *   editing is untouched -- no lock table involved).
 * NET_HOSTING: the host IS the authority, so this is atomic. First it checks
 *   EVERY id: if any is already locked by a player OTHER than the host (id 0),
 *   the whole grab is refused (return false, all-or-nothing -- a partial group
 *   grab would leave some entities locked and some not). Otherwise it claims
 *   every id for holder 0 and broadcasts a LOCK_ACQUIRE echo per id, exactly
 *   like host_apply_and_echo_ops' grant path, so every client's replica learns
 *   the host now holds them.
 * NET_CLIENT: OPTIMISTIC. It first checks the local REPLICA: if any id is
 *   already held by a player OTHER than local_player_id, the grab is refused
 *   immediately (fast local deny, nothing sent). Otherwise it sends one
 *   EDITOR_OP_LOCK_ACQUIRE REQUEST per id (op_seq 0, authored by local_player_id,
 *   naming the current level) and returns true -- the gesture proceeds
 *   optimistically before the host has ruled, and a later LOCK_DENY echo
 *   (drained by editor_drain_lock_denials) aborts it if the host refuses. */
[[nodiscard]] bool network_editor_try_grab(GameState *state, const int *entity_ids, int entity_count);

/* S8.7d2: called by the editor when a drag gesture ends (commit or cancel,
 * editor/core.c) and by the deny-abort path (editor_drain_lock_denials) with
 * the full multiselect id set, to release whatever locks the grab took.
 *
 * NET_OFFLINE (and any non-hosting/non-client mode): no-op.
 * NET_HOSTING: for each id it releases the host's own lock (holder 0) and, if
 *   it actually held one, broadcasts a LOCK_RELEASE echo so every replica clears.
 * NET_CLIENT: sends one EDITOR_OP_LOCK_RELEASE REQUEST per id. Releasing an id
 *   this peer never actually held (an optimistic grab the host denied, whose
 *   ACQUIRE never became a grant) is harmless: the host silently ignores a
 *   release of a lock the sender does not hold, so the deny-abort path can
 *   release the whole multiselect set without tracking which ids were granted. */
void network_editor_release_locks(GameState *state, const int *entity_ids, int entity_count);

/* S8.7f3a: called by the editor right after the selected entity (and its
 * descendant cascade) was deleted locally (editor/core.c's
 * delete_selection_networked). The lock gate (refusing a delete of an entity
 * another player holds) lives at the editor call site, not here -- this seam
 * only propagates.
 *
 * NET_OFFLINE: no-op (single-player).
 * NET_CLIENT: sends an EDITOR_OP_DELETE_ENTITY REQUEST (op_seq 0, authored by
 *   local_player_id, naming the current level). The local delete that already
 *   happened is this peer's preview; the host's echo is what authoritatively
 *   confirms it on every replica (a missing entity at echo-apply time is the
 *   expected originator case).
 * NET_HOSTING: the local delete was already the authoritative apply, so this
 *   clears any lock entry on the entity (the one-rule-everywhere DELETE
 *   lock-clear: the DELETE echo is the "gone" signal, every replica --
 *   including the host's own table -- drops the lock on apply, no LOCK_RELEASE
 *   echo is ever sent) and stamps + broadcasts the DELETE echo. */
void network_editor_commit_delete(GameState *state, int entity_id);

/* S8.7f3a: called by the editor at a PLACE commit (frame.c's
 * handle_place_input CONFIRM path).
 *
 * placed_entity_id is deliberately asymmetric: -1 from a NET_CLIENT caller
 * (a connected client never spawns locally -- the host assigns entity ids,
 * so nothing local exists to name), and the freshly spawned ROOT entity's id
 * from a NET_HOSTING caller (the local spawn already happened and the echo
 * must carry the id every replica forces onto its own spawn). The seam uses
 * it only for the HOSTING echo and ignores it for CLIENT requests.
 *
 * NET_OFFLINE: no-op (single-player).
 * NET_CLIENT: sends an EDITOR_OP_PLACE_ENTITY REQUEST (entity_id -1, op_seq
 *   0, authored by local_player_id) carrying blueprint_name and position;
 *   the host's echo is what materializes the entity on this peer.
 * NET_HOSTING: stamps op_seq = next_op_seq++ (author 0) and broadcasts the
 *   echo with entity_id = placed_entity_id. */
void network_editor_commit_place(GameState *state, int placed_entity_id, Strv blueprint_name, Vector2 position);

/* ---- S8.7f1: full-gamedata structural resync ----
 *
 * Structural edits (blueprints, rules, atlas, animation, levels, tiles) have
 * no fine-grained wire encoding by design. They propagate as a coarse
 * full-gamedata resync: the host re-emits the whole gamedata as TOML, reloads
 * its OWN GameState from those bytes, and streams the same bytes to every
 * client, which reload identically. Because both sides re-parse the same bytes,
 * per-level entity-id assignment (parse order) matches again on every peer, and
 * game_load_gamedata recomputes gamedata_hash so hashes re-converge too. A
 * structural edit is a session-wide barrier: it clears undo on every peer (the
 * same thing a load / hot-reload / level transition already does). The editor
 * trigger and client-side structural-mode blocking are a later slice; this one
 * builds the transfer + reload mechanism, invoked by these functions. */

/* HOST side. Re-emit the whole gamedata as TOML, self-reload from it, clear
 * undo and push a fresh baseline, arm the next resync generation for streaming,
 * and force-release every active lock (the barrier invalidates every claim --
 * entity ids are about to be reassigned). A no-op returning true under any mode
 * other than NET_HOSTING (so the future editor hook can call it unconditionally,
 * including single-player NET_OFFLINE). Returns false if the current level name
 * overflows the resync cap, the emit fails, or the self-reload fails -- in the
 * reload-failure case state is left however the load left it, the same contract
 * a hot-reload failure gives. */
[[nodiscard]] bool network_host_begin_structural_resync(
    Diag *diag, GameState *state, UndoHistory *undo_history, TextureLookupFn texture_lookup, void *texture_user_data);

/* CLIENT side. True when a full resync generation has finished reassembling
 * (network_resync_accept_chunk set resync_ready) and is waiting to be applied.
 * Cheap predicate the frame layer polls every NET_CLIENT frame. Always false on
 * a host/offline peer (only a client's reassembly sets resync_ready). */
[[nodiscard]] bool network_client_resync_ready(const GameState *state);

/* CLIENT side. Reload this client's GameState from the reassembled resync
 * buffer, clear undo and push a fresh baseline (mirroring the host), then clear
 * resync_ready. On SUCCESS, reliable-send a NETWORK_EVENT_RESYNC_COMPLETE back
 * to the host carrying the applied generation, so the host stops re-streaming
 * it. On load FAILURE, mark this generation failed (never retried for the same
 * generation; a newer one supersedes) and do NOT confirm -- the client stays
 * diverged until the next structural edit, a bounded pre-alpha compromise.
 * Call only when network_client_resync_ready returned true. */
[[nodiscard]] bool network_client_apply_resync(
    Diag *diag, GameState *state, UndoHistory *undo_history, TextureLookupFn texture_lookup, void *texture_user_data);
