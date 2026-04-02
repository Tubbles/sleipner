function(add_unit_test NAME)
    add_executable(${NAME} ${NAME}.c test_heap_alloc.c)
    target_compile_definitions(${NAME} PRIVATE _GNU_SOURCE)
    target_include_directories(${NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/fff
        $<TARGET_PROPERTY:raylib,INTERFACE_INCLUDE_DIRECTORIES>)
    target_link_libraries(${NAME} PRIVATE unity m)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

add_unit_test(stub_test)
add_unit_test(strv_test)
add_unit_test(error_test)
add_unit_test(shape_test)
add_unit_test(touch_test)

add_executable(engine_tests
    main_test.c
    test_helpers.c
    vec_test.c
    map_test.c
    arena_test.c
    alloc_test.c
    attribute_test.c
    blueprint_test.c
    entity_test.c
    game_test.c
    integration_test.c
    level_test.c
    particle_test.c
    collision_test.c
    editor_test.c
    toml_emitter_test.c
    rule_test.c
    str_test.c
    toml_str_test.c
)
target_compile_definitions(engine_tests PRIVATE _GNU_SOURCE)
target_include_directories(engine_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/fff)
target_link_libraries(engine_tests PRIVATE engine unity m)
add_test(NAME engine_tests COMMAND engine_tests)
