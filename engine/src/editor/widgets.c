#include "editor/internal.h"

#include "alloc.h"
#include "arena.h"
#include "debug.h"
#include "error.h"
#include "input.h"
#include "input_func.h"
#include "rule.h"
#include "str.h"
#include "strv.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* --- Radial picker --- */

static int radial_sector_from_stick(Vector2 stick, int item_count)
{
    float magnitude = sqrtf((stick.x * stick.x) + (stick.y * stick.y));
    if (magnitude < RADIAL_STICK_THRESHOLD) {
        return -1;
    }
    float angle = atan2f(stick.y, stick.x);
    float two_pi = 2.0F * (float)M_PI;
    float normalized = fmodf(angle + ((float)M_PI / 2.0F) + two_pi, two_pi);
    return (int)(normalized * (float)item_count / two_pi) % item_count;
}

static const char *radial_label(const EditorState *editor_state, int index)
{
    if (editor_state->radial_context == RADIAL_CTX_TOOLS) {
        static const char *const tools[] = {"Grab", "Place", "Handles", "Delete", "Blueprints", "Watch list", "Levels"};
        if (index >= 0 && index < EDITOR_TOOLS_ITEM_COUNT) {
            return tools[index];
        }
    } else if (editor_state->radial_context == RADIAL_CTX_ATTR_TYPE) {
        static const char *const types[] = {"Float", "Int", "Bool", "String"};
        if (index >= 0 && index < 4) {
            return types[index];
        }
    } else if (editor_state->radial_context == RADIAL_CTX_CHILD_PROPS) {
        static const char *const props[] = {"Tag", "Offset X", "Offset Y"};
        if (index >= 0 && index < 3) {
            return props[index];
        }
    }
    return "";
}

static const Color radial_highlight_color = {255, 200, 50, 200}; /* amber: selected radial sector */

void draw_radial_picker(ScreenSize screen, const EditorState *editor_state, Font ui_font)
{
    if (editor_state->sub_mode != EDITOR_SUB_RADIAL) {
        return;
    }
    int center_x = screen.width / 2;
    int center_y = screen.height / 2;
    DrawCircle(center_x, center_y, RADIAL_OUTER_RADIUS + RADIAL_BG_PADDING, debug_bg_color);
    int item_count = editor_state->radial_item_count;
    float sector_deg = RADIAL_FULL_CIRCLE_DEG / (float)item_count;
    for (int index = 0; index < item_count; index++) {
        float start = ((float)index * sector_deg) - RADIAL_NORTH_OFFSET_DEG;
        float end = start + sector_deg;
        bool selected = (index == editor_state->radial_selected);
        Color sector_color = selected ? radial_highlight_color
                                      : (Color){radial_highlight_color.r, radial_highlight_color.g,
                                                radial_highlight_color.b, radial_highlight_color.a / 2};
        DrawRing((Vector2){(float)center_x, (float)center_y}, RADIAL_INNER_RADIUS, RADIAL_OUTER_RADIUS, start, end, 16,
                 sector_color);
        float mid_deg = start + (sector_deg / 2.0F);
        float mid_rad = mid_deg * RADIAL_DEG_TO_RAD;
        float label_radius = (RADIAL_INNER_RADIUS + RADIAL_OUTER_RADIUS) / 2.0F;
        int label_x = center_x + (int)(cosf(mid_rad) * label_radius);
        int label_y = center_y + (int)(sinf(mid_rad) * label_radius);
        const char *label = radial_label(editor_state, index);
        int text_width = measure_ui_text(ui_font, label, RADIAL_FONT_SIZE);
        draw_ui_text(ui_font, label, label_x - (text_width / 2), label_y - (RADIAL_FONT_SIZE / 2), RADIAL_FONT_SIZE,
                     sector_color);
    }
    DrawCircle(center_x, center_y, RADIAL_INNER_RADIUS, debug_bg_color);
}

