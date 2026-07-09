#include "net_discovery.h"

#include "error.h"
#include "net.h"
#include "net_protocol.h"
#include "network.h"
#include "strv.h"

#include <stddef.h>
#include <stdint.h>

void discovery_host_tick(const NetTransport *transport,
                         NetAddr broadcast_target,
                         const char *host_name,
                         uint16_t listen_port,
                         float *beacon_timer,
                         float delta_time)
{
    *beacon_timer += delta_time;
    if (*beacon_timer < DISCOVERY_BEACON_INTERVAL_SECONDS) {
        return;
    }
    *beacon_timer -= DISCOVERY_BEACON_INTERVAL_SECONDS;

    BeaconMessage beacon = {.host_name = strv_from_cstr(host_name), .listen_port = listen_port};
    uint8_t buffer[NET_MAX_PACKET_SIZE];
    size_t packet_len = 0;
    if (!protocol_encode_beacon_packet(buffer, sizeof(buffer), 0, beacon, &packet_len)) {
        return;
    }
    (void)net_send(transport, broadcast_target, buffer, packet_len);
}

/* Decode one received datagram and, if it is a valid MSG_BEACON,
 * add-or-refresh its sender in list. Anything else (malformed, wrong
 * message type) is silently ignored. */
static void handle_beacon_datagram(NetAddr src, const uint8_t *buffer, size_t len, JoinList *list)
{
    DecodedPacket packet;
    ErrorState err = {0};
    if (!protocol_decode_packet(buffer, len, &packet, &err)) {
        return;
    }
    if (packet.header.type != MSG_BEACON) {
        return;
    }
    BeaconMessage beacon = {0};
    if (!protocol_decode_beacon(&packet.reader, &beacon)) {
        return;
    }

    char name[NET_NAME_MAX];
    strv_copy_to_cstr(beacon.host_name, name, sizeof(name));
    NetAddr host_addr = net_addr_make(src.host, beacon.listen_port);
    join_list_add_or_refresh(list, host_addr, name);
}

void discovery_client_tick(const NetTransport *transport, JoinList *list, float delta_time)
{
    uint8_t buffer[NET_MAX_PACKET_SIZE];
    NetAddr src = {0};
    int received = net_recv(transport, &src, buffer, sizeof(buffer));
    while (received > 0) {
        handle_beacon_datagram(src, buffer, (size_t)received, list);
        received = net_recv(transport, &src, buffer, sizeof(buffer));
    }
    join_list_age(list, delta_time);
    join_list_evict_timed_out(list, DISCOVERY_TIMEOUT_SECONDS);
}
