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

#include <stddef.h> // size_t

/* Reserved AttrRecord names carrying entity->position over the attr-record
 * stream -- see this header's own "How position rides the AttrRecord
 * stream" note above. Both always ATTR_FLOAT. */
#define NETWORK_ATTR_POS_X "pos_x"
#define NETWORK_ATTR_POS_Y "pos_y"

/* ---- S8.4c: reliable event sub-channel ----
 *
 * net_reliable.h's ReliableChannel (assign/resend on the send side,
 * dedup via a highest-seq + bitfield window on the receive side) is what
 * finally lets MSG_EVENT be delivered reliably and exactly once, unlike
 * S8.4a/b's fire-and-forget INPUT/SNAPSHOT/DELTA. network.h's
 * NetClient.event_channel/NetworkState.host_event_channel carry the actual
 * per-connection state; network_broadcast_reliable_event/
 * network_host_tick_reliable_channels/network_client_send_ack (network.h)
 * are the transport-level primitives; network_host_tick/network_client_tick
 * below are what call them at the right point in the tick.
 *
 * NETWORK_EVENT_PLAYER_JOINED is the one concrete event S8.4c wires
 * end-to-end: network_host_tick reliable-sends it to every active client
 * (including the one that just joined) whenever a new client registers,
 * carrying the newly-joined player's player_id as EventRecord.entity_id
 * (argument unused, an empty Strv). A client's network_client_tick applies
 * each newly-delivered copy (dedup'd via reliable_on_receive) by recording
 * it on NetworkState (last_delivered_event_type/_entity_id,
 * delivered_event_count) -- currently session-level bookkeeping, not a
 * gameplay mutation.
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
#define NETWORK_EVENT_PLAYER_JOINED 1

/* HOSTING only: network_host_receive (network.h) drains every pending
 * JOIN/INPUT/ACK packet on state->network.transport, hash-verifying JOINs
 * against state->gamedata_hash, storing the latest InputState for every
 * already-registered client, and (S8.4c) applying any incoming MSG_ACK to
 * the acking client's own event_channel. Call BEFORE game_update so this
 * tick's behavior dispatch (game.c's input_for_entity) sees the freshest
 * input a client sent. Any client newly registered by this call (S8.4b) is
 * sent a full SNAPSHOT of the current level (network_host_send_snapshot
 * below) before returning, so a joining client's very first received
 * packet is always the full state, never a partial DELTA -- and (S8.4c) a
 * NETWORK_EVENT_PLAYER_JOINED is reliable-sent to every active client (see
 * this header's own "S8.4c: reliable event sub-channel" note). Finally,
 * every active client's send-side event_channel is aged/resent
 * (network_host_tick_reliable_channels, network.h) by delta_time. No-op
 * under any mode other than NET_HOSTING. */
void network_host_tick(GameState *state, float delta_time);

/* JOINING or NET_CLIENT only. NET_JOINING: sends this session's MSG_JOIN
 * (network_client_send_join, state->gamedata_hash) and advances
 * state->network.mode to NET_CLIENT in the same call -- S8.4a's implicit
 * acceptance model, see NetMode's doc comment (network.h) for why there is
 * no accept/reject reply yet. NET_CLIENT (including the NET_CLIENT this
 * call itself just entered): sends local_input as this tick's MSG_INPUT
 * (network_client_send_input), then drains and applies every pending
 * SNAPSHOT/DELTA packet (network_client_apply_state below) -- S8.4b's
 * sync-back -- and every pending MSG_EVENT (S8.4c: dedup'd via
 * reliable_on_receive, net_reliable.h, and applied exactly once per
 * distinct sequence number). Finally sends the current ack for whatever
 * has been received so far (network_client_send_ack, network.h) -- a
 * no-op before the first reliable event ever arrives. No-op under any
 * other mode. */
void network_client_tick(GameState *state, const InputState *local_input);

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
 * "SNAPSHOT vs DELTA" note) onto state->gamedata.current_level. Each
 * record's entity_id is resolved via level_find_entity_by_id; a record for
 * an entity id the client's level doesn't have (a level the client hasn't
 * loaded yet, or an id mismatch) is silently skipped -- v1 requires both
 * sides to already be on the same level/gamedata for ids to line up
 * (verified once at JOIN via the gamedata hash, network_host_receive).
 * NETWORK_ATTR_POS_X/_Y write entity->position; every other record calls
 * attr_set_float/_int/_bool/_string on entity->attrs. Every record's Strv
 * name/value is a view into the caller's own receive buffer -- this
 * function copies each one out (via attr_set_*'s own internal
 * str_from_cstr) into state->gamedata_arena before returning, so nothing
 * borrowed from the packet buffer is ever retained. */
void network_client_apply_state(GameState *state, const AttrRecord *records, size_t count);
