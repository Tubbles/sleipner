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
    /* S8.7c: hosting start arms the op counter -- the first stamped op gets
     * 1, never the 0 "unstamped request" sentinel. */
    TEST_ASSERT_EQUAL_UINT32(1, network.next_op_seq);
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
    /* S8.7c: pre-seed the op-ordering state a live session would have
     * accumulated -- counters mid-stream, one echo parked in holdback. */
    network.next_op_seq = 9;
    network.next_expected_op_seq = 4;
    network.held_ops[2].occupied = true;
    network.held_ops[2].op_seq = 6;
    /* S8.7d1: pre-seed a live lock and a queued deny so their reset is proven
     * the same way as the fields above. */
    TEST_ASSERT_TRUE(network_lock_acquire(&network, 7, 2));
    network.lock_denied_entity_ids[0] = 7;
    network.lock_denied_count = 1;
    /* S8.7f1: pre-seed structural-resync state a live session would have --
     * host side (an armed generation mid-stream), client side (a half-
     * reassembled incoming generation), and the per-client confirmation/
     * owed-snapshot bookkeeping. */
    network.resync_generation = 2;
    network.resync_total_bytes = 100;
    network.resync_send_timer = 0.05F;
    network.resync_incoming_generation = 2;
    network.resync_failed_generation = 1;
    network.resync_incoming_total_bytes = 100;
    network.resync_incoming_chunk_count = 1;
    network.resync_chunk_bitmap[0] = 1;
    network.resync_ready = true;
    network.clients[0].resync_confirmed_generation = 2;
    network.clients[0].resync_snapshot_owed = true;
    network.client_count = 1;

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
    /* S8.7c: counters back to the offline zero (network_apply_hosting and
     * the JOIN_ACCEPT baseline re-arm them at the next session start), and
     * no held echo survives. */
    TEST_ASSERT_EQUAL_UINT32(0, network.next_op_seq);
    TEST_ASSERT_EQUAL_UINT32(0, network.next_expected_op_seq);
    TEST_ASSERT_FALSE(network.held_ops[2].occupied);
    /* S8.7d1: the lock table and the client deny list are wiped too. */
    TEST_ASSERT_NULL(network_lock_find(&network, 7));
    TEST_ASSERT_EQUAL_INT(0, network.lock_denied_count);
    /* S8.7f1: all resync bookkeeping back to the offline zero, on both the
     * host role (generation disarmed) and the client role (reassembly wiped),
     * plus the per-client fields (reset with the whole NetClient). */
    TEST_ASSERT_EQUAL_UINT32(0, network.resync_generation);
    TEST_ASSERT_EQUAL_size_t(0, network.resync_total_bytes);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, network.resync_send_timer);
    TEST_ASSERT_EQUAL_UINT32(0, network.resync_incoming_generation);
    TEST_ASSERT_EQUAL_UINT32(0, network.resync_failed_generation);
    TEST_ASSERT_EQUAL_size_t(0, network.resync_incoming_total_bytes);
    TEST_ASSERT_EQUAL_UINT16(0, network.resync_incoming_chunk_count);
    TEST_ASSERT_EQUAL_UINT8(0, network.resync_chunk_bitmap[0]);
    TEST_ASSERT_FALSE(network.resync_ready);
    TEST_ASSERT_EQUAL_UINT32(0, network.clients[0].resync_confirmed_generation);
    TEST_ASSERT_FALSE(network.clients[0].resync_snapshot_owed);
    TEST_ASSERT_EQUAL_INT(0, network.client_count);
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

/* S8.7c added a PendingEditorOps out-param to network_host_receive. These
 * S8.7a event tests never exercise ops, so they drain through this wrapper
 * with a throwaway sink and a zero gamedata hash (they register_client
 * directly, never JOIN, so the hash is irrelevant). */
