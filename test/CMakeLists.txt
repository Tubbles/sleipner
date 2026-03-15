add_library(unity STATIC vendor/unity/unity.c)
target_include_directories(unity PUBLIC vendor/unity)

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
