#pragma once

#include "alloc.h"
#include "error.h"
#include "input.h"
#include "raylib.h"
#include "vec.h"

#include <stdbool.h>
#include <stddef.h>

/* Input function layer — physical -> high-level translation.
 *
 * Game/editor/menu code calls input_pressed(in, store, ACTION_X), input_held,
 * input_axis, or input_axis_pair. It never reads raw keys, buttons, or stick
 * values. Physical bindings live in a BindingStore loaded from
 * data/keybindings.toml at startup, with built-in defaults as the fallback.
 *
 * --- Order-sensitivity warning ---
 * The function layer is a binding lookup, not a priority resolver. If
 * ACTION_NAV_LEFT is bound to LEFT_FACE_LEFT and ACTION_EDITOR_UNDO is bound
 * to chord [L1, LEFT_FACE_LEFT], then pressing L1+Left fires *both* unless
 * the caller checks EDITOR_UNDO first and early-returns on match. Caller is
 * responsible for evaluation order. */

/* Discrete actions. Naming convention: shared verbs are unprefixed
 * (CONFIRM, CANCEL, NAV_*); context-specific actions get a context prefix
 * (EDITOR_*, ATTR_*, BLUEPRINT_*, WB_*). */
typedef enum {
    /* Universal verbs */
    ACTION_CONFIRM,
    ACTION_CANCEL,
    ACTION_NAV_UP,
    ACTION_NAV_DOWN,
    ACTION_NAV_LEFT,
    ACTION_NAV_RIGHT,
    ACTION_PAGE_UP,
    ACTION_PAGE_DOWN,
    ACTION_TAB_PREV,
    ACTION_TAB_NEXT,

    /* Editor browse */
    ACTION_EDITOR_TOGGLE,
    ACTION_EDITOR_OPEN_TOOLS,
    ACTION_EDITOR_DELETE,
    ACTION_EDITOR_PLACE,
    ACTION_EDITOR_TYPE_PROPS,
    ACTION_EDITOR_GRAB,
    ACTION_EDITOR_HANDLES,
    ACTION_EDITOR_WATCH,
    ACTION_EDITOR_UNDO,
    ACTION_EDITOR_REDO,

    /* Attribute editing */
    ACTION_ATTR_INC_1,
    ACTION_ATTR_DEC_1,
    ACTION_ATTR_INC_10,
    ACTION_ATTR_DEC_10,
    ACTION_ATTR_INC_100,
    ACTION_ATTR_DEC_100,

    /* Blueprints */
    ACTION_BLUEPRINT_DUPLICATE,
    ACTION_BLUEPRINT_REMOVE,

    /* Word builder / gamepad keyboard */
    ACTION_WB_KEYBOARD_MODE,
    /* Backspace inside the on-screen keyboard widget (radial picker).
     * Kept distinct from ACTION_CANCEL so the host screen can use B / Esc
     * for "back / exit keyboard mode" without it also chewing characters
     * out of the buffer. */
    ACTION_KEYBOARD_BACKSPACE,

    /* Gameplay */
    ACTION_INTERACT,

    /* Global */
    ACTION_MENU_TOGGLE,
    ACTION_FONT_PREVIEW_TOGGLE,
    ACTION_QUIT,

    ACTION_COUNT,
} InputAction;

/* Analog scalar axes. Sticks decompose into named X/Y; pair them via
 * input_axis_pair(in, store, AXIS_X, AXIS_Y) to get a Vector2 with the
 * unit-disc clamp applied. */
typedef enum {
    AXIS_PRIMARY_X,
    AXIS_PRIMARY_Y,
    AXIS_SECONDARY_X,
    AXIS_SECONDARY_Y,
    AXIS_TRIGGER_LEFT,
    AXIS_TRIGGER_RIGHT,
    AXIS_COUNT,
} InputAxis;

/* An atom is one physical input element: a key press, a gamepad button, an
 * axis read, a trigger read, or a synth-axis from two keyboard keys.
 * Combine atoms into a PhysicalInput; a single-atom PhysicalInput is a
 * single key, a multi-atom PhysicalInput is a chord (all atoms must hold). */
