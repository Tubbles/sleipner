#include "net_session.h"

#include "alloc.h"
#include "arena.h"
#include "attribute.h"
#include "entity.h"
#include "error.h"
#include "game.h"
#include "input.h"
#include "level.h"
#include "net.h"
#include "net_protocol.h"
#include "net_reliable.h" // ReliableChannel, reliable_on_receive
#include "network.h"
#include "str.h"
#include "strv.h"

#include <stddef.h>
#include <stdint.h>

/* Defined below, alongside the rest of S8.4b's state-sync machinery --
 * forward-declared here since network_client_tick (right below) is kept
 * at the top of the file next to network_host_tick, mirroring S8.4a's
 * original layout. */
static void network_client_receive_state(GameState *state);

void network_host_tick(GameState *state, float delta_time)
{
    if (state->network.mode != NET_HOSTING) {
        return;
    }
    int clients_before = state->network.client_count;
    network_host_receive(&state->network, state->gamedata_hash);
    /* Every client register_client (network.c) added THIS call -- the
     * newly-joined tail of the clients array -- gets a full SNAPSHOT of
     * the current level before anything else reaches it. A re-JOIN from
     * an already-registered address is a no-op on client_count (see
     * register_client's own doc comment), so it does not re-send. */
    for (int index = clients_before; index < state->network.client_count; index++) {
        network_host_send_snapshot(state, &state->network.clients[index]);
        /* S8.4c: notify every currently active client (including the one
         * that just joined) that a new player connected -- the one
         * concrete event this slice wires end-to-end, see net_session.h's
         * NETWORK_EVENT_PLAYER_JOINED doc comment. */
        network_broadcast_reliable_event(&state->network,
                                         (EventRecord){.event_type = NETWORK_EVENT_PLAYER_JOINED,
                                                       .entity_id = state->network.clients[index].player_id,
                                                       .argument = (Strv){0}});
    }
    network_host_tick_reliable_channels(&state->network, delta_time);
}

void network_client_tick(GameState *state, const InputState *local_input)
{
    if (state->network.mode == NET_JOINING) {
        network_client_send_join(&state->network, state->gamedata_hash);
        state->network.mode = NET_CLIENT;
    }
    if (state->network.mode == NET_CLIENT) {
        network_client_send_input(&state->network, local_input);
        network_client_receive_state(state);
        network_client_send_ack(&state->network);
    }
}

/* ---- S8.4b: state sync ---- */

/* Conservative per-packet AttrRecord cap (v1, see net_session.h's own
 * "SNAPSHOT vs DELTA" note). Worst-case non-string record on the wire:
 * entity_id (4) + name length prefix (2) + NETWORK_ATTR_NAME_MAX name
 * bytes + type tag (1) + value (4, the widest non-string AttrType) = 59
 * bytes. NET_MAX_PACKET_SIZE (1400) minus PACKET_HEADER_SIZE (12) minus
 * the attr list's own u16 count (2) leaves 1386 usable bytes;
 * NETWORK_SYNC_RECORDS_PER_PACKET(20) * 59 = 1180, comfortable headroom
 * even before accounting for every attr name in this codebase today
 * (health, state, direction, ...) being far shorter than the assumed max.
 * A chunk that still overflows despite this budget (e.g. an unusually long
 * synced ATTR_STRING value) is silently dropped by send_sync_records
 * below, per its own doc comment -- never a buffer overflow, since
 * protocol_encode_snapshot_packet/_delta_packet themselves bounds-check
 * and return false rather than write past the buffer (net_protocol.c). */
#define NETWORK_ATTR_NAME_MAX 48
#define NETWORK_SYNC_RECORDS_PER_PACKET 20

/* Bound for the stack buffer used to null-terminate a synced ATTR_STRING
 * value before handing it to attr_set_string (which wants a const char *,
 * not a Strv). Every synced string attr value in this codebase today
 * (state: "walk"/"idle"/..., direction: "up"/"down"/...) sits far under
 * this; a longer one is silently truncated by strv_copy_to_cstr, the same
 * truncation contract rule.c's own MAX_ARG stack buffers already accept. */
#define NETWORK_ATTR_STRING_VALUE_MAX 128

static AttrRecordValue attr_value_to_record_value(AttrType type, AttrValue value)
{
    switch (type) {
    case ATTR_FLOAT:
        return (AttrRecordValue){.f = value.f};
    case ATTR_INT:
        return (AttrRecordValue){.i = value.i};
    case ATTR_BOOL:
        return (AttrRecordValue){.b = value.b};
    case ATTR_STRING:
        return (AttrRecordValue){.str = str_to_strv(value.str)};
    }
    return (AttrRecordValue){0};
}

/* Push one entity's synced state (position as the two reserved records,
 * then every entry in entity->attrs) onto *out. Every pushed record's
 * name/value Strv is a view into entity's own gamedata_arena-backed Str
 * data -- valid for the lifetime of the caller's send, per net_session.h's
 * own ownership note; nothing here allocates or copies. */
