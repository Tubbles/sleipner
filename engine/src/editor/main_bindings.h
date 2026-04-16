#pragma once

#include "editor/keybindings.h"

/* Binding tables for the top-level game loop — place submode and play-mode
 * global toggles. Kept in libengine (not main.c) so editor/draw.c's hints
 * bar can link against the accessors (declared in keybindings.h). */

enum {
    PLACE_ACT_CONFIRM,
    PLACE_ACT_CANCEL,
    PLACE_ACT_UP,
    PLACE_ACT_DOWN,
    PLACE_ACT_PAGE_UP,
    PLACE_ACT_PAGE_DOWN,
    PLACE_ACT_COUNT
};

enum { PLAY_ACT_DEBUG, PLAY_ACT_FONT_PREVIEW, PLAY_ACT_ENTER_EDITOR, PLAY_ACT_SAVE, PLAY_ACT_RELOAD, PLAY_ACT_COUNT };

extern const EditorBinding place_actions[PLACE_ACT_COUNT];
extern const EditorBinding play_mode_actions[PLAY_ACT_COUNT];
