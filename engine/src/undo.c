#include "undo.h"

#include "arena.h"
#include "error.h"
#include "game.h"
#include "strv.h"

#include <stdint.h>
#include <string.h>

static uint8_t *undo_arena_data(UndoEntry *entry)
{
    return (uint8_t *)(entry + 1);
}

bool undo_history_init(ErrorState *err, UndoHistory *history)
{
    *history = (UndoHistory){.current_position = -1, .saved_position = -1};
    return arena_init(err, &history->arena);
}

void undo_history_new_entry(UndoHistory *history, GameState *state, Strv description)
{
    size_t arena_data_size = arena_used_since(&state->gamedata_arena, state->gamedata_base);

    /* Truncate redo entries: if current is not the last entry, rewind the undo arena
     * to just past current's trailing data — everything after is overwritten. */
    if (history->current) {
        uint8_t *current_end = undo_arena_data(history->current) + history->current->arena_data_size;
        ArenaCheckpoint truncate_point =
            (ArenaCheckpoint)((size_t)(current_end - (uint8_t *)arena_ptr_at(&history->arena, 0)));
        arena_restore(&history->arena, truncate_point);

        /* If the saved entry was in the truncated region, invalidate it. */
        if (history->saved_position > history->current_position) {
            history->saved_position = -2;
        }
    } else {
        arena_restore(&history->arena, 0);
    }

    /* Allocate entry + trailing arena data from the undo arena. */
    size_t total_size = sizeof(UndoEntry) + arena_data_size;
    UndoEntry *entry = arena_alloc(&history->arena, total_size);

    entry->arena_data_size = arena_data_size;
    entry->description = description;
    entry->gamedata_copy = state->gamedata;

    /* Copy gamedata arena contents into the trailing data slot. */
    if (arena_data_size > 0) {
        memcpy(undo_arena_data(entry), arena_ptr_at(&state->gamedata_arena, state->gamedata_base), arena_data_size);
    }

    /* Link into the doubly-linked list. */
    entry->prev = history->current;
    entry->next = nullptr;
    if (history->current) {
        history->current->next = entry;
    }
    history->current = entry;
    history->current_position++;
}

static void restore_entry(UndoEntry *entry, GameState *state)
{
    /* Restore gamedata arena contents. */
    if (entry->arena_data_size > 0) {
        memcpy(arena_ptr_at(&state->gamedata_arena, state->gamedata_base), undo_arena_data(entry),
               entry->arena_data_size);
    }
    arena_restore(&state->gamedata_arena, state->gamedata_base + entry->arena_data_size);

    /* Restore the GamedataState struct. */
    state->gamedata = entry->gamedata_copy;
}

void undo_history_step_back(UndoHistory *history, GameState *state)
{
    if (!history->current) {
        return;
    }
    if (history->current->prev) {
        history->current = history->current->prev;
        history->current_position--;
    }
    restore_entry(history->current, state);
}

void undo_history_step_forward(UndoHistory *history, GameState *state)
{
    if (!history->current || !history->current->next) {
        return;
    }
    history->current = history->current->next;
    history->current_position++;
    restore_entry(history->current, state);
}

void undo_history_discard(UndoHistory *history)
{
    if (!history->current) {
        return;
    }
    UndoEntry *discarded = history->current;
    history->current = discarded->prev;
    if (history->current) {
        history->current->next = nullptr;
    }
    history->current_position--;
}

void undo_history_clear(UndoHistory *history)
{
    arena_restore(&history->arena, 0);
    history->current = nullptr;
    history->current_position = -1;
    history->saved_position = -1;
}

void undo_history_mark_saved(UndoHistory *history)
{
    history->saved_position = history->current_position;
}

bool undo_history_is_dirty(const UndoHistory *history)
{
    return history->current_position != history->saved_position;
}

Strv undo_history_description(const UndoHistory *history)
{
    if (!history->current) {
        return (Strv){0};
    }
    return history->current->description;
}

void undo_history_free(UndoHistory *history)
{
    arena_free(&history->arena);
    history->current = nullptr;
    history->current_position = -1;
    history->saved_position = -1;
}
