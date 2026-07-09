#include "network.h"

#include "net.h"
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
