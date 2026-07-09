#include "fff.h"
#include "unity.h"

#include "../src/error.c"        // NOLINT(bugprone-suspicious-include)
#include "../src/net.c"          // NOLINT(bugprone-suspicious-include)
#include "../src/net_loopback.c" // NOLINT(bugprone-suspicious-include)
#include "../src/net_udp.c"      // NOLINT(bugprone-suspicious-include)
#include "../src/strv.c"         // NOLINT(bugprone-suspicious-include)

DEFINE_FFF_GLOBALS;

#include "test_heap_alloc.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ---- NetAddr ---- */

void test_net_addr_eq_same_returns_true(void)
{
    NetAddr lhs = net_addr_make(10, 1000);
    NetAddr rhs = net_addr_make(10, 1000);
    TEST_ASSERT_TRUE(net_addr_eq(lhs, rhs));
}

void test_net_addr_eq_different_host_returns_false(void)
{
    NetAddr lhs = net_addr_make(10, 1000);
    NetAddr rhs = net_addr_make(11, 1000);
    TEST_ASSERT_FALSE(net_addr_eq(lhs, rhs));
}

void test_net_addr_eq_different_port_returns_false(void)
{
    NetAddr lhs = net_addr_make(10, 1000);
    NetAddr rhs = net_addr_make(10, 1001);
    TEST_ASSERT_FALSE(net_addr_eq(lhs, rhs));
}

void test_net_addr_from_ipv4_string_valid(void)
{
    NetAddr addr;
    TEST_ASSERT_TRUE(net_addr_from_ipv4_string("127.0.0.1", 9000, &addr));
    NetAddr expected = net_addr_make((127U << 24) | 1U, 9000);
    TEST_ASSERT_TRUE(net_addr_eq(expected, addr));
}

void test_net_addr_from_ipv4_string_rejects_too_few_octets(void)
{
    NetAddr addr;
    TEST_ASSERT_FALSE(net_addr_from_ipv4_string("192.168.1", 9000, &addr));
}

void test_net_addr_from_ipv4_string_rejects_too_many_octets(void)
{
    NetAddr addr;
    TEST_ASSERT_FALSE(net_addr_from_ipv4_string("192.168.1.1.1", 9000, &addr));
}

void test_net_addr_from_ipv4_string_rejects_out_of_range_octet(void)
{
    NetAddr addr;
    TEST_ASSERT_FALSE(net_addr_from_ipv4_string("192.168.1.256", 9000, &addr));
}

void test_net_addr_from_ipv4_string_rejects_non_digit(void)
{
    NetAddr addr;
    TEST_ASSERT_FALSE(net_addr_from_ipv4_string("192.168.1.abc", 9000, &addr));
}

/* ---- NetTransport wrapper null-safety ---- */

void test_net_send_recv_poll_null_transport_is_safe(void)
{
    NetAddr dest = net_addr_make(1, 1);
    char buffer[4];
    TEST_ASSERT_EQUAL_INT(0, net_send(nullptr, dest, "x", 1));
    TEST_ASSERT_EQUAL_INT(0, net_recv(nullptr, nullptr, buffer, sizeof(buffer)));
    net_poll(nullptr); /* must not crash */
}

void test_net_send_recv_poll_null_ops_is_safe(void)
{
    NetTransport transport = {0};
    NetAddr dest = net_addr_make(1, 1);
    char buffer[4];
    TEST_ASSERT_EQUAL_INT(0, net_send(&transport, dest, "x", 1));
    TEST_ASSERT_EQUAL_INT(0, net_recv(&transport, nullptr, buffer, sizeof(buffer)));
    net_poll(&transport); /* must not crash */
}

/* ---- net_loopback ---- */

static LoopbackNetwork make_loopback_network(void)
{
    LoopbackNetwork network;
    loopback_network_init(&network, &test_heap_alloc);
    return network;
}

