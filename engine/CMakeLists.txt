file(GLOB ZLOG_SOURCES vendor/zlog/*.c)
add_library(zlog STATIC ${ZLOG_SOURCES})
target_include_directories(zlog PUBLIC vendor/zlog)
target_link_libraries(zlog PRIVATE pthread)

add_library(unity STATIC vendor/unity/unity.c)
target_include_directories(unity PUBLIC vendor/unity)

add_library(tomlc99 STATIC vendor/tomlc99/toml.c)
target_include_directories(tomlc99 PUBLIC vendor/tomlc99)

add_library(engine STATIC
    src/arena.c
    src/audio.c
    src/blueprint.c
    src/collision.c
    src/level.c
    src/input.c
    src/particle.c
    src/render.c
    src/shape.c
)
target_include_directories(engine PUBLIC src)
target_link_libraries(engine PUBLIC raylib zlog tomlc99)

add_executable(sleipner src/main.c)
target_link_libraries(sleipner PRIVATE engine)

add_subdirectory(test)
