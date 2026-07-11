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
#include "net_reliable.h" // ReliableChannel, reliable_on_ack, reliable_on_receive
#include "network.h"
#include "str.h"
#include "strv.h"
#include "toml_emitter.h" // toml_emit_gamedata
#include "undo.h"         // UndoHistory, undo_history_clear, undo_history_new_entry

#include "raylib.h" // Vector2

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>  // snprintf
#include <string.h> // memcpy

/* Defined below, alongside the rest of S8.4b's state-sync machinery --
 * forward-declared here since network_client_tick (right below) is kept
 * at the top of the file next to network_host_tick, mirroring S8.4a's
 * original layout. */
static void network_client_receive_state(GameState *state);

/* S8.7f3b: the write-both attr appliers live with the rest of the S8.4b
 * state-sync machinery further down (they reuse apply_attr_record_to_set),
 * but the op handlers up here call them -- forward-declared to keep both in
 * their natural sections. */
static void apply_set_attr(Allocator *gamedata_alloc, Entity *entity, const AttrRecord *record);
static void apply_remove_attr(Allocator *gamedata_alloc, Entity *entity, Strv attr_name);

/* Bound for the stack buffer spawn_network_player formats
 * "network:<player_id>" into: NETWORK_INPUT_SOURCE_PREFIX (8 bytes) + up to
 * an int32's worth of digits + sign + nul -- comfortably under 32 even
 * though player_id in practice never exceeds NET_MAX_CLIENTS (network.h). */
#define NETWORK_INPUT_SOURCE_VALUE_MAX 32

/* Bound for stack buffers null-terminating a wire string (attr or blueprint
 * name) before handing it to a const char * API -- see
 * NETWORK_SYNC_RECORDS_PER_PACKET's doc comment (further down) for the
 * per-record wire budget derived from it. */
#define NETWORK_ATTR_NAME_MAX 48

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

/* Spawn blueprint's root entity (plus its blueprint-defined children) at
 * `position` into the current level. Returns the ROOT's index into
 * level->entities -- level_spawn_entity appends the root first, then its
 * children (level.c) -- or -1 on spawn failure. Tracking is deliberately NOT
 * rebuilt here: a caller that force-overwrites the root's id
 * (spawn_entity_with_forced_id below) must do so BEFORE
 * game_respawn_rebuild_tracking re-derives the id-keyed maps
 * (entity_blueprints, rule_table). */
static int spawn_blueprint_root(GameState *state, const Blueprint *blueprint, Vector2 position)
{
    Diag diag = {&state->error, &state->debug};
    Allocator gamedata_alloc = allocator_arena(&state->gamedata_arena);
    Level *level = &state->gamedata.current_level;
    int root_index = level->entities.count;
    if (!level_spawn_entity(&diag, level, blueprint, position, &state->gamedata.blueprints, texture_registry_lookup,
                            state, &gamedata_alloc)) {
        return -1;
    }
    return root_index;
}

/* CLIENT side: spawn blueprint at `position` and FORCE the new root entity's
 * id to forced_entity_id, bumping next_entity_id past it, then rebuild the
 * count-parallel tracking. The force is necessary, not optional:
 * level_spawn_entity's own id auto-assignment has no cross-peer
 * coordination, so only an explicit force keeps a host-assigned id
 * resolvable by every later op/record naming it. Shared by the S8.6
 * placeholder materialization (ensure_synced_entity_exists below) and the
 * S8.7f3a PLACE echo (client_apply_place_echo below). */
static void
spawn_entity_with_forced_id(GameState *state, const Blueprint *blueprint, Vector2 position, int forced_entity_id)
{
    int root_index = spawn_blueprint_root(state, blueprint, position);
    if (root_index < 0) {
        return;
    }
    Level *level = &state->gamedata.current_level;
    level->entities.data[root_index].id = forced_entity_id;
    if (level->next_entity_id <= forced_entity_id) {
        level->next_entity_id = forced_entity_id + 1;
    }
    Diag diag = {&state->error, &state->debug};
    (void)game_respawn_rebuild_tracking(&diag, state);
}

/* S8.7c/S8.7d1: stamp `operation` as `kind` under a fresh op_seq (authored by
 * the op's SENDER -- never the wire's author field, the host knows who sent
 * it) and echo it to every client. The ordered echo stream is what makes the
 * host's decision authoritative on every replica, in the one total order
 * op_seq defines. A dropped op (see the handlers below) is never stamped or
 * echoed, so that stream stays contiguous over exactly the ops that took
 * effect. */
static void host_stamp_and_broadcast(GameState *state, EditorOpKind kind, EditorOp *operation, int sender_player_id)
{
    operation->kind = kind;
    operation->author_player_id = sender_player_id;
    operation->op_seq = state->network.next_op_seq++;
    network_host_broadcast_reliable_op(&state->network, operation);
}

/* True when operation names the host's current level and a resolvable entity
 * in it -- the shared precondition a lock acquire needs. */
static bool host_op_targets_current_entity(const GameState *state, const EditorOp *operation)
{
    if (!strv_eq(operation->level_name, str_to_strv(state->gamedata.current_level.name))) {
        return false;
    }
    return level_find_entity_by_id(&state->gamedata.current_level, operation->entity_id) >= 0;
}

/* S8.7d1: a LOCK_ACQUIRE request from sender S. GRANT (echo LOCK_ACQUIRE) when
 * S names the current level, a known entity, AND network_lock_acquire claims
 * it for S (free entity or already held by S; a full table refuses). DENY
 * (echo LOCK_DENY) otherwise. Short-circuit order matters: network_lock_acquire
 * runs only after the level/entity check, so a cross-level or unknown-entity
 * request never touches the lock table. The DENY rides the same ordered echo
 * stream as every grant, so every replica sees one consistent total order --
 * only the named requester acts on it (client_apply_lock_deny_echo). */
static void host_handle_lock_acquire(GameState *state, EditorOp *operation, int sender_player_id)
{
    bool granted = host_op_targets_current_entity(state, operation) &&
                   network_lock_acquire(&state->network, operation->entity_id, sender_player_id);
    host_stamp_and_broadcast(state, granted ? EDITOR_OP_LOCK_ACQUIRE : EDITOR_OP_LOCK_DENY, operation,
                             sender_player_id);
}

/* S8.7d1: a LOCK_RELEASE request from sender S. Echo LOCK_RELEASE only if S
 * actually held the lock (network_lock_release), else silently drop -- a
 * release of something you do not hold is a no-op. */
static void host_handle_lock_release(GameState *state, EditorOp *operation, int sender_player_id)
{
    if (!network_lock_release(&state->network, operation->entity_id, sender_player_id)) {
        return;
    }
    host_stamp_and_broadcast(state, EDITOR_OP_LOCK_RELEASE, operation, sender_player_id);
}

/* S8.7c/S8.7d1: an EDITOR_OP_MOVE_ENTITY from sender S. Enforcement: a move of
 * an entity locked by someone OTHER than S is silently dropped (no apply, no
 * echo). Unlocked or held-by-S moves apply as before (entity->position set
 * directly, mirroring the editor's own drag on the host -- no interp shift,
 * host entities render authoritatively). Why unlocked moves STILL apply: a
 * client only produces a move after a grab, but its ACQUIRE and MOVE can
 * arrive host-side out of send order (per-client arrival order is not send
 * order across drops/resends) -- treating "unlocked" as permitted makes that
 * race harmless instead of losing the move. Cross-level and unknown-entity
 * moves are tolerant silent drops, same convention as the state appliers. */
static void host_handle_move(GameState *state, EditorOp *operation, int sender_player_id)
{
    if (!strv_eq(operation->level_name, str_to_strv(state->gamedata.current_level.name))) {
        return;
    }
    int entity_index = level_find_entity_by_id(&state->gamedata.current_level, operation->entity_id);
    if (entity_index < 0) {
        return;
    }
    const EntityLock *lock = network_lock_find(&state->network, operation->entity_id);
    if (lock != nullptr && lock->holder_player_id != sender_player_id) {
        return;
    }
    state->gamedata.current_level.entities.data[entity_index].position =
        (Vector2){operation->move_x, operation->move_y};
    host_stamp_and_broadcast(state, EDITOR_OP_MOVE_ENTITY, operation, sender_player_id);
}

/* S8.7f3a: an EDITOR_OP_DELETE_ENTITY from sender S. Level-name and
 * entity-must-exist preconditions mirror host_handle_move, as does the lock
 * rule: locked by someone OTHER than S is a silent drop (no apply, no
 * echo). On apply the whole descendant cascade is deleted
 * (game_delete_entity_cascade, game.h) and any lock entry on the entity is
 * cleared WITHOUT a LOCK_RELEASE echo -- the DELETE echo itself is the
 * authoritative "gone" signal, and EVERY replica (this host table here,
 * each client replica in client_apply_delete_echo, a hosting originator in
 * network_editor_commit_delete) drops the lock on apply. One rule
 * everywhere, no separate release traffic. */
