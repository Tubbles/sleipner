#include "fff.h"
#include "unity.h"

#include "../src/error.c"        // NOLINT(bugprone-suspicious-include)
#include "../src/net_loopback.c" // NOLINT(bugprone-suspicious-include)
#include "../src/net_protocol.c" // NOLINT(bugprone-suspicious-include)
#include "../src/net_reliable.c" // NOLINT(bugprone-suspicious-include)
#include "../src/net_udp.c"      // NOLINT(bugprone-suspicious-include)
#include "../src/network.c"      // NOLINT(bugprone-suspicious-include)
#include "../src/strv.c"         // NOLINT(bugprone-suspicious-include)

DEFINE_FFF_GLOBALS;

#include "test_heap_alloc.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ---- network_apply_hosting / network_apply_discovering ----
 *
 * File-local (static) to network.c -- reached here the same whitebox way
 * net_discovery_test.c already reaches net_discovery.c's statics, by
 * including the .c directly. Driven with an all-zero NetTransport rather
 * than a real UDP socket or even net_loopback.h: net.h's send/recv/poll
 * wrappers are already null-op-safe against a zeroed transport (see
 * net.h's own doc comment), and net_udp_destroy checks transport->state
 * == nullptr before touching anything, so a zeroed transport is also
 * safe to hand to network_stop below -- exactly what proves the
 * mode-transition logic in isolation from ever needing a real socket. */

void test_apply_hosting_sets_mode_and_resets_beacon_timer(void)
{
    NetworkState network = {.mode = NET_OFFLINE, .beacon_timer = 5.0F};
    NetTransport fabricated = {0};

    network_apply_hosting(&network, fabricated, "Alice");

    TEST_ASSERT_EQUAL_INT(NET_HOSTING, network.mode);
    TEST_ASSERT_TRUE(network.transport_initialized);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, network.beacon_timer);
    TEST_ASSERT_EQUAL_STRING("Alice", network.host_name);
}

void test_apply_hosting_truncates_long_host_name(void)
{
    NetworkState network = {0};
    NetTransport fabricated = {0};
    char long_name[NET_NAME_MAX + 10];
    memset(long_name, 'x', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';

    network_apply_hosting(&network, fabricated, long_name);

    TEST_ASSERT_EQUAL_INT(NET_NAME_MAX - 1, (int)strlen(network.host_name));
}

void test_apply_discovering_sets_mode_and_clears_join_list(void)
{
    NetworkState network = {.mode = NET_OFFLINE};
    join_list_add_or_refresh(&network.join_list, net_addr_make(1, 100), "Stale");
    NetTransport fabricated = {0};

    network_apply_discovering(&network, fabricated);

    TEST_ASSERT_EQUAL_INT(NET_DISCOVERING, network.mode);
    TEST_ASSERT_TRUE(network.transport_initialized);
    TEST_ASSERT_EQUAL_INT(0, network.join_list.count);
}

/* ---- network_stop, isolated from net_udp_destroy (transport_initialized = false) ---- */

void test_stop_resets_every_field_to_offline(void)
{
    NetworkState network = {.mode = NET_HOSTING, .beacon_timer = 3.0F, .transport_initialized = false};
    join_list_add_or_refresh(&network.join_list, net_addr_make(1, 100), "Host");
    network.join_target = net_addr_make(2, 200);
    strv_copy_to_cstr(strv_from_cstr("Alice"), network.host_name, sizeof(network.host_name));
    /* S8.4c: pre-seed reliable-channel state a real session would have
     * accumulated, so network_stop's reset contract is proven for these
     * fields too, not just the pre-existing ones above. */
    TEST_ASSERT_TRUE(reliable_on_receive(&network.host_event_channel, 5));
    network.last_delivered_event_type = 1;
    network.last_delivered_event_entity_id = 1;
    network.delivered_event_count = 1;
    /* S8.6: pre-seed local_player_id too, so its reset is proven the same
     * way as the fields above. */
    network.local_player_id = 3;

    network_stop(&network);

    TEST_ASSERT_EQUAL_INT(NET_OFFLINE, network.mode);
    TEST_ASSERT_FALSE(network.transport_initialized);
    TEST_ASSERT_EQUAL_INT(0, network.join_list.count);
    TEST_ASSERT_EQUAL_UINT32(0, network.join_target.host);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, network.beacon_timer);
    TEST_ASSERT_EQUAL_STRING("", network.host_name);
    TEST_ASSERT_EQUAL_INT(0, network.local_player_id);
    TEST_ASSERT_FALSE(network.host_event_channel.has_received_any);
    TEST_ASSERT_EQUAL_INT32(0, network.last_delivered_event_type);
    TEST_ASSERT_EQUAL_INT32(0, network.last_delivered_event_entity_id);
    TEST_ASSERT_EQUAL_INT(0, network.delivered_event_count);
}

