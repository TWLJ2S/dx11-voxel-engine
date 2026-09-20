#pragma once

#include "chunk.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <memory>
#include <stdexcept>
#include <cstring>

namespace ac {

	class gpuTerrainGenerator {
		struct constants {
			DirectX::XMINT2 chunkOrigin{};
			uint32_t seed = 0;
			uint32_t padding = 0;
		};
		struct columnData {
			float surfaceHeight, mountainWeight, oceanWeight, riverWeight;
			float aquiferLevel, temperature, moisture;
			uint32_t biome;
		};

		std::array<Microsoft::WRL::ComPtr<ID3D11ComputeShader>, 3> _shaders;
		Microsoft::WRL::ComPtr<ID3D11Buffer> _constants;
		Microsoft::WRL::ComPtr<ID3D11Buffer> _columns;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _columnSrv;
		Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> _columnUav;
		Microsoft::WRL::ComPtr<ID3D11Buffer> _blocks;
		Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> _blockUav;
		Microsoft::WRL::ComPtr<ID3D11Buffer> _readback;

		static Microsoft::WRL::ComPtr<ID3DBlob> compile(const char* entry) {
			Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
			UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
			flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
			if (FAILED(D3DCompileFromFile(L"asset/shader/gpuTerrainGeneration.hlsl", nullptr,
				D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, "cs_5_0", flags, 0, &code, &errors))) {
				std::string message = errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown error";
				throw std::runtime_error("GPU terrain shader failed: " + message);
			}
			return code;
		}

	public:
		void create(ID3D11Device* device) {
			const char* entries[3] = { "generateColumns", "generateBlocks", "placeTrees" };
			for (size_t i=0;i<3;++i) {
				auto code=compile(entries[i]);
				if (FAILED(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&_shaders[i])))
					throw std::runtime_error("Failed creating GPU terrain compute shader");
			}
			D3D11_BUFFER_DESC cb{}; cb.ByteWidth=sizeof(constants); cb.Usage=D3D11_USAGE_DYNAMIC;
			cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER; cb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
			if (FAILED(device->CreateBuffer(&cb,nullptr,&_constants))) throw std::runtime_error("GPU terrain constants failed");

			auto structured=[&](UINT count,UINT stride,UINT bind,ID3D11Buffer** out) {
				D3D11_BUFFER_DESC d{}; d.ByteWidth=count*stride; d.Usage=D3D11_USAGE_DEFAULT; d.BindFlags=bind;
				d.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; d.StructureByteStride=stride;
				if(FAILED(device->CreateBuffer(&d,nullptr,out))) throw std::runtime_error("GPU terrain buffer failed");
			};
			structured(256,sizeof(columnData),D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS,&_columns);
			structured(CHUNK_VOLUME,sizeof(uint32_t),D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE,&_blocks);
			D3D11_SHADER_RESOURCE_VIEW_DESC srv{}; srv.ViewDimension=D3D11_SRV_DIMENSION_BUFFER; srv.Format=DXGI_FORMAT_UNKNOWN; srv.Buffer.NumElements=256;
			if(FAILED(device->CreateShaderResourceView(_columns.Get(),&srv,&_columnSrv))) throw std::runtime_error("GPU terrain column SRV failed");
			D3D11_UNORDERED_ACCESS_VIEW_DESC uav{}; uav.ViewDimension=D3D11_UAV_DIMENSION_BUFFER; uav.Format=DXGI_FORMAT_UNKNOWN; uav.Buffer.NumElements=256;
			if(FAILED(device->CreateUnorderedAccessView(_columns.Get(),&uav,&_columnUav))) throw std::runtime_error("GPU terrain column UAV failed");
			uav.Buffer.NumElements=CHUNK_VOLUME;
			if(FAILED(device->CreateUnorderedAccessView(_blocks.Get(),&uav,&_blockUav))) throw std::runtime_error("GPU terrain block UAV failed");
			D3D11_BUFFER_DESC staging{}; staging.ByteWidth=CHUNK_VOLUME*sizeof(uint32_t); staging.Usage=D3D11_USAGE_STAGING; staging.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
			if(FAILED(device->CreateBuffer(&staging,nullptr,&_readback))) throw std::runtime_error("GPU terrain readback failed");
		}

		std::shared_ptr<const chunk> generate(ID3D11DeviceContext* context,const DirectX::XMINT3& position,uint64_t seed) {
			constants data{{position.x*CHUNK_WIDTH,position.z*CHUNK_LENGTH},static_cast<uint32_t>(seed),0};
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if(FAILED(context->Map(_constants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) throw std::runtime_error("GPU terrain constant map failed");
			std::memcpy(mapped.pData,&data,sizeof(data)); context->Unmap(_constants.Get(),0);
			ID3D11Buffer* cb=_constants.Get(); context->CSSetConstantBuffers(0,1,&cb);

			ID3D11UnorderedAccessView* columnUav=_columnUav.Get(); UINT keep=UINT(-1);
			context->CSSetShader(_shaders[0].Get(),nullptr,0); context->CSSetUnorderedAccessViews(0,1,&columnUav,&keep); context->Dispatch(2,2,1);
			ID3D11UnorderedAccessView* nullUav=nullptr; context->CSSetUnorderedAccessViews(0,1,&nullUav,nullptr);
			ID3D11ShaderResourceView* columnSrv=_columnSrv.Get(); ID3D11UnorderedAccessView* blockUav=_blockUav.Get();
			context->CSSetShaderResources(0,1,&columnSrv); context->CSSetUnorderedAccessViews(0,1,&blockUav,&keep);
			context->CSSetShader(_shaders[1].Get(),nullptr,0); context->Dispatch(2,32,2);
			context->CSSetShader(_shaders[2].Get(),nullptr,0); context->Dispatch(4,1,4);
			context->CSSetUnorderedAccessViews(0,1,&nullUav,nullptr); ID3D11ShaderResourceView* nullSrv=nullptr; context->CSSetShaderResources(0,1,&nullSrv); context->CSSetShader(nullptr,nullptr,0);

			context->CopyResource(_readback.Get(),_blocks.Get());
			if(FAILED(context->Map(_readback.Get(),0,D3D11_MAP_READ,0,&mapped))) throw std::runtime_error("GPU terrain readback map failed");
			std::array<blockId,CHUNK_VOLUME> dense; std::memcpy(dense.data(),mapped.pData,sizeof(dense)); context->Unmap(_readback.Get(),0);
			auto result=std::make_shared<chunk>(); result->_position=position; result->buildFromDense(dense); result->_dirty=true;
			return result;
		}
	};
}