static void host_handle_delete(GameState *state, EditorOp *operation, int sender_player_id)
{
    if (!host_op_targets_current_entity(state, operation)) {
        return;
    }
    EntityLock *lock = network_lock_find(&state->network, operation->entity_id);
    if (lock != nullptr && lock->holder_player_id != sender_player_id) {
        return;
    }
    game_delete_entity_cascade(state, operation->entity_id);
    if (lock != nullptr) {
        lock->active = false;
    }
    host_stamp_and_broadcast(state, EDITOR_OP_DELETE_ENTITY, operation, sender_player_id);
}

/* S8.7f3a/S8.7f3: an EDITOR_OP_PLACE_ENTITY from sender S. Preconditions:
 * names the current level and a blueprint that resolves -- anything else is a
 * silent drop (tolerant-skip convention). No locks are involved: a brand-new
 * entity cannot be contended. On apply the blueprint's root is spawned at
 * (move_x, move_y), every place_attrs record (S8.7f3, a networked paste's
 * attrs; empty for a bare place) is written onto the new root with the
 * write-both convergence rule, tracking is rebuilt (mirroring
 * spawn_network_player's spawn sequence), and the echo's entity_id is stamped
 * with the id the spawn assigned to the ROOT entity -- the request carried -1,
 * the host assigns ids (EditorOp's doc comment, net_protocol.h). The echoed id
 * and the same attr list are what every client forces/applies onto its own
 * replica spawn (client_apply_place_echo). */
static void host_handle_place(GameState *state, EditorOp *operation, int sender_player_id)
{
    if (!strv_eq(operation->level_name, str_to_strv(state->gamedata.current_level.name))) {
        return;
    }
    char blueprint_name[NETWORK_ATTR_NAME_MAX];
    strv_copy_to_cstr(operation->blueprint_name, blueprint_name, sizeof(blueprint_name));
    const Blueprint *blueprint = blueprint_find(&state->gamedata.blueprints, blueprint_name);
    if (!blueprint) {
        return;
    }
    int root_index = spawn_blueprint_root(state, blueprint, (Vector2){operation->move_x, operation->move_y});
    if (root_index < 0) {
        return;
    }
    operation->entity_id = state->gamedata.current_level.entities.data[root_index].id;
    Allocator gamedata_alloc = allocator_arena(&state->gamedata_arena);
    Entity *root = &state->gamedata.current_level.entities.data[root_index];
    for (size_t index = 0; index < operation->place_attr_count; index++) {
        apply_set_attr(&gamedata_alloc, root, &operation->place_attrs[index]);
    }
    Diag diag = {&state->error, &state->debug};
    (void)game_respawn_rebuild_tracking(&diag, state);
    host_stamp_and_broadcast(state, EDITOR_OP_PLACE_ENTITY, operation, sender_player_id);
}

/* S8.7f3b: an EDITOR_OP_SET_ATTR from sender S. Same level/entity/lock rules
 * as a move (locked by someone OTHER than S is silently dropped -- no apply,
 * no echo). On apply the record is written into BOTH attr sets (write-both
 * convergence, apply_set_attr), then stamped + echoed so every replica
 * converges in the one total order. */
static void host_handle_set_attr(GameState *state, EditorOp *operation, int sender_player_id)
{
    if (!strv_eq(operation->level_name, str_to_strv(state->gamedata.current_level.name))) {
        return;
    }
    int entity_index = level_find_entity_by_id(&state->gamedata.current_level, operation->entity_id);
    if (entity_index < 0) {
        return;
    }
    const EntityLock *lock = network_lock_find(&state->network, operation->entity_id);
    if (lock != nullptr && lock->holder_player_id != sender_player_id) {
        return;
    }
    Allocator gamedata_alloc = allocator_arena(&state->gamedata_arena);
    apply_set_attr(&gamedata_alloc, &state->gamedata.current_level.entities.data[entity_index], &operation->attr);
    host_stamp_and_broadcast(state, EDITOR_OP_SET_ATTR, operation, sender_player_id);
}

/* S8.7f3b: an EDITOR_OP_REMOVE_ATTR from sender S. Mirror of host_handle_set_attr.
 * attr_remove is a safe no-op on a name not present, so a removal is applied
 * and echoed unconditionally (given the level/entity/lock preconditions),
 * keeping every replica uniform even for a name only some peers still hold. */
static void host_handle_remove_attr(GameState *state, EditorOp *operation, int sender_player_id)
{
    if (!strv_eq(operation->level_name, str_to_strv(state->gamedata.current_level.name))) {
        return;
    }
    int entity_index = level_find_entity_by_id(&state->gamedata.current_level, operation->entity_id);
    if (entity_index < 0) {
        return;
    }
    const EntityLock *lock = network_lock_find(&state->network, operation->entity_id);
    if (lock != nullptr && lock->holder_player_id != sender_player_id) {
        return;
    }
    Allocator gamedata_alloc = allocator_arena(&state->gamedata_arena);
    apply_remove_attr(&gamedata_alloc, &state->gamedata.current_level.entities.data[entity_index],
                      operation->attr_name);
    host_stamp_and_broadcast(state, EDITOR_OP_REMOVE_ATTR, operation, sender_player_id);
}

/* S8.7c/S8.7d1/S8.7f3a/S8.7f3b: decode each op network_host_receive drained
 * into `ops` this tick and dispatch by kind -- move/delete enforcement, the
 * place spawn, the scene attr SET/REMOVE appliers, plus the lock protocol. A
 * malformed decode is skipped; a client never originates LOCK_DENY -- silently
 * dropped. */
static void host_apply_and_echo_ops(GameState *state, const PendingEditorOps *ops)
{
    for (int index = 0; index < ops->count; index++) {
        DecodedPacket packet;
        ErrorState decode_err = {0};
        if (!protocol_decode_packet(ops->packets[index], ops->lengths[index], &packet, &decode_err)) {
            continue;
        }
        EditorOp operation = {0};
        if (!protocol_decode_op(&packet.reader, &operation)) {
            continue;
        }
        int sender_player_id = ops->sender_player_ids[index];
        switch (operation.kind) {
        case EDITOR_OP_MOVE_ENTITY:
            host_handle_move(state, &operation, sender_player_id);
            break;
        case EDITOR_OP_DELETE_ENTITY:
            host_handle_delete(state, &operation, sender_player_id);
            break;
        case EDITOR_OP_PLACE_ENTITY:
            host_handle_place(state, &operation, sender_player_id);
            break;
        case EDITOR_OP_LOCK_ACQUIRE:
            host_handle_lock_acquire(state, &operation, sender_player_id);
            break;
        case EDITOR_OP_LOCK_RELEASE:
            host_handle_lock_release(state, &operation, sender_player_id);
            break;
        case EDITOR_OP_SET_ATTR:
            host_handle_set_attr(state, &operation, sender_player_id);
            break;
        case EDITOR_OP_REMOVE_ATTR:
            host_handle_remove_attr(state, &operation, sender_player_id);
            break;
        case EDITOR_OP_LOCK_DENY:
            break;
        }
    }
}

/* S8.7d1/S8.7f1: clear one active lock and echo a LOCK_RELEASE authored by its
 * former holder under a fresh op_seq, so every client's replica clears too.
 * Shared by the silence timeout (host_age_and_expire_locks) and the structural
 * resync barrier (network_host_begin_structural_resync). level_name is the
 * host's current level, passed in so the caller reads it once. */
static void host_force_release_lock(NetworkState *network, EntityLock *lock, Strv level_name)
{
    EditorOp release = {
        .kind = EDITOR_OP_LOCK_RELEASE,
        .level_name = level_name,
        .entity_id = lock->entity_id,
        .author_player_id = lock->holder_player_id,
        .op_seq = network->next_op_seq++,
    };
    lock->active = false;
    network_host_broadcast_reliable_op(network, &release);
}

/* S8.7d1: age every active lock whose holder is not 0 by delta_time (the
 * host's own player id 0 never ages -- the host cannot go silent on itself)
 * and force-release any past NETWORK_LOCK_TIMEOUT_SECONDS: clear the slot and
 * echo a LOCK_RELEASE authored by the former holder (so every client's
 * replica clears too). Called from network_host_tick AFTER network_host_receive
 * so this tick's incoming refreshes (network_lock_refresh_holder, driven by
 * every datagram a live client sends) land before the ageing -- an active
 * peer's lock is refreshed to 0 every tick and so never expires. */
