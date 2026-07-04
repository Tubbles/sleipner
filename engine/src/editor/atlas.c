#include "editor/internal.h"

#include "atlas.h"
#include "alloc.h"
#include "str.h"
#include "strv.h"

#include <string.h>

/* World-space anchor for the zoomed atlas texture view (editor/draw.c ->
 * draw_atlas_texture_view below). Deliberately far from the level origin
 * (0,0) so the texture view never overlaps the current level's ground
 * render, which still runs underneath every editor mode (S5.4b). */
#define ATLAS_VIEW_ANCHOR_X 10000.0F
#define ATLAS_VIEW_ANCHOR_Y 10000.0F

static const Color atlas_handle_color = {0, 255, 255, 255};       /* cyan: matches HANDLES's handle_color */
static const Color atlas_new_region_color = {100, 220, 100, 255}; /* green: matches the "+ ADD" sentinel convention */

/* --- Shared helpers --- */

static void toast_atlas_error(EditorState *editor_state, const char *message)
{
    editor_state->toast_text = strv_from_cstr(message);
    editor_state->toast_timer = TOAST_DURATION;
}

static int atlas_region_count_for_texture(const vec_atlas_region *regions, const char *texture_name)
{
    int count = 0;
    for (int index = 0; index < regions->count; index++) {
        if (strcmp(regions->data[index].texture.ptr, texture_name) == 0) {
            count++;
        }
    }
    return count;
}

/* Absolute index into `regions` of the `filtered_index`-th entry whose
 * texture matches `texture_name`, in vec order. Returns -1 if out of range. */
static int atlas_region_index_at(const vec_atlas_region *regions, const char *texture_name, int filtered_index)
{
    int seen = 0;
    for (int index = 0; index < regions->count; index++) {
        if (strcmp(regions->data[index].texture.ptr, texture_name) != 0) {
            continue;
        }
        if (seen == filtered_index) {
            return index;
        }
        seen++;
    }
    return -1;
}

/* Clamps a staging src rect to `texture_size` (texture px) and floors width/
 * height at 1px. texture_size.x/y <= 0 skips the bounds clamp (headless
 * tests seed a Texture2D with width/height when they want to exercise
 * clamping; a zero-sized texture just means "no known bounds yet"). Takes
 * the size as one Vector2 rather than two adjacent ints/floats to sidestep
 * bugprone-easily-swappable-parameters (same shape as level_detail_clamp's
 * NOLINT in editor/level.c, but avoidable here since there's a natural
 * single-value grouping). */
static void clamp_atlas_edit_src(Rectangle *src, Vector2 texture_size)
{
    if (src->width < 1.0F) {
        src->width = 1.0F;
    }
    if (src->height < 1.0F) {
        src->height = 1.0F;
    }
    if (texture_size.x > 0.0F) {
        if (src->x < 0.0F) {
            src->x = 0.0F;
        }
        if (src->x > texture_size.x - 1.0F) {
            src->x = texture_size.x - 1.0F;
        }
        if (src->width > texture_size.x - src->x) {
            src->width = texture_size.x - src->x;
        }
    }
    if (texture_size.y > 0.0F) {
        if (src->y < 0.0F) {
            src->y = 0.0F;
        }
        if (src->y > texture_size.y - 1.0F) {
            src->y = texture_size.y - 1.0F;
        }
        if (src->height > texture_size.y - src->y) {
            src->height = texture_size.y - src->y;
        }
    }
}

/* Default rect for a brand-new region: top-left origin, EDITOR_ATLAS_DEFAULT_REGION_SIZE
 * square, shrunk to fit a texture smaller than that default (texture_size.x/y <= 0
 * means "unknown bounds" -- keep the full default rather than shrinking to 0). */
static Rectangle default_region_src(Vector2 texture_size)
{
    float width = (texture_size.x > 0.0F && texture_size.x < (float)EDITOR_ATLAS_DEFAULT_REGION_SIZE)
                      ? texture_size.x
                      : (float)EDITOR_ATLAS_DEFAULT_REGION_SIZE;
    float height = (texture_size.y > 0.0F && texture_size.y < (float)EDITOR_ATLAS_DEFAULT_REGION_SIZE)
                       ? texture_size.y
                       : (float)EDITOR_ATLAS_DEFAULT_REGION_SIZE;
    return (Rectangle){0.0F, 0.0F, width, height};
}

static Vector2 atlas_texture_size(const GameState *state, int texture_index)
{
    if (texture_index < 0 || texture_index >= state->assets.textures.count) {
        return (Vector2){0.0F, 0.0F};
    }
    const Texture2D *texture = &state->assets.textures.data[texture_index].texture;
    return (Vector2){(float)texture->width, (float)texture->height};
}

