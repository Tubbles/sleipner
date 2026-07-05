function(add_unit_test NAME)
    add_executable(${NAME} ${NAME}.c test_heap_alloc.c)
    target_compile_definitions(${NAME} PRIVATE _GNU_SOURCE)
    target_include_directories(${NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    target_include_directories(${NAME} SYSTEM PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/fff
        ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/tomlc99
        $<TARGET_PROPERTY:raylib,INTERFACE_INCLUDE_DIRECTORIES>)
    target_link_libraries(${NAME} PRIVATE unity m)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

add_unit_test(stub_test)
add_unit_test(strv_test)
add_unit_test(input_test)
add_unit_test(input_func_test)
add_unit_test(keyboard_widget_test)
add_unit_test(platform_paths_test)
add_unit_test(preferences_test)
target_link_libraries(preferences_test PRIVATE tomlc99)
add_unit_test(error_test)
add_unit_test(shape_test)
add_unit_test(str_test)
add_unit_test(vec_test)
add_unit_test(particle_test)
add_unit_test(random_test)
add_unit_test(alloc_test)
add_unit_test(arena_test)
add_unit_test(map_test)
add_unit_test(audio_test)
add_unit_test(collision_test)
add_unit_test(depth_sort_test)
add_unit_test(attribute_test)
add_unit_test(entity_test)
add_unit_test(toml_str_test)
add_unit_test(rule_test)
add_unit_test(effect_test)
add_unit_test(editor_draw_test)
add_unit_test(editor_core_test)
add_unit_test(editor_attr_test)
add_unit_test(editor_widgets_test)
add_unit_test(editor_child_test)
add_unit_test(editor_blueprint_test)
add_unit_test(menu_test)
add_unit_test(font_cache_test)
add_unit_test(settings_capture_test)
add_unit_test(settings_general_test)
add_unit_test(hud_test)
add_unit_test(inventory_screen_test)
add_unit_test(save_screen_test)

add_executable(engine_tests
    main_test.c
    test_helpers.c
    assets_test.c
    blueprint_test.c
    game_test.c
    gamedata_source_test.c
    integration_test.c
    level_test.c
    render_test.c
    save_test.c
    toml_emitter_test.c
    rule_integration_test.c
    sprite_offset_integration_test.c
    collision_offset_integration_test.c
    collision_composite_integration_test.c
    undo_test.c
)
target_compile_definitions(engine_tests PRIVATE _GNU_SOURCE
    GAMEDATA_FIXTURE_PATH="${CMAKE_SOURCE_DIR}/data/gamedata.toml")
target_include_directories(engine_tests SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/fff)
target_link_libraries(engine_tests PRIVATE engine sleipner_assets unity m)
add_test(NAME engine_tests COMMAND engine_tests)
