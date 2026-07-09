#include "fff.h"
#include "unity.h"

#include "../src/error.c"         // NOLINT(bugprone-suspicious-include)
#include "../src/net.c"           // NOLINT(bugprone-suspicious-include)
#include "../src/net_discovery.c" // NOLINT(bugprone-suspicious-include)
#include "../src/net_loopback.c"  // NOLINT(bugprone-suspicious-include)
#include "../src/net_protocol.c"  // NOLINT(bugprone-suspicious-include)
#include "../src/net_udp.c"       // NOLINT(bugprone-suspicious-include)
#include "../src/network.c"       // NOLINT(bugprone-suspicious-include)
#include "../src/strv.c"          // NOLINT(bugprone-suspicious-include)

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

/* ---- JoinList helpers, tested directly ---- */

void test_join_list_find_returns_null_when_absent(void)
{
    JoinList list = {0};
    NetAddr addr = net_addr_make(1, 100);
    TEST_ASSERT_NULL(join_list_find(&list, addr));
}

void test_join_list_add_or_refresh_adds_new_entry(void)
{
    JoinList list = {0};
    NetAddr addr = net_addr_make(1, 100);
    join_list_add_or_refresh(&list, addr, "Alice");

    TEST_ASSERT_EQUAL_INT(1, list.count);
    DiscoveredHost *found = join_list_find(&list, addr);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("Alice", found->name);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, found->last_seen_seconds);
}

void test_join_list_add_or_refresh_existing_addr_updates_name_and_resets_age(void)
{
    JoinList list = {0};
    NetAddr addr = net_addr_make(1, 100);
    join_list_add_or_refresh(&list, addr, "Alice");
    join_list_age(&list, 2.0F);

    join_list_add_or_refresh(&list, addr, "Alice2");

    TEST_ASSERT_EQUAL_INT(1, list.count);
    DiscoveredHost *found = join_list_find(&list, addr);
    TEST_ASSERT_EQUAL_STRING("Alice2", found->name);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, found->last_seen_seconds);
}

void test_join_list_add_or_refresh_beyond_capacity_drops_silently(void)
{
    JoinList list = {0};
    for (int index = 0; index < DISCOVERED_HOSTS_MAX + 3; index++) {
        NetAddr addr = net_addr_make((uint32_t)index, 1000);
        join_list_add_or_refresh(&list, addr, "Host");
    }
    TEST_ASSERT_EQUAL_INT(DISCOVERED_HOSTS_MAX, list.count);
}

void test_join_list_age_accumulates_delta_time(void)
{
    JoinList list = {0};
    NetAddr addr = net_addr_make(1, 100);
    join_list_add_or_refresh(&list, addr, "Alice");

    join_list_age(&list, 1.5F);
    join_list_age(&list, 0.5F);

    TEST_ASSERT_EQUAL_FLOAT(2.0F, join_list_find(&list, addr)->last_seen_seconds);
}

void test_join_list_evict_timed_out_removes_only_stale_entries(void)
{
    JoinList list = {0};
    NetAddr fresh_addr = net_addr_make(1, 100);
    NetAddr stale_addr = net_addr_make(2, 200);
    join_list_add_or_refresh(&list, fresh_addr, "Fresh");
    join_list_add_or_refresh(&list, stale_addr, "Stale");
    join_list_find(&list, stale_addr)->last_seen_seconds = 10.0F;

    join_list_evict_timed_out(&list, 5.0F);

    TEST_ASSERT_EQUAL_INT(1, list.count);
    TEST_ASSERT_NOT_NULL(join_list_find(&list, fresh_addr));
    TEST_ASSERT_NULL(join_list_find(&list, stale_addr));
}

/* ---- discovery_host_tick: beacon cadence, over loopback ---- */

void test_discovery_host_tick_beacons_once_per_interval_not_every_frame(void)
{
    LoopbackNetwork network = make_loopback_network();
    NetAddr host_addr = net_addr_make(1, 100);
    NetAddr client_addr = net_addr_make(2, 200);
    NetTransport host_transport;
    NetTransport client_transport;
    TEST_ASSERT_TRUE(loopback_transport_create(&network, host_addr, &host_transport));
    TEST_ASSERT_TRUE(loopback_transport_create(&network, client_addr, &client_transport));

    uint8_t buffer[NET_MAX_PACKET_SIZE];
    float beacon_timer = 0.0F;

    /* Three small ticks summing to 0.75s: under the 1.0s interval, so no
     * beacon has gone out yet -- proves the tick doesn't fire every
     * frame. */
    for (int index = 0; index < 3; index++) {
        discovery_host_tick(&host_transport, client_addr, "Host", 9000, &beacon_timer, 0.25F);
    }
    TEST_ASSERT_EQUAL_INT(0, net_recv(&client_transport, nullptr, buffer, sizeof(buffer)));

    /* One more 0.25s tick crosses the 1.0s interval: exactly one beacon. */
    discovery_host_tick(&host_transport, client_addr, "Host", 9000, &beacon_timer, 0.25F);
    TEST_ASSERT_TRUE(net_recv(&client_transport, nullptr, buffer, sizeof(buffer)) > 0);
    TEST_ASSERT_EQUAL_INT(0, net_recv(&client_transport, nullptr, buffer, sizeof(buffer)));

    /* A second interval's worth of small ticks: exactly one more beacon,
     * not N. */
    for (int index = 0; index < 3; index++) {
        discovery_host_tick(&host_transport, client_addr, "Host", 9000, &beacon_timer, 0.25F);
    }
    TEST_ASSERT_EQUAL_INT(0, net_recv(&client_transport, nullptr, buffer, sizeof(buffer)));
    discovery_host_tick(&host_transport, client_addr, "Host", 9000, &beacon_timer, 0.25F);
    TEST_ASSERT_TRUE(net_recv(&client_transport, nullptr, buffer, sizeof(buffer)) > 0);
    TEST_ASSERT_EQUAL_INT(0, net_recv(&client_transport, nullptr, buffer, sizeof(buffer)));

    loopback_network_free(&network);
}