static void push_entity_sync_records(vec_attr_record *out, const Entity *entity)
{
    (void)vec_attr_record_push(out, (AttrRecord){.entity_id = entity->id,
                                                 .name = strv_from_cstr(NETWORK_ATTR_POS_X),
                                                 .type = ATTR_FLOAT,
                                                 .value = {.f = entity->position.x}});
    (void)vec_attr_record_push(out, (AttrRecord){.entity_id = entity->id,
                                                 .name = strv_from_cstr(NETWORK_ATTR_POS_Y),
                                                 .type = ATTR_FLOAT,
                                                 .value = {.f = entity->position.y}});
    for (int index = 0; index < entity->attrs.entries.count; index++) {
        const Attribute *attr = &entity->attrs.entries.data[index];
        AttrRecord record = {
            .entity_id = entity->id,
            .name = str_to_strv(attr->name),
            .type = attr->type,
            .value = attr_value_to_record_value(attr->type, attr->value),
        };
        (void)vec_attr_record_push(out, record);
    }
}

/* Build the full synced-state record list (S8.4b's "what's synced") for
 * every entity in level, into a scratch-arena-backed vec sized by actual
 * entity/attr count -- never an arbitrary MAX_* cap on either. */
static vec_attr_record build_level_sync_records(Arena *scratch_arena, const Level *level)
{
    vec_attr_record records = vec_attr_record_new(allocator_arena(scratch_arena));
    for (int index = 0; index < level->entities.count; index++) {
        push_entity_sync_records(&records, &level->entities.data[index]);
    }
    return records;
}

typedef bool (*AttrListPacketEncodeFn)(
    uint8_t *buffer, size_t capacity, uint32_t seq, const AttrRecord *records, size_t count, size_t *out_len);

/* Encode `records` in NETWORK_SYNC_RECORDS_PER_PACKET-sized chunks via
 * encode_fn (protocol_encode_snapshot_packet or _delta_packet) and send
 * each chunk to dest. A chunk that fails to encode (see
 * NETWORK_SYNC_RECORDS_PER_PACKET's own doc comment for when that can
 * happen despite the conservative budget) is silently skipped rather than
 * sent partially or truncated -- same fire-and-forget contract as every
 * other network_* send (network_client_send_join/_input,
 * network_host_receive's JOIN handling). */
static void send_sync_records(const NetTransport *transport,
                              NetAddr dest,
                              AttrListPacketEncodeFn encode_fn,
                              const vec_attr_record *records)
{
    for (int start = 0; start < records->count; start += NETWORK_SYNC_RECORDS_PER_PACKET) {
        int remaining = records->count - start;
        size_t chunk_count =
            remaining < NETWORK_SYNC_RECORDS_PER_PACKET ? (size_t)remaining : (size_t)NETWORK_SYNC_RECORDS_PER_PACKET;
        uint8_t buffer[NET_MAX_PACKET_SIZE];
        size_t packet_len = 0;
        if (!encode_fn(buffer, sizeof(buffer), 0, &records->data[start], chunk_count, &packet_len)) {
            continue;
        }
        (void)net_send(transport, dest, buffer, packet_len);
    }
}

void network_host_send_snapshot(GameState *state, const NetClient *client)
{
    SCRATCH_SCOPE(&state->scratch_arena);
    vec_attr_record records = build_level_sync_records(&state->scratch_arena, &state->gamedata.current_level);
    send_sync_records(&state->network.transport, client->addr, protocol_encode_snapshot_packet, &records);
}

void network_host_broadcast_delta(GameState *state)
{
    SCRATCH_SCOPE(&state->scratch_arena);
    vec_attr_record records = build_level_sync_records(&state->scratch_arena, &state->gamedata.current_level);
    for (int index = 0; index < state->network.client_count; index++) {
        if (!state->network.clients[index].active) {
            continue;
        }
        send_sync_records(&state->network.transport, state->network.clients[index].addr, protocol_encode_delta_packet,
                          &records);
    }
}

/* Apply one decoded record onto entity: NETWORK_ATTR_POS_X/_Y write
 * entity->position (see net_session.h's own "How position rides the
 * AttrRecord stream" note); every other record deep-copies its name (and,
 * for ATTR_STRING, its value) into a stack buffer -- record's Strv views
 * are not null-terminated and point into a shared receive buffer, but
 * attr_set_*'s API wants a null-terminated const char * -- then calls
 * attr_set_float/_int/_bool/_string, which itself copies the name (and
 * string value) into gamedata_alloc, so nothing borrowed from the packet
 * buffer survives this call. attr_set_* failure is treated the same way
 * capture_entity_delta (progression.c) treats attr_set_copy failure:
 * essentially unreachable against an arena allocator, silently skipped
 * rather than propagated -- there is no error channel back to the host for
 * a single dropped attribute anyway. */
