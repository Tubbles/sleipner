#include "save.h"

#include "alloc.h"
#include "arena.h"
#include "attribute.h"
#include "debug.h"
#include "diag.h"
#include "error.h"
#include "file_backup.h"
#include "game.h"
#include "map.h"
#include "platform_paths.h"
#include "progression.h"
#include "rule.h"
#include "str.h"
#include "strv.h"
#include "toml_emitter.h"
#include "toml_str.h"
#include "undo.h"
#include "vec.h"

#include "raylib.h"
#include "toml.h"

#include <stdlib.h>
#include <string.h>

/* Generous headroom over a typical save (a handful of flags/vars/items and
 * a couple of visited levels' worth of entity deltas) -- same fixed-buffer-
 * then-copy-into-Str shape toml_emitter.c's callers already use for
 * gamedata/keybindings (main.c's MAX_GAMEDATA_SIZE/MAX_KEYBINDINGS_SIZE). */
#define SAVE_SERIALIZE_BUFFER_SIZE (128UL * 1024)
#define SAVE_TOML_ERRBUF_SIZE 200

Str save_serialize(const SaveState *save, Allocator *alloc, ErrorState *err)
{
    char buffer[SAVE_SERIALIZE_BUFFER_SIZE];
    int written = toml_emit_save(err, buffer, (int)sizeof(buffer), SAVE_FORMAT_VERSION,
                                 str_to_strv(save->current_level_name), &save->progression.flags,
                                 &save->progression.vars, &save->progression.items, &save->progression.level_deltas);
    if (written < 0) {
        error_wrap(err, "save_serialize");
        return (Str){0};
    }

    Str result = str_new(*alloc);
    if (!str_from_cstr(&result, buffer)) {
        error_set(err, "save_serialize: string allocation failed");
        return (Str){0};
    }
    return result;
}

/* ---- Typed value parsing (mirrors blueprint.c's parse_custom_attr /
 * level.c's parse_instance_attr -- same bool/int/double/string cascade,
 * duplicated here rather than shared since those two are file-local
 * statics with no common header, and this is the third copy of an
 * established idiom, not a new one). ---- */

static bool parse_save_attr(Allocator *alloc, AttrSet *attrs, toml_table_t *table, const char *key)
{
    toml_datum_t bool_value = toml_bool_in(table, key);
    if (bool_value.ok) {
        return attr_set_bool(alloc, attrs, key, bool_value.u.b);
    }

    toml_datum_t int_value = toml_int_in(table, key);
    if (int_value.ok) {
        return attr_set_int(alloc, attrs, key, (int)int_value.u.i);
    }

    toml_datum_t double_value = toml_double_in(table, key);
    if (double_value.ok) {
        return attr_set_float(alloc, attrs, key, (float)double_value.u.d);
    }

    toml_datum_t string_value = toml_string_in(table, key);
    if (string_value.ok) {
        bool result = attr_set_string(alloc, attrs, (AttrStringPair){.name = key, .value = string_value.u.s});
        free(string_value.u.s);
        return result;
    }
    return true;
}

static void parse_save_flags(Allocator *alloc, FlagSet *flags, toml_table_t *save_table)
{
    *flags = (FlagSet){.names = vec_flag_name_new(*alloc)};
    toml_array_t *flag_array = toml_array_in(save_table, "flags");
    if (!flag_array) {
        return;
    }
    int count = toml_array_nelem(flag_array);
    for (int index = 0; index < count; index++) {
        toml_datum_t value = toml_string_at(flag_array, index);
        if (!value.ok) {
            continue;
        }
        FlagName entry = {.name = str_new(*alloc)};
        if (str_from_toml_datum(&entry.name, &value)) {
            (void)vec_flag_name_push(&flags->names, entry);
        }
    }
}

static void parse_save_vars(Allocator *alloc, AttrSet *vars, toml_table_t *save_table)
{
    toml_table_t *vars_table = toml_table_in(save_table, "vars");
    if (!vars_table) {
        return;
    }
    int key_count = toml_table_nkval(vars_table);
    for (int index = 0; index < key_count; index++) {
        const char *key = toml_key_in(vars_table, index);
        if (!key) {
            continue;
        }
        (void)parse_save_attr(alloc, vars, vars_table, key);
    }
}

