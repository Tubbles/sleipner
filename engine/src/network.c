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

#include "raylib.h" // Vector2

#include <stddef.h>
#include <stdint.h>
#include <string.h>

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
    /* S8.7c: the op counter is 0 while offline (see its own doc comment,
     * network.h) and comes alive here -- the first stamped op gets 1, never
     * the 0 "unstamped request" sentinel. Every joining client learns this
     * value via JoinAcceptMessage.op_seq_baseline (handle_join_datagram
     * below), so host and clients always agree on the stream's start. */
    network->next_op_seq = 1;
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
    /* Back to the offline zero (counters live only inside a session --
     * network_apply_hosting re-arms next_op_seq at hosting start, and the
     * JOIN_ACCEPT baseline re-seeds next_expected_op_seq at join time). */
    network->next_op_seq = 0;
    for (int index = 0; index < NETWORK_OP_HOLDBACK_MAX; index++) {
        network->held_ops[index] = (HeldEditorOp){0};
    }
    network->next_expected_op_seq = 0;
    /* S8.7d1: the lock table and the client deny list are session state --
     * back to the offline zero (host re-fills locks as clients acquire,
     * clients re-mirror from echoes). */
    for (int index = 0; index < NETWORK_LOCKS_MAX; index++) {
        network->locks[index] = (EntityLock){0};
    }
    for (int index = 0; index < NETWORK_LOCK_DENIED_MAX; index++) {
        network->lock_denied_entity_ids[index] = 0;
    }
    network->lock_denied_count = 0;
    /* S8.7e: the presence table and the outgoing bridge are session state --
     * back to the offline zero (both host and client re-fill the table from
     * incoming records, frame.c re-arms the bridge each editor frame). */
    for (int index = 0; index < NETWORK_PRESENCE_MAX; index++) {
        network->presence[index] = (PresenceEntry){0};
    }
    network->local_cursor = (Vector2){0};
    network->local_cursor_selected_entity_id = 0;
    network->local_cursor_valid = false;
    /* S8.7f1: the resync buffer and all its host/client bookkeeping are session
     * state -- back to the offline zero (a host re-arms resync_generation at the
     * next structural edit, a client re-reassembles from incoming chunks). */
    memset(network->resync_buffer, 0, sizeof(network->resync_buffer));
    network->resync_generation = 0;
    network->resync_total_bytes = 0;
    memset(network->resync_level_name, 0, sizeof(network->resync_level_name));
    network->resync_send_timer = 0.0F;
    /* S8.7f2: the editor-driven structural-resync debounce is session state. */
    network->structural_resync_debounce_timer = 0.0F;
    network->resync_incoming_generation = 0;
    network->resync_failed_generation = 0;
    network->resync_incoming_total_bytes = 0;
    network->resync_incoming_chunk_count = 0;
    memset(network->resync_chunk_bitmap, 0, sizeof(network->resync_chunk_bitmap));
    memset(network->resync_incoming_level_name, 0, sizeof(network->resync_incoming_level_name));
    network->resync_ready = false;
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

/* ---- S8.7d1: entity lock table helpers ---- */

EntityLock *network_lock_find(NetworkState *network, int entity_id)
{
    for (int index = 0; index < NETWORK_LOCKS_MAX; index++) {
        if (network->locks[index].active && network->locks[index].entity_id == entity_id) {
            return &network->locks[index];
        }
    }
    return nullptr;
}

bool network_lock_acquire(NetworkState *network, int entity_id, int holder_player_id)
{
    EntityLock *existing = network_lock_find(network, entity_id);
    if (existing != nullptr) {
        if (existing->holder_player_id != holder_player_id) {
            return false;
        }
        existing->seconds_since_refresh = 0.0F;
        return true;
    }
    for (int index = 0; index < NETWORK_LOCKS_MAX; index++) {
        if (!network->locks[index].active) {
            network->locks[index] = (EntityLock){.entity_id = entity_id,
                                                 .holder_player_id = holder_player_id,
                                                 .seconds_since_refresh = 0.0F,
                                                 .active = true};
            return true;
        }
    }
    return false;
}

bool network_lock_release(NetworkState *network, int entity_id, int holder_player_id)
{
    /* Scan for the entry owned by exactly (entity_id, holder_player_id) --
     * matching both keys in one condition keeps the two int parameters
     * distinguished at the point of use (a release you do not own is a
     * no-op). */
    for (int index = 0; index < NETWORK_LOCKS_MAX; index++) {
        EntityLock *lock = &network->locks[index];
        if (lock->active && lock->entity_id == entity_id && lock->holder_player_id == holder_player_id) {
            lock->active = false;
            return true;
        }
    }
    return false;
}

