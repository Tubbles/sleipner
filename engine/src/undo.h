#pragma once

#include "arena.h"
#include "error.h"
#include "game.h"
#include "strv.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct UndoEntry {
    struct UndoEntry *prev;
    struct UndoEntry *next;
    size_t arena_data_size;      /* bytes of gamedata arena content stored */
    Strv description;            /* non-owning: points into undo arena */
    GamedataState gamedata_copy; /* typed snapshot of the GamedataState struct */
    /* Followed in the undo arena by:
     *   uint8_t arena_data[arena_data_size]  (raw gamedata_arena contents)
     * Access via: (uint8_t *)(entry + 1) */
} UndoEntry;

typedef struct {
    Arena arena;          /* dedicated undo arena (mmap, same as gamedata/scratch) */
    UndoEntry *current;   /* cursor in the doubly-linked list */
    int current_position; /* 0-based index of current entry, -1 if empty */
    int saved_position;   /* position at last save, -1 if never saved */
} UndoHistory;

/* Initialize the undo arena. */
[[nodiscard]] bool undo_history_init(ErrorState *err, UndoHistory *history);

/* Snapshot the current gamedata into a new undo entry.
 * Truncates any redo entries after the current position. */
void undo_history_new_entry(UndoHistory *history,
                            GamedataState *gamedata,
                            Arena *gamedata_arena,
                            ArenaCheckpoint gamedata_base,
                            Strv description);

/* Restore the previous state (undo). No-op if at the beginning.
 * Caller is responsible for resetting any editor indices after this call. */
void undo_history_step_back(UndoHistory *history,
                            GamedataState *gamedata,
                            Arena *gamedata_arena,
                            ArenaCheckpoint gamedata_base);

/* Restore the next state (redo). No-op if at the end.
 * Caller is responsible for resetting any editor indices after this call. */
void undo_history_step_forward(UndoHistory *history,
                               GamedataState *gamedata,
                               Arena *gamedata_arena,
                               ArenaCheckpoint gamedata_base);

/* Remove the most recent entry without restoring. No-op if current is nullptr.
 * Called on cancel of multi-frame operations. */
void undo_history_discard(UndoHistory *history);

/* Clear all undo history. Called on hot-reload and level transitions. */
void undo_history_clear(UndoHistory *history);

/* Record the current position as the saved state. */
void undo_history_mark_saved(UndoHistory *history);

/* Return true if the current position differs from the last saved state. */
bool undo_history_is_dirty(const UndoHistory *history);

/* Return the description of the current entry, or empty Strv if none. */
Strv undo_history_description(const UndoHistory *history);

/* Release the undo arena. */
void undo_history_free(UndoHistory *history);
