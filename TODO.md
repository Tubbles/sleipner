# Sleipner TODO

## Code reuse
Convert remaining fixed-size array + count patterns to `vec_<name>` using the
new `VEC_DECL`/`VEC_IMPL` macros in `vec.h`.

- [ ] Inventory remaining hand-rolled fixed-size arrays with a count field
- [ ] Convert candidates (FlagSet, BlueprintChild list, etc.) one at a time