void handle_radial_input(EditorState *editor_state, const InputState *input, const BindingStore *bindings)
{
    Vector2 stick = input_axis_pair(input, bindings, AXIS_PRIMARY_X, AXIS_PRIMARY_Y);
    editor_state->radial_selected = radial_sector_from_stick(stick, editor_state->radial_item_count);
    if (input_pressed(input, bindings, ACTION_CANCEL)) {
        editor_state->radial_confirmed = -1;
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    if (input_pressed(input, bindings, ACTION_CONFIRM)) {
        editor_state->radial_confirmed = editor_state->radial_selected;
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
    }
}

/* --- Word builder --- */

static const char *const word_builder_builtin[] = {
    "chest", "locked",  "magic", "key",   "door",  "open", "closed", "hidden",  "secret", "boss",    "enemy",
    "spawn", "trigger", "zone",  "north", "south", "east", "west",   "bridge",  "gate",   "switch",  "lever",
    "fire",  "ice",     "water", "stone", "wood",  "gold", "silver", "sword",   "shield", "bow",     "arrow",
    "heart", "potion",  "fairy", "dark",  "light", "cave", "forest", "dungeon", "castle", "village", "temple",
    "tower", "path",    "wall",  "floor", "roof",  "big",  "small",  "red",     "blue",   "green",
};
#define WORD_BUILDER_BUILTIN_COUNT (int)(sizeof(word_builder_builtin) / sizeof(word_builder_builtin[0]))

static int word_builder_total_count(const GameState *state)
{
    return 1 + WORD_BUILDER_BUILTIN_COUNT + state->gamedata.blueprints.entries.count;
}

static const char *word_builder_item(const GameState *state, int index)
{
    if (index <= 0) {
        return "[ DONE ]";
    }
    int builtin_index = index - 1;
    if (builtin_index < WORD_BUILDER_BUILTIN_COUNT) {
        return word_builder_builtin[builtin_index];
    }
    int blueprint_index = builtin_index - WORD_BUILDER_BUILTIN_COUNT;
    if (blueprint_index < state->gamedata.blueprints.entries.count) {
        const char *name = attr_get_string(&state->gamedata.blueprints.entries.data[blueprint_index].attrs, "name");
        return name ? name : "?";
    }
    return "";
}

static void word_builder_append(EditorState *editor_state, const char *word)
{
    int word_len = (int)strlen(word);
    int separator = (editor_state->word_builder_len > 0) ? 1 : 0;
    int needed = editor_state->word_builder_len + separator + word_len;
    if (needed >= WORD_BUILDER_BUF_SIZE) {
        return;
    }
    if (separator > 0) {
        editor_state->word_builder_buf[editor_state->word_builder_len] = '_';
        editor_state->word_builder_len++;
    }
    memcpy(editor_state->word_builder_buf + editor_state->word_builder_len, word, (size_t)word_len);
    editor_state->word_builder_len += word_len;
    editor_state->word_builder_buf[editor_state->word_builder_len] = '\0';
}

static void word_builder_pop(EditorState *editor_state)
{
    if (editor_state->word_builder_len == 0) {
        return;
    }
    for (int index = editor_state->word_builder_len - 1; index >= 0; index--) {
        if (editor_state->word_builder_buf[index] == '_') {
            editor_state->word_builder_buf[index] = '\0';
            editor_state->word_builder_len = index;
            return;
        }
    }
    editor_state->word_builder_buf[0] = '\0';
    editor_state->word_builder_len = 0;
}

void draw_word_builder_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state)
{
    if (editor_state->sub_mode != EDITOR_SUB_WORD_BUILDER && editor_state->sub_mode != EDITOR_SUB_GAMEPAD_KB) {
        return;
    }
    int panel_x = screen.width - EDITOR_PANEL_WIDTH;
    DrawRectangle(panel_x, 0, EDITOR_PANEL_WIDTH, screen.height, debug_bg_color);
    int y_offset = 0;
    Font font = state->assets.ui_font;
    draw_ui_text(font, "[ Word Builder ]", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT;
    draw_ui_text(font, TextFormat("  %s", editor_state->word_builder_buf), panel_x + DEBUG_MARGIN, y_offset,
                 EDITOR_PANEL_FONT_SIZE, WHITE);
    y_offset += EDITOR_PANEL_LINE_HEIGHT * 2;

    int total = word_builder_total_count(state);
    int scroll = editor_state->word_builder_scroll;
    int visible = place_visible_count(screen.height);
    int scroll_offset = scroll - (visible / 2);
    if (scroll_offset < 0) {
        scroll_offset = 0;
    }
    int max_scroll = total - visible;
    if (max_scroll < 0) {
        max_scroll = 0;
    }
    if (scroll_offset > max_scroll) {
        scroll_offset = max_scroll;
    }
    int end = scroll_offset + visible;
    if (end > total) {
        end = total;
    }
    for (int index = scroll_offset; index < end; index++) {
        const char *item = word_builder_item(state, index);
        bool selected = (index == scroll);
        Color color = selected ? WHITE : debug_text_color;
        draw_ui_text(font, TextFormat("%s %s", selected ? ">" : " ", item), panel_x + DEBUG_MARGIN, y_offset,
                     EDITOR_PANEL_FONT_SIZE, color);
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
    }
}

static bool add_attr_by_name(Diag *diag, GameState *state, EditorState *editor_state, const char *name)
{
    if (!name || name[0] == '\0') {
        return false;
    }
    AttrSet *target = nullptr;
    if (editor_state->top_mode == EDITOR_TOP_BLUEPRINT || editor_state->adding_blueprint_attr) {
        int bp_idx = editor_state->selected_blueprint_index;
        /* In scene mode, selected_blueprint_index isn't set; resolve from the selected entity. */
        if (editor_state->top_mode == EDITOR_TOP_SCENE) {
            int sel = level_find_entity_by_id(&state->gamedata.current_level, editor_state->selected_entity_id);
            if (sel < 0) {
                return false;
            }
            Blueprint *blueprint =
                find_blueprint_by_name(state, state->gamedata.current_level.entities.data[sel].blueprint_name.ptr);
            if (!blueprint) {
                return false;
            }
            target = &blueprint->attrs;
        } else {
            if (bp_idx < 0 || bp_idx >= state->gamedata.blueprints.entries.count) {
                return false;
            }
            target = &state->gamedata.blueprints.entries.data[bp_idx].attrs;
        }
    } else {
        int sel = level_find_entity_by_id(&state->gamedata.current_level, editor_state->selected_entity_id);
        if (sel < 0) {
            return false;
        }
        Entity *entity = &state->gamedata.current_level.entities.data[sel];
        target = editor_state->adding_persisted_attr ? &entity->persisted_attrs : &entity->attrs;
    }
    if (attr_get(target, name)) {
        return false;
    }
    Allocator alloc = allocator_arena(&state->gamedata_arena);
    if (!attr_set_int(&alloc, target, name, 0)) {
        debug_log(diag->debug, "add attr: attr_set_int failed: %s", error_get(diag->error));
        error_clear(diag->error);
        return false;
    }
    return true;
}

static void word_builder_confirm(Diag *diag, GameState *state, EditorState *editor_state)
{
    Attribute *attr = nullptr;
    AttrSet *target = nullptr;
    if (editor_state->top_mode == EDITOR_TOP_BLUEPRINT) {
        int bp_idx = editor_state->selected_blueprint_index;
        int attr_idx = editor_state->blueprint_attr_index;
        if (bp_idx < 0 || attr_idx < 0) {
            return;
        }
        Blueprint *blueprint = &state->gamedata.blueprints.entries.data[bp_idx];
        if (attr_idx >= blueprint->attrs.entries.count) {
            return;
        }
        attr = &blueprint->attrs.entries.data[attr_idx];
        target = &blueprint->attrs;
    } else {
        int sel = level_find_entity_by_id(&state->gamedata.current_level, editor_state->selected_entity_id);
        if (sel < 0) {
            return;
        }
        Entity *entity = &state->gamedata.current_level.entities.data[sel];
        int attr_idx = editor_resolve_selected_attr_index(state, entity, editor_state);
        if (attr_idx < 0) {
            return;
        }
        attr = attr_at_display_index(state, entity, attr_idx);
        if (!attr) {
            return;
        }
        AttrRow row = attr_row_at(state, entity, attr_idx);
        target = attr_section_set(state, entity, row.section);
        if (!target) {
            return;
        }
    }
    Allocator alloc = allocator_arena(&state->gamedata_arena);
    AttrStringPair pair = {attr->name.ptr, editor_state->word_builder_buf};
    if (!attr_set_string(&alloc, target, pair)) {
        debug_log(diag->debug, "word builder: attr_set_string failed: %s", error_get(diag->error));
        error_clear(diag->error);
    }
}

static void
word_builder_navigate(EditorState *editor_state, const InputState *input, const BindingStore *bindings, int total)
{
    if (input_pressed(input, bindings, ACTION_NAV_UP)) {
        if (editor_state->word_builder_scroll > 0) {
            editor_state->word_builder_scroll--;
        }
    }
    if (input_pressed(input, bindings, ACTION_NAV_DOWN)) {
        if (editor_state->word_builder_scroll < total - 1) {
            editor_state->word_builder_scroll++;
        }
    }
    if (input_pressed(input, bindings, ACTION_PAGE_UP)) {
        int new_scroll = editor_state->word_builder_scroll - WORD_BUILDER_PAGE_SIZE;
        editor_state->word_builder_scroll = (new_scroll < 0) ? 0 : new_scroll;
    }
    if (input_pressed(input, bindings, ACTION_PAGE_DOWN)) {
        int new_scroll = editor_state->word_builder_scroll + WORD_BUILDER_PAGE_SIZE;
        editor_state->word_builder_scroll = (new_scroll >= total) ? total - 1 : new_scroll;
    }
}

void handle_word_builder_input(
    Diag *diag, GameState *state, EditorState *editor_state, UndoHistory *undo_history, const InputState *input)
{
    int total = word_builder_total_count(state);
    word_builder_navigate(editor_state, input, &state->bindings, total);
    if (input_pressed(input, &state->bindings, ACTION_CONFIRM)) {
        if (editor_state->word_builder_scroll == 0 && editor_state->creating_blueprint) {
            create_blank_blueprint(state, editor_state, undo_history, editor_state->word_builder_buf);
            editor_state->creating_blueprint = false;
            editor_state->sub_mode = EDITOR_SUB_BROWSE;
        } else if (editor_state->word_builder_scroll == 0 && editor_state->duplicating_blueprint) {
            duplicate_blueprint(state, editor_state, undo_history, editor_state->word_builder_buf);
            editor_state->duplicating_blueprint = false;
            editor_state->sub_mode = EDITOR_SUB_BROWSE;
        } else if (editor_state->word_builder_scroll == 0 && editor_state->creating_level) {
            create_new_level(diag, state, editor_state, undo_history, editor_state->word_builder_buf);
            editor_state->creating_level = false;
            editor_state->sub_mode = EDITOR_SUB_BROWSE;
        } else if (editor_state->word_builder_scroll == 0 &&
                   editor_state->editing_level_string_field != LEVEL_STRING_FIELD_NONE) {
            confirm_level_string_edit(state, editor_state, undo_history);
            editor_state->editing_level_string_field = LEVEL_STRING_FIELD_NONE;
            editor_state->sub_mode = EDITOR_SUB_BROWSE;
        } else if (editor_state->word_builder_scroll == 0 && editor_state->editing_child_tag) {
            confirm_child_tag_edit(diag, state, editor_state, undo_history);
            editor_state->sub_mode = EDITOR_SUB_BROWSE;
        } else if (editor_state->word_builder_scroll == 0 &&
                   (editor_state->adding_attr || editor_state->adding_blueprint_attr ||
                    editor_state->adding_persisted_attr)) {
            add_attr_by_name(diag, state, editor_state, editor_state->word_builder_buf);
            undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                                   strv_from_cstr("Add attribute"));
            editor_state->adding_attr = false;
            editor_state->adding_blueprint_attr = false;
            editor_state->adding_persisted_attr = false;
            editor_state->sub_mode = EDITOR_SUB_BROWSE;
        } else if (editor_state->word_builder_scroll == 0) {
            word_builder_confirm(diag, state, editor_state);
            undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                                   strv_from_cstr("Edit string"));
            editor_state->sub_mode = EDITOR_SUB_BROWSE;
        } else {
            word_builder_append(editor_state, word_builder_item(state, editor_state->word_builder_scroll));
        }
    }
    if (input_pressed(input, &state->bindings, ACTION_WB_KEYBOARD_MODE)) {
        keyboard_widget_reset(&editor_state->word_builder_kb, editor_state->word_builder_buf,
                              &editor_state->word_builder_len, WORD_BUILDER_BUF_SIZE);
        editor_state->sub_mode = EDITOR_SUB_GAMEPAD_KB;
        return;
    }
    if (input_pressed(input, &state->bindings, ACTION_CANCEL)) {
        if (editor_state->word_builder_len > 0) {
            word_builder_pop(editor_state);
        } else {
            editor_state->adding_attr = false;
            editor_state->adding_blueprint_attr = false;
            editor_state->adding_persisted_attr = false;
            editor_state->editing_child_tag = false;
            editor_state->creating_blueprint = false;
            editor_state->duplicating_blueprint = false;
            editor_state->creating_level = false;
            editor_state->editing_level_string_field = LEVEL_STRING_FIELD_NONE;
            editor_state->sub_mode = EDITOR_SUB_BROWSE;
        }
    }
}

