function(add_unit_test NAME)
    add_executable(${NAME} ${NAME}.c test_heap_alloc.c)
    target_compile_definitions(${NAME} PRIVATE _GNU_SOURCE)
    target_include_directories(${NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/fff
        ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/tomlc99
        $<TARGET_PROPERTY:raylib,INTERFACE_INCLUDE_DIRECTORIES>)
    target_link_libraries(${NAME} PRIVATE unity m)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

add_unit_test(stub_test)
add_unit_test(strv_test)
add_unit_test(error_test)
add_unit_test(shape_test)
add_unit_test(str_test)
add_unit_test(vec_test)
add_unit_test(particle_test)
add_unit_test(alloc_test)
add_unit_test(arena_test)
add_unit_test(map_test)
add_unit_test(collision_test)
add_unit_test(attribute_test)
add_unit_test(entity_test)
add_unit_test(toml_str_test)
add_unit_test(rule_test)
add_unit_test(editor_draw_test)
add_unit_test(editor_core_test)
add_unit_test(editor_attr_test)
add_unit_test(editor_widgets_test)

add_executable(engine_tests
    main_test.c
    test_helpers.c
    blueprint_test.c
    game_test.c
    integration_test.c
    level_test.c
    toml_emitter_test.c
    rule_integration_test.c
    undo_test.c
)
target_compile_definitions(engine_tests PRIVATE _GNU_SOURCE
    GAMEDATA_PATH="${CMAKE_SOURCE_DIR}/data/gamedata.toml")
target_include_directories(engine_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/fff)
target_link_libraries(engine_tests PRIVATE engine unity m)
add_test(NAME engine_tests COMMAND engine_tests)
