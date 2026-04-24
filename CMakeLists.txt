cmake_minimum_required(VERSION 3.20)
project(sleipner C ASM)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT WIN32)
    add_compile_options(-fsanitize=address,undefined,leak -fno-omit-frame-pointer -fno-optimize-sibling-calls)
    add_link_options(-fsanitize=address,undefined,leak)
endif()

if(NOT WIN32 AND NOT APPLE AND NOT CMAKE_CROSSCOMPILING)
    # Force-include glibc_compat.h in every C translation unit (engine
    # and vendored raylib/glfw alike) so .symver directives bind newer
    # math symbols back to their GLIBC_2.2.5 ABI. Keeps the final binary
    # runnable on glibc 2.36 (Debian bookworm). Scoped to C via the
    # generator expression so the embedded-asset .S files still
    # assemble cleanly.
    add_compile_options($<$<COMPILE_LANGUAGE:C>:-include${CMAKE_SOURCE_DIR}/engine/src/glibc_compat.h>)
endif()

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CUSTOMIZE_BUILD OFF CACHE BOOL "" FORCE)
set(USE_EXTERNAL_GLFW OFF CACHE BOOL "" FORCE)
if(WIN32)
    set(PLATFORM "SDL" CACHE STRING "" FORCE)
    set(OPENGL_VERSION "2.1" CACHE STRING "" FORCE)
else()
    set(PLATFORM "Desktop" CACHE STRING "" FORCE)
endif()
if(NOT WIN32)
    set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
endif()
add_subdirectory(engine/vendor/raylib)
get_target_property(_raylib_inc raylib INTERFACE_INCLUDE_DIRECTORIES)
if(_raylib_inc)
    set_target_properties(raylib PROPERTIES
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_raylib_inc}")
endif()

if(NOT CMAKE_CROSSCOMPILING)
    enable_testing()
endif()
add_subdirectory(engine)
