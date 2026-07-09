#include "net_session.h"

#include "alloc.h"
#include "arena.h"
#include "attribute.h"
#include "blueprint.h"
#include "diag.h"
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

#include "raylib.h" // Vector2

#include <stddef.h>
#include <stdint.h>
#include <stdio.h> // snprintf

/* Defined below, alongside the rest of S8.4b's state-sync machinery --
 * forward-declared here since network_client_tick (right below) is kept
 * at the top of the file next to network_host_tick, mirroring S8.4a's
 * original layout. */
static void network_client_receive_state(GameState *state);

/* Bound for the stack buffer spawn_network_player formats
 * "network:<player_id>" into: NETWORK_INPUT_SOURCE_PREFIX (8 bytes) + up to
 * an int32's worth of digits + sign + nul -- comfortably under 32 even
 * though player_id in practice never exceeds NET_MAX_CLIENTS (network.h). */
#define NETWORK_INPUT_SOURCE_VALUE_MAX 32

/* HOST side (S8.6): spawn a player entity for a client that just registered
 * (network_host_tick's own newly-joined loop below), cloning the SAME
 * blueprint this host's own local player uses -- game_get_player always
 * resolves to the local:0 entity here, never one of the network players
 * this function itself appends after it (player_index is fixed at the
 * first behavior=="player" entity; see game_get_local_player's own doc
 * comment, game.h). Spawn position: the local player's CURRENT position --
 * the simplest choice that's always valid even for a level authoring no
 * dedicated multiplayer spawn markers. Documented choice, not the only one
 * considered (an authored player-start attr or a level spawn-point system
 * are the alternatives) -- revisit once a level actually needs to
 * distinguish "where the host is standing" from "where a joining player
 * should appear". Stamps the new entity's input_source instance attr
 * "network:<player_id>" so the existing input routing (game.c's
 * input_for_entity) picks it up unchanged, then rebuilds the count-parallel
 * tracking via game_respawn_rebuild_tracking (game.c) -- the same S6.6 "add
 * an entity at runtime and fix up tracking" primitive the editor's spawn
 * action uses, preserving overlap edge-state exactly like that path does.
 * A missing local player or its blueprint (never expected in practice --
 * every fixture this runs against authors exactly one behavior=="player"
 * blueprint) is a silent no-op, same fire-and-forget posture as every other
 * network_* helper in this file. */
static void spawn_network_player(Diag *diag, GameState *state, int player_id)
{
    Entity *local_player = game_get_player(state);
    if (!local_player) {
        return;
    }
    const Blueprint *player_blueprint = entity_resolve_blueprint(state, local_player->id);
    if (!player_blueprint) {
        return;
    }
    Allocator gamedata_alloc = allocator_arena(&state->gamedata_arena);
    if (!level_spawn_entity(diag, &state->gamedata.current_level, player_blueprint, local_player->position,
                            &state->gamedata.blueprints, texture_registry_lookup, state, &gamedata_alloc)) {
        return;
    }
    Entity *spawned = &state->gamedata.current_level.entities.data[state->gamedata.current_level.entities.count - 1];
    char input_source[NETWORK_INPUT_SOURCE_VALUE_MAX];
    (void)snprintf(input_source, sizeof(input_source), "%s%d", NETWORK_INPUT_SOURCE_PREFIX, player_id);
    (void)attr_set_string(&gamedata_alloc, &spawned->attrs,
                          (AttrStringPair){.name = "input_source", .value = input_source});
    (void)game_respawn_rebuild_tracking(diag, state);
}

