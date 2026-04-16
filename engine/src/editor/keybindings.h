#pragma once

#include "editor/editor.h"

/* Editor keybinding registry.
 *
 * Each editor submode declares a static const EditorBindingTable describing
 * every action it handles. Input handlers consult the table via
 * binding_pressed()/binding_held(); the hints bar renders the same table,
 * so labels cannot drift from bindings.
 *
 * A binding's modifier (if non-zero) is a chord prefix — the binding only
 * fires while the modifier is held. Both keyboard and gamepad halves are
 * independently valid: {KEY_Z, GAMEPAD_BUTTON_LEFT_FACE_LEFT} with modifier
 * {KEY_LEFT_CONTROL, GAMEPAD_BUTTON_LEFT_TRIGGER_1} means "Ctrl+Z on keyboard
 * OR L1+Left on gamepad". The keyboard main key pairs with the keyboard
 * modifier and likewise for gamepad — the two halves never cross. */

typedef struct {
    ToggleBinding binding;   /* main key/button */
    ToggleBinding modifier;  /* chord prefix; {0,0} = no chord */
    const char *description; /* human-readable label shown in hints bar */
} EditorBinding;

typedef struct {
    const EditorBinding *actions;
    int count;
    const char *mode_label; /* submode name shown at left of hints bar */
} EditorBindingTable;

/* True on the frame the binding's main key/button transitioned from
 * not-pressed to pressed AND the modifier side (if any) is currently down.
 * Chord half-matching rule: if the keyboard main was pressed, the keyboard
 * modifier must be down; if the gamepad main was pressed, the gamepad
 * modifier must be down. Prevents cross-device false positives like
 * "Ctrl held on keyboard + A pressed on gamepad" firing an undo. */
bool binding_pressed(const EditorBinding *action);

/* True while the binding's main key/button is currently down and the
 * matching-side modifier (if any) is also down. Use for auto-repeat value
 * adjusters. */
bool binding_held(const EditorBinding *action);

/* True while either half of the modifier is currently down. Useful for
 * suppressing an unrelated single-key action when it is being used as a
 * chord prefix (e.g. don't fire Handles on L1 when L1 is being held for an
 * L1+Left undo). */
bool binding_modifier_down(const EditorBinding *action);

/* Render "Ctrl+Z / L1+Left" or "A/Ent" or "Stick" into out; returns bytes
 * written (excluding null terminator). If cap is insufficient, truncates
 * safely and still null-terminates. */
int binding_label(const EditorBinding *action, char *out, int cap);

/* Render "[mode_label]  |  binding1: desc1  |  binding2: desc2  |  ..."
 * into out. If the full line exceeds cap, truncates at a separator and
 * appends " +N". Always null-terminates. Returns bytes written. */
int binding_table_render(const EditorBindingTable *table, char *out, int cap);

/* --- Per-submode binding tables ---
 *
 * Each table is owned by the file that handles its submode; this header
 * re-exports the accessors so the hints bar (editor/draw.c) can look up the
 * current table by submode. */
const EditorBindingTable *browse_bindings(void);
const EditorBindingTable *drag_bindings(void);
const EditorBindingTable *handles_bindings(void);
const EditorBindingTable *place_bindings(void);
const EditorBindingTable *attr_edit_bindings(void);
const EditorBindingTable *radial_bindings(void);
const EditorBindingTable *word_builder_bindings(void);
const EditorBindingTable *fuzzy_finder_bindings(void);
const EditorBindingTable *gamepad_kb_bindings(void);
const EditorBindingTable *blueprint_list_bindings(void);
const EditorBindingTable *blueprint_detail_bindings(void);
const EditorBindingTable *play_mode_bindings(void);
