cmake_minimum_required(VERSION 3.20)
project(sleipner C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(raylib REQUIRED)

file(GLOB ZLOG_SOURCES vendor/zlog/*.c)
add_library(zlog STATIC ${ZLOG_SOURCES})
target_include_directories(zlog PUBLIC vendor/zlog)
target_link_libraries(zlog PRIVATE pthread)

add_library(unity STATIC vendor/unity/unity.c)
target_include_directories(unity PUBLIC vendor/unity)

add_subdirectory(engine)

add_executable(sleipner src/main.c)
target_link_libraries(sleipner PRIVATE engine zlog)