void test_loopback_round_trip_a_to_b(void)
{
    LoopbackNetwork network = make_loopback_network();
    NetAddr addr_a = net_addr_make(1, 100);
    NetAddr addr_b = net_addr_make(2, 200);
    NetTransport transport_a;
    NetTransport transport_b;
    TEST_ASSERT_TRUE(loopback_transport_create(&network, addr_a, &transport_a));
    TEST_ASSERT_TRUE(loopback_transport_create(&network, addr_b, &transport_b));

    TEST_ASSERT_EQUAL_INT(5, net_send(&transport_a, addr_b, "hello", 5));

    char buffer[16] = {0};
    NetAddr src = {0};
    int received = net_recv(&transport_b, &src, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_INT(5, received);
    TEST_ASSERT_EQUAL_MEMORY("hello", buffer, 5);
    TEST_ASSERT_TRUE(net_addr_eq(addr_a, src));

    /* Second recv on B: inbox is now empty. */
    TEST_ASSERT_EQUAL_INT(0, net_recv(&transport_b, nullptr, buffer, sizeof(buffer)));
    loopback_network_free(&network);
}

void test_loopback_round_trip_b_to_a(void)
{
    LoopbackNetwork network = make_loopback_network();
    NetAddr addr_a = net_addr_make(1, 100);
    NetAddr addr_b = net_addr_make(2, 200);
    NetTransport transport_a;
    NetTransport transport_b;
    TEST_ASSERT_TRUE(loopback_transport_create(&network, addr_a, &transport_a));
    TEST_ASSERT_TRUE(loopback_transport_create(&network, addr_b, &transport_b));

    TEST_ASSERT_EQUAL_INT(3, net_send(&transport_b, addr_a, "abc", 3));

    char buffer[16] = {0};
    NetAddr src = {0};
    int received = net_recv(&transport_a, &src, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_INT(3, received);
    TEST_ASSERT_EQUAL_MEMORY("abc", buffer, 3);
    TEST_ASSERT_TRUE(net_addr_eq(addr_b, src));
    loopback_network_free(&network);
}

void test_loopback_fifo_ordering(void)
{
    LoopbackNetwork network = make_loopback_network();
    NetAddr addr_a = net_addr_make(1, 100);
    NetAddr addr_b = net_addr_make(2, 200);
    NetTransport transport_a;
    NetTransport transport_b;
    TEST_ASSERT_TRUE(loopback_transport_create(&network, addr_a, &transport_a));
    TEST_ASSERT_TRUE(loopback_transport_create(&network, addr_b, &transport_b));

    TEST_ASSERT_EQUAL_INT(1, net_send(&transport_a, addr_b, "1", 1));
    TEST_ASSERT_EQUAL_INT(1, net_send(&transport_a, addr_b, "2", 1));
    TEST_ASSERT_EQUAL_INT(1, net_send(&transport_a, addr_b, "3", 1));

    char buffer[4] = {0};
    TEST_ASSERT_EQUAL_INT(1, net_recv(&transport_b, nullptr, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_CHAR('1', buffer[0]);
    TEST_ASSERT_EQUAL_INT(1, net_recv(&transport_b, nullptr, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_CHAR('2', buffer[0]);
    TEST_ASSERT_EQUAL_INT(1, net_recv(&transport_b, nullptr, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_CHAR('3', buffer[0]);
    TEST_ASSERT_EQUAL_INT(0, net_recv(&transport_b, nullptr, buffer, sizeof(buffer)));
    loopback_network_free(&network);
}

void test_loopback_recv_on_fresh_endpoint_returns_zero(void)
{
    LoopbackNetwork network = make_loopback_network();
    NetAddr addr_a = net_addr_make(1, 100);
    NetTransport transport_a;
    TEST_ASSERT_TRUE(loopback_transport_create(&network, addr_a, &transport_a));

    char buffer[4];
    TEST_ASSERT_EQUAL_INT(0, net_recv(&transport_a, nullptr, buffer, sizeof(buffer)));
    loopback_network_free(&network);
}

void test_loopback_inbox_full_tail_drops_incoming(void)
{
    LoopbackNetwork network = make_loopback_network();
    NetAddr addr_a = net_addr_make(1, 100);
    NetAddr addr_b = net_addr_make(2, 200);
    NetTransport transport_a;
    NetTransport transport_b;
    TEST_ASSERT_TRUE(loopback_transport_create(&network, addr_a, &transport_a));
    TEST_ASSERT_TRUE(loopback_transport_create(&network, addr_b, &transport_b));

    int sent_count = LOOPBACK_INBOX_CAPACITY + 5;
    for (int index = 0; index < sent_count; index++) {
        unsigned char payload = (unsigned char)index;
        TEST_ASSERT_EQUAL_INT(1, net_send(&transport_a, addr_b, &payload, 1));
    }

    /* Only the first LOOPBACK_INBOX_CAPACITY packets survive (tail-drop
     * of the newer, overflowing ones); no crash, no unbounded growth. */
    for (int index = 0; index < LOOPBACK_INBOX_CAPACITY; index++) {
        unsigned char buffer = 0;
        int received = net_recv(&transport_b, nullptr, &buffer, sizeof(buffer));
        TEST_ASSERT_EQUAL_INT(1, received);
        TEST_ASSERT_EQUAL_UINT8((unsigned char)index, buffer);
    }
    unsigned char buffer = 0;
    TEST_ASSERT_EQUAL_INT(0, net_recv(&transport_b, nullptr, &buffer, sizeof(buffer)));
    loopback_network_free(&network);
}

void test_loopback_duplicate_endpoint_registration_fails(void)
{
    LoopbackNetwork network = make_loopback_network();
    NetAddr addr_a = net_addr_make(1, 100);
    NetTransport transport_a;
    NetTransport transport_a_again;
    TEST_ASSERT_TRUE(loopback_transport_create(&network, addr_a, &transport_a));
    TEST_ASSERT_FALSE(loopback_transport_create(&network, addr_a, &transport_a_again));
    loopback_network_free(&network);
}

void test_loopback_send_to_unknown_destination_is_silently_dropped(void)
{
    LoopbackNetwork network = make_loopback_network();
    NetAddr addr_a = net_addr_make(1, 100);
    NetAddr addr_unknown = net_addr_make(9, 900);
    NetTransport transport_a;
    TEST_ASSERT_TRUE(loopback_transport_create(&network, addr_a, &transport_a));

    /* Fire-and-forget, like a real UDP send to an address nobody is
     * listening on: reports success, delivers nothing. */
    TEST_ASSERT_EQUAL_INT(3, net_send(&transport_a, addr_unknown, "abc", 3));
    loopback_network_free(&network);
}

/* ---- net_udp ----
 * Real sockets may be unavailable in a sandboxed CI environment. Every
 * test below skips gracefully via TEST_IGNORE_MESSAGE rather than
 * failing when socket()/bind() (or the round trip itself) doesn't work,
 * per S8.1's brief -- the suite must not depend on real sockets. */

void test_net_udp_create_destroy_ephemeral_port(void)
{
    NetTransport transport;
    ErrorState err = {0};
    if (!net_udp_create(&test_heap_alloc, 0, &transport, &err)) {
        TEST_IGNORE_MESSAGE("net_udp_create failed in this sandbox (socket()/bind() likely blocked)");
        return;
    }
    TEST_ASSERT_NOT_NULL(transport.state);
    net_udp_destroy(&transport);
    TEST_ASSERT_NULL(transport.state);
}

void test_net_udp_localhost_round_trip(void)
{
    NetTransport receiver;
    NetTransport sender;
    ErrorState err = {0};
    uint16_t receiver_port = 45201;
    uint16_t sender_port = 45202;

    if (!net_udp_create(&test_heap_alloc, receiver_port, &receiver, &err)) {
        TEST_IGNORE_MESSAGE("UDP sockets unavailable in this sandbox (receiver bind failed)");
        return;
    }
    if (!net_udp_create(&test_heap_alloc, sender_port, &sender, &err)) {
        net_udp_destroy(&receiver);
        TEST_IGNORE_MESSAGE("UDP sockets unavailable in this sandbox (sender bind failed)");
        return;
    }

    NetAddr receiver_addr;
    TEST_ASSERT_TRUE(net_addr_from_ipv4_string("127.0.0.1", receiver_port, &receiver_addr));
    int sent = net_send(&sender, receiver_addr, "ping", 4);

    char buffer[16] = {0};
    NetAddr src = {0};
    int received = 0;
    for (int attempt = 0; attempt < 1000 && received == 0; attempt++) {
        received = net_recv(&receiver, &src, buffer, sizeof(buffer));
    }

    net_udp_destroy(&sender);
    net_udp_destroy(&receiver);

    if (sent < 0 || received <= 0) {
        TEST_IGNORE_MESSAGE("Real UDP traffic did not complete in this sandbox");
        return;
    }
    TEST_ASSERT_EQUAL_INT(4, received);
    TEST_ASSERT_EQUAL_MEMORY("ping", buffer, 4);
}

int main(void)
{
    test_helpers_init();
    UNITY_BEGIN();
    RUN_TEST(test_net_addr_eq_same_returns_true);
    RUN_TEST(test_net_addr_eq_different_host_returns_false);
    RUN_TEST(test_net_addr_eq_different_port_returns_false);
    RUN_TEST(test_net_addr_from_ipv4_string_valid);
    RUN_TEST(test_net_addr_from_ipv4_string_rejects_too_few_octets);
    RUN_TEST(test_net_addr_from_ipv4_string_rejects_too_many_octets);
    RUN_TEST(test_net_addr_from_ipv4_string_rejects_out_of_range_octet);
    RUN_TEST(test_net_addr_from_ipv4_string_rejects_non_digit);
    RUN_TEST(test_net_send_recv_poll_null_transport_is_safe);
    RUN_TEST(test_net_send_recv_poll_null_ops_is_safe);
    RUN_TEST(test_loopback_round_trip_a_to_b);
    RUN_TEST(test_loopback_round_trip_b_to_a);
    RUN_TEST(test_loopback_fifo_ordering);
    RUN_TEST(test_loopback_recv_on_fresh_endpoint_returns_zero);
    RUN_TEST(test_loopback_inbox_full_tail_drops_incoming);
    RUN_TEST(test_loopback_duplicate_endpoint_registration_fails);
    RUN_TEST(test_loopback_send_to_unknown_destination_is_silently_dropped);
    RUN_TEST(test_net_udp_create_destroy_ephemeral_port);
    RUN_TEST(test_net_udp_localhost_round_trip);
    return UNITY_END();
}