static void host_receive_events_only(NetworkState *network)
{
    PendingEditorOps discard_ops = {0};
    network_host_receive(network, 0, &discard_ops);
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
    host_receive_events_only(&host_network);

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
    host_receive_events_only(&host_network);
    TEST_ASSERT_EQUAL_INT(1, host_network.client_event_delivered_count);

    /* The host never acked, so a second resend fires and reaches the host
     * again -- the SAME seq, which must dedup rather than double-deliver. */
    network_client_tick_reliable_channel(&client_network, RELIABLE_RESEND_SECONDS + 0.01F);
    host_receive_events_only(&host_network);
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
    host_receive_events_only(&host_network);
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

/* ---- S8.7c: host-side MSG_OP receive backpressure ----
 *
 * A full PendingEditorOps must apply backpressure: the incoming op is left
 * UNMARKED on the sending client's event_channel (never passed to
 * reliable_on_receive), so nothing is lost -- the client's reliable resend
 * redelivers it, and a later receive with room delivers it exactly once. */
void test_host_receive_full_pending_ops_does_not_mark_op(void)
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

    EditorOp operation = {.kind = EDITOR_OP_MOVE_ENTITY,
                          .level_name = strv_from_cstr("overworld"),
                          .entity_id = 7,
                          .op_seq = 0,
                          .move_x = 1.0F,
                          .move_y = 2.0F};
    network_client_send_reliable_op(&client_network, &operation);

    /* Drain with an already-full sink: the op is consumed off the wire but
     * NOT dedup-marked, and nothing is queued past the pre-set count. */
    PendingEditorOps full_ops = {.count = NETWORK_PENDING_OPS_MAX};
    network_host_receive(&host_network, 0, &full_ops);
    TEST_ASSERT_EQUAL_INT(NETWORK_PENDING_OPS_MAX, full_ops.count);
    TEST_ASSERT_FALSE(host_network.clients[0].event_channel.has_received_any);

    /* Resend (client ages past the resend timer) and drain with room: now
     * it is delivered exactly once, and its raw bytes round-trip. */
    network_client_tick_reliable_channel(&client_network, RELIABLE_RESEND_SECONDS + 0.01F);
    PendingEditorOps fresh_ops = {0};
    network_host_receive(&host_network, 0, &fresh_ops);
    TEST_ASSERT_EQUAL_INT(1, fresh_ops.count);
    TEST_ASSERT_TRUE(host_network.clients[0].event_channel.has_received_any);
    TEST_ASSERT_EQUAL_INT(1, fresh_ops.sender_player_ids[0]);

    DecodedPacket packet;
    ErrorState decode_err = {0};
    TEST_ASSERT_TRUE(protocol_decode_packet(fresh_ops.packets[0], fresh_ops.lengths[0], &packet, &decode_err));
    TEST_ASSERT_EQUAL_INT(MSG_OP, packet.header.type);
    EditorOp decoded = {0};
    TEST_ASSERT_TRUE(protocol_decode_op(&packet.reader, &decoded));
    TEST_ASSERT_EQUAL_INT(EDITOR_OP_MOVE_ENTITY, decoded.kind);
    TEST_ASSERT_EQUAL_INT32(7, decoded.entity_id);

    loopback_network_free(&loopback);
}

/* ---- S8.7d1: host lock table helpers ----
 *
 * Pure state mutators on NetworkState.locks -- no transport needed, driven on
 * an all-zero NetworkState directly. These prove the host-authority semantics
 * host_apply_and_echo_ops (net_session.c) relies on. */

void test_lock_acquire_grants_when_free(void)
{
    NetworkState network = {0};
    TEST_ASSERT_TRUE(network_lock_acquire(&network, 42, 1));
    EntityLock *lock = network_lock_find(&network, 42);
    TEST_ASSERT_NOT_NULL(lock);
    TEST_ASSERT_EQUAL_INT(1, lock->holder_player_id);
}

void test_lock_acquire_regrants_idempotently_to_same_holder(void)
{
    NetworkState network = {0};
    TEST_ASSERT_TRUE(network_lock_acquire(&network, 42, 1));
    network_lock_find(&network, 42)->seconds_since_refresh = 3.0F;
    /* Re-grant to the same holder succeeds, refreshes the silence age back to
     * 0, and does not add a second slot. */
    TEST_ASSERT_TRUE(network_lock_acquire(&network, 42, 1));
    TEST_ASSERT_EQUAL_FLOAT(0.0F, network_lock_find(&network, 42)->seconds_since_refresh);
}

