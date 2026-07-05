cmake_minimum_required(VERSION 3.22.1)
project(sleipner C ASM)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Project root (5 levels up from this CMakeLists.txt)
set(PROJECT_ROOT "${CMAKE_SOURCE_DIR}/../../../../..")

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(USE_EXTERNAL_GLFW OFF CACHE BOOL "" FORCE)
set(PLATFORM "Android" CACHE STRING "" FORCE)
# D40: shipped Android builds embed data/gamedata.toml as a load-time
# fallback (see engine/sources.cmake), so a fresh install boots even
# before the Syncthing-synced file exists on the device.
set(SLEIPNER_EMBED_GAMEDATA ON CACHE BOOL "" FORCE)
add_subdirectory(${PROJECT_ROOT}/engine/vendor/raylib ${CMAKE_BINARY_DIR}/raylib)

include(${PROJECT_ROOT}/engine/sources.cmake)

# native_app_glue from NDK
set(NATIVE_APP_GLUE_DIR "${ANDROID_NDK}/sources/android/native_app_glue")

foreach(src ${ENGINE_SOURCE_FILES})
    list(APPEND engine_srcs ${PROJECT_ROOT}/engine/src/${src})
endforeach()

# Build everything into a single shared library
add_library(sleipner SHARED
    ${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c
    ${PROJECT_ROOT}/engine/vendor/tomlc99/toml.c
    ${engine_srcs}
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

embed_all_assets(${PROJECT_ROOT})
target_link_libraries(sleipner PRIVATE sleipner_assets)

include(${PROJECT_ROOT}/cmake/GitVersion.cmake)
target_compile_definitions(sleipner PRIVATE SLEIPNER_VERSION="${SLEIPNER_GIT_VERSION}")
