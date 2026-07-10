#include "network.h"

#include "alloc.h"
#include "error.h"
#include "input.h"
#include "net.h"
#include "net_discovery.h" // DISCOVERY_PORT
#include "net_protocol.h"
#include "net_reliable.h" // ReliableChannel, reliable_on_ack, reliable_send, reliable_send_op, reliable_tick, reliable_make_ack
#include "net_udp.h"
#include "strv.h"

#include <stddef.h>
#include <stdint.h>

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
    for (int index = 0; index < NET_MAX_CLIENTS; index++) {
        network->clients[index] = (NetClient){0};
    }
    network->client_count = 0;
    network->local_player_id = 0;
    network->host_event_channel = (ReliableChannel){0};
    network->last_delivered_event_type = 0;
    network->last_delivered_event_entity_id = 0;
    network->delivered_event_count = 0;
    network->last_client_event_type = 0;
    network->last_client_event_entity_id = 0;
    network->last_client_event_player_id = 0;
    network->client_event_delivered_count = 0;
}

/* ---- S8.4a: session JOIN + INPUT flow ---- */

const NetClient *network_find_client_by_player_id(const NetworkState *network, int player_id)
{
    for (int index = 0; index < network->client_count; index++) {
        if (network->clients[index].active && network->clients[index].player_id == player_id) {
            return &network->clients[index];
        }
    }
    return nullptr;
}

static NetClient *find_client_by_addr(NetworkState *network, NetAddr addr)
{
    for (int index = 0; index < network->client_count; index++) {
        if (network->clients[index].active && net_addr_eq(network->clients[index].addr, addr)) {
            return &network->clients[index];
        }
    }
    return nullptr;
}

/* Register a newly-JOINed client at addr with the next player_id (1..N in
 * join order). A no-op (not an error) if addr is already registered --
 * re-JOIN refreshes rather than duplicating, and S8.4a has nothing else on
 * NetClient worth refreshing yet. Silently drops a genuinely new client
 * past NET_MAX_CLIENTS, the same bounded-capacity contract
 * join_list_add_or_refresh already uses for JoinList. */
static void register_client(NetworkState *network, NetAddr addr)
{
    if (find_client_by_addr(network, addr) != nullptr) {
        return;
    }
    if (network->client_count >= NET_MAX_CLIENTS) {
        return;
    }
    network->clients[network->client_count] = (NetClient){
        .addr = addr,
        .player_id = network->client_count + 1,
        .active = true,
    };
    network->client_count++;
}

void network_client_send_join(NetworkState *network, uint64_t local_gamedata_hash)
{
    JoinMessage message = {.gamedata_hash = local_gamedata_hash, .client_name = strv_from_cstr(network->host_name)};
    uint8_t buffer[NET_MAX_PACKET_SIZE];
    size_t packet_len = 0;
    if (!protocol_encode_join_packet(buffer, sizeof(buffer), 0, message, &packet_len)) {
        return;
    }
    (void)net_send(&network->transport, network->join_target, buffer, packet_len);
}

void network_client_send_input(NetworkState *network, const InputState *local_input)
{
    uint8_t buffer[NET_MAX_PACKET_SIZE];
    size_t packet_len = 0;
    if (!protocol_encode_input_packet(buffer, sizeof(buffer), 0, local_input, &packet_len)) {
        return;
    }
    (void)net_send(&network->transport, network->join_target, buffer, packet_len);
}

/* HOST side (S8.6): reply to an accepted JOIN with MSG_JOIN_ACCEPT carrying
 * player_id, fire-and-forget like every other network_* send in this file.
 * Called for every verified JOIN (new registration or re-JOIN refresh),
 * never for a refused one -- see handle_join_datagram below. */
static void send_join_accept(const NetworkState *network, NetAddr dest, int player_id)
{
    JoinAcceptMessage message = {.player_id = player_id};
    uint8_t buffer[NET_MAX_PACKET_SIZE];
    size_t packet_len = 0;
    if (!protocol_encode_join_accept_packet(buffer, sizeof(buffer), 0, message, &packet_len)) {
        return;
    }
    (void)net_send(&network->transport, dest, buffer, packet_len);
}

/* Decode one payload as MSG_JOIN and either register-and-accept or refuse
 * the sender, per network_host_receive's own doc comment. */
static void handle_join_datagram(NetworkState *network, NetAddr src, PacketReader *reader, uint64_t local_gamedata_hash)
{
    JoinMessage message = {0};
    if (!protocol_decode_join(reader, &message)) {
        return;
    }
    JoinVerifyResult verify = protocol_join_verify(local_gamedata_hash, message.gamedata_hash);
    if (!verify.ok) {
        return;
    }
    register_client(network, src);
    const NetClient *client = find_client_by_addr(network, src);
    if (client) {
        send_join_accept(network, src, client->player_id);
    }
}

/* Decode one payload as MSG_INPUT and store it as src's last_input, if src
 * is a registered client. Input from an unregistered address is ignored
 * (a client must JOIN before its INPUT is honored). */
static void handle_input_datagram(NetworkState *network, NetAddr src, PacketReader *reader)
{
    NetClient *client = find_client_by_addr(network, src);
    if (client == nullptr) {
        return;
    }
    if (!protocol_decode_input(reader, &client->last_input)) {
        return;
    }
}

/* Decode one payload as MSG_ACK (S8.4c) and apply it to src's own
 * event_channel, if src is a registered client -- an ACK from an
 * unregistered address is ignored, same as handle_input_datagram's own
 * contract. */