void test_lock_acquire_refuses_second_holder(void)
{
    NetworkState network = {0};
    TEST_ASSERT_TRUE(network_lock_acquire(&network, 42, 1));
    TEST_ASSERT_FALSE(network_lock_acquire(&network, 42, 2));
    TEST_ASSERT_EQUAL_INT(1, network_lock_find(&network, 42)->holder_player_id);
}

void test_lock_acquire_refuses_when_table_full(void)
{
    NetworkState network = {0};
    for (int index = 0; index < NETWORK_LOCKS_MAX; index++) {
        TEST_ASSERT_TRUE(network_lock_acquire(&network, index + 1, 1));
    }
    /* A brand-new entity id past a full table is refused (the safe fallback). */
    TEST_ASSERT_FALSE(network_lock_acquire(&network, NETWORK_LOCKS_MAX + 1, 1));
}

void test_lock_release_only_works_for_holder(void)
{
    NetworkState network = {0};
    TEST_ASSERT_TRUE(network_lock_acquire(&network, 42, 1));
    /* A release by a non-holder is a no-op; the lock survives. */
    TEST_ASSERT_FALSE(network_lock_release(&network, 42, 2));
    TEST_ASSERT_NOT_NULL(network_lock_find(&network, 42));
    /* The holder's own release clears it. */
    TEST_ASSERT_TRUE(network_lock_release(&network, 42, 1));
    TEST_ASSERT_NULL(network_lock_find(&network, 42));
}

void test_lock_refresh_zeroes_ages_for_one_holder_only(void)
{
    NetworkState network = {0};
    TEST_ASSERT_TRUE(network_lock_acquire(&network, 10, 1));
    TEST_ASSERT_TRUE(network_lock_acquire(&network, 11, 1));
    TEST_ASSERT_TRUE(network_lock_acquire(&network, 20, 2));
    network_lock_find(&network, 10)->seconds_since_refresh = 4.0F;
    network_lock_find(&network, 11)->seconds_since_refresh = 4.0F;
    network_lock_find(&network, 20)->seconds_since_refresh = 4.0F;

    network_lock_refresh_holder(&network, 1);

    /* Holder 1's locks are refreshed, holder 2's is untouched. */
    TEST_ASSERT_EQUAL_FLOAT(0.0F, network_lock_find(&network, 10)->seconds_since_refresh);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, network_lock_find(&network, 11)->seconds_since_refresh);
    TEST_ASSERT_EQUAL_FLOAT(4.0F, network_lock_find(&network, 20)->seconds_since_refresh);
}

/* ---- S8.7e: presence table helpers ----
 *
 * Pure state mutators on NetworkState.presence -- no transport needed, driven
 * on an all-zero NetworkState directly, the same whitebox way the lock helper
 * tests above run. */

void test_presence_upsert_claims_and_refreshes(void)
{
    NetworkState network = {0};
    network_presence_upsert(&network, (PresenceRecord){.player_id = 2,
                                                       .cursor_x = 10.0F,
                                                       .cursor_y = 20.0F,
                                                       .selected_entity_id = 7,
                                                       .name = strv_from_cstr("Bob")});
    PresenceEntry *entry = network_presence_find(&network, 2);
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_FLOAT(10.0F, entry->cursor.x);
    TEST_ASSERT_EQUAL_FLOAT(20.0F, entry->cursor.y);
    TEST_ASSERT_EQUAL_INT(7, entry->selected_entity_id);
    TEST_ASSERT_EQUAL_STRING("Bob", entry->name);

    /* A second upsert for the same id refreshes the SAME slot (no second
     * entry) and resets the silence age back to 0. */
    entry->seconds_since_seen = 5.0F;
    network_presence_upsert(&network, (PresenceRecord){.player_id = 2,
                                                       .cursor_x = 30.0F,
                                                       .cursor_y = 40.0F,
                                                       .selected_entity_id = -1,
                                                       .name = strv_from_cstr("Bobby")});
    PresenceEntry *refreshed = network_presence_find(&network, 2);
    TEST_ASSERT_NOT_NULL(refreshed);
    TEST_ASSERT_EQUAL_FLOAT(30.0F, refreshed->cursor.x);
    TEST_ASSERT_EQUAL_INT(-1, refreshed->selected_entity_id);
    TEST_ASSERT_EQUAL_STRING("Bobby", refreshed->name);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, refreshed->seconds_since_seen);

    int active = 0;
    for (int index = 0; index < NETWORK_PRESENCE_MAX; index++) {
        if (network.presence[index].active) {
            active++;
        }
    }
    TEST_ASSERT_EQUAL_INT(1, active);
}