/* ---- discovery_client_tick: join-list population, over loopback ---- */

void test_discovery_client_tick_populates_join_list_from_beacons(void)
{
    LoopbackNetwork network = make_loopback_network();
    NetAddr host_a_addr = net_addr_make(1, 100);
    NetAddr host_b_addr = net_addr_make(2, 200);
    NetAddr client_addr = net_addr_make(3, 300);
    NetTransport host_a_transport;
    NetTransport host_b_transport;
    NetTransport client_transport;
    TEST_ASSERT_TRUE(loopback_transport_create(&network, host_a_addr, &host_a_transport));
    TEST_ASSERT_TRUE(loopback_transport_create(&network, host_b_addr, &host_b_transport));
    TEST_ASSERT_TRUE(loopback_transport_create(&network, client_addr, &client_transport));

    JoinList list = {0};

    /* beacon_timer starts at the interval, so delta_time 0.0F still fires
     * immediately -- a convenience for tests that don't care about
     * cadence, only about join-list population. */
    float timer_a = DISCOVERY_BEACON_INTERVAL_SECONDS;
    discovery_host_tick(&host_a_transport, client_addr, "Alice", 9001, &timer_a, 0.0F);
    discovery_client_tick(&client_transport, &list, 0.1F);

    TEST_ASSERT_EQUAL_INT(1, list.count);
    TEST_ASSERT_EQUAL_STRING("Alice", list.hosts[0].name);
    NetAddr expected_a = net_addr_make(host_a_addr.host, 9001);
    TEST_ASSERT_TRUE(net_addr_eq(expected_a, list.hosts[0].addr));

    /* A second, distinct host beacons too: a second entry, not a
     * replacement. */
    float timer_b = DISCOVERY_BEACON_INTERVAL_SECONDS;
    discovery_host_tick(&host_b_transport, client_addr, "Bob", 9002, &timer_b, 0.0F);
    discovery_client_tick(&client_transport, &list, 0.1F);
    TEST_ASSERT_EQUAL_INT(2, list.count);

    /* Host A beacons again: refreshes its existing entry, no duplicate. */
    timer_a = DISCOVERY_BEACON_INTERVAL_SECONDS;
    discovery_host_tick(&host_a_transport, client_addr, "Alice", 9001, &timer_a, 0.0F);
    discovery_client_tick(&client_transport, &list, 0.1F);
    TEST_ASSERT_EQUAL_INT(2, list.count);

    loopback_network_free(&network);
}

void test_discovery_client_tick_evicts_host_after_timeout(void)
{
    LoopbackNetwork network = make_loopback_network();
    NetAddr host_addr = net_addr_make(1, 100);
    NetAddr client_addr = net_addr_make(2, 200);
    NetTransport host_transport;
    NetTransport client_transport;
    TEST_ASSERT_TRUE(loopback_transport_create(&network, host_addr, &host_transport));
    TEST_ASSERT_TRUE(loopback_transport_create(&network, client_addr, &client_transport));

    JoinList list = {0};
    float beacon_timer = DISCOVERY_BEACON_INTERVAL_SECONDS;
    discovery_host_tick(&host_transport, client_addr, "Host", 9000, &beacon_timer, 0.0F);
    discovery_client_tick(&client_transport, &list, 0.1F);
    TEST_ASSERT_EQUAL_INT(1, list.count);

    /* Host stops beaconing. Advance the client's own clock past the
     * timeout in one tick. */
    discovery_client_tick(&client_transport, &list, DISCOVERY_TIMEOUT_SECONDS + 0.1F);

    TEST_ASSERT_EQUAL_INT(0, list.count);
    loopback_network_free(&network);
}

int main(void)
{
    test_helpers_init();
    UNITY_BEGIN();
    RUN_TEST(test_join_list_find_returns_null_when_absent);
    RUN_TEST(test_join_list_add_or_refresh_adds_new_entry);
    RUN_TEST(test_join_list_add_or_refresh_existing_addr_updates_name_and_resets_age);
    RUN_TEST(test_join_list_add_or_refresh_beyond_capacity_drops_silently);
    RUN_TEST(test_join_list_age_accumulates_delta_time);
    RUN_TEST(test_join_list_evict_timed_out_removes_only_stale_entries);
    RUN_TEST(test_discovery_host_tick_beacons_once_per_interval_not_every_frame);
    RUN_TEST(test_discovery_client_tick_populates_join_list_from_beacons);
    RUN_TEST(test_discovery_client_tick_evicts_host_after_timeout);
    return UNITY_END();
}
