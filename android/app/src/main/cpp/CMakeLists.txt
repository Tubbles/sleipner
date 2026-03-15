cmake_minimum_required(VERSION 3.22.1)
project(sleipner C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(raylib REQUIRED)

# Project root (5 levels up from this CMakeLists.txt)
set(PROJECT_ROOT "${CMAKE_SOURCE_DIR}/../../../../..")

# native_app_glue from NDK
set(NATIVE_APP_GLUE_DIR "${ANDROID_NDK}/sources/android/native_app_glue")

# Build everything into a single shared library
add_library(sleipner SHARED
    ${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c
    ${PROJECT_ROOT}/vendor/zlog/zlog_android.c
    ${PROJECT_ROOT}/engine/src/audio.c
    ${PROJECT_ROOT}/engine/src/collision.c
    ${PROJECT_ROOT}/engine/src/input.c
    ${PROJECT_ROOT}/engine/src/particle.c
    ${PROJECT_ROOT}/engine/src/render.c
    ${PROJECT_ROOT}/engine/src/shape.c
    ${PROJECT_ROOT}/src/main.c
)

target_include_directories(sleipner PRIVATE
    ${NATIVE_APP_GLUE_DIR}
    ${PROJECT_ROOT}/vendor/zlog
    ${PROJECT_ROOT}/vendor/unity
    ${PROJECT_ROOT}/engine/src
    ${PROJECT_ROOT}/src
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
