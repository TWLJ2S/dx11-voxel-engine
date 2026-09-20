# Content packs

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
`recipes`, `terrainBlocks`, and `biomes`. Block and recipe content is additive.
Terrain and biome files are singleton configurations and require an explicit
manifest override.