void network_lock_refresh_holder(NetworkState *network, int holder_player_id)
{
    for (int index = 0; index < NETWORK_LOCKS_MAX; index++) {
        if (network->locks[index].active && network->locks[index].holder_player_id == holder_player_id) {
            network->locks[index].seconds_since_refresh = 0.0F;
        }
    }
}

/* ---- S8.7e: editor presence table helpers ---- */

PresenceEntry *network_presence_find(NetworkState *network, int player_id)
{
    for (int index = 0; index < NETWORK_PRESENCE_MAX; index++) {
        if (network->presence[index].active && network->presence[index].player_id == player_id) {
            return &network->presence[index];
        }
    }
    return nullptr;
}

/* The slot for record.player_id (an existing active entry) or the first free
 * slot to claim, or nullptr when the table is full and the id is new. */
static PresenceEntry *presence_slot_for(NetworkState *network, int player_id)
{
    PresenceEntry *existing = network_presence_find(network, player_id);
    if (existing != nullptr) {
        return existing;
    }
    for (int index = 0; index < NETWORK_PRESENCE_MAX; index++) {
        if (!network->presence[index].active) {
            return &network->presence[index];
        }
    }
    return nullptr;
}

void network_presence_upsert(NetworkState *network, PresenceRecord record)
{
    PresenceEntry *slot = presence_slot_for(network, record.player_id);
    if (slot == nullptr) {
        return;
    }
    slot->player_id = record.player_id;
    slot->cursor = (Vector2){record.cursor_x, record.cursor_y};
    slot->selected_entity_id = record.selected_entity_id;
    strv_copy_to_cstr(record.name, slot->name, sizeof(slot->name));
    slot->seconds_since_seen = 0.0F;
    slot->active = true;
}

void network_presence_age(NetworkState *network, float delta_time)
{
    for (int index = 0; index < NETWORK_PRESENCE_MAX; index++) {
        PresenceEntry *entry = &network->presence[index];
        if (!entry->active) {
            continue;
        }
        entry->seconds_since_seen += delta_time;
        if (entry->seconds_since_seen > NETWORK_PRESENCE_TIMEOUT_SECONDS) {
            entry->active = false;
        }
    }
}

/* ---- S8.7f1: full-gamedata structural resync (CLIENT reassembly) ---- */

/* Chunks the whole TOML spans: ceil(total_bytes / CHUNK_BYTES). */
static uint16_t resync_expected_chunk_count(size_t total_bytes)
{
    return (uint16_t)((total_bytes + NETWORK_RESYNC_CHUNK_BYTES - 1) / NETWORK_RESYNC_CHUNK_BYTES);
}

/* Adopt a strictly-newer generation: store its header and reset the bitmap.
 * Rejects (marks the generation failed, returns false) a level name that would
 * not fit the cap, a total_bytes with no room for the null terminator the
 * reload needs, or a chunk_count that disagrees with total_bytes or exceeds the
 * chunk cap. A failed generation is never retried; only a newer one supersedes. */
static bool resync_adopt_generation(NetworkState *network, const ResyncChunk *chunk)
{
    bool header_ok = chunk->current_level_name.len < NETWORK_RESYNC_LEVEL_NAME_MAX &&
                     chunk->total_bytes < NETWORK_RESYNC_MAX_BYTES && chunk->chunk_count <= NETWORK_RESYNC_MAX_CHUNKS &&
                     chunk->chunk_count == resync_expected_chunk_count(chunk->total_bytes);
    if (!header_ok) {
        network->resync_failed_generation = chunk->generation;
        return false;
    }
    network->resync_incoming_generation = chunk->generation;
    network->resync_incoming_total_bytes = chunk->total_bytes;
    network->resync_incoming_chunk_count = chunk->chunk_count;
    memset(network->resync_chunk_bitmap, 0, sizeof(network->resync_chunk_bitmap));
    strv_copy_to_cstr(chunk->current_level_name, network->resync_incoming_level_name,
                      sizeof(network->resync_incoming_level_name));
    network->resync_ready = false;
    return true;
}

/* True once every chunk index 0..chunk_count-1 of the in-flight generation has
 * its bitmap bit set. */
static bool resync_all_chunks_present(const NetworkState *network)
{
    for (uint16_t index = 0; index < network->resync_incoming_chunk_count; index++) {
        if ((network->resync_chunk_bitmap[index / 8] & (uint8_t)(1U << (index % 8))) == 0) {
            return false;
        }
    }
    return true;
}

