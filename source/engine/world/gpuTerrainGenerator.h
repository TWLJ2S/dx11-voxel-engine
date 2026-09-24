#pragma once

#include "minecraftTerrainGeneration.h"
#include <d3d11.h>
#include <memory>

namespace ac {

	class gpuTerrainGenerator {
		terrainBlockPalette _terrainBlocks{};
		terrainBiomeConfig _terrainBiomeConfig{};

	public:
		void create(
			ID3D11Device* device,
			const terrainBlockPalette& blocks,
			const terrainBiomeConfig& biomes);
		std::shared_ptr<const chunk> generate(
			ID3D11DeviceContext* context,
			const DirectX::XMINT3& position,
			uint64_t seed);
	};
}
