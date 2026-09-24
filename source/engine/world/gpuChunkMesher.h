#pragma once

#include "chunk.h"
#include "redstonePhysics.h"
#include <core/shader.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <bit>
#include <algorithm>

namespace ac {

	struct gpuVisibleFace {
		uint32_t packedPosition = 0;
		uint32_t packedSize = 0;
		uint32_t material = 0;
		uint32_t ao = 0xffu;
		float opacity = 1.0f;
	};

	struct gpuChunkFaceCounts {
		uint32_t opaque = 0;
		uint32_t translucent = 0;
	};

	struct gpuChunkFaceBuffer {
		Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
		Microsoft::WRL::ComPtr<ID3D11Buffer> indirectArgs;
		uint32_t count = 0;
		uint32_t capacity = 0;
	};

	struct gpuChunkFaceMesh {
		std::array<gpuChunkFaceBuffer, 2> faces;

		uint32_t count(bool translucent) const { return faces[translucent ? 1 : 0].count; }

		void draw(ID3D11DeviceContext* context, bool translucent) const {
			const gpuChunkFaceBuffer& target = faces[translucent ? 1 : 0];
			if (!target.view || !target.indirectArgs) return;
			ID3D11ShaderResourceView* resource = target.view.Get();
			context->VSSetShaderResources(66, 1, &resource);
			context->DrawInstancedIndirect(target.indirectArgs.Get(), 0);
		}
	};

	struct gpuChunkVoxelBuffer {
		Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
	};

	class gpuChunkMesherPrototype {
		static constexpr UINT PADDED_WIDTH = CHUNK_WIDTH + 2;
		static constexpr UINT PADDED_LENGTH = CHUNK_LENGTH + 2;
		static constexpr UINT PADDED_HEIGHT = CHUNK_HEIGHT + 2;
		static constexpr UINT PADDED_VOLUME = PADDED_WIDTH * PADDED_LENGTH * PADDED_HEIGHT;
		static constexpr UINT MAX_BLOCK_TYPES = 4096;
		static constexpr UINT MAX_VISIBLE_FACES = CHUNK_VOLUME * 6;
		static constexpr uint32_t DEFAULT_FACE_CAPACITY = 98304u;

		struct gpuBlockInfo {
			uint32_t flags = 0;
			std::array<uint32_t, BLOCK_FACE_COUNT> materials{};
			float opacity = 1.0f;
		};

		Microsoft::WRL::ComPtr<ID3D11ComputeShader> _shader;
		Microsoft::WRL::ComPtr<ID3D11ComputeShader> _haloShader;
		Microsoft::WRL::ComPtr<ID3D11Buffer> _voxels;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _voxelView;
		Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> _voxelUav;
		Microsoft::WRL::ComPtr<ID3D11Buffer> _blocks;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _blockView;
		std::array<Microsoft::WRL::ComPtr<ID3D11Buffer>, 2> _faces;
		std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, 2> _faceViews;
		std::array<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>, 2> _faceUavs;
		std::array<Microsoft::WRL::ComPtr<ID3D11Buffer>, 2> _indirectArgs;
		Microsoft::WRL::ComPtr<ID3D11Buffer> _countReadback;
		Microsoft::WRL::ComPtr<ID3D11Buffer> _mesherConstants;
		std::vector<uint32_t> _paddedVoxels = std::vector<uint32_t>(PADDED_VOLUME);

		static size_t paddedIndex(int32_t x, int32_t y, int32_t z) {
			return static_cast<size_t>(x + 1) + PADDED_WIDTH *
				(static_cast<size_t>(z + 1) + PADDED_LENGTH * static_cast<size_t>(y + 1));
		}

		static void createStructuredInput(
			ID3D11Device* device, UINT count, UINT stride,
			ID3D11Buffer** buffer, ID3D11ShaderResourceView** view
		) {
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = count * stride;
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			desc.StructureByteStride = stride;
			if (FAILED(device->CreateBuffer(&desc, nullptr, buffer)))
				throw std::runtime_error("Failed creating GPU chunk-mesher input");

			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			srv.Format = DXGI_FORMAT_UNKNOWN;
			srv.Buffer.NumElements = count;
			if (FAILED(device->CreateShaderResourceView(*buffer, &srv, view)))
				throw std::runtime_error("Failed creating GPU chunk-mesher SRV");
		}