/* --- Fuzzy finder (name picker) --- */

static int compare_cstr_ptrs(const void *lhs, const void *rhs)
{
    return strcmp(*(const char *const *)lhs, *(const char *const *)rhs);
}

static bool fuzzy_finder_contains(const char **items, int count, const char *name)
{
    for (int index = 0; index < count; index++) {
        if (strcmp(items[index], name) == 0) {
            return true;
        }
    }
    return false;
}

static void fuzzy_finder_try_add(const char **items, int *count, const char *name)
{
    if (name && name[0] != '\0' && !fuzzy_finder_contains(items, *count, name)) {
        items[(*count)++] = name;
    }
}

static void fuzzy_finder_collect_blueprint_names(const GamedataState *gamedata, const char **items, int *count)
{
    for (int index = 0; index < gamedata->blueprints.entries.count; index++) {
        fuzzy_finder_try_add(items, count, attr_get_string(&gamedata->blueprints.entries.data[index].attrs, "name"));
    }
}

static void fuzzy_finder_collect_flag_names(const FlagSet *flags, const char **items, int *count)
{
    for (int index = 0; index < flags->names.count; index++) {
        fuzzy_finder_try_add(items, count, flags->names.data[index].name.ptr);
    }
}

static void fuzzy_finder_collect_level_names(const GamedataState *gamedata, const char **items, int *count)
{
    fuzzy_finder_try_add(items, count, gamedata->current_level.name.ptr);
    for (int index = 0; index < gamedata->other_levels.count; index++) {
        fuzzy_finder_try_add(items, count, gamedata->other_levels.data[index].name.ptr);
    }
}

