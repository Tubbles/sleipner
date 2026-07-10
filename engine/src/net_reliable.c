#include "net_reliable.h"

#include "net.h"
#include "net_protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static ReliableSendSlot *send_slot_for_sequence(ReliableChannel *channel, uint32_t sequence_number)
{
    return &channel->send_window[sequence_number % RELIABLE_WINDOW];
}

/* Shared queue-slot-and-transmit tail of reliable_send/reliable_send_op:
 * once either has encoded its packet into slot->packet, record the
 * sequence and length, arm the resend timer, and transmit once. The two
 * senders differ only in which protocol_encode_*_packet call fills the slot
 * beforehand; a failed encode returns before reaching here, leaving the
 * slot untouched (its prior contents stay resendable -- fail closed). */
static void reliable_arm_and_transmit(
    ReliableSendSlot *slot, uint32_t sequence_number, const NetTransport *transport, NetAddr dest, size_t packet_length)
{
    slot->sequence_number = sequence_number;
    slot->packet_length = packet_length;
    slot->seconds_since_send = 0.0F;
    slot->awaiting_ack = true;
    (void)net_send(transport, dest, slot->packet, slot->packet_length);
}

void reliable_send(ReliableChannel *channel, const NetTransport *transport, NetAddr dest, EventRecord event)
{
    uint32_t sequence_number = channel->next_send_sequence;
    channel->next_send_sequence++;
    ReliableSendSlot *slot = send_slot_for_sequence(channel, sequence_number);

    size_t packet_length = 0;
    if (!protocol_encode_event_packet(slot->packet, sizeof(slot->packet), sequence_number, event, &packet_length)) {
        return;
    }
    reliable_arm_and_transmit(slot, sequence_number, transport, dest, packet_length);
}

void reliable_send_op(ReliableChannel *channel, const NetTransport *transport, NetAddr dest, const EditorOp *operation)
{
    uint32_t sequence_number = channel->next_send_sequence;
    channel->next_send_sequence++;
    ReliableSendSlot *slot = send_slot_for_sequence(channel, sequence_number);

    size_t packet_length = 0;
    if (!protocol_encode_op_packet(slot->packet, sizeof(slot->packet), sequence_number, operation, &packet_length)) {
        return;
    }
    reliable_arm_and_transmit(slot, sequence_number, transport, dest, packet_length);
}

void reliable_tick(ReliableChannel *channel, const NetTransport *transport, NetAddr dest, float delta_time)
{
    for (int index = 0; index < RELIABLE_WINDOW; index++) {
        ReliableSendSlot *slot = &channel->send_window[index];
        if (!slot->awaiting_ack) {
            continue;
        }
        slot->seconds_since_send += delta_time;
        if (slot->seconds_since_send < RELIABLE_RESEND_SECONDS) {
            continue;
        }
        (void)net_send(transport, dest, slot->packet, slot->packet_length);
        slot->seconds_since_send = 0.0F;
    }
}

static bool sequence_is_covered_by_ack(uint32_t sequence_number, AckMessage ack)
{
    if (sequence_number == ack.ack_seq) {
        return true;
    }
    if (sequence_number > ack.ack_seq) {
        return false;
    }
    uint32_t distance = ack.ack_seq - sequence_number;
    if (distance > PROTOCOL_ACK_BITFIELD_BITS) {
        return false;
    }
    uint32_t bit_index = distance - 1;
    return (ack.ack_bitfield & (1U << bit_index)) != 0;
}

void reliable_on_ack(ReliableChannel *channel, AckMessage ack)
{
    for (int index = 0; index < RELIABLE_WINDOW; index++) {
        ReliableSendSlot *slot = &channel->send_window[index];
        if (!slot->awaiting_ack) {
            continue;
        }
        if (sequence_is_covered_by_ack(slot->sequence_number, ack)) {
            slot->awaiting_ack = false;
        }
    }
}

bool reliable_on_receive(ReliableChannel *channel, uint32_t sequence_number)
{
    if (!channel->has_received_any) {
        channel->has_received_any = true;
        channel->highest_received_sequence = sequence_number;
        channel->received_bitfield = 0;
        return true;
    }
    if (sequence_number == channel->highest_received_sequence) {
        return false;
    }
    if (sequence_number > channel->highest_received_sequence) {
        uint32_t distance = sequence_number - channel->highest_received_sequence;
        /* The ternary's untaken branch is never evaluated, so the shift by
         * `distance` below only ever executes when distance < 32 -- a
         * distance >= PROTOCOL_ACK_BITFIELD_BITS shifts every old bit out
         * regardless, so 0 is the correct result without risking a
         * shift-by->=width UB on the `<<` itself. */
        uint32_t new_bitfield = distance < PROTOCOL_ACK_BITFIELD_BITS ? (channel->received_bitfield << distance) : 0;
        if (distance <= PROTOCOL_ACK_BITFIELD_BITS) {
            new_bitfield |= (1U << (distance - 1));
        }
        channel->highest_received_sequence = sequence_number;
        channel->received_bitfield = new_bitfield;
        return true;
    }

    uint32_t distance_back = channel->highest_received_sequence - sequence_number;
    if (distance_back > PROTOCOL_ACK_BITFIELD_BITS) {
        /* Too far behind the tracked window to know for sure -- treat as
         * already delivered rather than risk a re-delivery, per this
         * header's own "Receive side" doc comment. */
        return false;
    }
    uint32_t bit_mask = 1U << (distance_back - 1);
    if ((channel->received_bitfield & bit_mask) != 0) {
        return false;
    }
    channel->received_bitfield |= bit_mask;
    return true;
}

AckMessage reliable_make_ack(const ReliableChannel *channel)
{
    return (AckMessage){.ack_seq = channel->highest_received_sequence, .ack_bitfield = channel->received_bitfield};
}
