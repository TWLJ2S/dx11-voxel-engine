# Block behavior schema

All gameplay and presentation metadata is data-driven. The runtime does not infer
properties from a block's name. Each definition may set:

- `hardness`, `harvestLevel`, `toolHarvestLevel`, `tool`, `requiresTool`, and
  `miningSpeed` for mining.
- `food` and `armorReduction` for wearable/consumable items.
- `heldStyle`: `block`, `slab`, `sprite`, `tool`, `rod`, or `cross`.
- `soundMaterial`: `stone`, `wood`, `glass`, `grass`, `gravel`, `sand`, `snow`,
  or `cloth`.
- `lightTransmission`: an `[r, g, b]` multiplier used by transmitted voxel
  lighting.
- `connectsToPanes` and `flatIcon` for connection and inventory presentation.

Schema defaults are deliberately neutral. Shipped definitions record these
fields explicitly so renaming a block never changes its behavior.

Block and item definitions can include a `behavior` object. References use block
names rather than numeric IDs and are resolved after every JSON file in the pack
has loaded, so file ordering does not matter.

```json
"behavior": {
  "placement": "wall_or_floor",
  "use": "toggle_powered",
  "entity": "chest",
  "placeAs": "redstone_wire",
  "drop": "redstone",
  "dropCount": 1,
  "redstone": "switch",
  "redstoneOutput": 15,
  "poweredOnPlace": true
}
```

Supported values:

- `placement`: `standard`, `horizontal_facing`, `wall_only`, `wall_or_floor`,
  `trapdoor`, or `door`.
- `use`: `none`, `toggle_open`, `toggle_powered`, `container`, or `crafting`.
- `entity`: `none` or `chest`.
- `placeAs`: the block name placed by an item.
- `drop`: the item/block name dropped, or `none` for no drop.
- `dropCount`: zero to 64; only valid with `drop`.
- `redstone`: `none`, `wire`, `source`, `torch`, `lamp`, `switch`, or `listener`.
- `redstoneOutput`: zero to 15.
- `poweredOnPlace`: initial powered-state flag for placed blocks.

The older `interaction` field remains supported and is translated at runtime when
the corresponding behavior field is omitted. This lets existing resource packs
migrate incrementally.