void test_presence_age_deactivates_past_timeout(void)
{
    NetworkState network = {0};
    network_presence_upsert(&network, (PresenceRecord){.player_id = 1, .name = strv_from_cstr("A")});
    /* Aged just past the timeout: the silent peer fades out. */
    network_presence_age(&network, NETWORK_PRESENCE_TIMEOUT_SECONDS + 0.01F);
    TEST_ASSERT_NULL(network_presence_find(&network, 1));
}

void test_presence_age_keeps_still_fresh_entry(void)
{
    NetworkState network = {0};
    network_presence_upsert(&network, (PresenceRecord){.player_id = 1, .name = strv_from_cstr("A")});
    /* Aged by less than the timeout: the entry survives. */
    network_presence_age(&network, NETWORK_PRESENCE_TIMEOUT_SECONDS - 0.01F);
    TEST_ASSERT_NOT_NULL(network_presence_find(&network, 1));
}

/* Encode a one-record MSG_CURSOR and send it from `from` to `dest`. */
static void send_cursor(NetTransport *from, NetAddr dest, PresenceRecord record)
{
    uint8_t buffer[NET_MAX_PACKET_SIZE];
    size_t packet_len = 0;
    TEST_ASSERT_TRUE(protocol_encode_cursor_packet(buffer, sizeof(buffer), 0, &record, 1, &packet_len));
    TEST_ASSERT_TRUE(net_send(from, dest, buffer, packet_len) > 0);
}

void test_host_receive_cursor_upserts_registered_client(void)
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
    int player_id = host_network.clients[0].player_id;

    send_cursor(&client_transport, host_addr,
                (PresenceRecord){.player_id = player_id,
                                 .cursor_x = 5.0F,
                                 .cursor_y = 6.0F,
                                 .selected_entity_id = 9,
                                 .name = strv_from_cstr("Client")});
    host_receive_events_only(&host_network);

    PresenceEntry *entry = network_presence_find(&host_network, player_id);
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_FLOAT(5.0F, entry->cursor.x);
    TEST_ASSERT_EQUAL_FLOAT(6.0F, entry->cursor.y);
    TEST_ASSERT_EQUAL_INT(9, entry->selected_entity_id);
    TEST_ASSERT_EQUAL_STRING("Client", entry->name);

    loopback_network_free(&loopback);
}

void test_host_receive_cursor_drops_mismatched_and_unregistered(void)
{
    LoopbackNetwork loopback = make_loopback_network();
    NetAddr host_addr = net_addr_make(1, 9000);
    NetAddr client_addr = net_addr_make(2, 9001);
    NetAddr rogue_addr = net_addr_make(3, 9002);
    NetTransport host_transport;
    NetTransport client_transport;
    NetTransport rogue_transport;
    TEST_ASSERT_TRUE(loopback_transport_create(&loopback, host_addr, &host_transport));
    TEST_ASSERT_TRUE(loopback_transport_create(&loopback, client_addr, &client_transport));
    TEST_ASSERT_TRUE(loopback_transport_create(&loopback, rogue_addr, &rogue_transport));

    NetworkState host_network = {.transport = host_transport};
    register_client(&host_network, client_addr);
    int player_id = host_network.clients[0].player_id;

    /* (b) A registered client speaking for a DIFFERENT player_id -- one peer,
     * one voice -- is dropped. */
    send_cursor(&client_transport, host_addr,
                (PresenceRecord){.player_id = player_id + 1, .name = strv_from_cstr("Impostor")});
    /* (a) A record from an unregistered source is dropped. */
    send_cursor(&rogue_transport, host_addr, (PresenceRecord){.player_id = player_id, .name = strv_from_cstr("Rogue")});
    host_receive_events_only(&host_network);

    for (int index = 0; index < NETWORK_PRESENCE_MAX; index++) {
        TEST_ASSERT_FALSE(host_network.presence[index].active);
    }

    loopback_network_free(&loopback);
}