static void parse_save_items(Allocator *alloc, ItemSet *items, toml_table_t *save_table)
{
    *items = (ItemSet){.counts = map_strv_int_new(*alloc)};
    toml_table_t *items_table = toml_table_in(save_table, "items");
    if (!items_table) {
        return;
    }
    int key_count = toml_table_nkval(items_table);
    for (int index = 0; index < key_count; index++) {
        const char *key = toml_key_in(items_table, index);
        if (!key) {
            continue;
        }
        toml_datum_t count = toml_int_in(items_table, key);
        if (!count.ok) {
            continue;
        }
        Str owned_name = str_new(*alloc);
        if (!str_from_cstr(&owned_name, key)) {
            continue;
        }
        (void)map_strv_int_set(&items->counts, str_to_strv(owned_name), (int)count.u.i);
    }
}

/* Mirrors blueprint.c's parse_float_array: try double, fall back to int,
 * so both "120.5" (our own emit_float_literal output) and a plain "120"
 * (e.g. a hand-authored save) parse into the same float field. */
static float parse_position_component(toml_array_t *array, int index)
{
    toml_datum_t value = toml_double_at(array, index);
    if (value.ok) {
        return (float)value.u.d;
    }
    toml_datum_t int_value = toml_int_at(array, index);
    if (int_value.ok) {
        return (float)int_value.u.i;
    }
    return 0.0F;
}

static bool parse_entity_delta_scalars(toml_table_t *entity_table, EntityDelta *out)
{
    toml_datum_t id_value = toml_int_in(entity_table, "id");
    if (!id_value.ok) {
        return false;
    }
    out->id = (int)id_value.u.i;

    toml_array_t *pos = toml_array_in(entity_table, "pos");
    if (pos && toml_array_nelem(pos) == 2) {
        out->position.x = parse_position_component(pos, 0);
        out->position.y = parse_position_component(pos, 1);
    }

    toml_datum_t active = toml_bool_in(entity_table, "active");
    out->active = active.ok ? active.u.b : true;
    return true;
}

static bool entity_delta_key_is_known(const char *key)
{
    return strcmp(key, "id") == 0 || strcmp(key, "pos") == 0 || strcmp(key, "active") == 0;
}

/* Mirrors level.c's parse_instance_overrides: walk every key in the table
 * (kvals + arrays + subtables, since a custom attr could in principle be
 * any TOML type parse_save_attr understands), skipping the ones with
 * dedicated fields above. */
static bool parse_entity_delta(Allocator *alloc, toml_table_t *entity_table, EntityDelta *out)
{
    *out = (EntityDelta){0};
    if (!parse_entity_delta_scalars(entity_table, out)) {
        return false;
    }

    int total_keys = toml_table_nkval(entity_table) + toml_table_narr(entity_table) + toml_table_ntab(entity_table);
    for (int key_index = 0; key_index < total_keys; key_index++) {
        const char *key = toml_key_in(entity_table, key_index);
        if (!key || entity_delta_key_is_known(key)) {
            continue;
        }
        if (!parse_save_attr(alloc, &out->attrs, entity_table, key)) {
            return false;
        }
    }
    return true;
}

static bool parse_level_delta_entities(Allocator *alloc, toml_table_t *level_delta_table, vec_entity_delta *entities)
{
    toml_array_t *entity_array = toml_array_in(level_delta_table, "entity");
    if (!entity_array) {
        return true;
    }
    int count = toml_array_nelem(entity_array);
    for (int index = 0; index < count; index++) {
        toml_table_t *entity_table = toml_table_at(entity_array, index);
        if (!entity_table) {
            continue;
        }
        EntityDelta entity_delta;
        if (!parse_entity_delta(alloc, entity_table, &entity_delta)) {
            continue;
        }
        if (!vec_entity_delta_push(entities, entity_delta)) {
            return false;
        }
    }
    return true;
}

