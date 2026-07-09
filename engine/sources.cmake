# engine/sources.cmake
# Single source of truth: engine source files and embedded assets.
# Include from any CMakeLists.txt that needs to build the engine.
#
# Provides:
#   ENGINE_SOURCE_FILES        — .c paths relative to engine/src/ (no path prefix)
#   embed_asset(target symbol path [NULL_TERMINATE])
#                              — embed one binary asset via .incbin into target
#   embed_all_assets(root)     — define the sleipner_assets OBJECT library and
#                                 embed every standard asset into it; root is
#                                 the absolute project root. Callers must link
#                                 sleipner_assets into any target that uses
#                                 ASSET() (engine/src/assets.h) — see
#                                 engine/CMakeLists.txt, engine/test/CMakeLists.txt,
#                                 and android/app/src/main/cpp/CMakeLists.txt.

if(WIN32)
    set(ARENA_SOURCE arena_win32.c)
else()
    set(ARENA_SOURCE arena_posix.c)
endif()

# D40: file-first / embedded-fallback gamedata.toml loading. OFF by
# default -- a bare/dev `cmake` build embeds nothing and always reads the
# live data/gamedata.toml file. Shipping configurations flip this ON: the
# linux/windows presets (CMakePresets.json) and the Android build
# (android/app/src/main/cpp/CMakeLists.txt) set it via a forced cache
# variable so a distributed binary still boots with no external gamedata
# present. See gamedata_source.h/.c for the runtime load order.
option(SLEIPNER_EMBED_GAMEDATA "Embed data/gamedata.toml as a load-time fallback" OFF)

set(ENGINE_SOURCE_FILES
    ${ARENA_SOURCE}
    atlas.c
    attribute.c
    audio.c
    blueprint.c
    blur.c
    collision.c
    debug.c
    depth_sort.c
    editor/anim.c
    editor/atlas.c
    editor/attr.c
    editor/blueprint.c
    editor/child.c
    editor/core.c
    editor/draw.c
    editor/level.c
    editor/rule.c
    editor/tile.c
    editor/widgets.c
    effect.c
    entity.c
    error.c
    file_backup.c
    font_cache.c
    frame.c
    game.c
    gamedata_source.c
    hud.c
    input.c
    input_func.c
    input_func_toml.c
    inventory_screen.c
    keyboard_widget.c
    level.c
    map.c
    menu.c
    net.c
    net_loopback.c
    net_protocol.c
    net_udp.c
    particle.c
    platform_paths.c
    preferences.c
    progression.c
    random.c
    render.c
    rule.c
    save.c
    save_screen.c
    settings.c
    shape.c
    str.c
    strv.c
    tileset.c
    toml_emitter.c
    toml_str.c
    undo.c
    vec.c
)

# Embed a binary asset into a target via .incbin assembly directive.
# abs_asset must be an absolute path to the asset file.
# Produces linker symbols: embed_<symbol>_start, embed_<symbol>_end
# Pass NULL_TERMINATE for text assets consumed as a C string (e.g. GLSL
# shader source handed to raylib's LoadShaderFromMemory, which scans for
# a null terminator rather than taking an explicit length) — it appends a
# trailing zero byte inside the embedded range.
function(embed_asset target symbol_name abs_asset)
    cmake_parse_arguments(EMBED_ASSET "NULL_TERMINATE" "" "" ${ARGN})

    set(asm_file "${CMAKE_CURRENT_BINARY_DIR}/embed_${symbol_name}.S")

    if(WIN32)
        set(section_dir "    .section .rdata\n")
    else()
        set(section_dir "    .section .rodata\n")
    endif()

    set(terminator_dir "")
    if(EMBED_ASSET_NULL_TERMINATE)
        set(terminator_dir "    .byte 0\n")
    endif()

    file(WRITE "${asm_file}"
        "${section_dir}"
        "    .global embed_${symbol_name}_start\n"
        "    .global embed_${symbol_name}_end\n"
        "    .balign 16\n"
        "embed_${symbol_name}_start:\n"
        "    .incbin \"${abs_asset}\"\n"
        "${terminator_dir}"
        "embed_${symbol_name}_end:\n"
    )

    set_source_files_properties("${asm_file}" PROPERTIES OBJECT_DEPENDS "${abs_asset}")
    target_sources(${target} PRIVATE "${asm_file}")