/* Validate one chunk of the already-adopted generation against the stored
 * header and, if valid, copy its payload into resync_buffer and set its bitmap
 * bit; flips resync_ready once all chunks are present. Fail-closed: a chunk
 * disagreeing with the stored geometry, or with a payload of the wrong length,
 * is dropped without touching the buffer -- never overruns. */
static void resync_store_chunk(NetworkState *network, const ResyncChunk *chunk)
{
    if (chunk->total_bytes != network->resync_incoming_total_bytes ||
        chunk->chunk_count != network->resync_incoming_chunk_count) {
        return;
    }
    uint16_t chunk_count = network->resync_incoming_chunk_count;
    if (chunk->chunk_index >= chunk_count) {
        return;
    }
    size_t offset = (size_t)chunk->chunk_index * NETWORK_RESYNC_CHUNK_BYTES;
    bool is_last = chunk->chunk_index == (uint16_t)(chunk_count - 1);
    size_t expected_len = is_last ? network->resync_incoming_total_bytes - offset : NETWORK_RESYNC_CHUNK_BYTES;
    if (chunk->payload.len != expected_len) {
        return;
    }
    memcpy(network->resync_buffer + offset, chunk->payload.ptr, chunk->payload.len);
    network->resync_chunk_bitmap[chunk->chunk_index / 8] |= (uint8_t)(1U << (chunk->chunk_index % 8));
    if (resync_all_chunks_present(network)) {
        network->resync_ready = true;
    }
}

void network_resync_accept_chunk(NetworkState *network, const ResyncChunk *chunk)
{
    if (chunk->generation == network->resync_failed_generation) {
        return;
    }
    if (chunk->generation < network->resync_incoming_generation) {
        return;
    }
    if (chunk->generation > network->resync_incoming_generation && !resync_adopt_generation(network, chunk)) {
        return;
    }
    resync_store_chunk(network, chunk);
}

