add_executable(engine_tests
    test_main.c
    test_stub.c
    test_arena.c
    test_blueprint.c
    test_shape.c
    test_particle.c
    test_collision.c
)
target_link_libraries(engine_tests PRIVATE engine unity m)