static bool parse_level_deltas(Allocator *alloc, map_strv_level_delta *level_deltas, toml_table_t *root)
{
    toml_array_t *delta_array = toml_array_in(root, "level_delta");
    if (!delta_array) {
        return true;
    }
    int count = toml_array_nelem(delta_array);
    for (int index = 0; index < count; index++) {
        toml_table_t *level_delta_table = toml_table_at(delta_array, index);
        if (!level_delta_table) {
            continue;
        }
        toml_datum_t name = toml_string_in(level_delta_table, "name");
        if (!name.ok) {
            continue;
        }

        LevelDelta delta = {.entities = vec_entity_delta_new(*alloc)};
        bool entities_ok = parse_level_delta_entities(alloc, level_delta_table, &delta.entities);

        Str owned_name = str_new(*alloc);
        bool name_ok = str_from_toml_datum(&owned_name, &name);

        if (!entities_ok || !name_ok) {
            return false;
        }
        if (!map_strv_level_delta_set(level_deltas, str_to_strv(owned_name), delta)) {
            return false;
        }
    }
    return true;
}

static bool parse_save_table(toml_table_t *root, Allocator *alloc, ErrorState *err, SaveState *out)
{
    toml_table_t *save_table = toml_table_in(root, "save");
    if (!save_table) {
        error_set(err, "missing [save] section");
        return false;
    }

    toml_datum_t version = toml_int_in(save_table, "version");
    if (!version.ok || version.u.i != SAVE_FORMAT_VERSION) {
        error_set(err, "unsupported save format version (expected %d)", SAVE_FORMAT_VERSION);
        return false;
    }

    *out = (SaveState){0};
    out->current_level_name = str_new(*alloc);
    toml_datum_t level_name = toml_string_in(save_table, "current_level");
    if (level_name.ok && !str_from_toml_datum(&out->current_level_name, &level_name)) {
        error_set(err, "current_level allocation failed");
        return false;
    }

    parse_save_flags(alloc, &out->progression.flags, save_table);
    parse_save_vars(alloc, &out->progression.vars, save_table);
    parse_save_items(alloc, &out->progression.items, save_table);

    out->progression.level_deltas = map_strv_level_delta_new(*alloc);
    if (!parse_level_deltas(alloc, &out->progression.level_deltas, root)) {
        error_set(err, "level_delta parse failed");
        return false;
    }
    return true;
}

bool save_deserialize(const char *toml, Allocator *alloc, ErrorState *err, SaveState *out)
{
    size_t length = strlen(toml);
    char *buffer = alloc->malloc_fn(alloc->ctx, length + 1);
    if (!buffer) {
        error_set(err, "save_deserialize: allocation failed");
        return false;
    }
    memcpy(buffer, toml, length + 1);

    char errbuf[SAVE_TOML_ERRBUF_SIZE];
    toml_table_t *root = toml_parse(buffer, errbuf, (int)sizeof(errbuf));
    /* tomlc99 copies every string it needs out of `buffer` via its own
     * STRNDUP during the parse above (confirmed in toml.c) -- the table
     * it returns holds no pointers back into `buffer`, so it is safe to
     * free here regardless of whether the parse succeeded. */
    if (alloc->free_fn) {
        alloc->free_fn(alloc->ctx, buffer);
    }
    if (!root) {
        error_set(err, "toml_parse: %s", errbuf);
        error_wrap(err, "save_deserialize");
        return false;
    }

    bool parsed = parse_save_table(root, alloc, err, out);
    toml_free(root);
    if (!parsed) {
        error_wrap(err, "save_deserialize");
    }
    return parsed;
}

bool save_write(Diag *diag, GameState *state, const char *path)
{
    Allocator progression_alloc = allocator_arena(&state->progression_arena);
    progression_capture_level_delta(diag, &state->progression, &progression_alloc, &state->gamedata.current_level);

    SaveState save = {
        .current_level_name = state->gamedata.current_level.name,
        .progression = state->progression,
    };

    SCRATCH_SCOPE(&state->scratch_arena);
    Allocator scratch_alloc = allocator_arena(&state->scratch_arena);
    Str serialized = save_serialize(&save, &scratch_alloc, diag->error);
    if (!serialized.ptr) {
        error_wrap(diag->error, "save_write");
        return false;
    }

    if (FileExists(path) && !backup_file(diag, path)) {
        error_wrap(diag->error, "save_write");
        return false;
    }

    if (!SaveFileText(path, serialized.ptr)) {
        error_set(diag->error, "save_write: SaveFileText failed for %s", path);
        return false;
    }

    debug_log(diag->debug, "save: wrote %zu bytes to %s", serialized.len, path);
    return true;
}