void network_host_tick(GameState *state, float delta_time)
{
    if (state->network.mode != NET_HOSTING) {
        return;
    }
    Diag diag = {&state->error, &state->debug};
    int clients_before = state->network.client_count;
    network_host_receive(&state->network, state->gamedata_hash);
    /* Every client register_client (network.c) added THIS call -- the
     * newly-joined tail of the clients array -- gets (S8.6) a player entity
     * spawned for it BEFORE a full SNAPSHOT of the current level, so the
     * snapshot already includes it. A re-JOIN from an already-registered
     * address is a no-op on client_count (see register_client's own doc
     * comment), so neither the spawn nor the snapshot re-runs for it. */
    for (int index = clients_before; index < state->network.client_count; index++) {
        spawn_network_player(&diag, state, state->network.clients[index].player_id);
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
    if (state->network.mode != NET_JOINING && state->network.mode != NET_CLIENT) {
        return;
    }
    if (state->network.mode == NET_JOINING) {
        network_client_send_join(&state->network, state->gamedata_hash);
    }
    if (state->network.mode == NET_CLIENT) {
        network_client_send_input(&state->network, local_input);
    }
    /* Unconditional (JOINING or CLIENT): a MSG_JOIN_ACCEPT can arrive while
     * still JOINING (network_client_receive_state below applies it and
     * flips mode to NET_CLIENT itself), and draining here rather than only
     * once already-CLIENT means an accept and a SNAPSHOT that both landed
     * before this tick are both applied in the SAME pass -- no packet is
     * ever discarded by a narrower "just look for the accept" drain. */
    network_client_receive_state(state);
    if (state->network.mode == NET_CLIENT) {
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

/* Push one entity's synced state (blueprint_name, S8.6, first -- see that
 * reserved name's own doc comment, net_session.h -- then position as the
 * two reserved records, then every entry in entity->attrs) onto *out.
 * Every pushed record's name/value Strv is a view into entity's own
 * gamedata_arena-backed Str data -- valid for the lifetime of the caller's
 * send, per net_session.h's own ownership note; nothing here allocates or
 * copies. */
static void push_entity_sync_records(vec_attr_record *out, const Entity *entity)
{
    (void)vec_attr_record_push(out, (AttrRecord){.entity_id = entity->id,
                                                 .name = strv_from_cstr(NETWORK_ATTR_BLUEPRINT_NAME),
                                                 .type = ATTR_STRING,
                                                 .value = {.str = str_to_strv(entity->blueprint_name)}});
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

/* Shift entity's render-interp window (S8.5, entity.h's interp_from/
 * interp_to/interp_elapsed doc comments) to bracket a freshly synced
 * position: interp_from becomes whatever is CURRENTLY ON SCREEN (the
 * clamped lerp result at this instant, entity_render_position) so a
 * NET_CLIENT never visibly snaps when a new SNAPSHOT/DELTA lands,
 * interp_to becomes entity->position (already holding the complete new
 * (x, y) pair -- see this function's own call site below), and
 * interp_elapsed resets to 0 to start a fresh lerp. The very first synced
 * position this entity ever receives (interp_elapsed still negative,
 * entity_init's ENTITY_INTERP_NEVER_SYNCED seed) is a special case: there
 * is no "currently displayed" position worth lerping from yet, so both
 * ends of the window collapse onto the new position and interp_elapsed is
 * set to the interval itself -- entity_render_position then reports
 * exactly the new position, no lerp-from-spawn artifact. */
static void shift_interp_window(Entity *entity)
{
    if (entity->interp_elapsed < 0.0F) {
        entity->interp_from = entity->position;
        entity->interp_to = entity->position;
        entity->interp_elapsed = NETWORK_INTERP_INTERVAL_SECONDS;
        return;
    }
    entity->interp_from = entity_render_position(entity);
    entity->interp_to = entity->position;
    entity->interp_elapsed = 0.0F;
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
        /* entity->position now holds the complete new (x, y) pair --
         * push_entity_sync_records (above) always pushes POS_X immediately
         * before POS_Y for the same entity, so POS_Y is where the shift
         * belongs. See shift_interp_window's own doc comment for the S8.5
         * window-shift/first-sync logic. */
        entity->position.y = record->value.f;
        shift_interp_window(entity);
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

/* CLIENT side (S8.6): materialize a placeholder entity for record's
 * entity_id if this client's level doesn't already have one -- see
 * NETWORK_ATTR_BLUEPRINT_NAME's own doc comment (net_session.h) for why
 * this is necessary at all (a host-side runtime spawn has no local
 * counterpart from this client's own load-time parse) and why the id must
 * be FORCED rather than left to level_spawn_entity's own auto-assignment --
 * cross-peer id agreement is what lets every later record for this same
 * entity_id resolve normally, the same way it already does for an entity
 * both sides parsed identically at load time. No-op if record->entity_id
 * already resolves (already materialized, nothing to do), record isn't
 * actually an ATTR_STRING (malformed/reordered wire content -- must not
 * crash on it), or blueprint_find can't find the named blueprint (a
 * level/gamedata mismatch the JOIN hash check already guards against in
 * practice). Spawn position (0, 0) is a throwaway: push_entity_sync_records'
 * own push order always follows this record with NETWORK_ATTR_POS_X/_Y for
 * the SAME entity_id, in the SAME batch, which overwrite it immediately --
 * and entity_init's ENTITY_INTERP_NEVER_SYNCED seed means the first
 * shift_interp_window call (apply_sync_record's POS_Y branch) collapses the
 * render-interp window onto that real position too, so this placeholder
 * position is never actually rendered. */
static void ensure_synced_entity_exists(GameState *state, const AttrRecord *record)
{
    if (level_find_entity_by_id(&state->gamedata.current_level, record->entity_id) >= 0) {
        return;
    }
    if (record->type != ATTR_STRING) {
        return;
    }
    char blueprint_name[NETWORK_ATTR_NAME_MAX];
    strv_copy_to_cstr(record->value.str, blueprint_name, sizeof(blueprint_name));
    const Blueprint *blueprint = blueprint_find(&state->gamedata.blueprints, blueprint_name);
    if (!blueprint) {
        return;
    }
    Diag diag = {&state->error, &state->debug};
    Allocator gamedata_alloc = allocator_arena(&state->gamedata_arena);
    if (!level_spawn_entity(&diag, &state->gamedata.current_level, blueprint, (Vector2){0}, &state->gamedata.blueprints,
                            texture_registry_lookup, state, &gamedata_alloc)) {
        return;
    }
    Level *level = &state->gamedata.current_level;
    Entity *spawned = &level->entities.data[level->entities.count - 1];
    spawned->id = record->entity_id;
    if (level->next_entity_id <= record->entity_id) {
        level->next_entity_id = record->entity_id + 1;
    }
    (void)game_respawn_rebuild_tracking(&diag, state);
}

void network_client_apply_state(GameState *state, const AttrRecord *records, size_t count)
{
    Allocator gamedata_alloc = allocator_arena(&state->gamedata_arena);
    for (size_t index = 0; index < count; index++) {
        const AttrRecord *record = &records[index];
        if (strv_eq_cstr(record->name, NETWORK_ATTR_BLUEPRINT_NAME)) {
            ensure_synced_entity_exists(state, record);
            continue;
        }
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
 * apply whichever decodes as MSG_SNAPSHOT, MSG_DELTA, MSG_EVENT, or (S8.6)
 * MSG_JOIN_ACCEPT. MSG_SNAPSHOT/MSG_DELTA share the exact same attr-list
 * wire shape and this v1 sync sends full state either way (net_session.h's
 * own "SNAPSHOT vs DELTA" note), so there is nothing SNAPSHOT-specific to
 * do here beyond decoding whichever type arrived. MSG_EVENT (S8.4c) goes
 * through network_client_receive_event above for dedup before it is
 * applied. MSG_JOIN_ACCEPT stores its player_id onto
 * state->network.local_player_id and advances state->network.mode to
 * NET_CLIENT -- called unconditionally of mode by network_client_tick
 * (above), so this can run, and does run, while still NET_JOINING; a
 * duplicate accept (the host resends on every re-JOIN, network.c's
 * handle_join_datagram) just re-applies the same values, harmless. Anything
 * that fails to decode, or decodes as an unrelated message type, is
 * silently ignored -- same as network_host_receive's own drain loop
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
            } else if (packet.header.type == MSG_JOIN_ACCEPT) {
                JoinAcceptMessage accept = {0};
                if (protocol_decode_join_accept(&packet.reader, &accept)) {
                    state->network.local_player_id = accept.player_id;
                    state->network.mode = NET_CLIENT;
                }
            }
        }
        received = net_recv(&state->network.transport, &src, buffer, sizeof(buffer));
    }
}