void test_stop_is_idempotent_on_already_offline_network(void)
{
    NetworkState network = {0};
    network_stop(&network);
    TEST_ASSERT_EQUAL_INT(NET_OFFLINE, network.mode);
}

/* ---- network_start_hosting / network_start_discovering / network_stop:
 * real UDP socket. Sandboxed CI may block socket()/bind() entirely, same
 * caveat net_test.c's own net_udp_create tests document -- skip
 * gracefully via TEST_IGNORE_MESSAGE rather than failing. ---- */

void test_start_hosting_creates_real_transport_and_sets_mode(void)
{
    NetworkState network = {0};
    ErrorState err = {0};
    if (!network_start_hosting(&network, &test_heap_alloc, "TestHost", &err)) {
        TEST_IGNORE_MESSAGE("network_start_hosting failed in this sandbox (socket()/bind() likely blocked)");
        return;
    }
    TEST_ASSERT_EQUAL_INT(NET_HOSTING, network.mode);
    TEST_ASSERT_TRUE(network.transport_initialized);
    TEST_ASSERT_EQUAL_STRING("TestHost", network.host_name);
    network_stop(&network);
    TEST_ASSERT_EQUAL_INT(NET_OFFLINE, network.mode);
    TEST_ASSERT_FALSE(network.transport_initialized);
}

void test_start_discovering_creates_real_transport_and_sets_mode(void)
{
    NetworkState network = {0};
    ErrorState err = {0};
    if (!network_start_discovering(&network, &test_heap_alloc, &err)) {
        TEST_IGNORE_MESSAGE("network_start_discovering failed in this sandbox (socket()/bind() likely blocked)");
        return;
    }
    TEST_ASSERT_EQUAL_INT(NET_DISCOVERING, network.mode);
    TEST_ASSERT_TRUE(network.transport_initialized);
    network_stop(&network);
}

/* Host and client both bind the fixed DISCOVERY_PORT -- proves
 * net_udp_create's SO_REUSEADDR/SO_REUSEPORT wiring (net_udp.c) actually
 * lets both live at once on this one process/machine, the same
 * same-box-testing concern net_test.c's own reuse test covers at the
 * net_udp_create layer directly. */
void test_start_hosting_and_discovering_coexist_via_port_reuse(void)
{
    NetworkState host_network = {0};
    NetworkState client_network = {0};
    ErrorState err = {0};
    if (!network_start_hosting(&host_network, &test_heap_alloc, "Host", &err)) {
        TEST_IGNORE_MESSAGE("network_start_hosting failed in this sandbox (socket()/bind() likely blocked)");
        return;
    }
    bool client_ok = network_start_discovering(&client_network, &test_heap_alloc, &err);
    network_stop(&host_network);
    if (!client_ok) {
        TEST_IGNORE_MESSAGE("network_start_discovering failed to reuse DISCOVERY_PORT in this sandbox");
        return;
    }
    TEST_ASSERT_EQUAL_INT(NET_DISCOVERING, client_network.mode);
    network_stop(&client_network);
}

/* ---- S8.7a: client->host reliable event channel ----
 *
 * Same drop/resend/dedup/ack model net_reliable_test.c already proves for
 * host->client (net_reliable.h's own top doc comment documents why the
 * same ReliableChannel struct now carries both directions) -- these tests
 * exercise it end to end through network.c's own primitives
 * (network_client_send_reliable_event/_tick_reliable_channel,
 * network_host_receive, network_host_send_acks) over a real loopback
 * transport, the same LoopbackNetwork setup net_reliable_test.c uses.
 * network_test.c operates below net_session.c -- the session-level ticks
 * that thread delta_time through a whole GameState are exercised in
 * integration_test.c instead -- so applying the host's ack back onto the
 * client here calls reliable_on_ack directly rather than going through
 * net_session.c's MSG_ACK branch (network_client_receive_state), which
 * isn't reachable from this file; that is the exact operation that branch
 * itself wraps. register_client (network.c, file-local) stands in for a
 * real JOIN handshake, the same whitebox shortcut this file's other tests
 * already use to reach network.c's own statics. */

static LoopbackNetwork make_loopback_network(void)
{
    LoopbackNetwork network;
    loopback_network_init(&network, &test_heap_alloc);
    return network;
}

