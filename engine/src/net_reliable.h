#pragma once

/* net_reliable.h -- the reliable event sub-channel (S8.4c): assigns a
 * sequence number to every MSG_EVENT a HOST sends to one client, resends
 * anything still unacked on a timer, and lets the CLIENT dedup incoming
 * sequence numbers via a highest-seq + 32-bit bitfield window -- exactly
 * one socket (net.h's NetTransport), no TCP. Completes S8.4 (see
 * net_session.h's own top doc comment): S8.4a/b's INPUT/SNAPSHOT/DELTA are
 * fire-and-forget (net_session.c's own doc comments already document this
 * as "correct, at the cost of bandwidth" for continuously-resent state);
 * MSG_EVENT needs the opposite guarantee -- delivered exactly once, even
 * across a dropped packet -- since an event is a discrete notification, not
 * a value naturally superseded by next tick's resend.
 *
 * One ReliableChannel per CONNECTION DIRECTION, not per NetworkState: a
 * host keeps one per connected client (`NetClient.event_channel`,
 * network.h); a client keeps one for events exchanged with the host
 * (`NetworkState.host_event_channel`). S8.4c wired only the host->client
 * direction (send-side of a host's per-client event_channel, receive-side
 * of a client's host_event_channel); S8.7a adds the reverse -- a client
 * originating a reliable event (the collaborative editor's discrete ops)
 * now uses the send-side of ITS OWN host_event_channel, and a host dedups
 * each client's incoming events via the receive-side of THAT client's own
 * event_channel. Both directions share the one ReliableChannel type rather
 * than doubling up into per-direction types: a ReliableChannel's send-side
 * fields (next_send_sequence/send_window) and receive-side fields
 * (highest_received_sequence/received_bitfield) are entirely separate
 * field groups with independent sequence spaces, so the same struct
 * safely carries "this end's outgoing sequence" and "this end's incoming
 * dedup window" for its role in each direction at once -- a host's
 * per-client event_channel sends host->client (its send-side) and dedups
 * client->host (its receive-side); a client's host_event_channel sends
 * client->host (its send-side) and dedups host->client (its receive-side).
 *
 * ---- Send side: resend-until-acked ----
 *
 * reliable_send assigns the channel's next sequence number, encodes the
 * event as a full MSG_EVENT packet (net_protocol.h) into a ring-buffer slot
 * indexed by `sequence_number % RELIABLE_WINDOW`, and transmits it
 * immediately. reliable_send_op does the same for a single editor operation
 * (net_protocol.h's EditorOp), encoding it as a MSG_OP packet instead --
 * the two share the same slot arithmetic, eviction policy, and fail-closed
 * drop, differing only in which protocol_encode_*_packet call fills the
 * slot. reliable_tick ages every occupied slot by delta_time and
 * re-transmits (the exact same pre-encoded bytes, so a resend never
 * re-derives anything -- and never cares whether those bytes are a
 * MSG_EVENT or a MSG_OP) any slot whose age has reached
 * RELIABLE_RESEND_SECONDS, resetting its age to 0. reliable_on_ack clears
 * (frees) every occupied slot the incoming ack covers, so a resend loop
 * naturally stops on its own once the receiver's ack catches up -- no
 * separate "stop resending" call needed.
 *
 * ---- Shared sequence space: MSG_EVENT and MSG_OP ----
 *
 * MSG_EVENT (reliable_send) and MSG_OP (reliable_send_op) sent on the SAME
 * channel draw from the same next_send_sequence and are acked/deduped by
 * the same send/receive window -- there is no per-type sequence space.
 * That is intentional, not a shortcut: the receive side dedups purely on
 * the packet header's seq (reliable_on_receive never looks at the message
 * type), so one channel yields one total order per direction -- exactly
 * what the collaborative editor's op stream needs, a single serialized
 * sequence of edits the far end replays in order, with reliable delivery
 * shared by whatever events also ride the channel.
 *
 * Window/backpressure policy: RELIABLE_WINDOW (64) is a hard cap on
 * simultaneously in-flight (sent, not yet acked) events per channel, not a
 * MAX_*-style array-of-independent-entries cap -- the ring-buffer slot for
 * a new sequence number is `sequence_number % RELIABLE_WINDOW`, so sending
 * past 64 unacked events EVICTS (silently overwrites, never delivered) the
 * oldest still-unacked slot rather than blocking the caller or growing
 * unbounded. Justified: reliable events in this codebase are discrete,
 * infrequent notifications (see NETWORK_EVENT_PLAYER_JOINED, net_session.h)
 * -- 64 simultaneously in flight, unacked, on a LAN is far beyond any
 * expected pre-alpha session's event rate, so eviction is a theoretical
 * safety net, not an expected runtime path. This mirrors every other
 * bounded-capacity policy this codebase already documents as intentional
 * (JoinList/NetClient's own silent-drop-past-cap contracts, network.h).
 *
 * ---- Receive side: dedup via highest-seq + bitfield ----
 *
 * reliable_on_receive updates highest_received_sequence/received_bitfield
 * and returns whether `sequence_number` is newly delivered (true) or a
 * duplicate (false) -- the caller must only apply/deliver the event to the
 * game when this returns true. Uses the exact bit convention
 * PROTOCOL_ACK_BITFIELD_BITS documents for AckMessage.ack_bitfield (bit N
 * acks sequence `highest - 1 - N`), since reliable_make_ack below sends
 * received_bitfield out AS an AckMessage.ack_bitfield verbatim. A sequence
 * number more than PROTOCOL_ACK_BITFIELD_BITS (32) behind
 * highest_received_sequence falls outside the tracked window and is
 * treated as a duplicate (conservative: a packet that old was almost
 * certainly already delivered, and re-delivering it if it somehow was not
 * is worse than the rare miss).
 *
 * reliable_make_ack packages the current highest_received_sequence/
 * received_bitfield as an AckMessage (net_protocol.h) ready to send as
 * MSG_ACK. Meaningless before the channel has ever received anything --
 * callers check has_received_any (this struct) first, same as
 * network_client_send_ack (network.h) does before sending an ack. */

