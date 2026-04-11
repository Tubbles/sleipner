# IDEAS.md

Half-formed design ideas that aren't committed to yet. Things we've talked
about, see some merit in, but don't want to build right now. If an idea here
becomes real work, move it out of this file (into DESIGN.md or TODO.md) in
the same commit that implements it.

## Temporary snapshot on undo-at-left-edge

**Status:** not committed. Might be obsoleted by a future save game system.

**Context.** We currently fix the "player position resets after panning in
editor" bug (`engine/src/undo.c:82-99`) by making `undo_history_step_back`
a no-op at the left edge of the undo list. That preserves live runtime
drift — e.g. player position accumulated during play mode — because there
is no baseline snapshot to restore over it.

That fix is correct as a regression guard, but it leaves a usability gap:
the user *might* actually want undo-at-left-edge to do something. Concretely,
after a fresh level load with only play-mode runtime changes, pressing undo
currently does nothing. There is no way to say "reset me to the level as
loaded" without leaving editor mode.

**Proposed model.** When the user presses undo at the left edge AND the
current state differs from the baseline snapshot (i.e. there is live runtime
drift since load), do the following:

1. Push a new **temporary snapshot** capturing the current drifted state.
   This is marked as temporary so it doesn't dirty the "real" edit history.
2. Step back to the baseline (level-load) snapshot.

The user has then effectively "rewound to level load" but can redo back to
where they were if the undo was accidental.

**Drop conditions for temporary snapshots.** A temporary snapshot is dropped
(removed from history) if:

- The user undoes back to baseline and then redoes forward to the temporary
  snapshot **without making any intervening changes**. In that case the
  temporary snapshot served its purpose (safety net) and is no longer needed.
- The user leaves editor mode back into play mode. Play mode will produce
  fresh drift anyway; keeping the old temporary snapshot is just clutter.
- *(Open question)* any real edit made while on the temporary snapshot
  could promote it to a permanent entry, or could push a new permanent
  entry on top and drop the temporary one. Unclear which is less surprising.

**Related idea: manual "take snapshot now".** A dedicated editor binding
that snapshots the current state under a user-supplied label. Useful for
playtesting: mark "before boss room", "after puzzle 2", etc., so the user
can jump back to interesting states without hunting through unlabelled undo
history. This would produce normal (non-temporary) entries.

**Why we're not building this now.**

- The bug is already fixed by the simple no-op; there is no active pain.
- The temporary-snapshot semantics have at least one open question (what
  happens when you edit on top of a temporary) and no user has actually
  asked for the feature — it was surfaced while debating whether the
  left-edge fix was the right one.
- A proper save game system (DESIGN.md roadmap) will likely cover the
  "save interesting points in time" use case more naturally than overloading
  the undo stack with labelled/temporary entries.

**If we do build it later**, the changes land in `engine/src/undo.h` (new
`UndoEntry.is_temporary` flag or similar), `engine/src/undo.c` (push logic
at left edge, drop logic on transitions), and `engine/src/editor/core.c`
(the new editor binding for manual snapshot). The existing integration test
`test_integration_editor_undo_at_left_edge_preserves_play_state` would need
to be rewritten — under the new model, undo-at-left-edge *would* change
player position (back to TOML start), with a redo restoring it. The
regression the test currently guards would need a new shape.
