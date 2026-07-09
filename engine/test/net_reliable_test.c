#include "fff.h"
#include "unity.h"

#include "../src/error.c"        // NOLINT(bugprone-suspicious-include)
#include "../src/net_loopback.c" // NOLINT(bugprone-suspicious-include)
#include "../src/net_protocol.c" // NOLINT(bugprone-suspicious-include)
#include "../src/net_reliable.c" // NOLINT(bugprone-suspicious-include)
#include "../src/strv.c"         // NOLINT(bugprone-suspicious-include)

DEFINE_FFF_GLOBALS;

#include "test_heap_alloc.h"

void setUp(void) {}
void tearDown(void) {}

static LoopbackNetwork make_loopback_network(void)
{
    LoopbackNetwork network;
    loopback_network_init(&network, &test_heap_alloc);
    return network;
}

/* ---- Resend-until-acked ----
 *
 * reliable_send queues seq 0 and transmits it once; the test drains and
 * discards that original copy from the receiver's own inbox WITHOUT ever
 * feeding it to reliable_on_receive -- this models the packet never
 * reaching the game (S8.4c's brief calls this "simulate loss by not
 * delivering it", no real transport-level drop needed). reliable_tick past
 * RELIABLE_RESEND_SECONDS then retransmits the exact same seq; only THAT
 * copy is ever handed to reliable_on_receive, proving the event is
 * delivered (once) despite the simulated loss. The receiver's ack then
 * clears the sender's slot, and a further reliable_tick produces no more
 * traffic -- proving resend stops on its own once acked. */
void test_resend_until_acked_then_ack_stops_further_resends(void)
{
    LoopbackNetwork loopback = make_loopback_network();
    NetAddr sender_addr = net_addr_make(1, 9000);
    NetAddr receiver_addr = net_addr_make(2, 9001);
    NetTransport sender_transport;
    NetTransport receiver_transport;
    TEST_ASSERT_TRUE(loopback_transport_create(&loopback, sender_addr, &sender_transport));
    TEST_ASSERT_TRUE(loopback_transport_create(&loopback, receiver_addr, &receiver_transport));

    ReliableChannel sender_channel = {0};
    EventRecord event = {.event_type = 42, .entity_id = 7, .argument = (Strv){0}};
    reliable_send(&sender_channel, &sender_transport, receiver_addr, event);
    TEST_ASSERT_TRUE(sender_channel.send_window[0].awaiting_ack);
    TEST_ASSERT_EQUAL_UINT32(0, sender_channel.send_window[0].sequence_number);

    /* Simulate loss: the original transmission sits in the receiver's
     * inbox; drain and discard it rather than delivering it to the game. */
    uint8_t discard_buffer[NET_MAX_PACKET_SIZE];
    NetAddr discard_src = {0};
    TEST_ASSERT_TRUE(net_recv(&receiver_transport, &discard_src, discard_buffer, sizeof(discard_buffer)) > 0);
    TEST_ASSERT_EQUAL_INT(0, net_recv(&receiver_transport, &discard_src, discard_buffer, sizeof(discard_buffer)));

    /* Age past the resend timer -> the SAME event (seq 0) is retransmitted. */
    reliable_tick(&sender_channel, &sender_transport, receiver_addr, RELIABLE_RESEND_SECONDS + 0.01F);
    TEST_ASSERT_TRUE(sender_channel.send_window[0].awaiting_ack);

    uint8_t buffer[NET_MAX_PACKET_SIZE];
    NetAddr src = {0};
    int received = net_recv(&receiver_transport, &src, buffer, sizeof(buffer));
    TEST_ASSERT_TRUE(received > 0);
    DecodedPacket packet;
    ErrorState decode_err = {0};
    TEST_ASSERT_TRUE(protocol_decode_packet(buffer, (size_t)received, &packet, &decode_err));
    TEST_ASSERT_EQUAL_INT(MSG_EVENT, packet.header.type);
    TEST_ASSERT_EQUAL_UINT32(0, packet.header.seq);

    ReliableChannel receiver_channel = {0};
    TEST_ASSERT_TRUE(reliable_on_receive(&receiver_channel, packet.header.seq));

    AckMessage ack = reliable_make_ack(&receiver_channel);
    reliable_on_ack(&sender_channel, ack);
    TEST_ASSERT_FALSE(sender_channel.send_window[0].awaiting_ack);

    /* A further tick must not resend an already-acked slot. */
    reliable_tick(&sender_channel, &sender_transport, receiver_addr, RELIABLE_RESEND_SECONDS + 0.01F);
    TEST_ASSERT_EQUAL_INT(0, net_recv(&receiver_transport, &src, buffer, sizeof(buffer)));

    loopback_network_free(&loopback);
}

