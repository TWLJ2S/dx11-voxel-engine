# Terrain block palette

`overworld.json` maps semantic terrain roles to names from the loaded block
registry. The CPU generator, GPU generator, tree placement, ore placement, and
terrain water fill all use this palette; numeric block IDs are not embedded in
terrain-generation code.

`biomes.json` contains the biome content records used by both terrain paths:

- command/display names and aliases;
- top, subsurface, and flooded-surface blocks;
- soil depth and ocean/lake/wetland behavior;
- global snow, alpine rock, and highland rock rules;
- primary/alternate tree species, deterministic spawn probabilities, and one
  to four deterministic shape variants per species.

Biome block values are registry names, and probabilities are exact
`[numerator, denominator]` pairs. The biome IDs remain the stable CPU/GPU and
saved-world ABI; the values associated with those IDs are runtime data.

Change these values without recompiling. Every terrain role and biome is
required, and startup fails with a descriptive error when required data is
missing, duplicated, malformed, or refers to a block name that is not loaded.