static void handle_ack_datagram(NetworkState *network, NetAddr src, PacketReader *reader)
{
    NetClient *client = find_client_by_addr(network, src);
    if (client == nullptr) {
        return;
    }
    AckMessage ack = {0};
    if (!protocol_decode_ack(reader, &ack)) {
        return;
    }
    reliable_on_ack(&client->event_channel, ack);
}

/* S8.7a: decode one payload as MSG_EVENT and dedup it via src's own
 * event_channel receive-side (net_reliable.h's reliable_on_receive), if
 * src is a registered client -- an EVENT from an unregistered address is
 * ignored, same as handle_input_datagram/handle_ack_datagram's own
 * contract. A newly-delivered event (not a duplicate/resend) updates
 * network's last_client_event_type/_entity_id/_player_id and increments
 * client_event_delivered_count -- see network_host_receive's own doc
 * comment for the exactly-once contract this gives. */
static void handle_event_datagram(NetworkState *network, NetAddr src, uint32_t seq, PacketReader *reader)
{
    NetClient *client = find_client_by_addr(network, src);
    if (client == nullptr) {
        return;
    }
    EventRecord event = {0};
    if (!protocol_decode_event(reader, &event)) {
        return;
    }
    if (!reliable_on_receive(&client->event_channel, seq)) {
        return;
    }
    network->last_client_event_type = event.event_type;
    network->last_client_event_entity_id = event.entity_id;
    network->last_client_event_player_id = client->player_id;
    network->client_event_delivered_count++;
}

void network_host_receive(NetworkState *network, uint64_t local_gamedata_hash)
{
    uint8_t buffer[NET_MAX_PACKET_SIZE];
    NetAddr src = {0};
    int received = net_recv(&network->transport, &src, buffer, sizeof(buffer));
    while (received > 0) {
        DecodedPacket packet;
        ErrorState decode_err = {0};
        if (protocol_decode_packet(buffer, (size_t)received, &packet, &decode_err)) {
            if (packet.header.type == MSG_JOIN) {
                handle_join_datagram(network, src, &packet.reader, local_gamedata_hash);
            } else if (packet.header.type == MSG_INPUT) {
                handle_input_datagram(network, src, &packet.reader);
            } else if (packet.header.type == MSG_ACK) {
                handle_ack_datagram(network, src, &packet.reader);
            } else if (packet.header.type == MSG_EVENT) {
                handle_event_datagram(network, src, packet.header.seq, &packet.reader);
            }
        }
        received = net_recv(&network->transport, &src, buffer, sizeof(buffer));
    }
}

/* ---- S8.4c/S8.7a: reliable event sub-channel primitives ---- */

void network_broadcast_reliable_event(NetworkState *network, EventRecord event)
{
    for (int index = 0; index < network->client_count; index++) {
        NetClient *client = &network->clients[index];
        if (!client->active) {
            continue;
        }
        reliable_send(&client->event_channel, &network->transport, client->addr, event);
    }
}

void network_host_tick_reliable_channels(NetworkState *network, float delta_time)
{
    for (int index = 0; index < network->client_count; index++) {
        NetClient *client = &network->clients[index];
        if (!client->active) {
            continue;
        }
        reliable_tick(&client->event_channel, &network->transport, client->addr, delta_time);
    }
}

void network_client_send_ack(NetworkState *network)
{
    if (!network->host_event_channel.has_received_any) {
        return;
    }
    AckMessage ack = reliable_make_ack(&network->host_event_channel);
    uint8_t buffer[NET_MAX_PACKET_SIZE];
    size_t packet_len = 0;
    if (!protocol_encode_ack_packet(buffer, sizeof(buffer), 0, ack, &packet_len)) {
        return;
    }
    (void)net_send(&network->transport, network->join_target, buffer, packet_len);
}

void network_client_send_reliable_event(NetworkState *network, EventRecord event)
{
    reliable_send(&network->host_event_channel, &network->transport, network->join_target, event);
}

void network_client_tick_reliable_channel(NetworkState *network, float delta_time)
{
    reliable_tick(&network->host_event_channel, &network->transport, network->join_target, delta_time);
}

void network_host_send_acks(NetworkState *network)
{
    for (int index = 0; index < network->client_count; index++) {
        NetClient *client = &network->clients[index];
        if (!client->active || !client->event_channel.has_received_any) {
            continue;
        }
        AckMessage ack = reliable_make_ack(&client->event_channel);
        uint8_t buffer[NET_MAX_PACKET_SIZE];
        size_t packet_len = 0;
        if (!protocol_encode_ack_packet(buffer, sizeof(buffer), 0, ack, &packet_len)) {
            continue;
        }
        (void)net_send(&network->transport, client->addr, buffer, packet_len);
    }
}

/* ---- S8.7b: editor operations on the reliable sub-channel (send side) ---- */

void network_client_send_reliable_op(NetworkState *network, const EditorOp *operation)
{
    reliable_send_op(&network->host_event_channel, &network->transport, network->join_target, operation);
}

void network_host_broadcast_reliable_op(NetworkState *network, const EditorOp *operation)
{
    for (int index = 0; index < network->client_count; index++) {
        NetClient *client = &network->clients[index];
        if (!client->active) {
            continue;
        }
        reliable_send_op(&client->event_channel, &network->transport, client->addr, operation);
    }
}