		static void updateDynamic(ID3D11DeviceContext* context, ID3D11Buffer* buffer, const void* data, size_t size) {
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (FAILED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
				throw std::runtime_error("Failed mapping GPU chunk-mesher input");
			std::memcpy(mapped.pData, data, size);
			context->Unmap(buffer, 0);
		}

		static void ensureFaceBuffer(ID3D11Device* device, gpuChunkFaceBuffer& target, uint32_t count) {
			target.count = count;
			if (count <= target.capacity && target.buffer && target.view) return;

			const uint32_t requested = std::max<uint32_t>(
				DEFAULT_FACE_CAPACITY,
				count > DEFAULT_FACE_CAPACITY
					? std::min<uint32_t>(MAX_VISIBLE_FACES, std::bit_ceil(count))
					: DEFAULT_FACE_CAPACITY);
			target.capacity = std::min<uint32_t>(MAX_VISIBLE_FACES, requested);
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = target.capacity * sizeof(gpuVisibleFace);
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			desc.StructureByteStride = sizeof(gpuVisibleFace);
			target.buffer.Reset();
			target.view.Reset();
			if (FAILED(device->CreateBuffer(&desc, nullptr, &target.buffer)))
				throw std::runtime_error("Failed creating persistent GPU chunk-face buffer");

			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			srv.Format = DXGI_FORMAT_UNKNOWN;
			srv.Buffer.NumElements = target.capacity;
			if (FAILED(device->CreateShaderResourceView(target.buffer.Get(), &srv, &target.view)))
				throw std::runtime_error("Failed creating persistent GPU chunk-face view");
		}

		static void updateIndirectArgs(ID3D11Device* device, ID3D11DeviceContext* context, gpuChunkFaceBuffer& target) {
			const uint32_t args[4] = { 6u, target.count, 0u, 0u };
			if (!target.indirectArgs) {
				D3D11_BUFFER_DESC desc{};
				desc.ByteWidth = sizeof(args);
				desc.Usage = D3D11_USAGE_DEFAULT;
				desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
				D3D11_SUBRESOURCE_DATA initial{ args };
				if (FAILED(device->CreateBuffer(&desc, &initial, &target.indirectArgs)))
					throw std::runtime_error("Failed creating persistent indirect arguments");
			}
			else {
				context->UpdateSubresource(target.indirectArgs.Get(), 0, nullptr, args, 0, 0);
			}
		}

	public:
		void reloadDefinitions(
			ID3D11DeviceContext* context,
			const staticAssetManager& definitions
		) {
			if (!_blocks || !context) return;
			std::array<gpuBlockInfo, MAX_BLOCK_TYPES> blockData{};
			for (uint32_t id = 1; id < MAX_BLOCK_TYPES; ++id) {
				const blockDefinition* definition = definitions.get(id);
				if (!definition) continue;
				gpuBlockInfo& output = blockData[id];
				output.flags = 1u;
				if (definition->_occludes) output.flags |= 2u;
				if (definition->_renderMode == RENDER_MODE_TRANSPARENT) output.flags |= 4u;
				if (definition->_model == MODEL_CUBE) output.flags |= 8u;
				if (definition->_renderMode == RENDER_MODE_CUTOUT) output.flags |= 16u;
				if (definition->_behavior.redstone == blockRedstoneBehavior::lamp) output.flags |= 32u;
				for (uint32_t face = 0; face < BLOCK_FACE_COUNT; ++face)
					output.materials[face] = definition->materialForFace(face);
				output.opacity = definition->_opacity;
			}
			updateDynamic(context, _blocks.Get(), blockData.data(), sizeof(blockData));
		}

		void create(ID3D11Device* device, const staticAssetManager& definitions) {
			const auto bytecode = compileShaderBytecode(
				L"assets/shader/chunkFaceMesher.hlsl", "main", "cs_5_0");
			if (FAILED(device->CreateComputeShader(
				bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &_shader
			))) throw std::runtime_error("Failed creating GPU chunk-mesher shader");

			{
				const auto haloCode = compileShaderBytecode(
					L"assets/shader/chunkVoxelHalo.hlsl", "main", "cs_5_0");
				if (FAILED(device->CreateComputeShader(haloCode->GetBufferPointer(), haloCode->GetBufferSize(), nullptr, &_haloShader)))
					throw std::runtime_error("Failed creating chunk halo shader");
			}
			{
				D3D11_BUFFER_DESC desc{}; desc.ByteWidth=PADDED_VOLUME*sizeof(uint32_t); desc.Usage=D3D11_USAGE_DEFAULT;
				desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS; desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; desc.StructureByteStride=sizeof(uint32_t);
				if(FAILED(device->CreateBuffer(&desc,nullptr,&_voxels))) throw std::runtime_error("Failed creating GPU halo buffer");
				D3D11_SHADER_RESOURCE_VIEW_DESC srv{}; srv.ViewDimension=D3D11_SRV_DIMENSION_BUFFER; srv.Format=DXGI_FORMAT_UNKNOWN; srv.Buffer.NumElements=PADDED_VOLUME;
				if(FAILED(device->CreateShaderResourceView(_voxels.Get(),&srv,&_voxelView))) throw std::runtime_error("Failed creating GPU halo SRV");
				D3D11_UNORDERED_ACCESS_VIEW_DESC uav{}; uav.ViewDimension=D3D11_UAV_DIMENSION_BUFFER; uav.Format=DXGI_FORMAT_UNKNOWN; uav.Buffer.NumElements=PADDED_VOLUME;
				if(FAILED(device->CreateUnorderedAccessView(_voxels.Get(),&uav,&_voxelUav))) throw std::runtime_error("Failed creating GPU halo UAV");
			}
			createStructuredInput(device, MAX_BLOCK_TYPES, sizeof(gpuBlockInfo), &_blocks, &_blockView);

			Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
			device->GetImmediateContext(&context);
			reloadDefinitions(context.Get(), definitions);

			for (size_t i = 0; i < _faces.size(); ++i) {
				D3D11_BUFFER_DESC faceDesc{};
				faceDesc.ByteWidth = MAX_VISIBLE_FACES * sizeof(gpuVisibleFace);
				faceDesc.Usage = D3D11_USAGE_DEFAULT;
				faceDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
				faceDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
				faceDesc.StructureByteStride = sizeof(gpuVisibleFace);
				if (FAILED(device->CreateBuffer(&faceDesc, nullptr, &_faces[i])))
					throw std::runtime_error("Failed creating GPU visible-face buffer");

				D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
				srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
				srv.Format = DXGI_FORMAT_UNKNOWN;
				srv.Buffer.NumElements = MAX_VISIBLE_FACES;
				if (FAILED(device->CreateShaderResourceView(_faces[i].Get(), &srv, &_faceViews[i])))
					throw std::runtime_error("Failed creating GPU visible-face SRV");

				D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
				uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
				uav.Format = DXGI_FORMAT_UNKNOWN;
				uav.Buffer.NumElements = MAX_VISIBLE_FACES;
				uav.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
				if (FAILED(device->CreateUnorderedAccessView(_faces[i].Get(), &uav, &_faceUavs[i])))
					throw std::runtime_error("Failed creating GPU visible-face UAV");

				const uint32_t args[4] = { 6u, 0u, 0u, 0u };
				D3D11_BUFFER_DESC argsDesc{};
				argsDesc.ByteWidth = sizeof(args);
				argsDesc.Usage = D3D11_USAGE_DEFAULT;
				argsDesc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
				D3D11_SUBRESOURCE_DATA initialArgs{ args };
				if (FAILED(device->CreateBuffer(&argsDesc, &initialArgs, &_indirectArgs[i])))
					throw std::runtime_error("Failed creating chunk indirect-argument buffer");
			}

			D3D11_BUFFER_DESC readback{};
			readback.ByteWidth = sizeof(uint32_t) * 2;
			readback.Usage = D3D11_USAGE_STAGING;
			readback.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			if (FAILED(device->CreateBuffer(&readback, nullptr, &_countReadback)))
				throw std::runtime_error("Failed creating GPU face-count readback");

			D3D11_BUFFER_DESC constants{};
			constants.ByteWidth = 16;
			constants.Usage = D3D11_USAGE_DYNAMIC;
			constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			if (FAILED(device->CreateBuffer(&constants, nullptr, &_mesherConstants)))
				throw std::runtime_error("Failed creating GPU mesher constants");
		}

		static uint32_t occupiedSubchunkMask(const chunk& data) {
			uint32_t mask = 0u;
			for (uint32_t section = 0; section < CHUNK_SUBCHUNKS; ++section)
				if (!data.isSubchunkEmpty(section))
					mask |= 1u << section;
			return mask;
		}

		void dispatch(
			ID3D11DeviceContext* context,
			const chunk& center,
			const std::array<const chunk*, 8>& neighbors
		) {
			std::fill(_paddedVoxels.begin(), _paddedVoxels.end(), 0u);
			for (int32_t y = 0; y < CHUNK_HEIGHT; ++y) {
				for (int32_t z = -1; z <= CHUNK_LENGTH; ++z) {
					for (int32_t x = -1; x <= CHUNK_WIDTH; ++x) {
						const chunk* source = &center;
						int32_t localX = x;
						int32_t localZ = z;
						if (x < 0 && z < 0) { source = neighbors[4]; localX = CHUNK_WIDTH - 1; localZ = CHUNK_LENGTH - 1; }
						else if (x < 0 && z >= CHUNK_LENGTH) { source = neighbors[5]; localX = CHUNK_WIDTH - 1; localZ = 0; }
						else if (x >= CHUNK_WIDTH && z < 0) { source = neighbors[6]; localX = 0; localZ = CHUNK_LENGTH - 1; }
						else if (x >= CHUNK_WIDTH && z >= CHUNK_LENGTH) { source = neighbors[7]; localX = 0; localZ = 0; }
						else if (x < 0) { source = neighbors[0]; localX = CHUNK_WIDTH - 1; }
						else if (x >= CHUNK_WIDTH) { source = neighbors[1]; localX = 0; }
						else if (z < 0) { source = neighbors[2]; localZ = CHUNK_LENGTH - 1; }
						else if (z >= CHUNK_LENGTH) { source = neighbors[3]; localZ = 0; }
						if (source)
							_paddedVoxels[paddedIndex(x, y, z)] = source->getBlock(localX, y, localZ);
					}
				}
			}

			context->UpdateSubresource(_voxels.Get(), 0, nullptr, _paddedVoxels.data(), 0, 0);
			dispatchFaces(context, occupiedSubchunkMask(center));
		}

		void dispatchFaces(ID3D11DeviceContext* context, uint32_t occupiedSubchunks) {
			if (occupiedSubchunks == 0u) {
				const uint32_t emptyArgs[4] = { 6u, 0u, 0u, 0u };
				context->UpdateSubresource(_indirectArgs[0].Get(), 0, nullptr, emptyArgs, 0, 0);
				context->UpdateSubresource(_indirectArgs[1].Get(), 0, nullptr, emptyArgs, 0, 0);
				return;
			}
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (SUCCEEDED(context->Map(_mesherConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
				std::memcpy(mapped.pData, &occupiedSubchunks, sizeof(occupiedSubchunks));
				context->Unmap(_mesherConstants.Get(), 0);
			}
			ID3D11ShaderResourceView* inputs[] = { _voxelView.Get(), _blockView.Get() };
			ID3D11UnorderedAccessView* outputs[] = { _faceUavs[0].Get(), _faceUavs[1].Get() };
			const UINT initialCounts[] = { 0u, 0u };
			context->CSSetShader(_shader.Get(), nullptr, 0);
			context->CSSetConstantBuffers(0, 1, _mesherConstants.GetAddressOf());
			context->CSSetShaderResources(0, 2, inputs);
			context->CSSetUnorderedAccessViews(0, 2, outputs, initialCounts);
			context->Dispatch((CHUNK_HEIGHT + 15u) / 16u, 6, 1);

			ID3D11UnorderedAccessView* nullUavs[] = { nullptr, nullptr };
			ID3D11ShaderResourceView* nullSrvs[] = { nullptr, nullptr };
			context->CSSetUnorderedAccessViews(0, 2, nullUavs, nullptr);
			context->CSSetShaderResources(0, 2, nullSrvs);
			context->CSSetShader(nullptr, nullptr, 0);
			context->CopyStructureCount(_indirectArgs[0].Get(), sizeof(uint32_t), _faceUavs[0].Get());
			context->CopyStructureCount(_indirectArgs[1].Get(), sizeof(uint32_t), _faceUavs[1].Get());
		}

		static uint32_t packedVisualState(blockId state) {
			uint32_t visual = visualBlockState(state) & 65535u;
			// Lamps reuse the fluid nibble in the compact GPU voxel format.
			if (isRedstoneLamp(state))
				visual = (visual & BLOCK_TYPE_MASK) | (uint32_t(redstonePower(state)) << 12u);
			return visual;
		}

		void uploadVoxelBuffer(ID3D11Device* device, ID3D11DeviceContext* context, const chunk& data, gpuChunkVoxelBuffer& output) {
			constexpr uint32_t packedCount = (CHUNK_VOLUME + 1) / 2;
			std::vector<uint32_t> packed(packedCount, 0u);
			for(uint32_t y=0;y<CHUNK_HEIGHT;++y) for(uint32_t z=0;z<CHUNK_LENGTH;++z) for(uint32_t x=0;x<CHUNK_WIDTH;++x) {
				uint32_t index=x+CHUNK_WIDTH*(z+CHUNK_LENGTH*y);
				packed[index>>1] |= packedVisualState(data.getBlock(x,y,z)) << ((index&1u)*16u);
			}
			if(!output.buffer) {
				D3D11_BUFFER_DESC desc{}; desc.ByteWidth=packedCount*sizeof(uint32_t); desc.Usage=D3D11_USAGE_DEFAULT; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
				desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; desc.StructureByteStride=sizeof(uint32_t);
				D3D11_SUBRESOURCE_DATA initial{packed.data()};
				if(FAILED(device->CreateBuffer(&desc,&initial,&output.buffer))) throw std::runtime_error("Failed creating packed GPU chunk voxels");
				D3D11_SHADER_RESOURCE_VIEW_DESC srv{}; srv.ViewDimension=D3D11_SRV_DIMENSION_BUFFER; srv.Format=DXGI_FORMAT_UNKNOWN; srv.Buffer.NumElements=packedCount;
				if(FAILED(device->CreateShaderResourceView(output.buffer.Get(),&srv,&output.view))) throw std::runtime_error("Failed creating packed GPU voxel SRV");
			}
			else context->UpdateSubresource(output.buffer.Get(),0,nullptr,packed.data(),0,0);
		}

		void updateVoxel(
			ID3D11DeviceContext* context,
			const chunk& data,
			gpuChunkVoxelBuffer& output,
			uint32_t x,
			uint32_t y,
			uint32_t z
		) {
			if (!output.buffer) return;
			const uint32_t index = x + CHUNK_WIDTH * (z + CHUNK_LENGTH * y);
			const uint32_t first = index & ~1u;
			uint32_t packed = 0u;
			for (uint32_t offset = 0; offset < 2u && first + offset < CHUNK_VOLUME; ++offset) {
				const uint32_t linear = first + offset;
				const uint32_t localX = linear % CHUNK_WIDTH;
				const uint32_t yz = linear / CHUNK_WIDTH;
				const uint32_t localZ = yz % CHUNK_LENGTH;
				const uint32_t localY = yz / CHUNK_LENGTH;
				packed |= packedVisualState(data.getBlock(localX, localY, localZ)) << (offset * 16u);
			}
			D3D11_BOX destination{};
			destination.left = (first >> 1u) * sizeof(uint32_t);
			destination.right = destination.left + sizeof(uint32_t);
			destination.bottom = destination.back = 1u;
			context->UpdateSubresource(output.buffer.Get(), 0, &destination, &packed, 0, 0);
		}

		gpuChunkFaceCounts buildMeshGpu(
			ID3D11Device* device, ID3D11DeviceContext* context,
			const gpuChunkVoxelBuffer& center,
			const std::array<const gpuChunkVoxelBuffer*,8>& neighbors,
			gpuChunkFaceMesh& destination,
			uint32_t occupiedSubchunks
		) {
			ID3D11ShaderResourceView* sources[9] = { center.view.Get() };
			for(size_t i=0;i<neighbors.size();++i) sources[i+1]=neighbors[i]?neighbors[i]->view.Get():nullptr;
			ID3D11UnorderedAccessView* halo=_voxelUav.Get(); UINT keep=UINT(-1);
			context->CSSetShader(_haloShader.Get(),nullptr,0); context->CSSetShaderResources(0,9,sources); context->CSSetUnorderedAccessViews(0,1,&halo,&keep); context->Dispatch(3,(PADDED_HEIGHT + 7u) / 8u,3);
			ID3D11UnorderedAccessView* nullUav=nullptr; context->CSSetUnorderedAccessViews(0,1,&nullUav,nullptr);
			ID3D11ShaderResourceView* nullSources[9]={}; context->CSSetShaderResources(0,9,nullSources);
			dispatchFaces(context, occupiedSubchunks);
			for (size_t i = 0; i < destination.faces.size(); ++i) {
				auto& target = destination.faces[i];
				if (!target.buffer || !target.indirectArgs)
					ensureFaceBuffer(device, target, DEFAULT_FACE_CAPACITY);
				if (!target.indirectArgs)
					updateIndirectArgs(device, context, target);
				// A nonzero CPU marker means the GPU stream is initialized. The
				// authoritative instance count stays in the indirect-argument buffer.
				// Never Map a staging count here — that stalls the GPU on remesh.
				target.count = 1u;
				context->CopyStructureCount(target.indirectArgs.Get(), sizeof(uint32_t), _faceUavs[i].Get());
				D3D11_BOX box{ 0, 0, 0, target.capacity * static_cast<UINT>(sizeof(gpuVisibleFace)), 1, 1 };
				context->CopySubresourceRegion(target.buffer.Get(), 0, 0, 0, 0, _faces[i].Get(), 0, &box);
			}
			return { destination.faces[0].count, destination.faces[1].count };
		}

		gpuChunkFaceCounts readCounts(ID3D11DeviceContext* context) const {
			context->CopyStructureCount(_countReadback.Get(), 0, _faceUavs[0].Get());
			context->CopyStructureCount(_countReadback.Get(), sizeof(uint32_t), _faceUavs[1].Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (FAILED(context->Map(_countReadback.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
				throw std::runtime_error("Failed reading GPU face counts");
			const auto* counts = static_cast<const uint32_t*>(mapped.pData);
			const gpuChunkFaceCounts result{ counts[0], counts[1] };
			context->Unmap(_countReadback.Get(), 0);
			return result;
		}

		gpuChunkFaceCounts buildMesh(
			ID3D11Device* device,
			ID3D11DeviceContext* context,
			const chunk& center,
			const std::array<const chunk*, 8>& neighbors,
			gpuChunkFaceMesh& destination
		) {
			dispatch(context, center, neighbors);
			for (size_t i = 0; i < destination.faces.size(); ++i) {
				gpuChunkFaceBuffer& target = destination.faces[i];
				if (!target.buffer || !target.indirectArgs)
					ensureFaceBuffer(device, target, DEFAULT_FACE_CAPACITY);
				if (!target.indirectArgs)
					updateIndirectArgs(device, context, target);
				target.count = 1u;
				context->CopyStructureCount(target.indirectArgs.Get(), sizeof(uint32_t), _faceUavs[i].Get());
				D3D11_BOX source{};
				source.left = 0;
				source.right = target.capacity * static_cast<UINT>(sizeof(gpuVisibleFace));
				source.top = 0;
				source.bottom = 1;
				source.front = 0;
				source.back = 1;
				context->CopySubresourceRegion(
					target.buffer.Get(), 0, 0, 0, 0,
					_faces[i].Get(), 0, &source
				);
			}
			return { destination.faces[0].count, destination.faces[1].count };
		}

		ID3D11ShaderResourceView* faceView(bool translucent) const { return _faceViews[translucent ? 1 : 0].Get(); }
		ID3D11Buffer* indirectArgs(bool translucent) const { return _indirectArgs[translucent ? 1 : 0].Get(); }
	};

}