static void fuzzy_finder_collect_entity_tags(const GamedataState *gamedata, const char **items, int *count)
{
    for (int index = 0; index < gamedata->current_level.entities.count; index++) {
        fuzzy_finder_try_add(items, count, gamedata->current_level.entities.data[index].tag.ptr);
    }
}

static void fuzzy_finder_collect_attr_names_from_set(const AttrSet *attrs, const char **items, int *count)
{
    for (int index = 0; index < attrs->entries.count; index++) {
        fuzzy_finder_try_add(items, count, attrs->entries.data[index].name.ptr);
    }
}

static void fuzzy_finder_collect_all_attr_names(const GamedataState *gamedata, const char **items, int *count)
{
    for (int index = 0; index < gamedata->blueprints.entries.count; index++) {
        fuzzy_finder_collect_attr_names_from_set(&gamedata->blueprints.entries.data[index].attrs, items, count);
    }
    for (int index = 0; index < gamedata->current_level.entities.count; index++) {
        fuzzy_finder_collect_attr_names_from_set(&gamedata->current_level.entities.data[index].attrs, items, count);
    }
}

static int fuzzy_finder_max_item_count(const GamedataState *gamedata, const FlagSet *flags)
{
    int max_count = gamedata->blueprints.entries.count + flags->names.count + 1 + gamedata->other_levels.count +
                    gamedata->current_level.entities.count;
    for (int index = 0; index < gamedata->blueprints.entries.count; index++) {
        max_count += gamedata->blueprints.entries.data[index].attrs.entries.count;
    }
    for (int index = 0; index < gamedata->current_level.entities.count; index++) {
        max_count += gamedata->current_level.entities.data[index].attrs.entries.count;
    }
    return max_count;
}

