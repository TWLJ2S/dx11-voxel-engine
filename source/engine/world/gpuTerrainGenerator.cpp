#include "gpuTerrainGenerator.h"

namespace ac {
	void gpuTerrainGenerator::create(
		ID3D11Device* device,
		const terrainBlockPalette& blocks,
		const terrainBiomeConfig& biomes) {
		(void)device;
		_terrainBlocks = blocks;
		_terrainBiomeConfig = biomes;
	}

	std::shared_ptr<const chunk> gpuTerrainGenerator::generate(
		ID3D11DeviceContext* context,
		const DirectX::XMINT3& position,
		uint64_t seed) {
		// Chunk persistence does not record which generation backend produced a
		// chunk. The compute shader historically used a different 32-bit hash,
		// noise implementation, and sampling grid from terrainGenerator, so
		// changing the GPU Terrain setting placed unrelated terrain beside saved
		// chunks. Keep one canonical, runtime-data-driven world function for both
		// entry points; the legacy fixed-layout compute path is intentionally not
		// created.
		(void)context;
		terrainGenerator canonical(seed, _terrainBlocks, _terrainBiomeConfig);
		auto result = std::make_shared<chunk>();
		result->_position = position;
		canonical.generate(*result);
		return result;
	}
}
