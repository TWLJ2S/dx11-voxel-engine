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

        if (palette.require("stone") != definitions.getId("stone") ||
            palette.require("water") != definitions.getId("water") ||
            palette.require("bedrock") != definitions.getId("bedrock") ||
            palette.requireSetting("seaLevel") != 63.0f ||
            !palette.inGroup("treeLogs", definitions.getId("oak_log")) ||
            palette.lithology.empty()) {
            std::cerr << "terrain palette did not resolve block names\n";
            return 1;
        }
        ac::TERRAIN_BIOME parsed = biomes.id("plains");
        if (!biomes.parse("swamp", parsed) || parsed != biomes.id("wetland") ||
            std::string(biomes.displayName(parsed)) != "WETLAND" ||
            biomes.definitions[biomes.id("wetland")].generation.floodedTopBlock !=
                definitions.getId("mud") ||
			biomes.definitions[biomes.id("forest")].generation.alternateTreeKind != "birch" ||
			biomes.definitions[biomes.id("forest")].generation.alternateTreeLogBlock !=
				definitions.getId("birch_log") ||
			biomes.definitions[biomes.id("forest")].generation.treeVariants != 4u ||
			biomes.definitions[biomes.id("forest")].generation.climateSelectors.empty()) {
            std::cerr << "biome JSON data was not loaded\n";
            return 2;
        }

        palette.set("bedrock", definitions.getId("dirt"));
        ac::terrainGenerator generator(123456789ULL, palette, biomes);
        auto generated = std::make_unique<ac::chunk>();
        generated->_position = { 0, 0, 0 };
        generator.generate(*generated);

        for (uint32_t z = 0; z < CHUNK_LENGTH; ++z) {
            for (uint32_t x = 0; x < CHUNK_WIDTH; ++x) {
                if (generated->getBlock(x, 0, z) != palette.require("dirt")) {
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