void fuzzy_finder_build_items(GameState *state, EditorState *editor_state)
{
    int max_count = fuzzy_finder_max_item_count(&state->gamedata, &state->progression.flags);
    const char **items = (const char **)arena_alloc(&state->gamedata_arena, sizeof(const char *) * (size_t)max_count);
    if (!items) {
        editor_state->fuzzy_finder_items = nullptr;
        editor_state->fuzzy_finder_item_count = 0;
        return;
    }
    int count = 0;

    fuzzy_finder_collect_blueprint_names(&state->gamedata, items, &count);
    fuzzy_finder_collect_flag_names(&state->progression.flags, items, &count);
    fuzzy_finder_collect_level_names(&state->gamedata, items, &count);
    fuzzy_finder_collect_entity_tags(&state->gamedata, items, &count);
    fuzzy_finder_collect_all_attr_names(&state->gamedata, items, &count);

    qsort((void *)items, (size_t)count, sizeof(const char *), compare_cstr_ptrs);

    editor_state->fuzzy_finder_items = items;
    editor_state->fuzzy_finder_item_count = count;
}

static const char *fuzzy_finder_item(const EditorState *editor_state, int index)
{
    if (index <= 0) {
        return "[ NEW... ]";
    }
    int item_index = index - 1;
    if (item_index < editor_state->fuzzy_finder_item_count) {
        return editor_state->fuzzy_finder_items[item_index];
    }
    return "";
}

