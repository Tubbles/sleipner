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

- `src/platforms/rcore_desktop_glfw.c` — Wayland framebuffer-scaling
  fix from upstream PR #4909 (merge commit
  `5c954c1f52cb04631c118f67b36b0b768a1f40b2`). Forces
  `GLFW_SCALE_FRAMEBUFFER=FALSE` before window creation so GLFW 3.4
  does not opt the Wayland surface into compositor-driven fractional
  scaling that raylib's `CORE.Window.screen` / `CORE.Window.render`
  bookkeeping does not handle. Without this, desktop Wayland builds
  render a black screen after `SetWindowSize` / `ToggleBorderlessWindowed`
  while audio and input continue working. The original patch file is at
  `recipes/raylib/patches/5.5-0002-wayland-scale-framebuffer.patch`.

## Updating upstream

1. Download the new raylib tarball.
2. Replace `engine/vendor/raylib/` contents (same subset).
3. Re-apply the patches from `recipes/raylib/patches/` in numeric
   order (or drop any that have been absorbed upstream, and update
   this file accordingly).
4. Update this file with the new upstream version and sha256.
