#define NOMINMAX
#include <world/minecraftTerrainGeneration.h>

#include <iostream>
#include <memory>

int main() {
	ac::staticAssetManager blocks;
	blocks.load("assets/block");
	const auto palette = ac::terrainBlockPalette::load(
		"assets/terrain/overworld.json", blocks);
	const auto biomes = ac::terrainBiomeConfig::load(
		"assets/terrain/biomes.json", blocks);
	ac::terrainGenerator generator(488124745339700ULL, palette, biomes);

	auto west = std::make_unique<ac::chunk>();
	auto east = std::make_unique<ac::chunk>();
	west->_position = { -1, 0, 0 };
	east->_position = { 0, 0, 0 };
	generator.generate(*west);
	generator.generate(*east);

	uint32_t boundaryOccupancyChanges = 0;
	for (uint32_t z = 0; z < CHUNK_LENGTH; ++z) {
		for (uint32_t y = 0; y < CHUNK_HEIGHT; ++y) {
			const bool westAir =
				ac::blockType(west->getBlock(CHUNK_WIDTH - 1, y, z)) == palette.AIR_BLOCK;
			const bool eastAir =
				ac::blockType(east->getBlock(0, y, z)) == palette.AIR_BLOCK;
			boundaryOccupancyChanges += westAir != eastAir;
		}
	}

	// Adjacent columns may differ around slopes and caves, but a generator that
	// restarts its field per chunk exposes long vertical walls at this boundary.
	if (boundaryOccupancyChanges > CHUNK_LENGTH * 12u) {
		std::cerr << "terrain field restarted at a chunk boundary\n";
		return 1;
	}
	std::cout << "terrain seam checks passed\n";
	return 0;
}
