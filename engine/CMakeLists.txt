include(${CMAKE_CURRENT_SOURCE_DIR}/sources.cmake)

add_library(unity STATIC vendor/unity/unity.c)
target_include_directories(unity PUBLIC vendor/unity)

add_library(tomlc99 STATIC vendor/tomlc99/toml.c)
target_include_directories(tomlc99 PUBLIC vendor/tomlc99)

foreach(src ${ENGINE_SOURCE_FILES})
    list(APPEND engine_srcs src/${src})
endforeach()

add_library(engine STATIC ${engine_srcs})
target_include_directories(engine PUBLIC src)
target_compile_definitions(engine PRIVATE _POSIX_C_SOURCE=199309L)
target_link_libraries(engine PUBLIC raylib tomlc99)

add_executable(sleipner src/main.c)
target_link_libraries(sleipner PRIVATE engine)

include(${CMAKE_SOURCE_DIR}/cmake/GitVersion.cmake)
target_compile_definitions(sleipner PRIVATE SLEIPNER_VERSION="${SLEIPNER_GIT_VERSION}")

embed_all_assets(sleipner ${CMAKE_SOURCE_DIR})

add_subdirectory(test)
