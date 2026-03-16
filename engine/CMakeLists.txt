add_library(tomlc99 STATIC vendor/tomlc99/toml.c)
target_include_directories(tomlc99 PUBLIC vendor/tomlc99)

add_library(engine STATIC
    src/audio.c
    src/collision.c
    src/input.c
    src/particle.c
    src/render.c
    src/shape.c
)
target_include_directories(engine PUBLIC src)
target_link_libraries(engine PUBLIC raylib zlog tomlc99)

add_subdirectory(test)