static void apply_sync_record(Allocator *gamedata_alloc, Entity *entity, const AttrRecord *record)
{
    if (strv_eq_cstr(record->name, NETWORK_ATTR_POS_X)) {
        entity->position.x = record->value.f;
        return;
    }
    if (strv_eq_cstr(record->name, NETWORK_ATTR_POS_Y)) {
        entity->position.y = record->value.f;
        return;
    }

    char attr_name[NETWORK_ATTR_NAME_MAX];
    strv_copy_to_cstr(record->name, attr_name, sizeof(attr_name));
    switch (record->type) {
    case ATTR_FLOAT:
        (void)attr_set_float(gamedata_alloc, &entity->attrs, attr_name, record->value.f);
        return;
    case ATTR_INT:
        (void)attr_set_int(gamedata_alloc, &entity->attrs, attr_name, record->value.i);
        return;
    case ATTR_BOOL:
        (void)attr_set_bool(gamedata_alloc, &entity->attrs, attr_name, record->value.b);
        return;
    case ATTR_STRING: {
        char attr_value[NETWORK_ATTR_STRING_VALUE_MAX];
        strv_copy_to_cstr(record->value.str, attr_value, sizeof(attr_value));
        (void)attr_set_string(gamedata_alloc, &entity->attrs, (AttrStringPair){.name = attr_name, .value = attr_value});
        return;
    }
    }
}

void network_client_apply_state(GameState *state, const AttrRecord *records, size_t count)
{
    Allocator gamedata_alloc = allocator_arena(&state->gamedata_arena);
    for (size_t index = 0; index < count; index++) {
        const AttrRecord *record = &records[index];
        int entity_index = level_find_entity_by_id(&state->gamedata.current_level, record->entity_id);
        if (entity_index < 0) {
            continue;
        }
        apply_sync_record(&gamedata_alloc, &state->gamedata.current_level.entities.data[entity_index], record);
    }
}

/* "Apply" one newly-delivered reliable event (S8.4c) -- currently
 * session-level bookkeeping rather than a gameplay mutation, since
 * NETWORK_EVENT_PLAYER_JOINED (the one event this slice wires end-to-end,
 * see net_session.h's own doc comment) is a connection notification with
 * no game-state effect of its own yet. A future real gameplay trigger
 * routed over this channel would dispatch on event.event_type here
 * instead of just recording it. */
static void network_client_apply_event(NetworkState *network, EventRecord event)
{
    network->last_delivered_event_type = event.event_type;
    network->last_delivered_event_entity_id = event.entity_id;
    network->delivered_event_count++;
}

/* Decode one MSG_EVENT payload and dedup it via the client's own
 * host_event_channel (net_reliable.h's reliable_on_receive), keyed by the
 * packet header's own seq -- the reliable channel's sequence number, the
 * same one reliable_send (net_reliable.c) assigned on the host side. Only
 * a newly-delivered event (is_new) is applied; a duplicate (the original
 * plus a resend both arriving, or the same packet arriving twice) is
 * silently dropped -- exactly-once delivery. A payload that fails to
 * decode as EventRecord is ignored, same as every other malformed-payload
 * path in this file. */
static void network_client_receive_event(NetworkState *network, uint32_t seq, PacketReader *reader)
{
    EventRecord event = {0};
    if (!protocol_decode_event(reader, &event)) {
        return;
    }
    if (!reliable_on_receive(&network->host_event_channel, seq)) {
        return;
    }
    network_client_apply_event(network, event);
}

/* CLIENT side: drain every pending packet on state->network.transport and
 * apply whichever decodes as MSG_SNAPSHOT, MSG_DELTA, or MSG_EVENT.
 * MSG_SNAPSHOT/MSG_DELTA share the exact same attr-list wire shape and
 * this v1 sync sends full state either way (net_session.h's own "SNAPSHOT
 * vs DELTA" note), so there is nothing SNAPSHOT-specific to do here beyond
 * decoding whichever type arrived. MSG_EVENT (S8.4c) goes through
 * network_client_receive_event above for dedup before it is applied.
 * Anything that fails to decode, or decodes as an unrelated message type,
 * is silently ignored -- same as network_host_receive's own drain loop
 * (network.c). The decode array cap matches NETWORK_SYNC_RECORDS_PER_PACKET
 * exactly, since that is also the largest chunk send_sync_records ever
 * encodes into one packet. */
static void network_client_receive_state(GameState *state)
{
    uint8_t buffer[NET_MAX_PACKET_SIZE];
    NetAddr src = {0};
    int received = net_recv(&state->network.transport, &src, buffer, sizeof(buffer));
    while (received > 0) {
        DecodedPacket packet;
        ErrorState decode_err = {0};
        if (protocol_decode_packet(buffer, (size_t)received, &packet, &decode_err)) {
            if (packet.header.type == MSG_SNAPSHOT || packet.header.type == MSG_DELTA) {
                AttrRecord records[NETWORK_SYNC_RECORDS_PER_PACKET];
                size_t record_count = 0;
                if (protocol_decode_attr_list(&packet.reader, records, NETWORK_SYNC_RECORDS_PER_PACKET,
                                              &record_count)) {
                    network_client_apply_state(state, records, record_count);
                }
            } else if (packet.header.type == MSG_EVENT) {
                network_client_receive_event(&state->network, packet.header.seq, &packet.reader);
            }
        }
        received = net_recv(&state->network.transport, &src, buffer, sizeof(buffer));
    }
}