bool save_load(Diag *diag,
               GameState *state,
               const char *path,
               SaveLevelLoaderFn level_loader_fn,
               void *level_loader_user_data,
               UndoHistory *undo_history)
{
    char *content = LoadFileText(path);
    if (!content) {
        error_set(diag->error, "save_load: could not read %s", path);
        return false;
    }

    /* Deserialize straight into progression_arena via a checkpoint rather
     * than a scratch buffer + deep copy: on success the parsed SaveState's
     * flags/vars/items/level_deltas already live in the right arena and
     * state->progression can just be reassigned to it below; on failure
     * arena_restore discards the partial parse and state->progression
     * (still the OLD value) is untouched -- matches game_reset_progression's
     * "arena owns everything, no separate deep-copy pass" shape. */
    ArenaCheckpoint progression_checkpoint = arena_save(&state->progression_arena);
    Allocator progression_alloc = allocator_arena(&state->progression_arena);
    SaveState parsed = {0};
    bool parsed_ok = save_deserialize(content, &progression_alloc, diag->error, &parsed);
    UnloadFileText(content);
    if (!parsed_ok) {
        arena_restore(&state->progression_arena, progression_checkpoint);
        error_wrap(diag->error, "save_load");
        return false;
    }

    if (!level_loader_fn) {
        arena_restore(&state->progression_arena, progression_checkpoint);
        error_set(diag->error, "save_load: no level loader configured");
        return false;
    }
    /* parsed.current_level_name.ptr may be nullptr here (a save whose
     * [save] table omits current_level) -- level_loader_fn's underlying
     * game_load_gamedata/level_load treats a nullptr level_name as "load
     * the first level" (see level.c's find_level_table), not a crash, so
     * this is passed through unchanged rather than special-cased. */
    if (!level_loader_fn(diag, state, parsed.current_level_name.ptr, level_loader_user_data)) {
        arena_restore(&state->progression_arena, progression_checkpoint);
        error_wrap(diag->error, "save_load");
        return false;
    }

    /* Commit: the old state->progression's arena bytes are simply orphaned
     * below the checkpoint (never freed individually) -- a Load is a
     * bounded user action, not a per-frame allocation, so this fits the
     * arena's documented growth rule (see CLAUDE.md's "Arena growth
     * strategy"). */
    state->progression = parsed.progression;
    Allocator gamedata_alloc = allocator_arena(&state->gamedata_arena);
    progression_apply_level_delta(diag, &state->progression, &state->gamedata.current_level, &gamedata_alloc);
    game_snap_camera(state);

    undo_history_clear(undo_history);
    undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                           strv_from_cstr("Loaded save"));

    debug_log(diag->debug, "save: loaded %s (level '%s')", path, state->gamedata.current_level.name.ptr);
    return true;
}

#define AUTOSAVE_FILENAME "autosave.toml"

bool save_autosave(Diag *diag, GameState *state)
{
    SCRATCH_SCOPE(&state->scratch_arena);
    Allocator scratch_alloc = allocator_arena(&state->scratch_arena);

    Str saves_dir = {0};
    if (!platform_saves_dir(&saves_dir, scratch_alloc, diag->error)) {
        error_wrap(diag->error, "save_autosave");
        return false;
    }
    if (!platform_ensure_saves_dir(saves_dir.ptr, diag->error)) {
        error_wrap(diag->error, "save_autosave");
        return false;
    }

    Str autosave_path = str_new(scratch_alloc);
    if (!str_append_strv(&autosave_path, str_to_strv(saves_dir)) ||
        !str_append_cstr(&autosave_path, AUTOSAVE_FILENAME)) {
        error_set(diag->error, "save_autosave: path build failed");
        return false;
    }

    return save_write(diag, state, autosave_path.ptr);
}
