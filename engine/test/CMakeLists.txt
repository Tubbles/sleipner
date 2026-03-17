add_executable(engine_tests
    test_main.c
    test_stub.c
    test_arena.c
    test_attribute.c
    test_blueprint.c
    test_entity.c
    test_game.c
    test_integration.c
    test_level.c
    test_shape.c
    test_particle.c
    test_collision.c
    test_toml_emitter.c
)
target_link_libraries(engine_tests PRIVATE engine unity m)