typedef enum {
    ATOM_KEY,        /* int_a = raylib KEY_* */
    ATOM_GP_BUTTON,  /* int_a = raylib GAMEPAD_BUTTON_*, int_b = gamepad id (0) */
    ATOM_GP_AXIS,    /* int_a = raylib GAMEPAD_AXIS_*,   int_b = gamepad id, scale signs/inverts */
    ATOM_GP_TRIGGER, /* int_a = raylib GAMEPAD_AXIS_LEFT_TRIGGER/RIGHT_TRIGGER, int_b = gamepad id */
    ATOM_KB_AXIS,    /* int_a = neg_key (held -> -1), int_b = pos_key (held -> +1) */
} AtomKind;

typedef struct {
    AtomKind kind;
    int int_a;
    int int_b;
    float scale;
} AtomicInput;

VEC_DECL(atomic_input, AtomicInput)

typedef struct {
    vec_atomic_input parts; /* 1 atom = single binding; 2+ atoms = chord (all must hold). */
} PhysicalInput;

VEC_DECL(physical_input, PhysicalInput)

typedef struct {
    vec_physical_input alternatives; /* OR — any alternative that fires satisfies the action. */
} ActionBinding;

typedef struct {
    vec_physical_input alternatives; /* OR; values from each alternative are summed and clamped. */
} AxisBinding;

/* Enum-indexed fixed-size arrays. This is a justified MAX_* exception per
 * CLAUDE.md: there is exactly one binding per enum value, the count is
 * tied to the enum, not a guessed cap. The inner vecs (alternatives,
 * parts) follow the standard vec rule. */
typedef struct {
    ActionBinding actions[ACTION_COUNT];
    AxisBinding axes[AXIS_COUNT];
} BindingStore;

/* --- Comparison ------------------------------------------------------------ */

/* True if `first` and `second` require the same set of physical inputs to
 * fire. Order-independent: a chord evaluates as a set of atoms that must
 * all hold (see physical_held in input_func.c), not a sequence, so two
 * PhysicalInputs with the same atoms in different orders are the same
 * binding. Used by Settings to warn when a freshly captured binding
 * collides with an existing one on a different action. */
[[nodiscard]] bool physical_input_eq(const PhysicalInput *first, const PhysicalInput *second);

/* --- Evaluation ---------------------------------------------------------- */

/* True if any alternative has all parts down AND at least one part newly
 * pressed this frame. */
[[nodiscard]] bool input_pressed(const InputState *input, const BindingStore *store, InputAction action);

/* True if any alternative has all parts down. */
[[nodiscard]] bool input_held(const InputState *input, const BindingStore *store, InputAction action);

/* Sum signed contributions from each alternative atom; clamp to [-1, +1].
 * - ATOM_KB_AXIS: -1 if neg_key down, +1 if pos_key down (sum if both).
 * - ATOM_GP_AXIS: deadzoned axis value * scale.
 * - ATOM_GP_TRIGGER: 0..1 normalized.
 * Other atom kinds contribute 0. */
[[nodiscard]] float input_axis(const InputState *input, const BindingStore *store, InputAxis axis);

/* Read two axes as a Vector2, then apply the unit-disc clamp via
 * input_apply_deadzone(0.0F). Use this for player movement, editor camera
 * pan, entity drag — anywhere the (x, y) pair represents a single direction. */
[[nodiscard]] Vector2
input_axis_pair(const InputState *input, const BindingStore *store, InputAxis x_axis, InputAxis y_axis);

/* Render the binding for `action` as a HUD label like "L1+Left/Ctrl+Z" or
 * "A/Ent". Always null-terminates; truncates safely if cap is exceeded.
 * Returns bytes written (excluding the null terminator). */
int input_func_label(const BindingStore *store, InputAction action, char *out, size_t cap);

/* Same, but indexed by raw int. Mirror of input_func_action_toml_name_at:
 * lets list-row code that has already bounds-checked an integer index
 * skip the cast to InputAction, which the static analyzer flags
 * pessimistically through path-sensitive constraints. */
int input_func_label_at(const BindingStore *store, int action_index, char *out, size_t cap);

/* --- Loading ------------------------------------------------------------- */

/* Initialize `store` with built-in defaults. All vecs use `alloc`. */
void input_func_load_defaults(BindingStore *store, Allocator alloc);

