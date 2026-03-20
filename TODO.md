# Sleipner TODO

## Static data audit
Audit all static variables in the codebase. Goal: understand the current
landscape, then eliminate or document each one.

- [ ] Search for all `static` variable declarations in `engine/src/`
- [ ] For each: can it live in an existing holder struct (GameState, Level, etc.)?
  - If yes: move it — pass the struct explicitly
  - If no: document why it must be static and add a reset call in game_init
- [ ] Known candidates: font preview system, texture registry

## Code reuse
Convert remaining fixed-size array + count patterns to `vec_<name>` using the
new `VEC_DECL`/`VEC_IMPL` macros in `vec.h`.

- [ ] Inventory remaining hand-rolled fixed-size arrays with a count field
- [ ] Convert candidates (FlagSet, BlueprintChild list, etc.) one at a time