#include "net.h"          // NetTransport, NetAddr, net_send
#include "net_protocol.h" // EventRecord, EditorOp, AckMessage, PROTOCOL_ACK_BITFIELD_BITS

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Max simultaneously in-flight (sent, unacked) reliable events per channel.
 * See this header's own "Window/backpressure policy" note for why 64 is a
 * hard cap with silent eviction, not a soft/growable limit. */
#define RELIABLE_WINDOW 64

/* Seconds an unacked reliable event waits before reliable_tick resends it.
 * 0.1s -- responsive enough that a single dropped packet on a LAN is
 * invisible (re-delivered within a few frames at 60fps) without resending
 * so aggressively it meaningfully adds to LAN traffic for an event class
 * that fires rarely (see this header's own top doc comment). */
#define RELIABLE_RESEND_SECONDS 0.1F

/* Bound for one send-window slot's pre-encoded packet bytes. A slot now
 * holds either a MSG_EVENT or a MSG_OP packet (see this header's own
 * "Shared sequence space" note), whichever reliable_send/reliable_send_op
 * queued -- so it is sized for the MSG_OP worst case, which dominates the
 * small MSG_EVENT one. Deliberately NOT net.h's NET_MAX_PACKET_SIZE (1400)
 * -- that is the wire's hard MTU-safe ceiling, but an editor op is a
 * discrete edit, not a level-sized SNAPSHOT/DELTA payload. The MSG_OP worst
 * case is PACKET_HEADER_SIZE (12, net_protocol.h) plus the op kind byte
 * plus a length-prefixed level_name plus the op's three 4-byte header ints
 * (entity_id, author_player_id, op_seq) plus an AttrRecord whose string
 * value is bounded by net_session.c's own NETWORK_ATTR_STRING_VALUE_MAX
 * (128) -- roughly 300 bytes, rounded up with headroom to 512. An op (or
 * event) whose encoded packet would exceed this is silently dropped by
 * reliable_send_op/reliable_send (their underlying protocol_encode_*_packet
 * call fails the same bounds-check every other protocol_encode_*_packet
 * call already fails closed on) -- never a buffer overflow, just a packet
 * that never gets queued. Multiplying this by RELIABLE_WINDOW (64) *
 * (NET_MAX_CLIENTS host-side channels + one client-side channel = 5) keeps
 * a whole GameState's total reliable-channel footprint at 64 * 512 * 5 =
 * 160 KB -- still small. */
#define RELIABLE_SLOT_PACKET_MAX 512

/* One send-window slot: a pre-encoded MSG_EVENT or MSG_OP packet (header +
 * payload, ready to hand straight to net_send) plus resend bookkeeping.
 * awaiting_ack=false means the slot is free -- either never used, or its
 * sequence was cleared by reliable_on_ack; reliable_send/reliable_send_op/
 * _tick all check this before touching a slot's contents. */