/* --- Cross-file call: widgets.c's word builder CONFIRM dispatch --- */

/* Validates `name` (non-empty, not already used by any atlas region) and
 * stashes it in atlas_region_pending_name -- the name isn't written into
 * gamedata.atlas_regions until REGION_EDIT's CONFIRM (commit_atlas_region
 * below), so a CANCEL out of REGION_EDIT never has to unwind a partially
 * created region. Returns true and switches to REGION_EDIT on success;
 * on failure, toasts and leaves the caller to fall back to ATLAS_BROWSE. */
bool start_new_atlas_region(GameState *state, EditorState *editor_state, const char *name)
{
    if (!name || name[0] == '\0') {
        toast_atlas_error(editor_state, "Region name required");
        return false;
    }
    if (atlas_find_region(&state->gamedata.atlas_regions, name)) {
        toast_atlas_error(editor_state, "Region name already exists");
        return false;
    }
    int texture_index = editor_state->atlas_texture_index;
    if (texture_index < 0 || texture_index >= state->assets.textures.count) {
        toast_atlas_error(editor_state, "No texture selected");
        return false;
    }
    int name_len = (int)strlen(name);
    if (name_len >= EDITOR_ATTR_NAME_MAX) {
        name_len = EDITOR_ATTR_NAME_MAX - 1;
    }
    memcpy(editor_state->atlas_region_pending_name, name, (size_t)name_len);
    editor_state->atlas_region_pending_name[name_len] = '\0';
    editor_state->atlas_region_index = -1;
    editor_state->atlas_edit_src = default_region_src(atlas_texture_size(state, texture_index));
    return true;
}

/* --- REGION_EDIT --- */

static void commit_atlas_region(GameState *state, EditorState *editor_state, UndoHistory *undo_history)
{
    vec_atlas_region *regions = &state->gamedata.atlas_regions;
    if (editor_state->atlas_region_index >= 0 && editor_state->atlas_region_index < regions->count) {
        regions->data[editor_state->atlas_region_index].src = editor_state->atlas_edit_src;
        undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                               strv_from_cstr("Edit atlas region"));
        editor_state->sub_mode = EDITOR_SUB_ATLAS_BROWSE;
        return;
    }

    int texture_index = editor_state->atlas_texture_index;
    if (texture_index < 0 || texture_index >= state->assets.textures.count) {
        editor_state->sub_mode = EDITOR_SUB_ATLAS_BROWSE;
        return;
    }
    Allocator alloc = allocator_arena(&state->gamedata_arena);
    AtlasRegion region = {.src = editor_state->atlas_edit_src};
    region.name = str_new(alloc);
    (void)str_from_cstr(&region.name, editor_state->atlas_region_pending_name);
    region.texture = str_new(alloc);
    (void)str_from_cstr(&region.texture, state->assets.textures.data[texture_index].filename);
    if (vec_atlas_region_push(regions, region)) {
        editor_state->atlas_region_index = regions->count - 1;
        undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                               strv_from_cstr("Create atlas region"));
    }
    editor_state->sub_mode = EDITOR_SUB_ATLAS_BROWSE;
}

/* Dual-stick drag, same shape as handle_handle_input (editor/core.c):
 * left stick moves the offset, right stick grows/shrinks the size, both at
 * EDITOR_HANDLE_SPEED px/s, clamped every frame to stay inside the
 * texture and at least 1px. CONFIRM commits, CANCEL discards the staging
 * rect untouched (gamedata.atlas_regions was never written to). */
void handle_atlas_region_edit_input(
    GameState *state, EditorState *editor_state, UndoHistory *undo_history, InputState input, float delta_time)
{
    if (input_pressed(&input, &state->bindings, ACTION_CONFIRM)) {
        commit_atlas_region(state, editor_state, undo_history);
        return;
    }
    if (input_pressed(&input, &state->bindings, ACTION_CANCEL)) {
        editor_state->sub_mode = EDITOR_SUB_ATLAS_BROWSE;
        return;
    }
    Vector2 offset_delta = input_axis_pair(&input, &state->bindings, AXIS_PRIMARY_X, AXIS_PRIMARY_Y);
    Vector2 size_delta = input_axis_pair(&input, &state->bindings, AXIS_SECONDARY_X, AXIS_SECONDARY_Y);
    editor_state->atlas_edit_src.x += offset_delta.x * EDITOR_HANDLE_SPEED * delta_time;
    editor_state->atlas_edit_src.y += offset_delta.y * EDITOR_HANDLE_SPEED * delta_time;
    editor_state->atlas_edit_src.width += size_delta.x * EDITOR_HANDLE_SPEED * delta_time;
    editor_state->atlas_edit_src.height += size_delta.y * EDITOR_HANDLE_SPEED * delta_time;
    clamp_atlas_edit_src(&editor_state->atlas_edit_src, atlas_texture_size(state, editor_state->atlas_texture_index));
}

