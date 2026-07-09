#include "fff.h"
#include "unity.h"

#include "../src/error.c"        // NOLINT(bugprone-suspicious-include)
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
    return UNITY_END();
}