/* Overlay TOML file at `toml_path` onto already-loaded defaults. If the file
 * is missing, returns true (defaults remain). On parse error, sets `err` and
 * returns false (store may be in a partially-overlaid state).
 *
 * Call sequence at startup:
 *   input_func_load_defaults(store, alloc);
 *   if (!input_func_load_bindings_toml(store, alloc, err, KEYBINDINGS_PATH)) {
 *       debug_log(...);  // log and proceed with defaults
 *       error_clear(err);
 *   } */
[[nodiscard]] bool
input_func_load_bindings_toml(BindingStore *store, Allocator alloc, ErrorState *err, const char *toml_path);

/* --- Mutation API -------------------------------------------------------- */

/* All mutators deep-copy the supplied PhysicalInput so the store owns
 * its parts vec; the caller can free or stack-allocate the source freely.
 * `alloc` should be the same allocator used by input_func_load_defaults
 * (typically the gamedata arena). */

[[nodiscard]] bool input_func_set_action_alternative(
    BindingStore *store, Allocator alloc, InputAction action, int alt_index, const PhysicalInput *replacement);

[[nodiscard]] bool input_func_add_action_alternative(BindingStore *store,
                                                     Allocator alloc,
                                                     InputAction action,
                                                     const PhysicalInput *new_alt);

void input_func_remove_action_alternative(BindingStore *store, InputAction action, int alt_index);

/* Drop all alternatives for `action`. Distinguishes "explicitly unbound"
 * from "defaults" once the TOML overlay loader is wired in. */
void input_func_clear_action(BindingStore *store, InputAction action);

[[nodiscard]] bool input_func_set_axis_alternative(
    BindingStore *store, Allocator alloc, InputAxis axis, int alt_index, const PhysicalInput *replacement);

[[nodiscard]] bool
input_func_add_axis_alternative(BindingStore *store, Allocator alloc, InputAxis axis, const PhysicalInput *new_alt);

void input_func_remove_axis_alternative(BindingStore *store, InputAxis axis, int alt_index);

void input_func_clear_axis(BindingStore *store, InputAxis axis);

void input_func_reset_action_to_defaults(BindingStore *store, Allocator alloc, InputAction action);
void input_func_reset_axis_to_defaults(BindingStore *store, Allocator alloc, InputAxis axis);
void input_func_reset_all_to_defaults(BindingStore *store, Allocator alloc);

/* --- Code <-> name translation ------------------------------------------- */

/* Resolve a TOML name like "ENTER", "RIGHT_FACE_DOWN", "LEFT_X" to its
 * raylib enum value. Returns -1 if not found. Used by the TOML loader and
 * exposed for tests. */
[[nodiscard]] int input_func_key_from_name(const char *name);
[[nodiscard]] int input_func_gp_button_from_name(const char *name);
[[nodiscard]] int input_func_gp_axis_from_name(const char *name);

/* HUD short label for a key/button code, e.g. KEY_LEFT_CONTROL -> "Ctrl",
 * GAMEPAD_BUTTON_RIGHT_FACE_DOWN -> "A". Returns "?" if not found. */
[[nodiscard]] const char *input_func_key_label(int key);
[[nodiscard]] const char *input_func_gp_button_label(int button);

/* TOML name (e.g. "LEFT_CONTROL", "RIGHT_FACE_DOWN", "LEFT_X") for a code.
 * Returns nullptr if not found. Used by the bindings TOML emitter. */
[[nodiscard]] const char *input_func_key_name(int key);
[[nodiscard]] const char *input_func_gp_button_name(int button);
[[nodiscard]] const char *input_func_gp_axis_name(int axis);

/* TOML name for an action / axis enum value (the name used as the
 * `[function.NAME]` key in keybindings.toml). Returns nullptr if the
 * enum value is out of range. */
[[nodiscard]] const char *input_func_action_toml_name(InputAction action);
[[nodiscard]] const char *input_func_axis_toml_name(InputAxis axis);

/* Same, but indexed by raw int. Useful when iterating list rows whose
 * row index has already been bounds-checked but where casting to
 * InputAction / InputAxis would trip the EnumCastOutOfRange static
 * analyzer check. Returns nullptr for out-of-range indices. */
[[nodiscard]] const char *input_func_action_toml_name_at(int action_index);
[[nodiscard]] const char *input_func_axis_toml_name_at(int axis_index);
