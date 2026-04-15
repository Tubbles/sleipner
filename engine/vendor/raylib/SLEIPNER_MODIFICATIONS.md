# Sleipner modifications to vendored raylib 5.5

Source imported from the raylib 5.5 upstream tarball:
`https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz`
(sha256 `aea98ecf5bc5c5e0b789a76de0083a21a70457050ea4cc2aec7566935f5e258e`).

Only the subset needed for building is vendored: `src/`, `cmake/`,
top-level `CMakeLists.txt`, `CMakeOptions.txt`, `LICENSE`, `raylib.pc.in`.
Upstream `examples/`, `projects/`, `parser/`, `logo/`, bindings docs, and
the Zig build scripts are intentionally omitted.

## Patches applied in place

- `cmake/LibraryConfigurations.cmake` — Android shared-linker-flags fix
  from upstream PR #4671. Strips `-Wl,--no-undefined` and
  `-static-libstdc++` from `CMAKE_SHARED_LINKER_FLAGS` when
  `PLATFORM=Android`, which conflicts with `-Wl,-undefined,dynamic_lookup`
  needed for the missing `void main(void)` declaration in `android_main()`.
  The original patch file is kept in the repo at
  `recipes/raylib/patches/5.5-0001-fix-android-shared.patch` for
  provenance and for re-application after an upstream bump.

## Updating upstream

1. Download the new raylib tarball.
2. Replace `engine/vendor/raylib/` contents (same subset).
3. Re-apply the patch from
   `recipes/raylib/patches/5.5-0001-fix-android-shared.patch` (or
   whichever version-stamped patch replaces it).
4. Update this file with the new upstream version and sha256.