void network_structural_mark_dirty(NetworkState *network)
{
    if (network->mode != NET_HOSTING) {
        return;
    }
    network->structural_resync_debounce_timer = NETWORK_STRUCTURAL_RESYNC_DEBOUNCE_SECONDS;
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
 * player_id plus (S8.7c) op_seq_baseline -- the host's CURRENT next_op_seq,
 * i.e. the first editor-op echo this client should expect. Fire-and-forget
 * like every other network_* send in this file. Called for every verified
 * JOIN (new registration or re-JOIN refresh), never for a refused one --
 * see handle_join_datagram below. Because it re-fires on every re-JOIN, a
 * genuinely reconnecting client always gets a fresh, current baseline, not
 * the one from its first join. */
static void send_join_accept(const NetworkState *network, NetAddr dest, int player_id)
{
    JoinAcceptMessage message = {.player_id = player_id, .op_seq_baseline = network->next_op_seq};
    uint8_t buffer[NET_MAX_PACKET_SIZE];
    size_t packet_len = 0;
    if (!protocol_encode_join_accept_packet(buffer, sizeof(buffer), 0, message, &packet_len)) {
        return;
    }
    (void)net_send(&network->transport, dest, buffer, packet_len);
}

/* Decode one payload as MSG_JOIN and either register-and-accept or refuse
 * the sender, per network_host_receive's own doc comment. S8.7f1: a
 * hash-MISMATCHED join is accepted if and only if a structural resync
 * generation is armed (resync_generation > 0) -- the host holds emitted bytes
 * ready to stream, and the fresh NetClient's resync_confirmed_generation 0
 * trails that generation, so the next sweep streams the newcomer the host's
 * full gamedata automatically and convergence is guaranteed. Host always
 * wins: the accepted mismatched joiner is force-converted to the host's
 * gamedata, the intended host-authoritative behavior for editing sessions. A
 * mismatched join while generation == 0 stays refused exactly as before --
 * genuinely different gamedata with nothing armed to converge it (S8.7i's
 * desync recovery owns that case later). */
static void handle_join_datagram(NetworkState *network, NetAddr src, PacketReader *reader, uint64_t local_gamedata_hash)
{
    JoinMessage message = {0};
    if (!protocol_decode_join(reader, &message)) {
        return;
    }
    JoinVerifyResult verify = protocol_join_verify(local_gamedata_hash, message.gamedata_hash);
    if (!verify.ok && network->resync_generation == 0) {
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
    /* S8.7f1: a resync-complete confirmation carries the generation this client
     * finished applying in EventRecord.entity_id (reused as a plain u32 carrier).
     * Record it so the host's send sweep stops re-streaming that generation to
     * this client. The bookkeeping fields above still update as for any event. */
    if (event.event_type == NETWORK_EVENT_RESYNC_COMPLETE) {
        client->resync_confirmed_generation = (uint32_t)event.entity_id;
    }
}

/* S8.7c: an MSG_OP from a registered client is not applied here (network.c
 * has no game.h) -- its RAW datagram bytes, length, and the sender's
 * player_id are copied into out_ops for net_session.c's network_host_tick
 * to decode and apply after the drain. See network_host_receive's own doc
 * comment (network.h) and PendingEditorOps for the raw-bytes / backpressure
 * rationale. The backpressure check runs BEFORE reliable_on_receive so an
 * op that overflows out_ops stays unmarked and is redelivered by the
 * client's resend -- lossless. `datagram`/`datagram_len` are the whole
 * received packet bytes (header included), copied verbatim so the later
 * decode sees exactly what arrived. */
static void handle_op_datagram(NetworkState *network,
                               NetAddr src,
                               uint32_t seq,
                               const uint8_t *datagram,
                               size_t datagram_len,
                               PendingEditorOps *out_ops)
{
    NetClient *client = find_client_by_addr(network, src);
    if (client == nullptr) {
        return;
    }
    if (out_ops->count >= NETWORK_PENDING_OPS_MAX) {
        return;
    }
    if (!reliable_on_receive(&client->event_channel, seq)) {
        return;
    }
    /* A legit op never exceeds RELIABLE_SLOT_PACKET_MAX (reliable_send_op
     * fails to encode past it, net_reliable.c); an oversized datagram is
     * malformed, so drop it (already dedup-marked, so the client stops
     * resending it) rather than overrun the fixed slot. */
    if (datagram_len > RELIABLE_SLOT_PACKET_MAX) {
        return;
    }
    int slot = out_ops->count;
    memcpy(out_ops->packets[slot], datagram, datagram_len);
    out_ops->lengths[slot] = datagram_len;
    out_ops->sender_player_ids[slot] = client->player_id;
    out_ops->count++;
}

/* S8.7e: decode one MSG_CURSOR payload from a registered client and upsert
 * ONLY the records whose player_id matches the sending client's own
 * player_id -- one peer, one voice: the wire is never trusted to speak for
 * another peer, so a record claiming a different id is silently dropped. A
 * CURSOR from an unregistered address is ignored, same as every other
 * per-client handler above. Fire-and-forget: a malformed payload is dropped
 * with no dedup and no error channel (presence is loss-tolerant, the next
 * tick supersedes it). Presence is pure NetworkState data, so this lives in
 * network.c with no game.h involvement. */
static void handle_cursor_datagram(NetworkState *network, NetAddr src, PacketReader *reader)
{
    const NetClient *client = find_client_by_addr(network, src);
    if (client == nullptr) {
        return;
    }
    PresenceRecord records[NETWORK_PRESENCE_MAX];
    size_t record_count = 0;
    if (!protocol_decode_cursor(reader, records, NETWORK_PRESENCE_MAX, &record_count)) {
        return;
    }
    for (size_t index = 0; index < record_count; index++) {
        if (records[index].player_id != client->player_id) {
            continue;
        }
        network_presence_upsert(network, records[index]);
    }
}

void network_host_receive(NetworkState *network, uint64_t local_gamedata_hash, PendingEditorOps *out_ops)
{
    uint8_t buffer[NET_MAX_PACKET_SIZE];
    NetAddr src = {0};
    int received = net_recv(&network->transport, &src, buffer, sizeof(buffer));
    while (received > 0) {
        DecodedPacket packet;
        ErrorState decode_err = {0};
        if (protocol_decode_packet(buffer, (size_t)received, &packet, &decode_err)) {
            /* S8.7d1: any datagram from a registered client is proof of life --
             * refresh that client's locks so the silence timeout only fires on
             * a peer that has genuinely gone quiet. A single lookup here (the
             * handlers below still do their own) keeps this independent of the
             * per-type dispatch. */
            const NetClient *sender = find_client_by_addr(network, src);
            if (sender != nullptr) {
                network_lock_refresh_holder(network, sender->player_id);
            }
            if (packet.header.type == MSG_JOIN) {
                handle_join_datagram(network, src, &packet.reader, local_gamedata_hash);
            } else if (packet.header.type == MSG_INPUT) {
                handle_input_datagram(network, src, &packet.reader);
            } else if (packet.header.type == MSG_ACK) {
                handle_ack_datagram(network, src, &packet.reader);
            } else if (packet.header.type == MSG_EVENT) {
                handle_event_datagram(network, src, packet.header.seq, &packet.reader);
            } else if (packet.header.type == MSG_OP) {
                handle_op_datagram(network, src, packet.header.seq, buffer, (size_t)received, out_ops);
            } else if (packet.header.type == MSG_CURSOR) {
                handle_cursor_datagram(network, src, &packet.reader);
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
