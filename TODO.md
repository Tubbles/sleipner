# Sleipner TODO

## Static data audit
Audit all static variables in the codebase. Goal: understand the current
landscape, then eliminate or document each one.

- [x] ✅ Move texture registry to GameState (DONE)
- [x] ✅ Move font preview system to GameState (DONE)
- [x] ✅ Move audio system to GameState (DONE)
- [x] ✅ Refactor debug logging to use external state (DONE)
- [x] ✅ Convert error system to singleton pattern (DONE)
- [x] ✅ Move gamedata_mtime to GameState (DONE)
- [x] ✅ Complete static variable audit - all stateful statics eliminated (DONE)

## Code reuse
Convert remaining fixed-size array + count patterns to `vec_<name>` using the
new `VEC_DECL`/`VEC_IMPL` macros in `vec.h`.

- [ ] Inventory remaining hand-rolled fixed-size arrays with a count field
- [ ] Convert candidates (FlagSet, BlueprintChild list, etc.) one at a time