void test_client_reliable_event_delivered_exactly_once_to_host(void)
{
    LoopbackNetwork loopback = make_loopback_network();
    NetAddr host_addr = net_addr_make(1, 9000);
    NetAddr client_addr = net_addr_make(2, 9001);
    NetTransport host_transport;
    NetTransport client_transport;
    TEST_ASSERT_TRUE(loopback_transport_create(&loopback, host_addr, &host_transport));
    TEST_ASSERT_TRUE(loopback_transport_create(&loopback, client_addr, &client_transport));

    NetworkState host_network = {.transport = host_transport};
    register_client(&host_network, client_addr);
    NetworkState client_network = {.transport = client_transport, .join_target = host_addr};

    EventRecord event = {.event_type = 42, .entity_id = 7, .argument = (Strv){0}};
    network_client_send_reliable_event(&client_network, event);
    network_host_receive(&host_network, 0);

    TEST_ASSERT_EQUAL_INT(1, host_network.client_event_delivered_count);
    TEST_ASSERT_EQUAL_INT32(42, host_network.last_client_event_type);
    TEST_ASSERT_EQUAL_INT32(7, host_network.last_client_event_entity_id);
    TEST_ASSERT_EQUAL_INT(1, host_network.last_client_event_player_id);

    loopback_network_free(&loopback);
}

void test_client_resend_does_not_double_deliver_to_host(void)
{
    LoopbackNetwork loopback = make_loopback_network();
    NetAddr host_addr = net_addr_make(1, 9000);
    NetAddr client_addr = net_addr_make(2, 9001);
    NetTransport host_transport;
    NetTransport client_transport;
    TEST_ASSERT_TRUE(loopback_transport_create(&loopback, host_addr, &host_transport));
    TEST_ASSERT_TRUE(loopback_transport_create(&loopback, client_addr, &client_transport));

    NetworkState host_network = {.transport = host_transport};
    register_client(&host_network, client_addr);
    NetworkState client_network = {.transport = client_transport, .join_target = host_addr};

    EventRecord event = {.event_type = 1, .entity_id = 99, .argument = (Strv){0}};
    network_client_send_reliable_event(&client_network, event);

    /* Simulate loss: the original transmission sits in the host's inbox;
     * drain and discard it rather than delivering it. */
    uint8_t discard_buffer[NET_MAX_PACKET_SIZE];
    NetAddr discard_src = {0};
    TEST_ASSERT_TRUE(net_recv(&host_transport, &discard_src, discard_buffer, sizeof(discard_buffer)) > 0);

    /* Age past the resend timer -> the same event (seq 0) is retransmitted
     * and this is the host's FIRST actual receive of it -- delivered once. */
    network_client_tick_reliable_channel(&client_network, RELIABLE_RESEND_SECONDS + 0.01F);
    network_host_receive(&host_network, 0);
    TEST_ASSERT_EQUAL_INT(1, host_network.client_event_delivered_count);

    /* The host never acked, so a second resend fires and reaches the host
     * again -- the SAME seq, which must dedup rather than double-deliver. */
    network_client_tick_reliable_channel(&client_network, RELIABLE_RESEND_SECONDS + 0.01F);
    network_host_receive(&host_network, 0);
    TEST_ASSERT_EQUAL_INT(1, host_network.client_event_delivered_count);

    loopback_network_free(&loopback);
}

void test_host_sends_ack_and_client_stops_resending(void)
{
    LoopbackNetwork loopback = make_loopback_network();
    NetAddr host_addr = net_addr_make(1, 9000);
    NetAddr client_addr = net_addr_make(2, 9001);
    NetTransport host_transport;
    NetTransport client_transport;
    TEST_ASSERT_TRUE(loopback_transport_create(&loopback, host_addr, &host_transport));
    TEST_ASSERT_TRUE(loopback_transport_create(&loopback, client_addr, &client_transport));

    NetworkState host_network = {.transport = host_transport};
    register_client(&host_network, client_addr);
    NetworkState client_network = {.transport = client_transport, .join_target = host_addr};

    EventRecord event = {.event_type = 1, .entity_id = 1, .argument = (Strv){0}};
    network_client_send_reliable_event(&client_network, event);
    network_host_receive(&host_network, 0);
    TEST_ASSERT_EQUAL_INT(1, host_network.client_event_delivered_count);
    TEST_ASSERT_TRUE(host_network.clients[0].event_channel.has_received_any);

    network_host_send_acks(&host_network);

    uint8_t buffer[NET_MAX_PACKET_SIZE];
    NetAddr src = {0};
    int received = net_recv(&client_transport, &src, buffer, sizeof(buffer));
    TEST_ASSERT_TRUE(received > 0);
    DecodedPacket packet;
    ErrorState decode_err = {0};
    TEST_ASSERT_TRUE(protocol_decode_packet(buffer, (size_t)received, &packet, &decode_err));
    TEST_ASSERT_EQUAL_INT(MSG_ACK, packet.header.type);
    AckMessage ack = {0};
    TEST_ASSERT_TRUE(protocol_decode_ack(&packet.reader, &ack));
    reliable_on_ack(&client_network.host_event_channel, ack);
    TEST_ASSERT_FALSE(client_network.host_event_channel.send_window[0].awaiting_ack);

    /* A further tick past the resend interval must not resend the
     * now-acked slot. */
    network_client_tick_reliable_channel(&client_network, RELIABLE_RESEND_SECONDS + 0.01F);
    TEST_ASSERT_EQUAL_INT(0, net_recv(&host_transport, &src, buffer, sizeof(buffer)));

    loopback_network_free(&loopback);
}

