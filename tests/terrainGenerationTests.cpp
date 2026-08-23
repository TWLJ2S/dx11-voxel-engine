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
    ac::terrainGenerator generator(seed);
    ac::terrainGenerator otherGenerator(seed + 1);
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

    std::array<uint64_t, 12> counts{};
    uint64_t undergroundVoid = 0;
    for (int32_t chunkZ = -1; chunkZ <= 1; ++chunkZ) {
        for (int32_t chunkX = -1; chunkX <= 1; ++chunkX) {
            ac::chunk generated;
            generated._position = { chunkX, 0, chunkZ };
            generator.generate(generated);

            for (uint32_t z = 0; z < CHUNK_LENGTH; ++z) {
                for (uint32_t x = 0; x < CHUNK_WIDTH; ++x) {
                    if (generated.getBlock(x, 0, z) != 4) {
                        std::cerr << "bedrock floor is not sealed\n";
                        return 3;
                    }
                    for (uint32_t y = 0; y < CHUNK_HEIGHT; ++y) {
                        const ac::blockId id = generated.getBlock(x, y, z);
                        if (id >= counts.size()) {
                            std::cerr << "generator emitted an unregistered block id\n";
                            return 4;
                        }
                        ++counts[id];
                    }

                    int32_t terrainTop = CHUNK_HEIGHT - 1;
                    while (terrainTop > 0) {
                        const ac::blockId id = generated.getBlock(x, terrainTop, z);
                        if (id != 0 && id != 5 && id != 10 && id != 11)
                            break;
                        --terrainTop;
                    }
                    for (int32_t y = 8; y < terrainTop - 5; ++y) {
                        const ac::blockId id = generated.getBlock(x, y, z);
                        if (id == 0 || id == 5)
                            ++undergroundVoid;
                    }
                }
            }
        }
    }

    std::cout << "air=" << counts[0]
        << " stone=" << counts[1]
        << " dirt=" << counts[2]
        << " grass=" << counts[3]
        << " bedrock=" << counts[4]
        << " water=" << counts[5]
        << " sand=" << counts[6]
        << " snow=" << counts[7]
        << " deepslate=" << counts[8]
        << " gravel=" << counts[9]
        << " logs=" << counts[10]
        << " leaves=" << counts[11] << '\n';

    if (counts[1] == 0 || counts[4] == 0) {
        std::cerr << "core terrain layers were not generated\n";
        return 5;
    }
    if (undergroundVoid == 0) {
        std::cerr << "no underground caves were generated in the sample area\n";
        return 6;
    }

	ac::modelManager models;
	auto cube = std::make_unique<ac::model>();
	const ac::modelData cubeData = ac::loadModelFromJson("asset/model/cube.json");
	cube->_vertex = cubeData._vertex;
	cube->_index = cubeData._index;
	models.load(std::move(cube), ac::MODEL_CUBE);
	ac::staticAssetManager blockDefinitions;
	blockDefinitions.loadValidated("asset/block", models);

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
		return 7;
	}

    std::cout << "terrain generation checks passed in " << elapsed.count() << " ms\n";
	std::cout << "nine chunks generated and meshed in " << meshElapsed.count() << " ms\n";
	std::cout << "stage totals: generation=" << generationNanoseconds / 1'000'000.0
		<< " ms cache=" << cacheNanoseconds / 1'000'000.0
		<< " ms faces=" << faceNanoseconds / 1'000'000.0 << " ms\n";
    return 0;
}