static int fuzzy_finder_total_count(const EditorState *editor_state)
{
    return 1 + editor_state->fuzzy_finder_item_count;
}

void draw_fuzzy_finder_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state)
{
    if (editor_state->sub_mode != EDITOR_SUB_FUZZY_FINDER) {
        return;
    }
    int panel_x = screen.width - EDITOR_PANEL_WIDTH;
    DrawRectangle(panel_x, 0, EDITOR_PANEL_WIDTH, screen.height, debug_bg_color);
    int y_offset = 0;
    Font font = state->assets.ui_font;
    draw_ui_text(font, TextFormat("[ Name Picker ] (%d)", editor_state->fuzzy_finder_item_count),
                 panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT * 2;

    int total = fuzzy_finder_total_count(editor_state);
    int scroll = editor_state->fuzzy_finder_scroll;
    int visible = place_visible_count(screen.height);
    int scroll_offset = scroll - (visible / 2);
    if (scroll_offset < 0) {
        scroll_offset = 0;
    }
    int max_scroll = total - visible;
    if (max_scroll < 0) {
        max_scroll = 0;
    }
    if (scroll_offset > max_scroll) {
        scroll_offset = max_scroll;
    }
    int end = scroll_offset + visible;
    if (end > total) {
        end = total;
    }
    for (int index = scroll_offset; index < end; index++) {
        const char *item = fuzzy_finder_item(editor_state, index);
        bool selected = (index == scroll);
        Color color = selected ? WHITE : debug_text_color;
        draw_ui_text(font, TextFormat("%s %s", selected ? ">" : " ", item), panel_x + DEBUG_MARGIN, y_offset,
                     EDITOR_PANEL_FONT_SIZE, color);
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
    }
}

static void
fuzzy_finder_navigate(EditorState *editor_state, const InputState *input, const BindingStore *bindings, int total)
{
    if (input_pressed(input, bindings, ACTION_NAV_UP)) {
        if (editor_state->fuzzy_finder_scroll > 0) {
            editor_state->fuzzy_finder_scroll--;
        }
    }
    if (input_pressed(input, bindings, ACTION_NAV_DOWN)) {
        if (editor_state->fuzzy_finder_scroll < total - 1) {
            editor_state->fuzzy_finder_scroll++;
        }
    }
    if (input_pressed(input, bindings, ACTION_PAGE_UP)) {
        int new_scroll = editor_state->fuzzy_finder_scroll - FUZZY_FINDER_PAGE_SIZE;
        editor_state->fuzzy_finder_scroll = (new_scroll < 0) ? 0 : new_scroll;
    }
    if (input_pressed(input, bindings, ACTION_PAGE_DOWN)) {
        int new_scroll = editor_state->fuzzy_finder_scroll + FUZZY_FINDER_PAGE_SIZE;
        editor_state->fuzzy_finder_scroll = (new_scroll >= total) ? total - 1 : new_scroll;
    }
}

static void fuzzy_finder_enter_word_builder(GameState *state, EditorState *editor_state)
{
    int sel = level_find_entity_by_id(&state->gamedata.current_level, editor_state->selected_entity_id);
    const char *existing = "";
    if (sel >= 0) {
        Entity *entity = &state->gamedata.current_level.entities.data[sel];
        int attr_idx = editor_resolve_selected_attr_index(state, entity, editor_state);
        const Attribute *attr = (attr_idx >= 0) ? attr_at_display_index(state, entity, attr_idx) : nullptr;
        if (attr && attr->type == ATTR_STRING && attr->value.str.ptr) {
            existing = attr->value.str.ptr;
        }
    }
    int existing_len = (int)strlen(existing);
    if (existing_len >= WORD_BUILDER_BUF_SIZE) {
        existing_len = WORD_BUILDER_BUF_SIZE - 1;
    }
    memcpy(editor_state->word_builder_buf, existing, (size_t)existing_len);
    editor_state->word_builder_buf[existing_len] = '\0';
    editor_state->word_builder_len = existing_len;
    editor_state->word_builder_scroll = 0;
    editor_state->sub_mode = EDITOR_SUB_WORD_BUILDER;
}

static void fuzzy_finder_confirm(Diag *diag, GameState *state, EditorState *editor_state)
{
    Attribute *attr = nullptr;
    AttrSet *target = nullptr;
    if (editor_state->top_mode == EDITOR_TOP_BLUEPRINT) {
        int bp_idx = editor_state->selected_blueprint_index;
        int attr_idx = editor_state->blueprint_attr_index;
        if (bp_idx < 0 || attr_idx < 0) {
            return;
        }
        Blueprint *blueprint = &state->gamedata.blueprints.entries.data[bp_idx];
        if (attr_idx >= blueprint->attrs.entries.count) {
            return;
        }
        attr = &blueprint->attrs.entries.data[attr_idx];
        target = &blueprint->attrs;
    } else {
        int sel = level_find_entity_by_id(&state->gamedata.current_level, editor_state->selected_entity_id);
        if (sel < 0) {
            return;
        }
        Entity *entity = &state->gamedata.current_level.entities.data[sel];
        int attr_idx = editor_resolve_selected_attr_index(state, entity, editor_state);
        if (attr_idx < 0) {
            return;
        }
        attr = attr_at_display_index(state, entity, attr_idx);
        if (!attr) {
            return;
        }
        AttrRow row = attr_row_at(state, entity, attr_idx);
        target = attr_section_set(state, entity, row.section);
        if (!target) {
            return;
        }
    }
    const char *chosen = fuzzy_finder_item(editor_state, editor_state->fuzzy_finder_scroll);
    Allocator alloc = allocator_arena(&state->gamedata_arena);
    AttrStringPair pair = {attr->name.ptr, chosen};
    if (!attr_set_string(&alloc, target, pair)) {
        debug_log(diag->debug, "fuzzy finder: attr_set_string failed: %s", error_get(diag->error));
        error_clear(diag->error);
    }
}

void handle_fuzzy_finder_input(Diag *diag,
                               GameState *state,
                               EditorState *editor_state,
                               UndoHistory *undo_history,
                               TextureLookupFn texture_lookup,
                               void *texture_user_data,
                               const InputState *input)
{
    int total = fuzzy_finder_total_count(editor_state);
    fuzzy_finder_navigate(editor_state, input, &state->bindings, total);
    if (input_pressed(input, &state->bindings, ACTION_CONFIRM)) {
        if (editor_state->adding_child) {
            if (editor_state->fuzzy_finder_scroll == 0) {
                fuzzy_finder_enter_word_builder(state, editor_state);
            } else {
                const char *chosen = fuzzy_finder_item(editor_state, editor_state->fuzzy_finder_scroll);
                add_blueprint_child(diag, state, editor_state, undo_history, chosen, texture_lookup, texture_user_data);
                editor_state->adding_child = false;
                editor_state->sub_mode = EDITOR_SUB_BROWSE;
            }
        } else if (editor_state->adding_attr || editor_state->adding_blueprint_attr ||
                   editor_state->adding_persisted_attr) {
            if (editor_state->fuzzy_finder_scroll == 0) {
                fuzzy_finder_enter_word_builder(state, editor_state);
            } else {
                const char *chosen = fuzzy_finder_item(editor_state, editor_state->fuzzy_finder_scroll);
                add_attr_by_name(diag, state, editor_state, chosen);
                undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                                       strv_from_cstr("Add attribute"));
                editor_state->adding_attr = false;
                editor_state->adding_blueprint_attr = false;
                editor_state->adding_persisted_attr = false;
                editor_state->sub_mode = EDITOR_SUB_BROWSE;
            }
        } else if (editor_state->fuzzy_finder_scroll == 0) {
            fuzzy_finder_enter_word_builder(state, editor_state);
        } else {
            fuzzy_finder_confirm(diag, state, editor_state);
            undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                                   strv_from_cstr("Pick name"));
            editor_state->sub_mode = EDITOR_SUB_BROWSE;
        }
    }
    if (input_pressed(input, &state->bindings, ACTION_CANCEL)) {
        editor_state->adding_attr = false;
        editor_state->adding_child = false;
        editor_state->adding_blueprint_attr = false;
        editor_state->adding_persisted_attr = false;
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
    }
}

