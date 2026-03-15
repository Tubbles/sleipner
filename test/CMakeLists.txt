add_library(unity STATIC ${CMAKE_SOURCE_DIR}/vendor/unity/unity.c)
target_include_directories(unity PUBLIC ${CMAKE_SOURCE_DIR}/vendor/unity)

add_executable(sleipner_tests
    test_main.c
    test_stub.c
    test_shape.c
    test_particle.c
    test_collision.c
    ${CMAKE_SOURCE_DIR}/src/shape.c
    ${CMAKE_SOURCE_DIR}/src/particle.c
    ${CMAKE_SOURCE_DIR}/src/collision.c
    ${CMAKE_SOURCE_DIR}/src/render.c
)
target_link_libraries(sleipner_tests PRIVATE unity raylib m)
target_include_directories(sleipner_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