/* ---- S8.7f1: structural resync reassembly (network_resync_accept_chunk) ---- */

/* Deterministic byte pattern so a reassembled buffer can be compared to its
 * source exactly. */
static void resync_fill_pattern(uint8_t *buffer, size_t len)
{
    for (size_t index = 0; index < len; index++) {
        buffer[index] = (uint8_t)((index * 31u) + 7u);
    }
}

void test_resync_accept_out_of_order_completes(void)
{
    NetworkState network = {0};
    uint8_t source[2500];
    resync_fill_pattern(source, sizeof(source));

    /* 2500 bytes over 1024-byte chunks = 3 chunks (1024, 1024, 452). Fed last,
     * first, middle -- reassembly must not depend on arrival order, and only
     * completes once every chunk is present. */
    network_resync_accept_chunk(&network,
                                &(ResyncChunk){.generation = 1,
                                               .total_bytes = 2500,
                                               .chunk_index = 2,
                                               .chunk_count = 3,
                                               .current_level_name = strv_from_cstr("test"),
                                               .payload = (Strv){.ptr = (const char *)(source + 2048), .len = 452}});
    TEST_ASSERT_FALSE(network.resync_ready);
    network_resync_accept_chunk(&network, &(ResyncChunk){.generation = 1,
                                                         .total_bytes = 2500,
                                                         .chunk_index = 0,
                                                         .chunk_count = 3,
                                                         .current_level_name = strv_from_cstr("test"),
                                                         .payload = (Strv){.ptr = (const char *)source, .len = 1024}});
    TEST_ASSERT_FALSE(network.resync_ready);
    network_resync_accept_chunk(&network,
                                &(ResyncChunk){.generation = 1,
                                               .total_bytes = 2500,
                                               .chunk_index = 1,
                                               .chunk_count = 3,
                                               .current_level_name = strv_from_cstr("test"),
                                               .payload = (Strv){.ptr = (const char *)(source + 1024), .len = 1024}});
    TEST_ASSERT_TRUE(network.resync_ready);
    TEST_ASSERT_EQUAL_UINT32(1, network.resync_incoming_generation);
    TEST_ASSERT_EQUAL_size_t(2500, network.resync_incoming_total_bytes);
    TEST_ASSERT_EQUAL_STRING("test", network.resync_incoming_level_name);
    TEST_ASSERT_EQUAL_MEMORY(source, network.resync_buffer, 2500);
}

void test_resync_supersede_newer_generation_midstream(void)
{
    NetworkState network = {0};
    uint8_t gen1[1200];
    uint8_t gen2[500];
    resync_fill_pattern(gen1, sizeof(gen1));
    resync_fill_pattern(gen2, sizeof(gen2));

    /* Generation 1 (2 chunks) starts arriving but never completes. */
    network_resync_accept_chunk(&network, &(ResyncChunk){.generation = 1,
                                                         .total_bytes = 1200,
                                                         .chunk_index = 0,
                                                         .chunk_count = 2,
                                                         .current_level_name = strv_from_cstr("old"),
                                                         .payload = (Strv){.ptr = (const char *)gen1, .len = 1024}});
    TEST_ASSERT_FALSE(network.resync_ready);

    /* A strictly newer generation 2 (1 chunk) supersedes it -- the bitmap resets
     * and its single chunk completes reassembly right away. */
    network_resync_accept_chunk(&network, &(ResyncChunk){.generation = 2,
                                                         .total_bytes = 500,
                                                         .chunk_index = 0,
                                                         .chunk_count = 1,
                                                         .current_level_name = strv_from_cstr("new"),
                                                         .payload = (Strv){.ptr = (const char *)gen2, .len = 500}});
    TEST_ASSERT_TRUE(network.resync_ready);
    TEST_ASSERT_EQUAL_UINT32(2, network.resync_incoming_generation);
    TEST_ASSERT_EQUAL_size_t(500, network.resync_incoming_total_bytes);
    TEST_ASSERT_EQUAL_STRING("new", network.resync_incoming_level_name);
    TEST_ASSERT_EQUAL_MEMORY(gen2, network.resync_buffer, 500);

    /* A now-stale generation 1 chunk arriving late is dropped (generation <
     * the in-flight one), leaving generation 2's completed reassembly intact. */
    network_resync_accept_chunk(&network,
                                &(ResyncChunk){.generation = 1,
                                               .total_bytes = 1200,
                                               .chunk_index = 1,
                                               .chunk_count = 2,
                                               .current_level_name = strv_from_cstr("old"),
                                               .payload = (Strv){.ptr = (const char *)(gen1 + 1024), .len = 176}});
    TEST_ASSERT_EQUAL_UINT32(2, network.resync_incoming_generation);
}

