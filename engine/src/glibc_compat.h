#pragma once

/*
 * Pin math symbols to their oldest stable ABI so the binary runs on
 * Debian bookworm (glibc 2.36) and similar-vintage distros. glibc 2.38
 * introduced higher-precision fmod / fmodf implementations that the
 * compiler binds to by default via versioned symbols; the original
 * GLIBC_2.2.5 aliases are retained indefinitely for backwards
 * compatibility and are adequate for our sub-pixel-grade use.
 *
 * Force-included via add_compile_options(-include ...) at the top-level
 * CMakeLists.txt, which is itself guarded on native Linux desktop
 * builds. The surrounding CMake guard (NOT WIN32, NOT APPLE, NOT
 * CMAKE_CROSSCOMPILING) keeps this away from the Windows / Android /
 * macOS targets, so an __linux__ guard here is sufficient. A feature
 * macro check like __GLIBC__ would require pulling in <features.h>
 * first, which would clobber raylib's own _GNU_SOURCE ordering.
 */
#if defined(__linux__)
__asm__(".symver fmod,fmod@GLIBC_2.2.5");
__asm__(".symver fmodf,fmodf@GLIBC_2.2.5");
#endif
