# Sleipner TODO

## Eradicate Static State (The "Context Passing" Migration)
Audit and eliminate all static variables in the codebase to ensure mathematically pure functions, flawless hot-reloading, and completely isolated headless testing. No static state is allowed—not even for logging or error reporting.

- [ ] Create a root `EngineContext` (or `LogContext`/`ErrorContext`) struct to hold all state.
- [ ] Move the `trace_file` and `log_lines` ring buffer from `debug.c` into the context.
- [ ] Move the static error string buffer from `error.c` into the context.
- [ ] Thread the context pointer through all sub-systems, specifically updating every `debug_log` and `error_set`/`error_wrap` call to take the context pointer.
- [ ] Move the asset registries (texture, font preview, audio) into the `EngineContext`.

## Code reuse
Convert remaining fixed-size array + count patterns to `vec_<name>` using the
new `VEC_DECL`/`VEC_IMPL` macros in `vec.h`.

- [ ] Inventory remaining hand-rolled fixed-size arrays with a count field
- [ ] Convert candidates (FlagSet, BlueprintChild list, etc.) one at a time