static void host_age_and_expire_locks(GameState *state, float delta_time)
{
    NetworkState *network = &state->network;
    for (int index = 0; index < NETWORK_LOCKS_MAX; index++) {
        EntityLock *lock = &network->locks[index];
        if (!lock->active || lock->holder_player_id == 0) {
            continue;
        }
        lock->seconds_since_refresh += delta_time;
        if (lock->seconds_since_refresh < NETWORK_LOCK_TIMEOUT_SECONDS) {
            continue;
        }
        host_force_release_lock(network, lock, str_to_strv(state->gamedata.current_level.name));
    }
}

/* S8.7e: this peer's own presence record, assembled from the bridge fields
 * frame.c wrote this editor frame. own_player_id is the peer's own id (0 on
 * the host, local_player_id on a client); name is network->host_name (its
 * dual host/client display-name role, network.h). */
static PresenceRecord local_presence_record(const NetworkState *network, int own_player_id)
{
    return (PresenceRecord){
        .player_id = own_player_id,
        .cursor_x = network->local_cursor.x,
        .cursor_y = network->local_cursor.y,
        .selected_entity_id = network->local_cursor_selected_entity_id,
        .name = strv_from_cstr(network->host_name),
    };
}

/* S8.7e: build a record list from every active presence entry and relay it as
 * ONE MSG_CURSOR to every active client -- the host is the star-topology relay,
 * so each client learns every OTHER peer's cursor (and its own echoed back,
 * which the client skips as stale, see network_client_receive_cursor). Fire-
 * and-forget: a failed encode/send is silently dropped. */
static void host_broadcast_presence(NetworkState *network)
{
    PresenceRecord records[NETWORK_PRESENCE_MAX];
    size_t count = 0;
    for (int index = 0; index < NETWORK_PRESENCE_MAX; index++) {
        const PresenceEntry *entry = &network->presence[index];
        if (!entry->active) {
            continue;
        }
        records[count] = (PresenceRecord){
            .player_id = entry->player_id,
            .cursor_x = entry->cursor.x,
            .cursor_y = entry->cursor.y,
            .selected_entity_id = entry->selected_entity_id,
            .name = strv_from_cstr(entry->name),
        };
        count++;
    }
    uint8_t buffer[NET_MAX_PACKET_SIZE];
    size_t packet_len = 0;
    if (!protocol_encode_cursor_packet(buffer, sizeof(buffer), 0, records, count, &packet_len)) {
        return;
    }
    for (int index = 0; index < network->client_count; index++) {
        const NetClient *client = &network->clients[index];
        if (!client->active) {
            continue;
        }
        (void)net_send(&network->transport, client->addr, buffer, packet_len);
    }
}

/* S8.7e: HOST-side presence, once per hosting tick. Consume this editor
 * frame's bridged cursor into the host's own entry (player_id 0), age the
 * whole table so a silent peer drops out, then relay the aggregate. Ageing
 * runs BEFORE the broadcast so an expired entry is never relayed; the host's
 * own upsert runs before ageing so an actively-editing host never ages itself
 * out. Incoming client cursors were already upserted by network_host_receive
 * (above) this same tick, so the relay reflects the freshest of every peer. */
static void host_tick_presence(NetworkState *network, float delta_time)
{
    if (network->local_cursor_valid) {
        network_presence_upsert(network, local_presence_record(network, 0));
        network->local_cursor_valid = false;
    }
    network_presence_age(network, delta_time);
    host_broadcast_presence(network);
}

/* S8.7e: CLIENT-side presence, once per client tick. If frame.c bridged a
 * cursor this editor frame, send this client's own presence to the host (one
 * record) and mirror it into this client's own presence table (self-entry, so
 * rendering treats every cursor uniformly), then clear the bridge flag. Age
 * the table every tick so a peer the host stops relaying fades out. */
static void client_tick_presence(NetworkState *network, float delta_time)
{
    if (network->local_cursor_valid) {
        PresenceRecord record = local_presence_record(network, network->local_player_id);
        uint8_t buffer[NET_MAX_PACKET_SIZE];
        size_t packet_len = 0;
        if (protocol_encode_cursor_packet(buffer, sizeof(buffer), 0, &record, 1, &packet_len)) {
            (void)net_send(&network->transport, network->join_target, buffer, packet_len);
        }
        network_presence_upsert(network, record);
        network->local_cursor_valid = false;
    }
    network_presence_age(network, delta_time);
}

/* ---- S8.7f1: full-gamedata structural resync (HOST emit + stream) ---- */

/* Chunks the whole TOML spans: ceil(total_bytes / CHUNK_BYTES). */
static uint16_t host_resync_chunk_count(size_t total_bytes)
{
    return (uint16_t)((total_bytes + NETWORK_RESYNC_CHUNK_BYTES - 1) / NETWORK_RESYNC_CHUNK_BYTES);
}

/* Re-emit the whole gamedata (current level first, then every other level --
 * the exact combined-level array main.c's save_gamedata builds) into
 * resync_buffer, mirroring save_gamedata's toml_emit_gamedata call. Returns the
 * emitted length (< NETWORK_RESYNC_MAX_BYTES, and the buffer is null-terminated
 * at that offset, so game_load_gamedata can read it straight) on success, or a
 * negative value (state->error set) on emit failure or buffer overflow. */
static int host_emit_gamedata_into_resync_buffer(GameState *state)
{
    SCRATCH_SCOPE(&state->scratch_arena);
    int total_levels = 1 + state->gamedata.other_levels.count;
    Level *all_levels = (Level *)arena_alloc(&state->scratch_arena, sizeof(Level) * (size_t)total_levels);
    all_levels[0] = state->gamedata.current_level;
    for (int index = 0; index < state->gamedata.other_levels.count; index++) {
        all_levels[1 + index] = state->gamedata.other_levels.data[index];
    }
    return toml_emit_gamedata(&state->error, (char *)state->network.resync_buffer, (int)NETWORK_RESYNC_MAX_BYTES,
                              &state->gamedata.blueprints, &state->gamedata.subroutines, &state->gamedata.tileset,
                              &state->gamedata.atlas_regions, all_levels, total_levels);
}

/* Send the full chunk set of the armed generation to one client. Fire-and-forget:
 * a chunk that fails to encode is skipped, and the next sweep resends the whole
 * set anyway (repeat-set delivery). */
static void host_send_resync_to_client(NetworkState *network, const NetClient *client)
{
    uint16_t chunk_count = host_resync_chunk_count(network->resync_total_bytes);
    for (uint16_t index = 0; index < chunk_count; index++) {
        size_t offset = (size_t)index * NETWORK_RESYNC_CHUNK_BYTES;
        size_t remaining = network->resync_total_bytes - offset;
        size_t payload_len = remaining < NETWORK_RESYNC_CHUNK_BYTES ? remaining : NETWORK_RESYNC_CHUNK_BYTES;
        ResyncChunk chunk = {
            .generation = network->resync_generation,
            .total_bytes = (uint32_t)network->resync_total_bytes,
            .chunk_index = index,
            .chunk_count = chunk_count,
            .current_level_name = strv_from_cstr(network->resync_level_name),
            .payload = (Strv){.ptr = (const char *)(network->resync_buffer + offset), .len = payload_len},
        };
        uint8_t buffer[NET_MAX_PACKET_SIZE];
        size_t packet_len = 0;
        if (!protocol_encode_resync_packet(buffer, sizeof(buffer), 0, &chunk, &packet_len)) {
            continue;
        }
        (void)net_send(&network->transport, client->addr, buffer, packet_len);
    }
}

/* S8.7f1: once per host tick. While a generation is armed, every send interval,
 * stream the full chunk set to each client still trailing it (marking it as
 * owing a post-confirm snapshot, see NetClient.resync_snapshot_owed). All
 * clients confirmed => no chunk sends, but the generation stays armed so a LATE
 * JOINER (fresh NetClient, resync_confirmed_generation 0 < generation) is
 * streamed on the next sweep automatically -- this is the editing-join path.
 * Separately, EVERY tick (not just on the sweep cadence, so the heal lands the
 * tick the confirmation arrives): any client that was streamed chunks and has
 * now confirmed the armed generation gets ONE fresh entity SNAPSHOT
 * (network_host_send_snapshot, the join-time send) -- the streamed buffer is
 * frozen at the emit instant, so the reload left the client on emit-time
 * entity positions, and with deltas suspended during an editor session nothing
 * else would overlay the live ones. Called after network_host_receive so this
 * tick's incoming RESYNC_COMPLETE confirmations are already recorded before
 * deciding who still needs the stream or the heal. */