typedef struct {
    uint32_t sequence_number;
    uint8_t packet[RELIABLE_SLOT_PACKET_MAX];
    size_t packet_length;
    float seconds_since_send;
    bool awaiting_ack;
} ReliableSendSlot;

/* Send side (assigns/resends) plus receive side (dedups/acks) for one
 * connection direction -- see this header's own top doc comment for why
 * both directions (S8.7a) reuse this one struct rather than splitting into
 * two types. Zero-initialized to "nothing sent, nothing received yet":
 * every slot's awaiting_ack starts false, has_received_any starts false. */
typedef struct {
    /* ---- send side ---- */
    uint32_t next_send_sequence;
    ReliableSendSlot send_window[RELIABLE_WINDOW];

    /* ---- receive side ---- */
    bool has_received_any;
    uint32_t highest_received_sequence;
    uint32_t received_bitfield;
} ReliableChannel;

/* Encode `event` as a MSG_EVENT packet under channel's next sequence
 * number, store it in the send window (evicting whatever previously
 * occupied that ring-buffer slot, per this header's own "Window/
 * backpressure policy" note), and transmit it once immediately via
 * transport to dest. A no-op (nothing queued, nothing sent) if the encoded
 * packet would exceed RELIABLE_SLOT_PACKET_MAX. */
void reliable_send(ReliableChannel *channel, const NetTransport *transport, NetAddr dest, EventRecord event);

/* Encode `operation` as a MSG_OP packet (net_protocol.h's
 * protocol_encode_op_packet) under channel's next sequence number and queue
 * plus transmit it exactly as reliable_send does its MSG_EVENT -- same slot
 * arithmetic, same eviction policy, same shared next_send_sequence and
 * send window (see this header's own "Shared sequence space" note), same
 * fail-closed drop (nothing queued, nothing sent) if the encoded packet
 * would exceed RELIABLE_SLOT_PACKET_MAX. The op is taken by pointer since
 * EditorOp is larger than EventRecord (same reason protocol_encode_op_packet
 * does). */
void reliable_send_op(ReliableChannel *channel, const NetTransport *transport, NetAddr dest, const EditorOp *operation);

/* Age every occupied send-window slot by delta_time; resend (the exact
 * bytes queued by reliable_send, unmodified) any slot whose age has
 * reached RELIABLE_RESEND_SECONDS and reset its age to 0. Call once per
 * hosting tick, per client (see network.h's network_host_tick_reliable_channels
 * for the per-NetworkState fan-out). */
void reliable_tick(ReliableChannel *channel, const NetTransport *transport, NetAddr dest, float delta_time);

/* Mark every occupied send-window slot covered by ack (ack.ack_seq/
 * ack.ack_bitfield, PROTOCOL_ACK_BITFIELD_BITS's bit convention,
 * net_protocol.h) as acked -- frees the slot (awaiting_ack = false) so
 * reliable_tick stops resending it. A slot outside the ack's tracked
 * window (more than PROTOCOL_ACK_BITFIELD_BITS behind ack.ack_seq) is left
 * untouched -- unreachable by THIS ack, but reachable by a later one once
 * the sender's own resends push it back into range. Takes the whole
 * AckMessage (rather than its two uint32 fields separately) so no two
 * adjacent parameters share a type -- same dodge net_protocol.c's
 * write_be_uint/packet_begin already use for
 * bugprone-easily-swappable-parameters. */
void reliable_on_ack(ReliableChannel *channel, AckMessage ack);

/* Update channel's receive-side highest_received_sequence/received_bitfield
 * for an incoming sequence_number and report whether it is newly
 * delivered. Returns true exactly once per distinct sequence_number ever
 * passed to this channel (modulo the PROTOCOL_ACK_BITFIELD_BITS
 * tracked-window caveat documented in this header's own top doc comment)
 * -- the caller must only apply the event to the game when this returns
 * true; a false return means drop it, the event already reached the game
 * once. */
[[nodiscard]] bool reliable_on_receive(ReliableChannel *channel, uint32_t sequence_number);

/* Package channel's current receive-side state as an AckMessage
 * (net_protocol.h), ready to send as MSG_ACK. Only meaningful once
 * has_received_any is true -- callers check that first (see this header's
 * own top doc comment). */
[[nodiscard]] AckMessage reliable_make_ack(const ReliableChannel *channel);
