# engine/sources.cmake
# Single source of truth: engine source files and embedded assets.
# Include from any CMakeLists.txt that needs to build the engine.
#
# Provides:
#   ENGINE_SOURCE_FILES              — .c basenames under engine/src/ (no path prefix)
#   embed_asset(target symbol path)  — embed one binary asset via .incbin
#   embed_all_assets(target root)    — embed all standard assets; root is absolute project root

if(WIN32)
    set(ARENA_SOURCE arena_win32.c)
else()
    set(ARENA_SOURCE arena_posix.c)
endif()

set(ENGINE_SOURCE_FILES
    ${ARENA_SOURCE}
    attribute.c
    audio.c
    blueprint.c
    collision.c
    debug.c
    editor.c
    entity.c
    error.c
    game.c
    input.c
    level.c
    map.c
    particle.c
    render.c
    rule.c
    shape.c
    str.c
    strv.c
    toml_emitter.c
    toml_str.c
    vec.c
)

# Embed a binary asset into a target via .incbin assembly directive.
# abs_asset must be an absolute path to the asset file.
# Produces linker symbols: embed_<symbol>_start, embed_<symbol>_end
function(embed_asset target symbol_name abs_asset)
    set(asm_file "${CMAKE_CURRENT_BINARY_DIR}/embed_${symbol_name}.S")

    if(WIN32)
        set(section_dir "    .section .rdata\n")
    else()
        set(section_dir "    .section .rodata\n")
    endif()

    file(WRITE "${asm_file}"
        "${section_dir}"
        "    .global embed_${symbol_name}_start\n"
        "    .global embed_${symbol_name}_end\n"
        "    .balign 16\n"
        "embed_${symbol_name}_start:\n"
        "    .incbin \"${abs_asset}\"\n"
        "embed_${symbol_name}_end:\n"
    )

    set_source_files_properties("${asm_file}" PROPERTIES OBJECT_DEPENDS "${abs_asset}")
    target_sources(${target} PRIVATE "${asm_file}")
endfunction()

# Embed all standard game assets into target.
# root is the absolute path to the project root (where assets/ lives).
macro(embed_all_assets target root)
    # Sprites
    embed_asset(${target} player_png           "${root}/assets/sprites/player.png")
    embed_asset(${target} grass_png            "${root}/assets/sprites/grass.png")
    embed_asset(${target} tree_png             "${root}/assets/sprites/tree.png")
    embed_asset(${target} chest_png            "${root}/assets/sprites/chest.png")
    embed_asset(${target} house_png            "${root}/assets/sprites/house.png")
    embed_asset(${target} fence_png            "${root}/assets/sprites/fence.png")
    embed_asset(${target} floor_png            "${root}/assets/sprites/floor.png")
    # Music
    embed_asset(${target} bgm_mp3              "${root}/assets/music/bgm.mp3")
    # Fonts
    embed_asset(${target} earth_illusion_ttf   "${root}/assets/ttf/Role Playing Fonts/Role Playing Fonts/Earth Illusion/earth-illusion.ttf")
    embed_asset(${target} golden_apple_ttf     "${root}/assets/ttf/Role Playing Fonts/Role Playing Fonts/Golden Apple/golden-apple.ttf")
    embed_asset(${target} menucard_ttf         "${root}/assets/ttf/RolePlayingFonts3_SysL/menu_card/MenuCard.ttf")
    embed_asset(${target} nudge_orb_ttf        "${root}/assets/ttf/RolePlayingFonts3_SysL/nudge_orb/Nudge Orb.ttf")
    embed_asset(${target} cardboardcrown_ttf   "${root}/assets/ttf/RolePlayingFontsII-SysL/CardboardCrown/CardboardCrown.ttf")
    embed_asset(${target} royalfibre_ttf       "${root}/assets/ttf/RolePlayingFontsII-SysL/RoyalFibre/RoyalFibre.ttf")
    # Input mappings
    embed_asset(${target} gamecontrollerdb_txt "${root}/assets/gamecontrollerdb.txt")
endmacro()