static void host_tick_resync(GameState *state, float delta_time)
{
    NetworkState *network = &state->network;
    if (network->resync_generation == 0) {
        return;
    }
    for (int index = 0; index < network->client_count; index++) {
        NetClient *client = &network->clients[index];
        if (!client->active || !client->resync_snapshot_owed ||
            client->resync_confirmed_generation < network->resync_generation) {
            continue;
        }
        client->resync_snapshot_owed = false;
        network_host_send_snapshot(state, client);
    }
    network->resync_send_timer += delta_time;
    if (network->resync_send_timer < NETWORK_RESYNC_SEND_INTERVAL_SECONDS) {
        return;
    }
    network->resync_send_timer = 0.0F;
    for (int index = 0; index < network->client_count; index++) {
        NetClient *client = &network->clients[index];
        if (!client->active || client->resync_confirmed_generation >= network->resync_generation) {
            continue;
        }
        client->resync_snapshot_owed = true;
        host_send_resync_to_client(network, client);
    }
}

bool network_host_begin_structural_resync(
    Diag *diag, GameState *state, UndoHistory *undo_history, TextureLookupFn texture_lookup, void *texture_user_data)
{
    /* No-op success under any non-hosting mode so the future editor hook can
     * call this unconditionally: single-player (NET_OFFLINE) has no peers to
     * resync, and a client is not the session authority (a client structural
     * edit is the next slice's blocked path). */
    if (state->network.mode != NET_HOSTING) {
        return true;
    }
    NetworkState *network = &state->network;

    /* (1) Capture the current level name -- clients reload onto it and the
     * self-reload below re-parses this same level. */
    Strv level_name = str_to_strv(state->gamedata.current_level.name);
    if (level_name.len >= NETWORK_RESYNC_LEVEL_NAME_MAX) {
        error_set(&state->error, "network_host_begin_structural_resync: level name too long (%zu bytes)",
                  level_name.len);
        return false;
    }
    strv_copy_to_cstr(level_name, network->resync_level_name, sizeof(network->resync_level_name));

    /* (2) Re-emit the whole gamedata as TOML into the stable send buffer. */
    int emitted = host_emit_gamedata_into_resync_buffer(state);
    if (emitted < 0) {
        error_wrap(&state->error, "network_host_begin_structural_resync");
        return false;
    }

    /* (3) Self-reload from those exact bytes: identical parse order realigns
     * per-level entity ids and game_load_gamedata recomputes gamedata_hash, so
     * host and clients re-converge once the clients reload the same bytes. On
     * failure state is left however the load left it -- the hot-reload contract. */
    if (!game_load_gamedata(diag, state,
                            (GamedataParams){.toml_string = (const char *)network->resync_buffer,
                                             .level_name = network->resync_level_name,
                                             .texture_lookup = texture_lookup,
                                             .texture_user_data = texture_user_data})) {
        return false;
    }

    /* (4) The structural edit is a session-wide barrier: clear undo and push a
     * fresh baseline, mirroring the hot-reload / transition reload paths. */
    undo_history_clear(undo_history);
    undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                           strv_from_cstr("Structural resync"));

    /* (5) Arm the next generation for streaming (first edit makes it 1);
     * resync_send_timer 0 sends on the next host tick's sweep. */
    network->resync_generation++;
    network->resync_total_bytes = (size_t)emitted;
    network->resync_send_timer = 0.0F;

    /* (6) Force-release every active lock: the barrier invalidates every claim
     * (entity ids are about to be reassigned), and each release echo clears the
     * matching replica on every client. Client op holdback / selection staleness
     * heals on the client's own apply (network_client_apply_resync). */
    for (int index = 0; index < NETWORK_LOCKS_MAX; index++) {
        EntityLock *lock = &network->locks[index];
        if (lock->active) {
            host_force_release_lock(network, lock, str_to_strv(state->gamedata.current_level.name));
        }
    }
    return true;
}

void network_host_tick(GameState *state, float delta_time)
{
    if (state->network.mode != NET_HOSTING) {
        return;
    }
    Diag diag = {&state->error, &state->debug};
    int clients_before = state->network.client_count;
    PendingEditorOps pending_ops = {0};
    network_host_receive(&state->network, state->gamedata_hash, &pending_ops);
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
    host_apply_and_echo_ops(state, &pending_ops);
    /* S8.7d1: this tick's incoming refreshes already landed inside
     * network_host_receive above, so ageing here expires only genuinely
     * silent holders. */
    host_age_and_expire_locks(state, delta_time);
    /* S8.7e: consume this frame's bridged cursor, age the presence table, and
     * relay the aggregate to every client -- AFTER network_host_receive so
     * this tick's incoming client cursors are already in the table. */
    host_tick_presence(&state->network, delta_time);
    network_host_tick_reliable_channels(&state->network, delta_time);
    network_host_send_acks(&state->network);
    /* S8.7f1: after this tick's incoming RESYNC_COMPLETE confirmations landed in
     * network_host_receive, stream the armed generation to every client still
     * trailing it (and any late joiner) on the send-interval cadence, and send
     * the one-shot post-confirm snapshot heal to any client that just caught up. */
    host_tick_resync(state, delta_time);
}

void network_client_tick(GameState *state, const InputState *local_input, float delta_time)
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
        network_client_tick_reliable_channel(&state->network, delta_time);
        network_client_send_ack(&state->network);
        /* S8.7e: send this client's own cursor to the host and age the
         * presence table (host-relayed entries were upserted by the receive
         * drain above). */
        client_tick_presence(&state->network, delta_time);
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
 * and return false rather than write past the buffer (net_protocol.c).
 * NETWORK_ATTR_NAME_MAX itself is defined at the top of this file -- the op
 * handlers there need it too. */
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

