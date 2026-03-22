add_library(unity STATIC vendor/unity/unity.c)
target_include_directories(unity PUBLIC vendor/unity)

add_library(tomlc99 STATIC vendor/tomlc99/toml.c)
target_include_directories(tomlc99 PUBLIC vendor/tomlc99)

add_library(engine STATIC
    src/arena.c
    src/attribute.c
    src/audio.c
    src/blueprint.c
    src/collision.c
    src/debug.c
    src/entity.c
    src/error.c
    src/game.c
    src/level.c
    src/input.c
    src/particle.c
    src/render.c
    src/rule.c
    src/shape.c
    src/str.c
    src/strv.c
    src/toml_emitter.c
    src/vec.c
)
target_include_directories(engine PUBLIC src)
target_compile_definitions(engine PRIVATE _POSIX_C_SOURCE=199309L)
target_link_libraries(engine PUBLIC raylib tomlc99)

# Embed a binary asset into the executable via .incbin assembly directive.
# Produces symbols: embed_<symbol>_start, embed_<symbol>_end
function(embed_asset target symbol_name asset_path)
    set(asm_file "${CMAKE_CURRENT_BINARY_DIR}/embed_${symbol_name}.S")
    set(abs_asset "${CMAKE_SOURCE_DIR}/${asset_path}")

    file(WRITE "${asm_file}"
        "    .section .rodata\n"
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

add_executable(sleipner src/main.c)
target_link_libraries(sleipner PRIVATE engine)

# Embedded assets — sprites
embed_asset(sleipner player_png "assets/sprites/player.png")
embed_asset(sleipner grass_png "assets/sprites/grass.png")
embed_asset(sleipner tree_png "assets/sprites/tree.png")
embed_asset(sleipner chest_png "assets/sprites/chest.png")
embed_asset(sleipner house_png "assets/sprites/house.png")
embed_asset(sleipner fence_png "assets/sprites/fence.png")

# Embedded assets — music
embed_asset(sleipner bgm_mp3 "assets/music/bgm.mp3")

# Embedded assets — fonts
embed_asset(sleipner earth_illusion_ttf "assets/ttf/Role Playing Fonts/Role Playing Fonts/Earth Illusion/earth-illusion.ttf")
embed_asset(sleipner golden_apple_ttf "assets/ttf/Role Playing Fonts/Role Playing Fonts/Golden Apple/golden-apple.ttf")
embed_asset(sleipner menucard_ttf "assets/ttf/RolePlayingFonts3_SysL/menu_card/MenuCard.ttf")
embed_asset(sleipner nudge_orb_ttf "assets/ttf/RolePlayingFonts3_SysL/nudge_orb/Nudge Orb.ttf")
embed_asset(sleipner cardboardcrown_ttf "assets/ttf/RolePlayingFontsII-SysL/CardboardCrown/CardboardCrown.ttf")
embed_asset(sleipner royalfibre_ttf "assets/ttf/RolePlayingFontsII-SysL/RoyalFibre/RoyalFibre.ttf")

# Embedded assets — input mappings
embed_asset(sleipner gamecontrollerdb_txt "assets/gamecontrollerdb.txt")

add_subdirectory(test)
