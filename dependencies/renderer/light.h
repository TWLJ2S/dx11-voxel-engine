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
		GPU_LIGHT_MESH = 2
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
        dx::XMFLOAT3 padding;
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
	constexpr uint32_t VOXEL_LIGHT_TABLE_SIZE = 1u << 20;
	constexpr uint32_t CHUNK_LIGHT_INDEX_GROUPS = 16;

	struct gpuVoxelLight {
		DirectX::XMINT3 position{};
		uint32_t packedColor = 0;
	};

	struct voxelLightBufferData {
		uint32_t tableMask = VOXEL_LIGHT_TABLE_SIZE - 1u;
		uint32_t enabled = 0;
		uint32_t quality = 1;
		uint32_t padding = 0;
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

        void init(ID3D11Device* device)
        {
            buffer.create(device, 4096);


            D3D11_SHADER_RESOURCE_VIEW_DESC desc{};

            desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
            desc.BufferEx.NumElements = 4096;


            DX_CHECK(device->CreateShaderResourceView(buffer._buffer.Get(), &desc, srv.GetAddressOf()));
        }


        void bind(ID3D11DeviceContext* context) {
            context->PSSetShaderResources(
                10,
                1,
                srv.GetAddressOf()
            );
        }
    };

}
