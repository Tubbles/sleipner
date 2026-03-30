add_executable(engine_tests
    test_main.c
    test_helpers.c
    test_stub.c
    test_vec.c
    test_map.c
    test_arena.c
    test_alloc.c
    test_attribute.c
    test_blueprint.c
    test_entity.c
    test_error.c
    test_game.c
    test_integration.c
    test_level.c
    test_shape.c
    test_particle.c
    test_collision.c
    test_editor.c
    test_toml_emitter.c
    test_rule.c
    test_str.c
    test_touch.c
    test_strv.c
    test_toml_str.c
)
target_compile_definitions(engine_tests PRIVATE _GNU_SOURCE)
target_include_directories(engine_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/fff)
target_link_libraries(engine_tests PRIVATE engine unity m)
