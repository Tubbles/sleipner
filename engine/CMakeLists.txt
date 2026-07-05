include(${CMAKE_CURRENT_SOURCE_DIR}/sources.cmake)

add_library(unity STATIC vendor/unity/unity.c)
target_include_directories(unity SYSTEM PUBLIC vendor/unity)

add_library(tomlc99 STATIC vendor/tomlc99/toml.c)
target_include_directories(tomlc99 SYSTEM PUBLIC vendor/tomlc99)

foreach(src ${ENGINE_SOURCE_FILES})
    list(APPEND engine_srcs src/${src})
endforeach()

add_library(engine STATIC ${engine_srcs})
target_include_directories(engine PUBLIC src)
if(WIN32)
    target_link_libraries(engine PUBLIC kernel32)
else()
    target_compile_definitions(engine PRIVATE _GNU_SOURCE)
endif()
target_link_libraries(engine PUBLIC raylib tomlc99)

add_executable(sleipner src/main.c)
target_link_libraries(sleipner PRIVATE engine)
if(NOT WIN32 AND NOT APPLE AND NOT CMAKE_CROSSCOMPILING)
    # Linux desktop build links the bundled GLFW. main.c calls
    # glfwInitHint to disable libdecor on Wayland, which needs the
    # GLFW header on the include path.
    target_include_directories(sleipner PRIVATE
        ${CMAKE_SOURCE_DIR}/engine/vendor/raylib/src/external/glfw/include)
endif()

if(NOT WIN32 AND NOT APPLE AND NOT CMAKE_CROSSCOMPILING)
    # patchelf rewrites the interpreter to the distro-standard
    # /lib64/ld-linux-x86-64.so.2 and strips the Nix-store RUNPATH
    # entries, forcing the loader to resolve libs via the target
    # system's /etc/ld.so.cache. libgcc_s remains dynamically linked,
    # which is mandatory for safe unwinding across dlopen'd libraries
    # (e.g. GPU drivers) per GCC's own guidance on -shared-libgcc.
    find_program(PATCHELF patchelf REQUIRED)
    add_custom_command(TARGET sleipner POST_BUILD
        COMMAND ${PATCHELF} --set-interpreter /lib64/ld-linux-x86-64.so.2 $<TARGET_FILE:sleipner>
        COMMAND ${PATCHELF} --remove-rpath $<TARGET_FILE:sleipner>
        VERBATIM
        COMMENT "Rewriting sleipner interpreter + RPATH for non-Nix portability"
    )
endif()

include(${CMAKE_SOURCE_DIR}/cmake/GitVersion.cmake)
target_compile_definitions(sleipner PRIVATE SLEIPNER_VERSION="${SLEIPNER_GIT_VERSION}")

embed_all_assets(${CMAKE_SOURCE_DIR})
target_link_libraries(sleipner PRIVATE sleipner_assets)

if(NOT CMAKE_CROSSCOMPILING)
    add_subdirectory(test)
endif()
