add_executable(engine_tests
    main_test.c
    test_helpers.c
    stub_test.c
    vec_test.c
    map_test.c
    arena_test.c
    alloc_test.c
    attribute_test.c
    blueprint_test.c
    entity_test.c
    error_test.c
    game_test.c
    integration_test.c
    level_test.c
    shape_test.c
    particle_test.c
    collision_test.c
    editor_test.c
    toml_emitter_test.c
    rule_test.c
    str_test.c
    touch_test.c
    strv_test.c
    toml_str_test.c
)
target_compile_definitions(engine_tests PRIVATE _GNU_SOURCE)
target_include_directories(engine_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/fff)
target_link_libraries(engine_tests PRIVATE engine unity m)