endfunction()

# Define the sleipner_assets OBJECT library and embed every standard game
# asset into it. root is the absolute path to the project root (where
# assets/ lives). Callers link sleipner_assets into whichever target(s)
# need ASSET() — see the header comment above.
function(embed_all_assets root)
    add_library(sleipner_assets OBJECT)

    # Sprites
    embed_asset(sleipner_assets player_png           "${root}/assets/sprites/player.png")
    embed_asset(sleipner_assets grass_png            "${root}/assets/sprites/grass.png")
    embed_asset(sleipner_assets tree_png             "${root}/assets/sprites/tree.png")
    embed_asset(sleipner_assets chest_png            "${root}/assets/sprites/chest.png")
    embed_asset(sleipner_assets house_png            "${root}/assets/sprites/house.png")
    embed_asset(sleipner_assets fence_png            "${root}/assets/sprites/fence.png")
    embed_asset(sleipner_assets floor_png            "${root}/assets/sprites/floor.png")
    # Music
    embed_asset(sleipner_assets bgm_mp3              "${root}/assets/music/bgm.mp3")
    # SFX (placeholders pending real sound design, see U1)
    embed_asset(sleipner_assets pickup_wav           "${root}/assets/sfx/pickup.wav")
    embed_asset(sleipner_assets hit_wav              "${root}/assets/sfx/hit.wav")
    # Fonts
    embed_asset(sleipner_assets earth_illusion_ttf   "${root}/assets/ttf/Role Playing Fonts/Role Playing Fonts/Earth Illusion/earth-illusion.ttf")
    embed_asset(sleipner_assets golden_apple_ttf     "${root}/assets/ttf/Role Playing Fonts/Role Playing Fonts/Golden Apple/golden-apple.ttf")
    embed_asset(sleipner_assets menucard_ttf         "${root}/assets/ttf/RolePlayingFonts3_SysL/menu_card/MenuCard.ttf")
    embed_asset(sleipner_assets nudge_orb_ttf        "${root}/assets/ttf/RolePlayingFonts3_SysL/nudge_orb/Nudge Orb.ttf")
    embed_asset(sleipner_assets cardboardcrown_ttf   "${root}/assets/ttf/RolePlayingFontsII-SysL/CardboardCrown/CardboardCrown.ttf")
    embed_asset(sleipner_assets royalfibre_ttf       "${root}/assets/ttf/RolePlayingFontsII-SysL/RoyalFibre/RoyalFibre.ttf")
    # Shaders
    embed_asset(sleipner_assets blur_fs              "${root}/assets/shaders/blur.fs" NULL_TERMINATE)
    # Input mappings
    embed_asset(sleipner_assets gamecontrollerdb_txt "${root}/assets/gamecontrollerdb.txt")

    # Gamedata fallback (D40) -- only embedded when SLEIPNER_EMBED_GAMEDATA
    # is ON. PUBLIC so every target that links sleipner_assets (sleipner,
    # engine_tests, the Android sleipner .so) sees the #define and can
    # gate ASSET(gamedata_toml) behind #if defined(SLEIPNER_EMBED_GAMEDATA).
    if(SLEIPNER_EMBED_GAMEDATA)
        embed_asset(sleipner_assets gamedata_toml "${root}/data/gamedata.toml" NULL_TERMINATE)
        target_compile_definitions(sleipner_assets PUBLIC SLEIPNER_EMBED_GAMEDATA)
    endif()
endfunction()