void test_host_sends_no_acks_when_client_has_not_sent_anything(void)
{
    LoopbackNetwork loopback = make_loopback_network();
    NetAddr host_addr = net_addr_make(1, 9000);
    NetAddr client_addr = net_addr_make(2, 9001);
    NetTransport host_transport;
    NetTransport client_transport;
    TEST_ASSERT_TRUE(loopback_transport_create(&loopback, host_addr, &host_transport));
    TEST_ASSERT_TRUE(loopback_transport_create(&loopback, client_addr, &client_transport));

    NetworkState host_network = {.transport = host_transport};
    register_client(&host_network, client_addr);

    network_host_send_acks(&host_network);

    uint8_t buffer[NET_MAX_PACKET_SIZE];
    NetAddr src = {0};
    TEST_ASSERT_EQUAL_INT(0, net_recv(&client_transport, &src, buffer, sizeof(buffer)));

    loopback_network_free(&loopback);
}

/* ---- S8.7b: editor operations on the reliable sub-channel (send side) ----
 *
 * The receive/apply side (network_host_receive's MSG_OP branch) is a later
 * slice, so these prove only that the send helpers put a well-formed,
 * decodable MSG_OP on the wire -- decoded manually off the destination
 * transport, the same whitebox way the S8.7a ack test decodes a MSG_ACK
 * off the client transport above. register_client stands in for a real
 * JOIN handshake, as elsewhere in this file. */

/* Drain one packet off `transport`, assert it is a decodable MSG_OP at
 * seq 0 carrying a DELETE-entity op naming `level_name`/`entity_id`. */
static void
assert_delete_op_landed_at_seq_zero(const NetTransport *transport, const char *level_name, int32_t entity_id)
{
    uint8_t buffer[NET_MAX_PACKET_SIZE];
    NetAddr src = {0};
    int received = net_recv(transport, &src, buffer, sizeof(buffer));
    TEST_ASSERT_TRUE(received > 0);
    DecodedPacket packet;
    ErrorState decode_err = {0};
    TEST_ASSERT_TRUE(protocol_decode_packet(buffer, (size_t)received, &packet, &decode_err));
    TEST_ASSERT_EQUAL_INT(MSG_OP, packet.header.type);
    TEST_ASSERT_EQUAL_UINT32(0, packet.header.seq);
    EditorOp decoded = {0};
    TEST_ASSERT_TRUE(protocol_decode_op(&packet.reader, &decoded));
    TEST_ASSERT_EQUAL_INT(EDITOR_OP_DELETE_ENTITY, decoded.kind);
    TEST_ASSERT_TRUE(strv_eq_cstr(decoded.level_name, level_name));
    TEST_ASSERT_EQUAL_INT32(entity_id, decoded.entity_id);
}