/* --- Gamepad keyboard shim ---
 * The two-level radial character picker lives in keyboard_widget.{h,c}
 * so settings can reuse it. The editor host wires the widget to the
 * word_builder_buf via word_builder_kb (see enter_word_builder_empty
 * in blueprint.c) and routes EXIT_REQUESTED back into WORD_BUILDER. */

void draw_gamepad_kb(ScreenSize screen, const EditorState *editor_state, Font ui_font)
{
    if (editor_state->sub_mode != EDITOR_SUB_GAMEPAD_KB) {
        return;
    }
    keyboard_widget_draw(&editor_state->word_builder_kb, (KbScreenSize){screen.width, screen.height}, ui_font);
}

void handle_gamepad_kb_input(EditorState *editor_state, const InputState *input, const BindingStore *bindings)
{
    KeyboardWidgetResult result = keyboard_widget_handle_input(&editor_state->word_builder_kb, input, bindings);
    if (result == KB_RESULT_EXIT_REQUESTED) {
        editor_state->sub_mode = EDITOR_SUB_WORD_BUILDER;
        return;
    }
    if (input_pressed(input, bindings, ACTION_WB_KEYBOARD_MODE)) {
        editor_state->sub_mode = EDITOR_SUB_WORD_BUILDER;
    }
}

