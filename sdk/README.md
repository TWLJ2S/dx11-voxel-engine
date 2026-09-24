# Code-mod SDK

The currently executable ABI is the small core-WebAssembly interface in
[`include/ac_mod.h`](include/ac_mod.h). It is versioned independently from C++
engine internals and intentionally exposes values and integer handles rather
than engine pointers. The WIT file in [`wit/ac-game.wit`](wit/ac-game.wit)
describes the higher-level component-model interface that this ABI is intended
to grow into; it is not yet the wire format loaded by the game.

Build the C example with a Clang toolchain that supports `wasm32`:

```powershell
clang --target=wasm32 -nostdlib -Wl,--no-entry -Wl,--export-memory `
  -Wl,--allow-undefined -Isdk/include sdk/examples/hello/hello.c `
  -o sdk/examples/hello/hello.wasm
```

Copy the `hello` directory into `mods`. The game discovers `pack.json`, loads
`hello.wasm` with Wasmtime, and makes `/hello` available. Code-only packs may
omit the `content` object.

## ABI rules

- `ac_init` is required and returns zero on success. Every other callback is
  optional and is subscribed only when exported.
- Optional world-open/save/close and resource-reloaded callbacks let modules
  synchronize world-scoped storage without polling from the tick callback.
- Registry handles become usable in `ac_registries_frozen`; general gameplay
  startup work can run in `ac_engine_started`.
- All strings are UTF-8 pointer/length pairs in the module's exported memory.
- `ac_block_read` packs presence into the upper 32 bits and the runtime block
  state into the lower 32 bits. Runtime states must not be persisted by mods.
- `ac_block_changing` returns the replacement state in the lower 32 bits and a
  cancellation flag in the upper 32 bits.
- `ac_command` receives the ID returned by `ac_register_command`. Arguments can
  be copied with `ac_argument_length` and `ac_argument_read` only while that
  callback is running.
- A module gets five million fuel units for each engine-to-WASM call. Traps and
  exhausted fuel are contained and logged by the host.

## Capabilities

Declare only the host access the module needs in `pack.json`:

| Capability | Host calls |
| --- | --- |
| `registry.blocks.read` | `ac_block_find` |
| `world.read` | `ac_block_read` |
| `world.write` | `ac_block_write` |
| `commands.register` | `ac_register_command` |
| `registries.read` | `ac_registry_find`, `ac_registry_size` |
| `registries.write` | add entries during mod initialization with `ac_registry_add` |
| `entities.read` | `ac_entity_exists`, `ac_entity_position` |
| `entities.spawn` | `ac_entity_spawn` |
| `entities.write` | teleport, damage, and remove entity calls |
| `render.particles` | `ac_particle_burst` |
| `ui.read` | current screen length/read calls |
| `ui.notify` | `ac_ui_notify` |
| `storage.read` | `ac_storage_length`, `ac_storage_read` |
| `storage.write` | `ac_storage_write`, `ac_storage_remove` |
| `network.send` | `ac_network_send` when a transport is available |

Logging and replies require no capability. WASI is not linked, so modules do
not receive filesystem, process, clock, environment, or network access from
the runtime.

Entity values use stable 64-bit handles. The renderer port accepts bounded
presentation commands instead of graphics pointers. Storage is confined to
`<world>/mod_data/<pack-id>` with safe relative keys and a 4 MiB value limit.
`ac_network_available` currently returns false because the game has no network
transport yet; keeping that port explicit avoids baking a future protocol into
the engine or pretending sends succeeded.

Custom registries can also be supplied as the `registries` content kind. A
registry document has `schemaVersion`, a namespaced `registry` ID, and an
`entries` array containing string `id` and `value` fields. Entries from packs
and `ac_registry_add` are frozen after module initialization and receive stable,
lexicographically assigned runtime handles.