/* --- ATLAS_BROWSE: texture picker (atlas_texture_index < 0) --- */

static void
handle_atlas_texture_list_input(GameState *state, Camera2D *camera, EditorState *editor_state, const InputState *input)
{
    if (input_pressed(input, &state->bindings, ACTION_CANCEL)) {
        editor_state->top_mode = EDITOR_TOP_SCENE;
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    int count = state->assets.textures.count;
    if (input_pressed(input, &state->bindings, ACTION_NAV_DOWN) && editor_state->atlas_texture_scroll < count - 1) {
        editor_state->atlas_texture_scroll++;
    }
    if (input_pressed(input, &state->bindings, ACTION_NAV_UP) && editor_state->atlas_texture_scroll > 0) {
        editor_state->atlas_texture_scroll--;
    }
    if (input_pressed(input, &state->bindings, ACTION_CONFIRM) && count > 0) {
        editor_state->atlas_texture_index = editor_state->atlas_texture_scroll;
        editor_state->atlas_region_scroll = 0;
        Vector2 texture_size = atlas_texture_size(state, editor_state->atlas_texture_index);
        camera->target = (Vector2){ATLAS_VIEW_ANCHOR_X + (texture_size.x * EDITOR_ATLAS_ZOOM / 2.0F),
                                   ATLAS_VIEW_ANCHOR_Y + (texture_size.y * EDITOR_ATLAS_ZOOM / 2.0F)};
    }
}

/* --- ATLAS_BROWSE: region picker for the chosen texture --- */

static void enter_atlas_word_builder_empty(EditorState *editor_state)
{
    editor_state->word_builder_len = 0;
    editor_state->word_builder_buf[0] = '\0';
    editor_state->word_builder_scroll = 0;
    editor_state->sub_mode = EDITOR_SUB_WORD_BUILDER;
}

static void handle_atlas_region_list_input(GameState *state, EditorState *editor_state, const InputState *input)
{
    if (input_pressed(input, &state->bindings, ACTION_CANCEL)) {
        editor_state->atlas_texture_index = -1;
        editor_state->atlas_region_scroll = 0;
        return;
    }
    const char *texture_name = state->assets.textures.data[editor_state->atlas_texture_index].filename;
    int region_count = atlas_region_count_for_texture(&state->gamedata.atlas_regions, texture_name);
    int total = region_count + 1; /* +1 for "+ NEW REGION" */
    if (input_pressed(input, &state->bindings, ACTION_NAV_DOWN) && editor_state->atlas_region_scroll < total - 1) {
        editor_state->atlas_region_scroll++;
    }
    if (input_pressed(input, &state->bindings, ACTION_NAV_UP) && editor_state->atlas_region_scroll > 0) {
        editor_state->atlas_region_scroll--;
    }
    if (!input_pressed(input, &state->bindings, ACTION_CONFIRM)) {
        return;
    }
    if (editor_state->atlas_region_scroll == region_count) {
        editor_state->creating_atlas_region = true;
        enter_atlas_word_builder_empty(editor_state);
        return;
    }
    int absolute =
        atlas_region_index_at(&state->gamedata.atlas_regions, texture_name, editor_state->atlas_region_scroll);
    if (absolute < 0) {
        return;
    }
    editor_state->atlas_region_index = absolute;
    editor_state->atlas_edit_src = state->gamedata.atlas_regions.data[absolute].src;
    editor_state->sub_mode = EDITOR_SUB_ATLAS_REGION_EDIT;
}

void handle_atlas_browse_input(GameState *state, Camera2D *camera, EditorState *editor_state, const InputState *input)
{
    if (editor_state->atlas_texture_index < 0) {
        handle_atlas_texture_list_input(state, camera, editor_state, input);
    } else {
        handle_atlas_region_list_input(state, editor_state, input);
    }
}

/* --- Draw: side panels --- */

void draw_atlas_texture_list_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state)
{
    int count = state->assets.textures.count;
    int panel_x = screen.width - EDITOR_PANEL_WIDTH;
    DrawRectangle(panel_x, 0, EDITOR_PANEL_WIDTH, screen.height, debug_bg_color);
    Font font = state->assets.ui_font;
    int y_offset = 0;
    draw_ui_text(font, "[ Atlas: Pick Texture ]", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE,
                 debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT * 2;

    if (count == 0) {
        draw_ui_text(font, "(no textures embedded)", panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE,
                     debug_text_color);
        return;
    }

    int visible = place_visible_count(screen.height);
    int scroll = editor_state->atlas_texture_scroll - (visible / 2);
    if (scroll < 0) {
        scroll = 0;
    }
    int max_scroll = count - visible;
    if (max_scroll < 0) {
        max_scroll = 0;
    }
    if (scroll > max_scroll) {
        scroll = max_scroll;
    }
    int end = scroll + visible;
    if (end > count) {
        end = count;
    }
    for (int index = scroll; index < end; index++) {
        bool selected = (index == editor_state->atlas_texture_scroll);
        Color color = selected ? WHITE : debug_text_color;
        draw_ui_text(font, TextFormat("%s %s", selected ? ">" : " ", state->assets.textures.data[index].filename),
                     panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, color);
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
    }
}

void draw_atlas_region_list_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state)
{
    int texture_index = editor_state->atlas_texture_index;
    if (texture_index < 0 || texture_index >= state->assets.textures.count) {
        return;
    }
    const char *texture_name = state->assets.textures.data[texture_index].filename;
    const vec_atlas_region *regions = &state->gamedata.atlas_regions;
    int region_count = atlas_region_count_for_texture(regions, texture_name);
    int total = region_count + 1;

    int panel_x = screen.width - EDITOR_PANEL_WIDTH;
    DrawRectangle(panel_x, 0, EDITOR_PANEL_WIDTH, screen.height, debug_bg_color);
    Font font = state->assets.ui_font;
    int y_offset = 0;
    draw_ui_text(font, TextFormat("[ Atlas: %s ]", texture_name), panel_x + DEBUG_MARGIN, y_offset,
                 EDITOR_PANEL_FONT_SIZE, debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT * 2;

    int visible = place_visible_count(screen.height);
    int scroll = editor_state->atlas_region_scroll - (visible / 2);
    if (scroll < 0) {
        scroll = 0;
    }
    int max_scroll = total - visible;
    if (max_scroll < 0) {
        max_scroll = 0;
    }
    if (scroll > max_scroll) {
        scroll = max_scroll;
    }
    int end = scroll + visible;
    if (end > total) {
        end = total;
    }
    for (int display_row = scroll; display_row < end; display_row++) {
        bool selected = (display_row == editor_state->atlas_region_scroll);
        if (display_row == region_count) {
            draw_ui_text(font, TextFormat("%s + NEW REGION", selected ? ">" : " "), panel_x + DEBUG_MARGIN, y_offset,
                         EDITOR_PANEL_FONT_SIZE, selected ? WHITE : atlas_new_region_color);
        } else {
            int absolute = atlas_region_index_at(regions, texture_name, display_row);
            const AtlasRegion *region = &regions->data[absolute];
            draw_ui_text(font,
                         TextFormat("%s %s  (%.0f,%.0f %.0fx%.0f)", selected ? ">" : " ", region->name.ptr,
                                    region->src.x, region->src.y, region->src.width, region->src.height),
                         panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, selected ? WHITE : debug_text_color);
        }
        y_offset += EDITOR_PANEL_LINE_HEIGHT;
    }
}

void draw_atlas_region_edit_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state)
{
    int panel_x = screen.width - EDITOR_PANEL_WIDTH;
    DrawRectangle(panel_x, 0, EDITOR_PANEL_WIDTH, screen.height, debug_bg_color);
    Font font = state->assets.ui_font;
    int y_offset = 0;
    bool is_existing =
        editor_state->atlas_region_index >= 0 && editor_state->atlas_region_index < state->gamedata.atlas_regions.count;
    const char *name = is_existing ? state->gamedata.atlas_regions.data[editor_state->atlas_region_index].name.ptr
                                   : editor_state->atlas_region_pending_name;
    draw_ui_text(font, TextFormat("[ Region: %s ]", name), panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE,
                 debug_text_color);
    y_offset += EDITOR_PANEL_LINE_HEIGHT;
    Rectangle src = editor_state->atlas_edit_src;
    draw_ui_text(font, TextFormat("src: %.0f, %.0f  %.0fx%.0f", src.x, src.y, src.width, src.height),
                 panel_x + DEBUG_MARGIN, y_offset, EDITOR_PANEL_FONT_SIZE, debug_text_color);
}

