#define NOMINMAX
#include <world/minecraftTerrainGeneration.h>

#include <iostream>
#include <memory>

int main() {
    try {
        ac::staticAssetManager definitions;
        definitions.load("assets/block");
        ac::terrainBlockPalette palette = ac::terrainBlockPalette::load(
            "assets/terrain/overworld.json", definitions);
        ac::terrainBiomeConfig biomes = ac::terrainBiomeConfig::load(
            "assets/terrain/biomes.json", definitions);

        if (palette.STONE_BLOCK != definitions.getId("stone") ||
            palette.WATER_BLOCK != definitions.getId("water") ||
            palette.BEDROCK_BLOCK != definitions.getId("bedrock")) {
            std::cerr << "terrain palette did not resolve block names\n";
            return 1;
        }
        ac::TERRAIN_BIOME parsed = ac::BIOME_PLAINS;
        if (!biomes.parse("swamp", parsed) || parsed != ac::BIOME_WETLAND ||
            std::string(biomes.displayName(parsed)) != "WETLAND" ||
            biomes.definitions[ac::BIOME_WETLAND].gpu.floodedTopBlock !=
                definitions.getId("mud") ||
			biomes.definitions[ac::BIOME_FOREST].gpu.alternateTreeKind != ac::TREE_BIRCH ||
			((biomes.definitions[ac::BIOME_FOREST].gpu.flags & ac::BIOME_TREE_VARIANT_MASK) >>
				ac::BIOME_TREE_VARIANT_SHIFT) + 1u != 4u) {
            std::cerr << "biome JSON data was not loaded\n";
            return 2;
        }

        palette.BEDROCK_BLOCK = definitions.getId("dirt");
        ac::terrainGenerator generator(123456789ULL, palette, biomes);
        auto generated = std::make_unique<ac::chunk>();
        generated->_position = { 0, 0, 0 };
        generator.generate(*generated);

        for (uint32_t z = 0; z < CHUNK_LENGTH; ++z) {
            for (uint32_t x = 0; x < CHUNK_WIDTH; ++x) {
                if (generated->getBlock(x, 0, z) != palette.DIRT_BLOCK) {
                    std::cerr << "terrain generator ignored remapped bedrock\n";
                    return 3;
                }
            }
        }
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 4;
    }

    std::cout << "terrain palette checks passed\n";
    return 0;
}
