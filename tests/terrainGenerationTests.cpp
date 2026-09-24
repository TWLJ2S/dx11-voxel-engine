#define NOMINMAX
#include <world/minecraftTerrainGeneration.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>

namespace {
    bool equalChunks(const ac::chunk& a, const ac::chunk& b) {
        for (uint32_t y = 0; y < CHUNK_HEIGHT; ++y)
            for (uint32_t z = 0; z < CHUNK_LENGTH; ++z)
                for (uint32_t x = 0; x < CHUNK_WIDTH; ++x)
                    if (a.getBlock(x, y, z) != b.getBlock(x, y, z))
                        return false;
        return true;
    }
}

int main() {
    constexpr uint64_t seed = 123456789ULL;
    ac::staticAssetManager terrainDefinitions;
    terrainDefinitions.load("assets/block");
    const ac::terrainBlockPalette palette = ac::terrainBlockPalette::load(
        "assets/terrain/overworld.json", terrainDefinitions);
    const ac::terrainBiomeConfig biomes = ac::terrainBiomeConfig::load(
        "assets/terrain/biomes.json", terrainDefinitions);
    ac::terrainGenerator generator(seed, palette, biomes);
    ac::terrainGenerator otherGenerator(seed + 1, palette, biomes);
    ac::chunk first;
    ac::chunk repeated;
    ac::chunk different;
    first._position = { 0, 0, 0 };
    repeated._position = first._position;
    different._position = first._position;

    const auto start = std::chrono::steady_clock::now();
    generator.generate(first);
    generator.generate(repeated);
    otherGenerator.generate(different);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start
    );

    if (!equalChunks(first, repeated)) {
        std::cerr << "same seed and coordinates produced different chunks\n";
        return 1;
    }
    if (equalChunks(first, different)) {
        std::cerr << "different seeds produced identical chunks\n";
        return 2;
    }

    std::array<uint64_t, 4096> counts{};
    uint64_t undergroundVoid = 0;
    for (int32_t chunkZ = -1; chunkZ <= 1; ++chunkZ) {
        for (int32_t chunkX = -1; chunkX <= 1; ++chunkX) {
            ac::chunk generated;
            generated._position = { chunkX, 0, chunkZ };
            generator.generate(generated);

            for (uint32_t z = 0; z < CHUNK_LENGTH; ++z) {
                for (uint32_t x = 0; x < CHUNK_WIDTH; ++x) {
                    if (generated.getBlock(x, 0, z) != palette.require("bedrock")) {
                        std::cerr << "bedrock floor is not sealed\n";
                        return 3;
                    }
                    for (uint32_t y = 0; y < CHUNK_HEIGHT; ++y) {
                        const ac::blockId type = ac::blockType(generated.getBlock(x, y, z));
                        if (type >= counts.size()) {
                            std::cerr << "generator emitted an unregistered block id\n";
                            return 4;
                        }
                        ++counts[type];
                    }

                    int32_t terrainTop = CHUNK_HEIGHT - 1;
                    while (terrainTop > 0) {
                        const ac::blockId type = ac::blockType(generated.getBlock(x, terrainTop, z));
                    if (type != palette.AIR_BLOCK && type != palette.require("water") &&
                            type != palette.require("oak_log") && type != palette.require("oak_leaves") &&
                            type != palette.require("spruce_log") && type != palette.require("spruce_leaves") &&
                            type != palette.require("birch_log") && type != palette.require("birch_leaves") &&
                            type != palette.require("jungle_log") && type != palette.require("jungle_leaves") &&
                            type != palette.require("acacia_log") && type != palette.require("acacia_leaves") &&
                            type != palette.require("dark_oak_log") && type != palette.require("dark_oak_leaves"))
                            break;
                        --terrainTop;
                    }
                    for (int32_t y = 8; y < terrainTop - 5; ++y) {
                        const ac::blockId type = ac::blockType(generated.getBlock(x, y, z));
                        if (type == palette.AIR_BLOCK || type == palette.require("water"))
                            ++undergroundVoid;
                    }
                }
            }
        }
    }

    std::cout << "air=" << counts[palette.AIR_BLOCK]
        << " stone=" << counts[palette.require("stone")]
        << " dirt=" << counts[palette.require("dirt")]
        << " grass=" << counts[palette.require("grass")]
        << " bedrock=" << counts[palette.require("bedrock")]
        << " water=" << counts[palette.require("water")]
        << " sand=" << counts[palette.require("sand")]
        << " snow=" << counts[palette.require("snow")]
        << " deepslate=" << counts[palette.require("deepslate")]
        << " gravel=" << counts[palette.require("gravel")]
        << " logs=" << counts[palette.require("oak_log")]
        << " leaves=" << counts[palette.require("oak_leaves")] << '\n';

    if (counts[palette.require("stone")] == 0 || counts[palette.require("bedrock")] == 0) {
        std::cerr << "core terrain layers were not generated\n";
        return 5;
    }
    if (undergroundVoid == 0) {
        std::cerr << "no underground caves were generated in the sample area\n";
        return 6;
    }

    ac::terrainBlockPalette remapped = palette;
    remapped.set("bedrock", palette.require("dirt"));
    ac::terrainGenerator remappedGenerator(seed, remapped, biomes);
    ac::chunk remappedChunk;
    remappedChunk._position = { 0, 0, 0 };
    remappedGenerator.generate(remappedChunk);
    if (remappedChunk.getBlock(0, 0, 0) != palette.require("dirt")) {
        std::cerr << "terrain generator ignored the configured block palette\n";
        return 7;
    }

	ac::modelManager models;
	auto cube = std::make_unique<ac::model>();
	const ac::modelData cubeData = ac::loadModelFromJson("assets/model/cube.json");
	cube->_vertex = cubeData._vertex;
	cube->_index = cubeData._index;
	models.load(std::move(cube), ac::MODEL_CUBE);
	models.load(std::make_unique<ac::model>(), ac::MODEL_DOOR);
	models.load(std::make_unique<ac::model>(), ac::MODEL_DOOR_OPEN);
	models.load(std::make_unique<ac::model>(), ac::MODEL_SLAB);
	models.load(std::make_unique<ac::model>(), ac::MODEL_STAIRS);
	models.load(std::make_unique<ac::model>(), ac::MODEL_STAIRS_OUTER);
	models.load(std::make_unique<ac::model>(), ac::MODEL_STAIRS_INNER);
	models.load(std::make_unique<ac::model>(), ac::MODEL_STAIRS_INNER_RIGHT);
	models.load(std::make_unique<ac::model>(), ac::MODEL_FENCE);
	models.load(std::make_unique<ac::model>(), ac::MODEL_FENCE_POST);
	models.load(std::make_unique<ac::model>(), ac::MODEL_FENCE_SIDE);
	models.load(std::make_unique<ac::model>(), ac::MODEL_PANE);
	models.load(std::make_unique<ac::model>(), ac::MODEL_PANE_POST);
	models.load(std::make_unique<ac::model>(), ac::MODEL_PANE_SIDE);
	ac::staticAssetManager blockDefinitions;
	blockDefinitions.loadValidated("assets/block", models);

	const auto meshStart = std::chrono::steady_clock::now();
	uint64_t meshIndices = 0;
	uint64_t generationNanoseconds = 0;
	uint64_t cacheNanoseconds = 0;
	uint64_t faceNanoseconds = 0;
	for (int32_t z = -1; z <= 1; ++z) {
		for (int32_t x = -1; x <= 1; ++x) {
			ac::chunk generated;
			generated._position = { x, 0, z };
			const auto generationStart = std::chrono::steady_clock::now();
			generator.generate(generated);
			generationNanoseconds += static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - generationStart
				).count()
			);
			ac::chunkMesher mesher;
			ac::chunkMeshTimings timings;
			const ac::chunkMesh mesh = mesher.generate(
				generated, blockDefinitions, models,
				nullptr, nullptr, nullptr, nullptr,
				nullptr, nullptr, nullptr, nullptr,
				&timings
			);
			cacheNanoseconds += timings.cacheNanoseconds;
			faceNanoseconds += timings.faceNanoseconds;
			meshIndices += mesh._opaque._index.size() + mesh._translucent._index.size();
		}
	}
	const auto meshElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - meshStart
	);
	if (meshIndices == 0) {
		std::cerr << "terrain mesher produced no geometry\n";
		return 8;
	}

    std::cout << "terrain generation checks passed in " << elapsed.count() << " ms\n";
	std::cout << "nine chunks generated and meshed in " << meshElapsed.count() << " ms\n";
	std::cout << "stage totals: generation=" << generationNanoseconds / 1'000'000.0
		<< " ms cache=" << cacheNanoseconds / 1'000'000.0
		<< " ms faces=" << faceNanoseconds / 1'000'000.0 << " ms\n";
    return 0;
}