/* ---- Dedup ---- */

void test_reliable_on_receive_dedups_same_sequence(void)
{
    ReliableChannel channel = {0};
    TEST_ASSERT_TRUE(reliable_on_receive(&channel, 5));
    TEST_ASSERT_FALSE(reliable_on_receive(&channel, 5));
}

void test_reliable_on_receive_first_call_is_always_new(void)
{
    ReliableChannel channel = {0};
    /* Even sequence 0 (the zero-init default) must be reported as new on
     * the very first call -- has_received_any is what disambiguates
     * "never received anything" from "received seq 0 already". */
    TEST_ASSERT_TRUE(reliable_on_receive(&channel, 0));
    TEST_ASSERT_FALSE(reliable_on_receive(&channel, 0));
}

/* ---- Ack bitfield / out-of-order ---- */

void test_reliable_on_receive_out_of_order_fills_gap(void)
{
    ReliableChannel channel = {0};
    TEST_ASSERT_TRUE(reliable_on_receive(&channel, 10)); /* N */
    TEST_ASSERT_TRUE(reliable_on_receive(&channel, 12)); /* N+2; N+1 (11) still missing */
    TEST_ASSERT_EQUAL_UINT32(12, channel.highest_received_sequence);
    /* bit1 (seq 10 = 12-1-1) set; bit0 (seq 11 = 12-1-0) clear -- the gap. */
    TEST_ASSERT_EQUAL_UINT32(0x2, channel.received_bitfield);

    TEST_ASSERT_TRUE(reliable_on_receive(&channel, 11)); /* N+1 arrives late */
    TEST_ASSERT_EQUAL_UINT32(0x3, channel.received_bitfield);
    TEST_ASSERT_FALSE(reliable_on_receive(&channel, 11)); /* now a duplicate */
}

void test_reliable_on_ack_partial_bitfield_clears_only_covered_seqs(void)
{
    NetTransport null_transport = {0};
    NetAddr dest = net_addr_make(9, 9000);
    ReliableChannel sender = {0};
    reliable_send(&sender, &null_transport, dest, (EventRecord){.event_type = 1, .entity_id = 10}); /* seq 0 = N */
    reliable_send(&sender, &null_transport, dest, (EventRecord){.event_type = 1, .entity_id = 11}); /* seq 1 = N+1 */
    reliable_send(&sender, &null_transport, dest, (EventRecord){.event_type = 1, .entity_id = 12}); /* seq 2 = N+2 */

    /* Receiver has only seen seq 0 and seq 2 so far -- seq 1 still lost. */
    ReliableChannel receiver = {0};
    TEST_ASSERT_TRUE(reliable_on_receive(&receiver, 0));
    TEST_ASSERT_TRUE(reliable_on_receive(&receiver, 2));
    AckMessage ack = reliable_make_ack(&receiver);

    reliable_on_ack(&sender, ack);
    TEST_ASSERT_FALSE(sender.send_window[0].awaiting_ack); /* N acked */
    TEST_ASSERT_TRUE(sender.send_window[1].awaiting_ack);  /* N+1 still pending */
    TEST_ASSERT_FALSE(sender.send_window[2].awaiting_ack); /* N+2 acked */
}

/* ---- Window bound ---- */

void test_reliable_window_bound_evicts_oldest_without_crash(void)
{
    NetTransport null_transport = {0};
    NetAddr dest = net_addr_make(9, 9000);
    ReliableChannel channel = {0};
    for (int index = 0; index < RELIABLE_WINDOW + 1; index++) {
        reliable_send(&channel, &null_transport, dest, (EventRecord){.event_type = index, .entity_id = index});
    }
    /* Sequence RELIABLE_WINDOW (the 65th send) maps to the same slot as
     * sequence 0 and evicts it -- documented backpressure, not a crash. */
    TEST_ASSERT_EQUAL_UINT32((uint32_t)RELIABLE_WINDOW, channel.send_window[0].sequence_number);
    TEST_ASSERT_TRUE(channel.send_window[0].awaiting_ack);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(RELIABLE_WINDOW + 1), channel.next_send_sequence);
}

int main(void)
{
    test_helpers_init();
    UNITY_BEGIN();
    RUN_TEST(test_resend_until_acked_then_ack_stops_further_resends);
    RUN_TEST(test_reliable_on_receive_dedups_same_sequence);
    RUN_TEST(test_reliable_on_receive_first_call_is_always_new);
    RUN_TEST(test_reliable_on_receive_out_of_order_fills_gap);
    RUN_TEST(test_reliable_on_ack_partial_bitfield_clears_only_covered_seqs);
    RUN_TEST(test_reliable_window_bound_evicts_oldest_without_crash);
    return UNITY_END();
}