/* --- Draw: world-space zoomed texture view (drawn inside BeginMode2D) --- */

void draw_atlas_texture_view(const GameState *state, const EditorState *editor_state)
{
    if (editor_state->top_mode != EDITOR_TOP_ATLAS) {
        return;
    }
    int texture_index = editor_state->atlas_texture_index;
    if (texture_index < 0 || texture_index >= state->assets.textures.count) {
        return;
    }
    const TextureEntry *entry = &state->assets.textures.data[texture_index];
    Rectangle source = {0, 0, (float)entry->texture.width, (float)entry->texture.height};
    Rectangle dest = {ATLAS_VIEW_ANCHOR_X, ATLAS_VIEW_ANCHOR_Y, (float)entry->texture.width * EDITOR_ATLAS_ZOOM,
                      (float)entry->texture.height * EDITOR_ATLAS_ZOOM};
    DrawTexturePro(entry->texture, source, dest, (Vector2){0, 0}, 0.0F, WHITE);

    const vec_atlas_region *regions = &state->gamedata.atlas_regions;
    for (int index = 0; index < regions->count; index++) {
        const AtlasRegion *region = &regions->data[index];
        if (strcmp(region->texture.ptr, entry->filename) != 0) {
            continue;
        }
        if (editor_state->sub_mode == EDITOR_SUB_ATLAS_REGION_EDIT && editor_state->atlas_region_index == index) {
            continue; /* drawn highlighted below instead */
        }
        Rectangle outline = {ATLAS_VIEW_ANCHOR_X + (region->src.x * EDITOR_ATLAS_ZOOM),
                             ATLAS_VIEW_ANCHOR_Y + (region->src.y * EDITOR_ATLAS_ZOOM),
                             region->src.width * EDITOR_ATLAS_ZOOM, region->src.height * EDITOR_ATLAS_ZOOM};
        DrawRectangleLinesEx(outline, 1.0F, atlas_handle_color);
    }

    if (editor_state->sub_mode != EDITOR_SUB_ATLAS_REGION_EDIT) {
        return;
    }
    Rectangle src = editor_state->atlas_edit_src;
    Rectangle outline = {ATLAS_VIEW_ANCHOR_X + (src.x * EDITOR_ATLAS_ZOOM),
                         ATLAS_VIEW_ANCHOR_Y + (src.y * EDITOR_ATLAS_ZOOM), src.width * EDITOR_ATLAS_ZOOM,
                         src.height * EDITOR_ATLAS_ZOOM};
    DrawRectangleLinesEx(outline, 2.0F, WHITE);
    int half = EDITOR_HANDLE_SIZE / 2;
    DrawRectangle((int)outline.x - half, (int)outline.y - half, EDITOR_HANDLE_SIZE, EDITOR_HANDLE_SIZE,
                  atlas_handle_color);
    DrawRectangle((int)(outline.x + outline.width) - half, (int)outline.y - half, EDITOR_HANDLE_SIZE,
                  EDITOR_HANDLE_SIZE, atlas_handle_color);
    DrawRectangle((int)outline.x - half, (int)(outline.y + outline.height) - half, EDITOR_HANDLE_SIZE,
                  EDITOR_HANDLE_SIZE, atlas_handle_color);
    DrawRectangle((int)(outline.x + outline.width) - half, (int)(outline.y + outline.height) - half, EDITOR_HANDLE_SIZE,
                  EDITOR_HANDLE_SIZE, atlas_handle_color);
}

/* --- Hint tables --- */

static const EditorActionHint atlas_browse_hints[] = {
    {ACTION_CANCEL, "Back"},
    {ACTION_NAV_UP, "Prev"},
    {ACTION_NAV_DOWN, "Next"},
    {ACTION_CONFIRM, "Select / New"},
};

static const EditorHintTable atlas_browse_table = {
    .hints = atlas_browse_hints,
    .count = (int)(sizeof(atlas_browse_hints) / sizeof(atlas_browse_hints[0])),
    .mode_label = "Atlas",
};

const EditorHintTable *atlas_browse_hints_table(void)
{
    return &atlas_browse_table;
}

static const EditorActionHint atlas_region_edit_hints[] = {
    {ACTION_CONFIRM, "Confirm region"},
    {ACTION_CANCEL, "Cancel"},
};

static const EditorHintTable atlas_region_edit_table = {
    .hints = atlas_region_edit_hints,
    .count = (int)(sizeof(atlas_region_edit_hints) / sizeof(atlas_region_edit_hints[0])),
    .mode_label = "Atlas region  (L-stick: offset, R-stick: size)",
};

const EditorHintTable *atlas_region_edit_hints_table(void)
{
    return &atlas_region_edit_table;
}