AttrRecord network_attr_record_from_attribute(int entity_id, const Attribute *attr)
{
    return (AttrRecord){
        .entity_id = entity_id,
        .name = str_to_strv(attr->name),
        .type = attr->type,
        .value = attr_value_to_record_value(attr->type, attr->value),
    };
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
        (void)vec_attr_record_push(out,
                                   network_attr_record_from_attribute(entity->id, &entity->attrs.entries.data[index]));
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

/* Write entity->position and shift its render-interp window (S8.5) so a
 * NET_CLIENT lerps smoothly toward the new position instead of snapping.
 * The shared "write position, then re-bracket the interp window" step for
 * both a POS_X/POS_Y sync pair (apply_sync_record below) and a whole
 * EDITOR_OP_MOVE_ENTITY echo (client_apply_move_echo below, S8.7c) --
 * shift_interp_window reads entity->position for its interp_to end, so the
 * write must precede it. */
static void apply_synced_position(Entity *entity, Vector2 position)
{
    entity->position = position;
    shift_interp_window(entity);
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
/* Write one decoded AttrRecord's typed value into `set`, deep-copying the
 * name (and, for ATTR_STRING, the value) out of the packet buffer into
 * gamedata_alloc first -- record's Strv views are not null-terminated and
 * point into a shared receive buffer, but attr_set_*'s API wants a
 * null-terminated const char *, and attr_set_* itself copies both into the
 * arena, so nothing borrowed from the packet survives. attr_set_* failure is
 * silently skipped (essentially unreachable against an arena allocator, same
 * as capture_entity_delta's attr_set_copy handling). Shared by the position-
 * free path of apply_sync_record and the SET_ATTR op appliers below. */
static void apply_attr_record_to_set(Allocator *gamedata_alloc, AttrSet *set, const AttrRecord *record)
{
    char attr_name[NETWORK_ATTR_NAME_MAX];
    strv_copy_to_cstr(record->name, attr_name, sizeof(attr_name));
    switch (record->type) {
    case ATTR_FLOAT:
        (void)attr_set_float(gamedata_alloc, set, attr_name, record->value.f);
        return;
    case ATTR_INT:
        (void)attr_set_int(gamedata_alloc, set, attr_name, record->value.i);
        return;
    case ATTR_BOOL:
        (void)attr_set_bool(gamedata_alloc, set, attr_name, record->value.b);
        return;
    case ATTR_STRING: {
        char attr_value[NETWORK_ATTR_STRING_VALUE_MAX];
        strv_copy_to_cstr(record->value.str, attr_value, sizeof(attr_value));
        (void)attr_set_string(gamedata_alloc, set, (AttrStringPair){.name = attr_name, .value = attr_value});
        return;
    }
    }
}

static void apply_sync_record(Allocator *gamedata_alloc, Entity *entity, const AttrRecord *record)
{
    if (strv_eq_cstr(record->name, NETWORK_ATTR_POS_X)) {
        entity->position.x = record->value.f;
        return;
    }
    if (strv_eq_cstr(record->name, NETWORK_ATTR_POS_Y)) {
        /* POS_X already wrote entity->position.x via the record immediately
         * before this one (push_entity_sync_records always pushes POS_X then
         * POS_Y for the same entity), so the complete new (x, y) is known
         * here -- the interp shift belongs on POS_Y. See apply_synced_position
         * / shift_interp_window for the S8.5 window-shift/first-sync logic. */
        apply_synced_position(entity, (Vector2){entity->position.x, record->value.f});
        return;
    }
    apply_attr_record_to_set(gamedata_alloc, &entity->attrs, record);
}

/* S8.7f3b: apply one EDITOR_OP_SET_ATTR record onto entity, writing the value
 * into BOTH entity->attrs AND entity->persisted_attrs (the write-both
 * convergence rule). The scene editor sites differ in which set they mutate
 * locally, but every scene attr edit is an authored, persistence-intended
 * edit; converging both sets on every peer -- including the originator, via
 * its own echo re-apply, which is idempotent -- makes all replicas
 * byte-identical in both sets, which the emit/resync path then preserves. */
static void apply_set_attr(Allocator *gamedata_alloc, Entity *entity, const AttrRecord *record)
{
    apply_attr_record_to_set(gamedata_alloc, &entity->attrs, record);
    apply_attr_record_to_set(gamedata_alloc, &entity->persisted_attrs, record);
}

/* S8.7f3b: remove attr_name from BOTH sets, the mirror of apply_set_attr.
 * attr_remove is a safe no-op on a name a given set does not hold, so a
 * removal present in only one set (or neither) converges uniformly. */
static void apply_remove_attr(Allocator *gamedata_alloc, Entity *entity, Strv attr_name)
{
    char name[NETWORK_ATTR_NAME_MAX];
    strv_copy_to_cstr(attr_name, name, sizeof(name));
    attr_remove(gamedata_alloc, &entity->attrs, name);
    attr_remove(gamedata_alloc, &entity->persisted_attrs, name);
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
    spawn_entity_with_forced_id(state, blueprint, (Vector2){0}, record->entity_id);
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

/* S8.7c: apply one MOVE echo onto this client's replica -- reuses
 * apply_synced_position so the moved entity lerps smoothly toward its new
 * position exactly as a synced POS_X/POS_Y pair does. No-op for an entity id
 * this client's level does not have. */
static void client_apply_move_echo(GameState *state, const EditorOp *operation)
{
    int entity_index = level_find_entity_by_id(&state->gamedata.current_level, operation->entity_id);
    if (entity_index < 0) {
        return;
    }
    apply_synced_position(&state->gamedata.current_level.entities.data[entity_index],
                          (Vector2){operation->move_x, operation->move_y});
}

/* S8.7d1: mirror a LOCK_ACQUIRE echo into this client's replica table. The
 * host echo is authoritative, so it must OVERWRITE even a locally-stale
 * different holder -- network_lock_acquire refuses an entity held by someone
 * else, so drop any existing replica entry first, then the acquire always
 * claims a fresh slot for the echoed holder. */
static void client_apply_lock_acquire_echo(NetworkState *network, const EditorOp *operation)
{
    EntityLock *stale = network_lock_find(network, operation->entity_id);
    if (stale != nullptr) {
        stale->active = false;
    }
    (void)network_lock_acquire(network, operation->entity_id, operation->author_player_id);
}

/* S8.7d1: mirror a LOCK_RELEASE echo -- clear the entity's replica lock
 * unconditionally (the host echo is authoritative; do not holder-check the
 * replica). No-op if the client's replica already has no lock on it. */
static void client_apply_lock_release_echo(NetworkState *network, const EditorOp *operation)
{
    EntityLock *lock = network_lock_find(network, operation->entity_id);
    if (lock != nullptr) {
        lock->active = false;
    }
}

/* S8.7d1: a LOCK_DENY echo. Only the named requester acts on it: if this echo
 * is addressed to this peer (author == local_player_id), append the entity id
 * for the editor to drain next frame (the drain is S8.7d2; this slice only
 * populates). Overflow past NETWORK_LOCK_DENIED_MAX is dropped silently -- one
 * gesture requests at most the 32-entry multiselect worth of locks. */
static void client_apply_lock_deny_echo(NetworkState *network, const EditorOp *operation)
{
    if (operation->author_player_id != network->local_player_id) {
        return;
    }
    if (network->lock_denied_count >= NETWORK_LOCK_DENIED_MAX) {
        return;
    }
    network->lock_denied_entity_ids[network->lock_denied_count] = operation->entity_id;
    network->lock_denied_count++;
}

/* S8.7f3a: apply one DELETE echo. The cascade delete
 * (game_delete_entity_cascade, game.h) no-ops when the id no longer
 * resolves -- the expected ORIGINATOR case: the deleting client already
 * removed its local copy as a preview. Any replica lock entry on the entity
 * is dropped without a LOCK_RELEASE echo ever arriving -- the DELETE echo IS
 * the "gone" signal, the same one-rule-everywhere clear the host applies
 * (host_handle_delete). The local editor needs no plumbing beyond this:
 * selection and watches resolve by stable id, so a vanished id simply reads
 * as "nothing selected" on its next lookup (editor/core.c's
 * delete_selected_entity doc comment) -- unlike a structural resync (S8.7f1),
 * whose blast radius forces a frame-layer reset, a delete's blast radius is
 * one id the selection code already tolerates. */
static void client_apply_delete_echo(GameState *state, const EditorOp *operation)
{
    game_delete_entity_cascade(state, operation->entity_id);
    EntityLock *lock = network_lock_find(&state->network, operation->entity_id);
    if (lock != nullptr) {
        lock->active = false;
    }
}

/* S8.7f3a/S8.7f3: apply one PLACE echo. An entity with the echoed id already
 * existing is a tolerant dedup no-op (e.g. this peer already materialized it
 * from a redelivered echo) -- its place_attrs were already applied on the
 * spawn that first materialized it, so they are skipped here too. Otherwise
 * the named blueprint is spawned at the echoed position with the echoed
 * HOST-ASSIGNED id forced onto the root -- the exact mechanism
 * ensure_synced_entity_exists uses, shared via spawn_entity_with_forced_id --
 * then every place_attrs record (S8.7f3, a networked paste's attrs; empty for
 * a bare place) is written onto the new root with the write-both convergence
 * rule, exactly as host_handle_place applied them on the authority. An unknown
 * blueprint is a silent skip, the appliers' tolerant convention. The
 * ORIGINATING client also lands here: it spawned nothing locally at commit
 * time (frame.c's handle_place_input / editor/core.c's handle_browse_paste,
 * NET_CLIENT path), so this echo apply is what materializes its own place --
 * echo-driven spawn is what makes cross-peer id divergence impossible. */
static void client_apply_place_echo(GameState *state, const EditorOp *operation)
{
    if (level_find_entity_by_id(&state->gamedata.current_level, operation->entity_id) >= 0) {
        return;
    }
    char blueprint_name[NETWORK_ATTR_NAME_MAX];
    strv_copy_to_cstr(operation->blueprint_name, blueprint_name, sizeof(blueprint_name));
    const Blueprint *blueprint = blueprint_find(&state->gamedata.blueprints, blueprint_name);
    if (!blueprint) {
        return;
    }
    spawn_entity_with_forced_id(state, blueprint, (Vector2){operation->move_x, operation->move_y},
                                operation->entity_id);
    int root_index = level_find_entity_by_id(&state->gamedata.current_level, operation->entity_id);
    if (root_index < 0) {
        return;
    }
    Allocator gamedata_alloc = allocator_arena(&state->gamedata_arena);
    Entity *root = &state->gamedata.current_level.entities.data[root_index];
    for (size_t index = 0; index < operation->place_attr_count; index++) {
        apply_set_attr(&gamedata_alloc, root, &operation->place_attrs[index]);
    }
}

/* S8.7f3b: apply one SET_ATTR echo onto this client's replica, writing both
 * attr sets (apply_set_attr, the write-both convergence rule). Tolerant skip
 * for an entity id this client's level does not have. The client re-derives
 * collision shapes fresh from attrs every frame (S4.1), so a collision-attr
 * change needs no shape rebuild here -- the next render/collision pass reads
 * the updated attrs directly. */
static void client_apply_set_attr_echo(GameState *state, const EditorOp *operation)
{
    int entity_index = level_find_entity_by_id(&state->gamedata.current_level, operation->entity_id);
    if (entity_index < 0) {
        return;
    }
    Allocator gamedata_alloc = allocator_arena(&state->gamedata_arena);
    apply_set_attr(&gamedata_alloc, &state->gamedata.current_level.entities.data[entity_index], &operation->attr);
}

/* S8.7f3b: apply one REMOVE_ATTR echo -- mirror of client_apply_set_attr_echo,
 * removing from both sets. Same fresh-from-attrs collision-shape rationale. */
static void client_apply_remove_attr_echo(GameState *state, const EditorOp *operation)
{
    int entity_index = level_find_entity_by_id(&state->gamedata.current_level, operation->entity_id);
    if (entity_index < 0) {
        return;
    }
    Allocator gamedata_alloc = allocator_arena(&state->gamedata_arena);
    apply_remove_attr(&gamedata_alloc, &state->gamedata.current_level.entities.data[entity_index],
                      operation->attr_name);
}

/* S8.7c/S8.7d1/S8.7f3a: apply one host op echo onto this client's replica,
 * dispatched by kind. All kinds share the current-level tolerant skip
 * (silent, same convention as apply_sync_record) -- an echo naming a level
 * other than the current one is ignored (cross-level ops are a later slice).
 * MOVE writes the entity position; DELETE cascade-deletes and drops the
 * replica lock; PLACE materializes the spawn under the echoed id; the three
 * lock kinds maintain the echo-driven replica lock table
 * (LOCK_ACQUIRE/RELEASE) and the deny list (LOCK_DENY). SET_ATTR/REMOVE_ATTR
 * (S8.7f3b) write/remove the attr on both attr sets (write-both convergence). */
static void client_apply_op(GameState *state, const EditorOp *operation)
{
    if (!strv_eq(operation->level_name, str_to_strv(state->gamedata.current_level.name))) {
        return;
    }
    switch (operation->kind) {
    case EDITOR_OP_MOVE_ENTITY:
        client_apply_move_echo(state, operation);
        return;
    case EDITOR_OP_DELETE_ENTITY:
        client_apply_delete_echo(state, operation);
        return;
    case EDITOR_OP_PLACE_ENTITY:
        client_apply_place_echo(state, operation);
        return;
    case EDITOR_OP_LOCK_ACQUIRE:
        client_apply_lock_acquire_echo(&state->network, operation);
        return;
    case EDITOR_OP_LOCK_RELEASE:
        client_apply_lock_release_echo(&state->network, operation);
        return;
    case EDITOR_OP_LOCK_DENY:
        client_apply_lock_deny_echo(&state->network, operation);
        return;
    case EDITOR_OP_SET_ATTR:
        client_apply_set_attr_echo(state, operation);
        return;
    case EDITOR_OP_REMOVE_ATTR:
        client_apply_remove_attr_echo(state, operation);
        return;
    }
}

/* Decode the op parked in `held` and apply it. A slot whose bytes fail to
 * decode is still consumed by the caller (its op_seq occupied a place in
 * the total order), just not applied. */
static void client_apply_held_op(GameState *state, const HeldEditorOp *held)
{
    DecodedPacket packet;
    ErrorState decode_err = {0};
    if (!protocol_decode_packet(held->packet, held->length, &packet, &decode_err)) {
        return;
    }
    EditorOp operation = {0};
    if (!protocol_decode_op(&packet.reader, &operation)) {
        return;
    }
    client_apply_op(state, &operation);
}

/* After an in-order op applies and next_expected_op_seq advances, apply any
 * consecutive op_seqs already parked in held_ops, freeing each slot -- so a
 * gap that filled out of order converges the moment its missing prefix
 * arrives. */
static void client_drain_held_ops(GameState *state)
{
    NetworkState *net = &state->network;
    bool progressed = true;
    while (progressed) {
        progressed = false;
        for (int index = 0; index < NETWORK_OP_HOLDBACK_MAX; index++) {
            HeldEditorOp *held = &net->held_ops[index];
            if (!held->occupied || held->op_seq != net->next_expected_op_seq) {
                continue;
            }
            client_apply_held_op(state, held);
            held->occupied = false;
            net->next_expected_op_seq++;
            progressed = true;
            break;
        }
    }
}

static int client_find_free_held_slot(const NetworkState *net)
{
    for (int index = 0; index < NETWORK_OP_HOLDBACK_MAX; index++) {
        if (!net->held_ops[index].occupied) {
            return index;
        }
    }
    return -1;
}

/* S8.7c: dedup + strict-op_seq-order one MSG_OP echo. `packet` is the
 * already-header-decoded view (for the op_seq peek and header.seq dedup);
 * `bytes`/`len` are the raw datagram, copied into a held slot when the echo
 * arrives ahead of its turn. Ordering vs the transport-level reliable
 * dedup:
 *  - op_seq == next_expected: apply now, mark received, drain held_ops for
 *    the consecutive successors.
 *  - op_seq  > next_expected: park it -- but only if a slot is free; a full
 *    holdback returns WITHOUT marking received, so the sender's resend
 *    redelivers it once a slot frees (lossless). Slot availability is
 *    therefore checked BEFORE reliable_on_receive.
 *  - op_seq  < next_expected: already applied; mark received and drop. */
static void network_client_receive_op(GameState *state, const uint8_t *bytes, size_t len, DecodedPacket *packet)
{
    EditorOp operation = {0};
    if (!protocol_decode_op(&packet->reader, &operation)) {
        return;
    }
    NetworkState *net = &state->network;
    uint32_t channel_seq = packet->header.seq;
    if (operation.op_seq == net->next_expected_op_seq) {
        (void)reliable_on_receive(&net->host_event_channel, channel_seq);
        client_apply_op(state, &operation);
        net->next_expected_op_seq++;
        client_drain_held_ops(state);
        return;
    }
    if (operation.op_seq > net->next_expected_op_seq) {
        /* Oversized is malformed (the host never encodes an op past
         * RELIABLE_SLOT_PACKET_MAX, net_reliable.c) -- drop before the slot
         * copy so it can never overrun a held slot. */
        if (len > RELIABLE_SLOT_PACKET_MAX) {
            return;
        }
        int slot = client_find_free_held_slot(net);
        if (slot < 0) {
            return;
        }
        (void)reliable_on_receive(&net->host_event_channel, channel_seq);
        memcpy(net->held_ops[slot].packet, bytes, len);
        net->held_ops[slot].length = len;
        net->held_ops[slot].op_seq = operation.op_seq;
        net->held_ops[slot].occupied = true;
        return;
    }
    (void)reliable_on_receive(&net->host_event_channel, channel_seq);
}

/* S8.7c: apply one decoded MSG_JOIN_ACCEPT. The op-ordering baseline
 * (next_expected_op_seq seeded from accept.op_seq_baseline, held_ops
 * cleared) is applied ONLY on the JOINING->CLIENT flip edge -- i.e. when
 * mode is not yet NET_CLIENT at the moment the accept lands. The host
 * re-sends an accept for every re-JOIN, and a duplicate arriving
 * mid-session (original + resent both delivered, or a stale in-flight copy)
 * MUST NOT re-seed the counter or drop parked echoes: ops may already have
 * applied and advanced next_expected_op_seq past the baseline, so a reset
 * would corrupt the ordering state (re-apply already-applied ops or park
 * the stream forever). local_player_id/mode stay unconditional as before --
 * a duplicate re-applies the same values, harmless. */
static void client_apply_join_accept(NetworkState *network, JoinAcceptMessage accept)
{
    if (network->mode != NET_CLIENT) {
        network->next_expected_op_seq = accept.op_seq_baseline;
        for (int index = 0; index < NETWORK_OP_HOLDBACK_MAX; index++) {
            network->held_ops[index].occupied = false;
        }
    }
    network->local_player_id = accept.player_id;
    network->mode = NET_CLIENT;
}

/* S8.7e: apply one host-relayed MSG_CURSOR onto this client's presence table.
 * Upsert every record EXCEPT this client's own (player_id == local_player_id):
 * the host echoes this client's entry back in the aggregate relay, but the
 * local bridge copy (client_tick_presence) is always fresher, so the echo of
 * self is skipped. Fire-and-forget: a malformed payload is silently dropped,
 * the next relayed packet supersedes it. */
static void network_client_receive_cursor(NetworkState *network, PacketReader *reader)
{
    PresenceRecord records[NETWORK_PRESENCE_MAX];
    size_t record_count = 0;
    if (!protocol_decode_cursor(reader, records, NETWORK_PRESENCE_MAX, &record_count)) {
        return;
    }
    for (size_t index = 0; index < record_count; index++) {
        if (records[index].player_id == network->local_player_id) {
            continue;
        }
        network_presence_upsert(network, records[index]);
    }
}

/* CLIENT side: drain every pending packet on state->network.transport and
 * apply whichever decodes as MSG_SNAPSHOT, MSG_DELTA, MSG_EVENT, MSG_OP
 * (S8.7c), MSG_ACK (S8.7a), or (S8.6) MSG_JOIN_ACCEPT. A MSG_OP echo goes
 * through network_client_receive_op above -- deduped on the packet header
 * seq like MSG_EVENT, then applied in strict op_seq order (out-of-order
 * arrivals parked in held_ops until their turn). MSG_SNAPSHOT/MSG_DELTA
 * share the
 * exact same attr-list wire shape and this v1 sync sends full state either
 * way (net_session.h's own "SNAPSHOT vs DELTA" note), so there is nothing
 * SNAPSHOT-specific to do here beyond decoding whichever type arrived.
 * MSG_EVENT (S8.4c) goes through network_client_receive_event above for
 * dedup before it is applied. MSG_ACK (S8.7a) is applied to
 * host_event_channel's own send-side (reliable_on_ack, net_reliable.h),
 * clearing whichever of this client's own outgoing reliable events (see
 * network_client_send_reliable_event, network.h) the host's ack covers --
 * the mirror of network.c's handle_ack_datagram, one direction reversed.
 * MSG_JOIN_ACCEPT goes through client_apply_join_accept above: it stores
 * its player_id onto state->network.local_player_id, advances
 * state->network.mode to NET_CLIENT, and (S8.7c, on the JOINING->CLIENT
 * flip edge only -- see that helper's own doc comment) seeds the op-echo
 * ordering baseline from accept.op_seq_baseline -- called unconditionally
 * of mode by network_client_tick (above), so this can run, and does run,
 * while still NET_JOINING; a duplicate accept (the host resends on every
 * re-JOIN, network.c's handle_join_datagram) re-applies the same
 * player_id/mode, harmless, and leaves the ordering baseline strictly
 * alone. Anything that fails to decode, or decodes as an
 * unrelated message type, is silently ignored -- same as
 * network_host_receive's own drain loop (network.c). The decode array cap
 * matches NETWORK_SYNC_RECORDS_PER_PACKET exactly, since that is also the
 * largest chunk send_sync_records ever encodes into one packet. */
/* Dispatch one already-header-decoded packet by type onto this client's state.
 * `bytes`/`len` are the whole datagram (MSG_OP parks raw bytes, see
 * network_client_receive_op). Split out of network_client_receive_state's drain
 * loop so neither trips clang-tidy's cognitive-complexity ceiling. */
static void network_client_dispatch_packet(GameState *state, const uint8_t *bytes, size_t len, DecodedPacket *packet)
{
    if (packet->header.type == MSG_SNAPSHOT || packet->header.type == MSG_DELTA) {
        AttrRecord records[NETWORK_SYNC_RECORDS_PER_PACKET];
        size_t record_count = 0;
        if (protocol_decode_attr_list(&packet->reader, records, NETWORK_SYNC_RECORDS_PER_PACKET, &record_count)) {
            network_client_apply_state(state, records, record_count);
        }
    } else if (packet->header.type == MSG_EVENT) {
        network_client_receive_event(&state->network, packet->header.seq, &packet->reader);
    } else if (packet->header.type == MSG_OP) {
        network_client_receive_op(state, bytes, len, packet);
    } else if (packet->header.type == MSG_ACK) {
        AckMessage ack = {0};
        if (protocol_decode_ack(&packet->reader, &ack)) {
            reliable_on_ack(&state->network.host_event_channel, ack);
        }
    } else if (packet->header.type == MSG_JOIN_ACCEPT) {
        JoinAcceptMessage accept = {0};
        if (protocol_decode_join_accept(&packet->reader, &accept)) {
            client_apply_join_accept(&state->network, accept);
        }
    } else if (packet->header.type == MSG_CURSOR) {
        network_client_receive_cursor(&state->network, &packet->reader);
    } else if (packet->header.type == MSG_RESYNC) {
        ResyncChunk chunk = {0};
        if (protocol_decode_resync(&packet->reader, &chunk)) {
            network_resync_accept_chunk(&state->network, &chunk);
        }
    }
}

static void network_client_receive_state(GameState *state)
{
    uint8_t buffer[NET_MAX_PACKET_SIZE];
    NetAddr src = {0};
    int received = net_recv(&state->network.transport, &src, buffer, sizeof(buffer));
    while (received > 0) {
        DecodedPacket packet;
        ErrorState decode_err = {0};
        if (protocol_decode_packet(buffer, (size_t)received, &packet, &decode_err)) {
            network_client_dispatch_packet(state, buffer, (size_t)received, &packet);
        }
        received = net_recv(&state->network.transport, &src, buffer, sizeof(buffer));
    }
}

bool network_client_resync_ready(const GameState *state)
{
    return state->network.resync_ready;
}

bool network_client_apply_resync(
    Diag *diag, GameState *state, UndoHistory *undo_history, TextureLookupFn texture_lookup, void *texture_user_data)
{
    NetworkState *network = &state->network;
    /* network_resync_accept_chunk guarantees total_bytes < NETWORK_RESYNC_MAX_BYTES,
     * so this one-byte terminator always fits and game_load_gamedata can read
     * the reassembled TOML as a C string. Clear resync_ready up front: whether
     * the reload succeeds or fails this generation is done being applied. */
    size_t total_bytes = network->resync_incoming_total_bytes;
    network->resync_buffer[total_bytes] = 0;
    network->resync_ready = false;
    if (!game_load_gamedata(diag, state,
                            (GamedataParams){.toml_string = (const char *)network->resync_buffer,
                                             .level_name = network->resync_incoming_level_name,
                                             .texture_lookup = texture_lookup,
                                             .texture_user_data = texture_user_data})) {
        /* A failed parse is never retried for the same generation; a newer one
         * supersedes. This leaves the client diverged until the next structural
         * edit (or S8.7i recovery) -- a bounded pre-alpha compromise. Do NOT
         * confirm, so the host keeps this generation armed for the client. */
        network->resync_failed_generation = network->resync_incoming_generation;
        return false;
    }
    /* Session-wide barrier, mirroring the host: clear undo, push a fresh
     * baseline. Both sides re-parsed the same bytes, so entity ids and
     * gamedata_hash re-converge here. */
    undo_history_clear(undo_history);
    undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                           strv_from_cstr("Structural resync"));
    /* Confirm to the host over the reliable client->host channel so it stops
     * re-streaming this generation. The generation rides EventRecord.entity_id
     * (handle_event_datagram, network.c reads it back). */
    network_client_send_reliable_event(network, (EventRecord){.event_type = NETWORK_EVENT_RESYNC_COMPLETE,
                                                              .entity_id = (int32_t)network->resync_incoming_generation,
                                                              .argument = (Strv){0}});
    return true;
}

void network_editor_commit_move(GameState *state, int entity_id, Vector2 new_position)
{
    if (state->network.mode == NET_CLIENT) {
        /* Op REQUEST: op_seq 0 (the host stamps the real serialization
         * number on its echo); the local preview mutation already happened
         * during the drag, the host's echo is what authoritatively confirms
         * it on every replica. */
        EditorOp operation = {
            .kind = EDITOR_OP_MOVE_ENTITY,
            .level_name = str_to_strv(state->gamedata.current_level.name),
            .entity_id = entity_id,
            .author_player_id = state->network.local_player_id,
            .op_seq = 0,
            .move_x = new_position.x,
            .move_y = new_position.y,
        };
        network_client_send_reliable_op(&state->network, &operation);
        return;
    }
    if (state->network.mode == NET_HOSTING) {
        /* The local drag mutation is already the authoritative apply, so
         * this only stamps (author 0 = the host's own player id) and
         * broadcasts the echo -- it does not re-apply the position. */
        EditorOp operation = {
            .kind = EDITOR_OP_MOVE_ENTITY,
            .level_name = str_to_strv(state->gamedata.current_level.name),
            .entity_id = entity_id,
            .author_player_id = 0,
            .op_seq = state->network.next_op_seq++,
            .move_x = new_position.x,
            .move_y = new_position.y,
        };
        network_host_broadcast_reliable_op(&state->network, &operation);
        return;
    }
}

void network_editor_commit_delete(GameState *state, int entity_id)
{
    if (state->network.mode == NET_CLIENT) {
        /* Op REQUEST (op_seq 0, the host stamps its echo); the local delete
         * already happened as this peer's preview. */
        EditorOp operation = {
            .kind = EDITOR_OP_DELETE_ENTITY,
            .level_name = str_to_strv(state->gamedata.current_level.name),
            .entity_id = entity_id,
            .author_player_id = state->network.local_player_id,
            .op_seq = 0,
        };
        network_client_send_reliable_op(&state->network, &operation);
        return;
    }
    if (state->network.mode == NET_HOSTING) {
        /* The local delete was already the authoritative apply. Clear any
         * lock entry on the entity without a release echo -- the DELETE echo
         * is the "gone" signal every replica clears its lock on, and the
         * host's own table is a replica of that rule too (one rule
         * everywhere, see host_handle_delete). */
        EntityLock *lock = network_lock_find(&state->network, entity_id);
        if (lock != nullptr) {
            lock->active = false;
        }
        EditorOp operation = {
            .kind = EDITOR_OP_DELETE_ENTITY,
            .level_name = str_to_strv(state->gamedata.current_level.name),
            .entity_id = entity_id,
            .author_player_id = 0,
            .op_seq = state->network.next_op_seq++,
        };
        network_host_broadcast_reliable_op(&state->network, &operation);
        return;
    }
}

/* S8.7f3: copy at most NETWORK_PLACE_ATTRS_MAX place attrs from `src` into an
 * EditorOp's fixed place_attrs array, returning how many were copied. The
 * clamp is the seam's documented drop of pasted attrs beyond the cap (see
 * network_editor_commit_place, net_session.h); a bare place passes count 0. */
static size_t fill_place_attrs(AttrRecord *dest, const AttrRecord *src, size_t count)
{
    size_t copied = count < NETWORK_PLACE_ATTRS_MAX ? count : NETWORK_PLACE_ATTRS_MAX;
    for (size_t index = 0; index < copied; index++) {
        dest[index] = src[index];
    }
    return copied;
}

void network_editor_commit_place(GameState *state,
                                 int placed_entity_id,
                                 Strv blueprint_name,
                                 Vector2 position,
                                 const AttrRecord *attrs,
                                 size_t attr_count)
{
    if (state->network.mode == NET_CLIENT) {
        /* Op REQUEST: entity_id MUST be -1 -- the host assigns entity ids
         * (EditorOp's doc comment, net_protocol.h). placed_entity_id is
         * ignored on this path by contract (net_session.h): no local spawn
         * happened, so there is no local id to name. */
        EditorOp operation = {
            .kind = EDITOR_OP_PLACE_ENTITY,
            .level_name = str_to_strv(state->gamedata.current_level.name),
            .entity_id = -1,
            .author_player_id = state->network.local_player_id,
            .op_seq = 0,
            .move_x = position.x,
            .move_y = position.y,
            .blueprint_name = blueprint_name,
        };
        operation.place_attr_count = fill_place_attrs(operation.place_attrs, attrs, attr_count);
        network_client_send_reliable_op(&state->network, &operation);
        return;
    }
    if (state->network.mode == NET_HOSTING) {
        /* The local spawn was already the authoritative apply; the echo
         * carries the ROOT id it assigned (placed_entity_id) so every
         * replica forces the same id onto its own spawn, plus the same attrs
         * (copy-buffer-sourced, identical to what a CLIENT request carries). */
        EditorOp operation = {
            .kind = EDITOR_OP_PLACE_ENTITY,
            .level_name = str_to_strv(state->gamedata.current_level.name),
            .entity_id = placed_entity_id,
            .author_player_id = 0,
            .op_seq = state->network.next_op_seq++,
            .move_x = position.x,
            .move_y = position.y,
            .blueprint_name = blueprint_name,
        };
        operation.place_attr_count = fill_place_attrs(operation.place_attrs, attrs, attr_count);
        network_host_broadcast_reliable_op(&state->network, &operation);
        return;
    }
}

void network_editor_commit_set_attr(GameState *state, int entity_id, AttrRecord record)
{
    if (state->network.mode == NET_CLIENT) {
        /* Op REQUEST (op_seq 0, the host stamps its echo); the local mutation
         * already happened as this peer's preview. The record's own entity_id
         * is overwritten from the header by the encoder (the mirror invariant,
         * net_protocol.h), so it need not match here. */
        EditorOp operation = {
            .kind = EDITOR_OP_SET_ATTR,
            .level_name = str_to_strv(state->gamedata.current_level.name),
            .entity_id = entity_id,
            .author_player_id = state->network.local_player_id,
            .op_seq = 0,
            .attr = record,
        };
        network_client_send_reliable_op(&state->network, &operation);
        return;
    }
    if (state->network.mode == NET_HOSTING) {
        /* The local mutation was already the authoritative apply; this stamps
         * (author 0 = the host's own player id) and broadcasts the echo. */
        EditorOp operation = {
            .kind = EDITOR_OP_SET_ATTR,
            .level_name = str_to_strv(state->gamedata.current_level.name),
            .entity_id = entity_id,
            .author_player_id = 0,
            .op_seq = state->network.next_op_seq++,
            .attr = record,
        };
        network_host_broadcast_reliable_op(&state->network, &operation);
        return;
    }
}

void network_editor_commit_remove_attr(GameState *state, int entity_id, Strv attr_name)
{
    if (state->network.mode == NET_CLIENT) {
        EditorOp operation = {
            .kind = EDITOR_OP_REMOVE_ATTR,
            .level_name = str_to_strv(state->gamedata.current_level.name),
            .entity_id = entity_id,
            .author_player_id = state->network.local_player_id,
            .op_seq = 0,
            .attr_name = attr_name,
        };
        network_client_send_reliable_op(&state->network, &operation);
        return;
    }
    if (state->network.mode == NET_HOSTING) {
        EditorOp operation = {
            .kind = EDITOR_OP_REMOVE_ATTR,
            .level_name = str_to_strv(state->gamedata.current_level.name),
            .entity_id = entity_id,
            .author_player_id = 0,
            .op_seq = state->network.next_op_seq++,
            .attr_name = attr_name,
        };
        network_host_broadcast_reliable_op(&state->network, &operation);
        return;
    }
}

/* S8.7d2: build a lock op REQUEST (no op_seq -- the host stamps its echo)
 * naming the current level and entity_id, authored by author_player_id. Shared
 * by the client-side grab/release senders below. */
static EditorOp make_lock_request(GameState *state, EditorOpKind kind, int entity_id, int author_player_id)
{
    return (EditorOp){
        .kind = kind,
        .level_name = str_to_strv(state->gamedata.current_level.name),
        .entity_id = entity_id,
        .author_player_id = author_player_id,
        .op_seq = 0,
    };
}

/* S8.7d2, NET_HOSTING branch of network_editor_try_grab: refuse the whole
 * group if any id is already locked by a player other than the host (id 0). */
static bool host_group_grab_permitted(NetworkState *network, const int *entity_ids, int entity_count)
{
    for (int index = 0; index < entity_count; index++) {
        const EntityLock *lock = network_lock_find(network, entity_ids[index]);
        if (lock != nullptr && lock->holder_player_id != 0) {
            return false;
        }
    }
    return true;
}

/* S8.7d2, NET_CLIENT branch of network_editor_try_grab: fast local deny if any
 * id is already held (in this client's replica) by a player other than us. */
static bool client_group_grab_permitted(NetworkState *network, const int *entity_ids, int entity_count)
{
    for (int index = 0; index < entity_count; index++) {
        const EntityLock *lock = network_lock_find(network, entity_ids[index]);
        if (lock != nullptr && lock->holder_player_id != network->local_player_id) {
            return false;
        }
    }
    return true;
}

bool network_editor_try_grab(GameState *state, const int *entity_ids, int entity_count)
{
    if (state->network.mode == NET_HOSTING) {
        if (!host_group_grab_permitted(&state->network, entity_ids, entity_count)) {
            return false;
        }
        for (int index = 0; index < entity_count; index++) {
            (void)network_lock_acquire(&state->network, entity_ids[index], 0);
            EditorOp grant = make_lock_request(state, EDITOR_OP_LOCK_ACQUIRE, entity_ids[index], 0);
            host_stamp_and_broadcast(state, EDITOR_OP_LOCK_ACQUIRE, &grant, 0);
        }
        return true;
    }
    if (state->network.mode == NET_CLIENT) {
        if (!client_group_grab_permitted(&state->network, entity_ids, entity_count)) {
            return false;
        }
        for (int index = 0; index < entity_count; index++) {
            EditorOp request =
                make_lock_request(state, EDITOR_OP_LOCK_ACQUIRE, entity_ids[index], state->network.local_player_id);
            network_client_send_reliable_op(&state->network, &request);
        }
        return true;
    }
    return true;
}

void network_editor_release_locks(GameState *state, const int *entity_ids, int entity_count)
{
    if (state->network.mode == NET_HOSTING) {
        for (int index = 0; index < entity_count; index++) {
            if (!network_lock_release(&state->network, entity_ids[index], 0)) {
                continue;
            }
            EditorOp release = make_lock_request(state, EDITOR_OP_LOCK_RELEASE, entity_ids[index], 0);
            host_stamp_and_broadcast(state, EDITOR_OP_LOCK_RELEASE, &release, 0);
        }
        return;
    }
    if (state->network.mode == NET_CLIENT) {
        for (int index = 0; index < entity_count; index++) {
            EditorOp request =
                make_lock_request(state, EDITOR_OP_LOCK_RELEASE, entity_ids[index], state->network.local_player_id);
            network_client_send_reliable_op(&state->network, &request);
        }
    }
}
