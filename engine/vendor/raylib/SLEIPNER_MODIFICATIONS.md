# Sleipner modifications to vendored raylib 6.0

Source imported from the raylib 6.0 upstream tarball:
`https://github.com/raysan5/raylib/archive/refs/tags/6.0.tar.gz`
(sha256 `2b3ee1e2120c7a0796b33062c7e9a694dd8a8caa56a96319ac8c8ecf54a90d0b`).

Only the subset needed for building is vendored: `src/`, `cmake/`,
top-level `CMakeLists.txt`, `CMakeOptions.txt`, `LICENSE`, `raylib.pc.in`,
`README.md`. Upstream `examples/`, `projects/`, `tools/`, `logo/`, bindings
docs, and the Zig build scripts are intentionally omitted.

## Patches applied in place

- **0001-android-trust-keycode-not-source-bits.patch** —
  `src/platforms/rcore_android.c` `AndroidInputCallback` regression
  introduced by upstream PR #5439 (commit `5e14ac5`, shipped in
  raylib 6.0). The new guard
  `!FLAG_IS_SET(source, AINPUT_SOURCE_KEYBOARD)` drops every
  face-button event from controllers that advertise the
  `AINPUT_SOURCE_KEYBOARD` bit alongside `JOYSTICK`/`GAMEPAD` (e.g.
  GameSir X2 Type-C reports source `0x01000511`). The patch removes
  that guard and uses `AndroidTranslateGamepadButton(keycode)` as
  the authoritative discriminator: recognised gamepad keycodes route
  to the gamepad path, anything else falls through to the keyboard
  handler instead of being silently dropped. This also fixes the
  original bug PR #5439 was trying to address (Motorola Razr volume
  keys arriving with `GAMEPAD` source bit set), since
  `AKEYCODE_VOLUME_*` is not in the gamepad translation table and
  now reaches the keyboard handler. Patch source:
  `recipes/raylib/patches/0001-android-trust-keycode-not-source-bits.patch`.
  Upstream issue / PR not yet filed.

## Patches absorbed in 6.0 (no-op upgrade)

- **PR #4671** — Android shared-linker-flags fix. Strips
  `-Wl,--no-undefined` and `-static-libstdc++` from
  `CMAKE_SHARED_LINKER_FLAGS` when `PLATFORM=Android`. Now in
  `cmake/LibraryConfigurations.cmake` upstream (lines 84–89).
- **PR #4909 + PR #5564** — Wayland framebuffer-scaling fix.
  Disables `GLFW_SCALE_FRAMEBUFFER` on Wayland for the non-HiDPI
  path, reads framebuffer size via `glfwGetFramebufferSize` for the
  HiDPI path, and skips redundant `SetMouseScale` on Wayland (since
  GLFW already reports mouse coords in logical space there). Folded
  into the broader **REDESIGNED Fullscreen modes and High-DPI
  content scaling** rework that defines raylib 6.0's window system.
  See `src/platforms/rcore_desktop_glfw.c` for the platform-aware
  `glfwGetPlatform() == GLFW_PLATFORM_WAYLAND` branches.

## Updating upstream

1. Download the new raylib tarball, compute sha256, update this file.
2. Replace `engine/vendor/raylib/` contents (same subset).
3. If a patch is required, place it under `recipes/raylib/patches/`
   (recreate the directory if needed) and document it under
   "Patches applied in place" above.
