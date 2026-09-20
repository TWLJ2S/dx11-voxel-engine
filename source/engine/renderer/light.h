#pragma once
#include <DirectXMath.h>
#include <cstdint>
#include <vector>
#include <array>
#include "buffer.h"

namespace ac {
	enum GPU_LIGHT_TYPE : uint32_t {
		GPU_LIGHT_POINT = 0,
		GPU_LIGHT_AREA_FACE = 1,
		GPU_LIGHT_MESH = 2,
		// Camera-centred cubemap used only to shadow the global celestial light.
		GPU_LIGHT_DIRECTIONAL_PROXY = 3
	};

    struct gpuLight {
        DirectX::XMFLOAT3 position;
        float radius;
        DirectX::XMFLOAT3 color;
        float intensity;
		DirectX::XMFLOAT3 halfExtent = { 0, 0, 0 };
		uint32_t type = GPU_LIGHT_POINT;
    };

    struct lightBufferData {
        uint32_t count;
        DirectX::XMFLOAT3 padding;
    };

    // A packed shadow mask centred on the moving light. Keeping this as a bit
    // field makes both the upload and the per-pixel lookup inexpensive.
    constexpr uint32_t LIGHT_OCCLUSION_SIZE = 40;
    constexpr uint32_t LIGHT_OCCLUSION_LIGHTS = 64;
    constexpr uint32_t LIGHT_OCCLUSION_VOXELS =
        LIGHT_OCCLUSION_SIZE * LIGHT_OCCLUSION_SIZE * LIGHT_OCCLUSION_SIZE;
    constexpr uint32_t LIGHT_OCCLUSION_WORDS =
        (LIGHT_OCCLUSION_VOXELS + 31u) / 32u;
	constexpr uint32_t LIGHT_OCCLUSION_TOTAL_WORDS =
		LIGHT_OCCLUSION_WORDS * LIGHT_OCCLUSION_LIGHTS;
	// 262k entries still comfortably hold the propagated light field while
	// avoiding three separate 16 MB allocations/uploads during startup.
	constexpr uint32_t VOXEL_LIGHT_TABLE_SIZE = 1u << 18;
	constexpr uint32_t CHUNK_LIGHT_INDEX_GROUPS = 16;

	struct gpuVoxelLight {
		DirectX::XMINT3 position{};
		uint32_t packedColor = 0;
	};

	struct voxelLightBufferData {
		uint32_t tableMask = VOXEL_LIGHT_TABLE_SIZE - 1u;
		uint32_t enabled = 0;
		uint32_t quality = 1;
		uint32_t waterMaterial = UINT32_MAX;
		uint32_t worldSeed = 0;
		DirectX::XMFLOAT3 padding{};
	};

	struct daylightBufferData {
		DirectX::XMFLOAT3 sunDirection{ 0.0f, 1.0f, 0.0f };
		float sunIntensity = 1.0f;
		DirectX::XMFLOAT3 sunColor{ 1.0f, 0.96f, 0.84f };
		float ambientIntensity = 0.55f;
		DirectX::XMFLOAT3 skyColor{ 0.25f, 0.45f, 0.72f };
		float dayPhase = 0.25f;
		DirectX::XMFLOAT3 moonDirection{ 0.0f, -1.0f, 0.0f };
		float moonIntensity = 0.0f;
		DirectX::XMFLOAT3 moonColor{ 0.42f, 0.55f, 0.78f };
		uint32_t celestialLightIndex = 0u;
		float cloudCoverage = 0.36f;
		float cloudDensity = 0.72f;
		float cloudShadowStrength = 0.30f;
		float daylightPadding = 0.0f;
	};

	struct chunkLightBufferData {
		uint32_t count = 0;
		DirectX::XMUINT3 padding{};
		std::array<DirectX::XMUINT4, CHUNK_LIGHT_INDEX_GROUPS> indices{};
	};

    struct lightOcclusionBufferData {
		std::array<DirectX::XMINT4, LIGHT_OCCLUSION_LIGHTS> origins{};
		uint32_t size = LIGHT_OCCLUSION_SIZE;
		DirectX::XMINT3 padding{};
    };

    struct lightManager {

        ac::structuredBuffer<gpuLight> buffer;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;

        std::vector<gpuLight> lights;

        void init(ID3D11Device* device);
        void bind(ID3D11DeviceContext* context);
    };

}
