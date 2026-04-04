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

find_package(raylib REQUIRED)

if(NOT CMAKE_CROSSCOMPILING)
    enable_testing()
endif()
add_subdirectory(engine)
