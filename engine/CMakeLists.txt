add_library(engine STATIC
    src/audio.c
    src/collision.c
    src/input.c
    src/particle.c
    src/render.c
    src/shape.c
)
target_include_directories(engine PUBLIC src)
target_link_libraries(engine PUBLIC raylib zlog)

add_subdirectory(test)
