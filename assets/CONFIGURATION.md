# Runtime configuration

Authored gameplay values live with the content that owns them:

- `application.json` contains registry block aliases, default hotbar contents,
  settings choices, world clock tuning, and player vital limits/timers.
- `player/controller.json` contains collision poses, look sensitivity, movement,
  water, flight, jump, and landing behavior.
- `player/animation.json` contains animation curves, pose-state smoothing, camera
  offsets, body-camera response, FOV response, and third-person composition.
- `player/held_items.json` contains held-item and viewmodel poses.
- `entities/entities.json` contains per-entity movement, combat, hitbox, and
  animation values.
- Individual block JSON files own block behavior and item properties. Runtime
  IDs are never authored numeric IDs; `application.json` aliases resolve names
  through the active content registry.
- `terrain/overworld.json` owns the runtime block-role map, numeric worldgen
  settings, named block groups, and the ordered lithology/ore rules. Roles and
  groups are stored in hash maps; adding or remapping one does not require a
  C++ block-ID declaration.
- `terrain/biomes.json` assigns biome IDs from array order. Each biome contains
  one or more weighted climate selectors whose `ranges` may use `height`,
  `mountain`, `ocean`, `lake`, `river`, `temperature`, `moisture`,
  `continentalness`, `erosion`, `weirdness`, `highland`, `alpine`, or `arid`.
  A range is `[minimum, maximum, falloff]`. Surface blocks, water behavior,
  weather traits, tree shape, tree blocks, chances, and variants are all owned
  by the biome definition. Names and aliases are runtime lookup keys; there is
  no compiled biome enum or authored numeric ID.

Required configuration is validated and fails startup with a field-specific
error. This prevents a missing JSON property from silently changing behavior by
falling back to an unrelated C++ initializer.

Constants that define an engine/storage contract remain in code: packed block
state bit positions, chunk and GPU buffer dimensions, shader binding slots,
vertex layouts, mathematical constants, and numerical safety epsilons. Changing
those requires coordinated format or shader changes and is not runtime tuning.

## Reloading content

Use `/reload` in the in-game console to rediscover all packs and reload every
area declared under `content` in `pack.json`. Pass one or more content keys to
reload only those areas, for example `/reload recipes`, `/reload entities`, or
`/reload models blocks`. Area names are discovered from the active manifests
and are available through console tab completion.

Reloads are staged and validated before the live registries are replaced.
Reloading models, blocks, or terrain briefly pauses chunk workers and restreams
loaded chunks. Existing block runtime IDs must keep the same names so loaded and
saved voxel states cannot silently change meaning.
