cmake_minimum_required(VERSION 3.22.1)
project(sleipner C ASM)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Project root (5 levels up from this CMakeLists.txt)
set(PROJECT_ROOT "${CMAKE_SOURCE_DIR}/../../../../..")

# Find Conan-generated raylib from ci.sh output
set(CONAN_GENERATORS "${PROJECT_ROOT}/build/android/arm64-v8a/build/Release/generators")
list(PREPEND CMAKE_PREFIX_PATH "${CONAN_GENERATORS}")
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

find_package(raylib REQUIRED)

# native_app_glue from NDK
set(NATIVE_APP_GLUE_DIR "${ANDROID_NDK}/sources/android/native_app_glue")

# Build everything into a single shared library
add_library(sleipner SHARED
    ${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c
    ${PROJECT_ROOT}/engine/vendor/tomlc99/toml.c
    ${PROJECT_ROOT}/engine/src/arena.c
    ${PROJECT_ROOT}/engine/src/attribute.c
    ${PROJECT_ROOT}/engine/src/audio.c
    ${PROJECT_ROOT}/engine/src/blueprint.c
    ${PROJECT_ROOT}/engine/src/collision.c
    ${PROJECT_ROOT}/engine/src/entity.c
    ${PROJECT_ROOT}/engine/src/game.c
    ${PROJECT_ROOT}/engine/src/input.c
    ${PROJECT_ROOT}/engine/src/level.c
    ${PROJECT_ROOT}/engine/src/particle.c
    ${PROJECT_ROOT}/engine/src/render.c
    ${PROJECT_ROOT}/engine/src/rule.c
    ${PROJECT_ROOT}/engine/src/shape.c
    ${PROJECT_ROOT}/engine/src/debug.c
    ${PROJECT_ROOT}/engine/src/error.c
    ${PROJECT_ROOT}/engine/src/toml_emitter.c
    ${PROJECT_ROOT}/engine/src/main.c
)

target_include_directories(sleipner PRIVATE
    ${NATIVE_APP_GLUE_DIR}
    ${PROJECT_ROOT}/engine/vendor/unity
    ${PROJECT_ROOT}/engine/vendor/tomlc99
    ${PROJECT_ROOT}/engine/src
)

target_link_libraries(sleipner PRIVATE
    raylib
    android
    log
    EGL
    GLESv2
    OpenSLES
    m
)

# Embed a binary asset into the library via .incbin assembly directive.
# Produces symbols: embed_<symbol>_start, embed_<symbol>_end
function(embed_asset target symbol_name asset_path)
    set(asm_file "${CMAKE_CURRENT_BINARY_DIR}/embed_${symbol_name}.S")
    set(abs_asset "${PROJECT_ROOT}/${asset_path}")

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
