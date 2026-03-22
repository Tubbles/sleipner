# GitVersion.cmake — set SLEIPNER_GIT_VERSION via `git describe`.
# If SLEIPNER_GIT_VERSION is already defined (e.g. passed in via -D from
# Gradle or another caller), it is left unchanged.
# Uses CMAKE_CURRENT_LIST_DIR to locate the repo root regardless of which
# CMakeLists.txt includes this file.

if(NOT SLEIPNER_GIT_VERSION)
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
            WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
            OUTPUT_VARIABLE SLEIPNER_GIT_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()
    if(NOT SLEIPNER_GIT_VERSION)
        set(SLEIPNER_GIT_VERSION "unknown")
    endif()
endif()
