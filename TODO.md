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
Are there patterns in the codebase that suggest we need common primitives?
Look at dynamic arrays, hash maps, and stacks — are we reinventing them?
Candidate libraries: stb_ds, stc, cc. Evaluate before adding any dependency.

- [ ] Inventory places where we hand-roll fixed-size arrays with a count field
- [ ] Decide: is the current approach sufficient, or does complexity justify a lib?