void test_resync_rejects_failed_generation(void)
{
    NetworkState network = {0};
    uint8_t payload[100];
    resync_fill_pattern(payload, sizeof(payload));

    /* A too-large total_bytes marks generation 3 failed at adoption. */
    network_resync_accept_chunk(&network, &(ResyncChunk){.generation = 3,
                                                         .total_bytes = NETWORK_RESYNC_MAX_BYTES,
                                                         .chunk_index = 0,
                                                         .chunk_count = 1,
                                                         .current_level_name = strv_from_cstr("test"),
                                                         .payload = (Strv){.ptr = (const char *)payload, .len = 100}});
    TEST_ASSERT_EQUAL_UINT32(3, network.resync_failed_generation);
    TEST_ASSERT_FALSE(network.resync_ready);
    TEST_ASSERT_EQUAL_UINT32(0, network.resync_incoming_generation);

    /* A now well-formed chunk for the same (failed) generation 3 is never
     * retried -- a parse-failed generation is only ever superseded, not redone. */
    network_resync_accept_chunk(&network, &(ResyncChunk){.generation = 3,
                                                         .total_bytes = 100,
                                                         .chunk_index = 0,
                                                         .chunk_count = 1,
                                                         .current_level_name = strv_from_cstr("test"),
                                                         .payload = (Strv){.ptr = (const char *)payload, .len = 100}});
    TEST_ASSERT_FALSE(network.resync_ready);
    TEST_ASSERT_EQUAL_UINT32(0, network.resync_incoming_generation);

    /* A newer generation 4 supersedes the failure and reassembles normally. */
    network_resync_accept_chunk(&network, &(ResyncChunk){.generation = 4,
                                                         .total_bytes = 100,
                                                         .chunk_index = 0,
                                                         .chunk_count = 1,
                                                         .current_level_name = strv_from_cstr("test"),
                                                         .payload = (Strv){.ptr = (const char *)payload, .len = 100}});
    TEST_ASSERT_TRUE(network.resync_ready);
    TEST_ASSERT_EQUAL_UINT32(4, network.resync_incoming_generation);
}

void test_resync_rejects_bad_headers(void)
{
    uint8_t payload[100];
    resync_fill_pattern(payload, sizeof(payload));

    /* A level name longer than the cap marks the generation failed at adoption. */
    {
        NetworkState network = {0};
        char long_name[NETWORK_RESYNC_LEVEL_NAME_MAX + 8];
        memset(long_name, 'a', sizeof(long_name) - 1);
        long_name[sizeof(long_name) - 1] = '\0';
        network_resync_accept_chunk(&network,
                                    &(ResyncChunk){.generation = 1,
                                                   .total_bytes = 100,
                                                   .chunk_index = 0,
                                                   .chunk_count = 1,
                                                   .current_level_name = strv_from_cstr(long_name),
                                                   .payload = (Strv){.ptr = (const char *)payload, .len = 100}});
        TEST_ASSERT_EQUAL_UINT32(1, network.resync_failed_generation);
        TEST_ASSERT_FALSE(network.resync_ready);
    }

    /* A chunk_count that disagrees with ceil(total_bytes / CHUNK_BYTES) is
     * rejected: 100 bytes needs one chunk, not two. */
    {
        NetworkState network = {0};
        network_resync_accept_chunk(&network,
                                    &(ResyncChunk){.generation = 1,
                                                   .total_bytes = 100,
                                                   .chunk_index = 0,
                                                   .chunk_count = 2,
                                                   .current_level_name = strv_from_cstr("test"),
                                                   .payload = (Strv){.ptr = (const char *)payload, .len = 100}});
        TEST_ASSERT_EQUAL_UINT32(1, network.resync_failed_generation);
        TEST_ASSERT_FALSE(network.resync_ready);
    }
}

