# Content and code mods

Each immediate subdirectory of `mods` can be a content pack. A pack is enabled
when it contains a `pack.json` manifest. Packs load after `assets/pack.json` in
dependency order.

Numeric block and item IDs are never part of the mod API. The loader assigns
compact process-local IDs every time the game starts. Use stable names such as
`example:ruby_block` in definitions and references. Unqualified names inside a
non-core pack are automatically placed in that pack's namespace.

Existing worlds keep a `content_registry.json` snapshot. Chunks, dropped items,
and player inventory are translated between saved IDs and the current runtime
registry. Adding or reordering packs therefore does not reinterpret existing
world data. If a referenced pack is temporarily absent, its saved name and slot
remain reserved; unavailable blocks load as air until missing-block support is
added.

Example layout:

```text
mods/
  ruby_tools/
    pack.json
    blocks/
      blocks.json
    recipes.json
    textures/
      ruby_block.png
```

`pack.json`:

```json
{
  "schemaVersion": 1,
  "id": "ruby_tools",
  "version": "1.0.0",
  "dependencies": ["core"],
  "content": {
    "blocks": "blocks",
    "recipes": "recipes.json"
  }
}
```

`blocks/blocks.json` (notice that there is no numeric `id`):

```json
{
  "blocks": [
    {
      "name": "ruby_block",
      "model": 0,
      "texture": "mods/ruby_tools/textures/ruby_block.png",
      "solid": true,
      "occludes": true,
      "hardness": 5.0,
      "soundMaterial": "stone"
    }
  ]
}
```

The resolved name is `ruby_tools:ruby_block`. A legacy numeric `id` field is
accepted only so old core data can migrate old saves; it is ignored when the
runtime registry is allocated. New content should omit it.

Content kinds currently supported by manifests are `models`, `blocks`,
`recipes`, `entities`, `registries`, `terrainBlocks`, and `biomes`. Block,
recipe, entity, and registry content is additive.
Terrain and biome files are singleton configurations and require an explicit
manifest override.

Generic mod registries use a small portable document and are frozen after code
modules initialize:

```json
{
  "schemaVersion": 1,
  "registry": "ruby_tools:spells",
  "entries": [
    { "id": "ruby_tools:spark", "value": "{\"power\":2}" }
  ]
}
```

Reference that file as the `registries` content kind. Values are
opaque UTF-8 strings owned by the mod; IDs, ordering, and process-local handles
are managed by the host.

## Code entrypoints

Packs may also declare executable entrypoints. WASM is the portable, sandboxed
code-mod format; native modules are an optional unsafe tier. Code modules use
the same engine API and never receive engine pointers or DirectX objects.

```json
{
  "schemaVersion": 1,
  "id": "ruby_tools",
  "version": "1.0.0",
  "dependencies": ["core"],
  "capabilities": ["world.read", "world.write", "commands.register"],
  "entrypoints": {
    "wasm": "ruby_tools.wasm"
  },
  "content": {
    "blocks": "blocks"
  }
}
```

The executable ABI and a buildable C example are documented in
[`sdk/README.md`](../sdk/README.md). The WIT file is the planned higher-level
component-model contract; the current loader uses the versioned core-WASM ABI
in `sdk/include/ac_mod.h`. A code-only pack may omit `content` entirely.

The engine dispatches only callbacks that a module exports. World and block
calls use integer handles only after namespaced identifiers have been resolved
by the host.

Native code and WASM runtimes are adapters behind the same `modRuntimePort`.
This keeps the public API independent of a particular runtime and lets tests
load in-process modules without starting a WASM VM.
