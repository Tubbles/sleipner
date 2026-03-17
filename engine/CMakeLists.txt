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
    src/shape.c
    src/toml_emitter.c
)
target_include_directories(engine PUBLIC src)
target_compile_definitions(engine PRIVATE _POSIX_C_SOURCE=199309L)
target_link_libraries(engine PUBLIC raylib tomlc99)

add_executable(sleipner src/main.c)
target_link_libraries(sleipner PRIVATE engine)

add_subdirectory(test)