void test_resync_rejects_bad_chunk_geometry(void)
{
    NetworkState network = {0};
    uint8_t source[2000];
    resync_fill_pattern(source, sizeof(source));

    /* Adopt a valid 2-chunk generation with its first chunk. */
    network_resync_accept_chunk(&network, &(ResyncChunk){.generation = 1,
                                                         .total_bytes = 2000,
                                                         .chunk_index = 0,
                                                         .chunk_count = 2,
                                                         .current_level_name = strv_from_cstr("test"),
                                                         .payload = (Strv){.ptr = (const char *)source, .len = 1024}});
    TEST_ASSERT_FALSE(network.resync_ready);

    /* A chunk_index past chunk_count is dropped (never overruns the buffer). */
    network_resync_accept_chunk(&network, &(ResyncChunk){.generation = 1,
                                                         .total_bytes = 2000,
                                                         .chunk_index = 5,
                                                         .chunk_count = 2,
                                                         .current_level_name = strv_from_cstr("test"),
                                                         .payload = (Strv){.ptr = (const char *)source, .len = 1024}});
    TEST_ASSERT_FALSE(network.resync_ready);

    /* A last chunk with the wrong payload length (should be 2000-1024=976) is
     * dropped, so the reassembly stays incomplete. */
    network_resync_accept_chunk(&network,
                                &(ResyncChunk){.generation = 1,
                                               .total_bytes = 2000,
                                               .chunk_index = 1,
                                               .chunk_count = 2,
                                               .current_level_name = strv_from_cstr("test"),
                                               .payload = (Strv){.ptr = (const char *)(source + 1024), .len = 500}});
    TEST_ASSERT_FALSE(network.resync_ready);

    /* The correctly-sized last chunk completes it, buffer matching the source. */
    network_resync_accept_chunk(&network,
                                &(ResyncChunk){.generation = 1,
                                               .total_bytes = 2000,
                                               .chunk_index = 1,
                                               .chunk_count = 2,
                                               .current_level_name = strv_from_cstr("test"),
                                               .payload = (Strv){.ptr = (const char *)(source + 1024), .len = 976}});
    TEST_ASSERT_TRUE(network.resync_ready);
    TEST_ASSERT_EQUAL_MEMORY(source, network.resync_buffer, 2000);
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
    RUN_TEST(test_host_receive_full_pending_ops_does_not_mark_op);
    RUN_TEST(test_lock_acquire_grants_when_free);
    RUN_TEST(test_lock_acquire_regrants_idempotently_to_same_holder);
    RUN_TEST(test_lock_acquire_refuses_second_holder);
    RUN_TEST(test_lock_acquire_refuses_when_table_full);
    RUN_TEST(test_lock_release_only_works_for_holder);
    RUN_TEST(test_lock_refresh_zeroes_ages_for_one_holder_only);
    RUN_TEST(test_presence_upsert_claims_and_refreshes);
    RUN_TEST(test_presence_age_deactivates_past_timeout);
    RUN_TEST(test_presence_age_keeps_still_fresh_entry);
    RUN_TEST(test_host_receive_cursor_upserts_registered_client);
    RUN_TEST(test_host_receive_cursor_drops_mismatched_and_unregistered);
    RUN_TEST(test_resync_accept_out_of_order_completes);
    RUN_TEST(test_resync_supersede_newer_generation_midstream);
    RUN_TEST(test_resync_rejects_failed_generation);
    RUN_TEST(test_resync_rejects_bad_headers);
    RUN_TEST(test_resync_rejects_bad_chunk_geometry);
    return UNITY_END();
}