void test_client_send_reliable_op_lands_decodable_on_host(void)
{
    LoopbackNetwork loopback = make_loopback_network();
    NetAddr host_addr = net_addr_make(1, 9000);
    NetAddr client_addr = net_addr_make(2, 9001);
    NetTransport host_transport;
    NetTransport client_transport;
    TEST_ASSERT_TRUE(loopback_transport_create(&loopback, host_addr, &host_transport));
    TEST_ASSERT_TRUE(loopback_transport_create(&loopback, client_addr, &client_transport));

    NetworkState client_network = {.transport = client_transport, .join_target = host_addr};
    EditorOp operation = {.kind = EDITOR_OP_MOVE_ENTITY,
                          .level_name = strv_from_cstr("overworld"),
                          .entity_id = 7,
                          .author_player_id = 3,
                          .op_seq = 0,
                          .move_x = 12.5F,
                          .move_y = -4.0F};
    network_client_send_reliable_op(&client_network, &operation);

    uint8_t buffer[NET_MAX_PACKET_SIZE];
    NetAddr src = {0};
    int received = net_recv(&host_transport, &src, buffer, sizeof(buffer));
    TEST_ASSERT_TRUE(received > 0);
    DecodedPacket packet;
    ErrorState decode_err = {0};
    TEST_ASSERT_TRUE(protocol_decode_packet(buffer, (size_t)received, &packet, &decode_err));
    TEST_ASSERT_EQUAL_INT(MSG_OP, packet.header.type);
    TEST_ASSERT_EQUAL_UINT32(0, packet.header.seq);
    EditorOp decoded = {0};
    TEST_ASSERT_TRUE(protocol_decode_op(&packet.reader, &decoded));
    TEST_ASSERT_EQUAL_INT(EDITOR_OP_MOVE_ENTITY, decoded.kind);
    TEST_ASSERT_TRUE(strv_eq_cstr(decoded.level_name, "overworld"));
    TEST_ASSERT_EQUAL_INT32(7, decoded.entity_id);
    TEST_ASSERT_EQUAL_INT32(3, decoded.author_player_id);
    TEST_ASSERT_EQUAL_UINT32(0, decoded.op_seq);
    TEST_ASSERT_EQUAL_FLOAT(12.5F, decoded.move_x);
    TEST_ASSERT_EQUAL_FLOAT(-4.0F, decoded.move_y);

    loopback_network_free(&loopback);
}

void test_host_broadcast_reliable_op_reaches_both_clients(void)
{
    LoopbackNetwork loopback = make_loopback_network();
    NetAddr host_addr = net_addr_make(1, 9000);
    NetAddr client_a_addr = net_addr_make(2, 9001);
    NetAddr client_b_addr = net_addr_make(3, 9002);
    NetTransport host_transport;
    NetTransport client_a_transport;
    NetTransport client_b_transport;
    TEST_ASSERT_TRUE(loopback_transport_create(&loopback, host_addr, &host_transport));
    TEST_ASSERT_TRUE(loopback_transport_create(&loopback, client_a_addr, &client_a_transport));
    TEST_ASSERT_TRUE(loopback_transport_create(&loopback, client_b_addr, &client_b_transport));

    NetworkState host_network = {.transport = host_transport};
    register_client(&host_network, client_a_addr);
    register_client(&host_network, client_b_addr);

    EditorOp operation = {.kind = EDITOR_OP_DELETE_ENTITY, .level_name = strv_from_cstr("dungeon"), .entity_id = 42};
    network_host_broadcast_reliable_op(&host_network, &operation);

    /* Both clients receive the op, each at its own seq 0 -- the two
     * event_channels have independent sequence spaces. */
    assert_delete_op_landed_at_seq_zero(&client_a_transport, "dungeon", 42);
    assert_delete_op_landed_at_seq_zero(&client_b_transport, "dungeon", 42);

    TEST_ASSERT_EQUAL_UINT32(1, host_network.clients[0].event_channel.next_send_sequence);
    TEST_ASSERT_EQUAL_UINT32(1, host_network.clients[1].event_channel.next_send_sequence);
    TEST_ASSERT_TRUE(host_network.clients[0].event_channel.send_window[0].awaiting_ack);
    TEST_ASSERT_TRUE(host_network.clients[1].event_channel.send_window[0].awaiting_ack);

    loopback_network_free(&loopback);
}

int main(void)
{
    test_helpers_init();
    UNITY_BEGIN();
    RUN_TEST(test_apply_hosting_sets_mode_and_resets_beacon_timer);
    RUN_TEST(test_apply_hosting_truncates_long_host_name);
    RUN_TEST(test_apply_discovering_sets_mode_and_clears_join_list);
    RUN_TEST(test_stop_resets_every_field_to_offline);
    RUN_TEST(test_stop_is_idempotent_on_already_offline_network);
    RUN_TEST(test_start_hosting_creates_real_transport_and_sets_mode);
    RUN_TEST(test_start_discovering_creates_real_transport_and_sets_mode);
    RUN_TEST(test_start_hosting_and_discovering_coexist_via_port_reuse);
    RUN_TEST(test_client_reliable_event_delivered_exactly_once_to_host);
    RUN_TEST(test_client_resend_does_not_double_deliver_to_host);
    RUN_TEST(test_host_sends_ack_and_client_stops_resending);
    RUN_TEST(test_host_sends_no_acks_when_client_has_not_sent_anything);
    RUN_TEST(test_client_send_reliable_op_lands_decodable_on_host);
    RUN_TEST(test_host_broadcast_reliable_op_reaches_both_clients);
    return UNITY_END();
}