/* --- Hint tables --- */

static const EditorActionHint radial_hints[] = {
    {ACTION_CONFIRM, "Confirm"},
    {ACTION_CANCEL, "Cancel"},
};

static const EditorHintTable radial_table = {
    .hints = radial_hints,
    .count = (int)(sizeof(radial_hints) / sizeof(radial_hints[0])),
    .mode_label = "Radial picker",
};

const EditorHintTable *radial_hints_table(void)
{
    return &radial_table;
}

static const EditorActionHint word_builder_hints[] = {
    {ACTION_NAV_UP, "Prev"},
    {ACTION_NAV_DOWN, "Next"},
    {ACTION_PAGE_UP, "Page up"},
    {ACTION_PAGE_DOWN, "Page down"},
    {ACTION_CONFIRM, "Append / Done"},
    {ACTION_WB_KEYBOARD_MODE, "Keyboard"},
    {ACTION_KEYBOARD_BACKSPACE, "Bksp"},
    {ACTION_CANCEL, "Pop / Cancel"},
};

static const EditorHintTable word_builder_table = {
    .hints = word_builder_hints,
    .count = (int)(sizeof(word_builder_hints) / sizeof(word_builder_hints[0])),
    .mode_label = "Word builder",
};

const EditorHintTable *word_builder_hints_table(void)
{
    return &word_builder_table;
}

static const EditorActionHint fuzzy_finder_hints[] = {
    {ACTION_NAV_UP, "Prev"},         {ACTION_NAV_DOWN, "Next"}, {ACTION_PAGE_UP, "Page up"},
    {ACTION_PAGE_DOWN, "Page down"}, {ACTION_CONFIRM, "Pick"},  {ACTION_CANCEL, "Cancel"},
};

static const EditorHintTable fuzzy_finder_table = {
    .hints = fuzzy_finder_hints,
    .count = (int)(sizeof(fuzzy_finder_hints) / sizeof(fuzzy_finder_hints[0])),
    .mode_label = "Name picker",
};

const EditorHintTable *fuzzy_finder_hints_table(void)
{
    return &fuzzy_finder_table;
}

static const EditorActionHint gamepad_kb_hints[] = {
    {ACTION_CONFIRM, "Select"},
    {ACTION_KEYBOARD_BACKSPACE, "Bksp"},
    {ACTION_CANCEL, "Back"},
    {ACTION_WB_KEYBOARD_MODE, "Word builder"},
};

static const EditorHintTable gamepad_kb_table = {
    .hints = gamepad_kb_hints,
    .count = (int)(sizeof(gamepad_kb_hints) / sizeof(gamepad_kb_hints[0])),
    .mode_label = "Keyboard",
};

const EditorHintTable *gamepad_kb_hints_table(void)
{
    return &gamepad_kb_table;
}
