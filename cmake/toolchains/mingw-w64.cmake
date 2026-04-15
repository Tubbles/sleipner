set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_AR x86_64-w64-mingw32-ar)
set(CMAKE_RANLIB x86_64-w64-mingw32-ranlib)
# raylib and SDL2 both emit some D3D math helpers (MatrixIdentity,
# MatrixMultiply) and MinGW intrin inlines (__cpuidex); allow the
# linker to pick the first definition rather than fail. -mwindows
# hides the console window on launch.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-mwindows -Wl,--allow-multiple-definition")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
