#include "network.h"

#include "alloc.h"
#include "error.h"
#include "net.h"
#include "net_discovery.h" // DISCOVERY_PORT
#include "net_udp.h"
#include "strv.h"

#include <stddef.h>

DiscoveredHost *join_list_find(JoinList *list, NetAddr addr)
{
    for (int index = 0; index < list->count; index++) {
        if (net_addr_eq(list->hosts[index].addr, addr)) {
            return &list->hosts[index];
        }
    }
    return nullptr;
}

void join_list_add_or_refresh(JoinList *list, NetAddr addr, const char *name)
{
    DiscoveredHost *existing = join_list_find(list, addr);
    if (existing != nullptr) {
        strv_copy_to_cstr(strv_from_cstr(name), existing->name, sizeof(existing->name));
        existing->last_seen_seconds = 0.0F;
        return;
    }
    if (list->count >= DISCOVERED_HOSTS_MAX) {
        return;
    }
    DiscoveredHost *slot = &list->hosts[list->count];
    slot->addr = addr;
    strv_copy_to_cstr(strv_from_cstr(name), slot->name, sizeof(slot->name));
    slot->last_seen_seconds = 0.0F;
    list->count++;
}

void join_list_age(JoinList *list, float delta_time)
{
    for (int index = 0; index < list->count; index++) {
        list->hosts[index].last_seen_seconds += delta_time;
    }
}

void join_list_evict_timed_out(JoinList *list, float timeout_seconds)
{
    int write_index = 0;
    for (int read_index = 0; read_index < list->count; read_index++) {
        if (list->hosts[read_index].last_seen_seconds <= timeout_seconds) {
            list->hosts[write_index] = list->hosts[read_index];
            write_index++;
        }
    }
    list->count = write_index;
}

/* Apply an already-created transport to `network` as the HOSTING
 * beacon socket -- split out from network_start_hosting so a test can
 * drive this half of the mode transition with a fabricated transport
 * (net_loopback.h's LoopbackNetwork, or even an all-zero null-op-safe
 * NetTransport per net.h) instead of a real UDP socket. Both this and
 * network_apply_discovering below are file-local: a test reaches them by
 * including this .c file directly, the same whitebox pattern
 * net_discovery_test.c already uses for net_discovery.c/net_loopback.c/
 * network.c. */
static void network_apply_hosting(NetworkState *network, NetTransport transport, const char *host_name)
{
    network->transport = transport;
    network->transport_initialized = true;
    network->mode = NET_HOSTING;
    network->beacon_timer = 0.0F;
    strv_copy_to_cstr(strv_from_cstr(host_name), network->host_name, sizeof(network->host_name));
}

/* Apply an already-created transport to `network` as the DISCOVERING
 * listen socket. See network_apply_hosting above for why this is
 * file-local and split out from network_start_discovering. */
static void network_apply_discovering(NetworkState *network, NetTransport transport)
{
    network->transport = transport;
    network->transport_initialized = true;
    network->mode = NET_DISCOVERING;
    network->join_list = (JoinList){0};
}

bool network_start_hosting(NetworkState *network, Allocator *alloc, const char *host_name, ErrorState *err)
{
    NetTransport transport;
    if (!net_udp_create(alloc, DISCOVERY_PORT, true, &transport, err)) {
        return false;
    }
    network_apply_hosting(network, transport, host_name);
    return true;
}

bool network_start_discovering(NetworkState *network, Allocator *alloc, ErrorState *err)
{
    NetTransport transport;
    if (!net_udp_create(alloc, DISCOVERY_PORT, false, &transport, err)) {
        return false;
    }
    network_apply_discovering(network, transport);
    return true;
}

void network_stop(NetworkState *network)
{
    if (network->transport_initialized) {
        net_udp_destroy(&network->transport);
        network->transport_initialized = false;
    }
    network->mode = NET_OFFLINE;
    network->join_list = (JoinList){0};
    network->join_target = (NetAddr){0};
    network->beacon_timer = 0.0F;
    network->host_name[0] = '\0';
}
