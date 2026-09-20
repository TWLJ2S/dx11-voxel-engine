#pragma once

#include <core/debug.h>
#include <core/shader.h>
#include <core/texture.h>
#include <renderer/buffer.h>

#include <DirectXMath.h>
#include <DirectXTex/DirectXTex.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace ac {

	class blockBreakOverlayRenderer {
	public:
		static constexpr int STAGE_COUNT = 10;

		void create(ID3D11Device* device) {
			_program.initVertexShader(device, L"assets/shader/blockBreakOverlay.hlsl", "vertexMain", "vs_5_0");
			_program.initPixelShader(device, L"assets/shader/blockBreakOverlay.hlsl", "pixelMain", "ps_5_0");
			_program.initInputLayout(device, {
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
			});
			_constantBuffer.create(device);

			struct vertex { DirectX::XMFLOAT3 position; DirectX::XMFLOAT2 uv; };
			// Cube with UVs on every face (same UV layout as a break overlay).
			const std::array<vertex, 24> vertices = {{
				{{0,0,0},{0,1}}, {{1,0,0},{1,1}}, {{1,1,0},{1,0}}, {{0,1,0},{0,0}}, // -Z
				{{1,0,1},{0,1}}, {{0,0,1},{1,1}}, {{0,1,1},{1,0}}, {{1,1,1},{0,0}}, // +Z
				{{0,0,1},{0,1}}, {{0,0,0},{1,1}}, {{0,1,0},{1,0}}, {{0,1,1},{0,0}}, // -X
				{{1,0,0},{0,1}}, {{1,0,1},{1,1}}, {{1,1,1},{1,0}}, {{1,1,0},{0,0}}, // +X
				{{0,0,1},{0,1}}, {{1,0,1},{1,1}}, {{1,0,0},{1,0}}, {{0,0,0},{0,0}}, // -Y
				{{0,1,0},{0,1}}, {{1,1,0},{1,1}}, {{1,1,1},{1,0}}, {{0,1,1},{0,0}}, // +Y
			}};
			const std::array<uint16_t, 36> indices = {{
				0,1,2, 0,2,3,
				4,5,6, 4,6,7,
				8,9,10, 8,10,11,
				12,13,14, 12,14,15,
				16,17,18, 16,18,19,
				20,21,22, 20,22,23
			}};

			D3D11_BUFFER_DESC vb{};
			vb.ByteWidth = sizeof(vertices);
			vb.Usage = D3D11_USAGE_IMMUTABLE;
			vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			D3D11_SUBRESOURCE_DATA vdata{ vertices.data() };
			DX_CHECK(device->CreateBuffer(&vb, &vdata, _vertexBuffer.GetAddressOf()));

			D3D11_BUFFER_DESC ib{};
			ib.ByteWidth = sizeof(indices);
			ib.Usage = D3D11_USAGE_IMMUTABLE;
			ib.BindFlags = D3D11_BIND_INDEX_BUFFER;
			D3D11_SUBRESOURCE_DATA idata{ indices.data() };
			DX_CHECK(device->CreateBuffer(&ib, &idata, _indexBuffer.GetAddressOf()));

			D3D11_BLEND_DESC blend{};
			blend.RenderTarget[0].BlendEnable = TRUE;
			blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
			blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
			blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
			blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
			blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			DX_CHECK(device->CreateBlendState(&blend, _blendState.GetAddressOf()));

			D3D11_DEPTH_STENCIL_DESC depth{};
			depth.DepthEnable = TRUE;
			depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
			DX_CHECK(device->CreateDepthStencilState(&depth, _depthState.GetAddressOf()));

			D3D11_RASTERIZER_DESC rasterizer{};
			rasterizer.FillMode = D3D11_FILL_SOLID;
			rasterizer.CullMode = D3D11_CULL_NONE;
			rasterizer.DepthClipEnable = TRUE;
			DX_CHECK(device->CreateRasterizerState(&rasterizer, _rasterizerState.GetAddressOf()));

			D3D11_SAMPLER_DESC sampler{};
			sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler.MaxLOD = D3D11_FLOAT32_MAX;
			DX_CHECK(device->CreateSamplerState(&sampler, _sampler.GetAddressOf()));

			loadStages(device);
		}

		void render(
			ID3D11DeviceContext* context,
			const DirectX::XMINT3& block,
			const DirectX::XMFLOAT3& localMin,
			const DirectX::XMFLOAT3& localMax,
			float progress,
			const DirectX::XMFLOAT4X4& view,
			const DirectX::XMFLOAT4X4& projection
		) {
			if (!_srv) return;
			const int stage = std::clamp(static_cast<int>(progress * STAGE_COUNT), 0, STAGE_COUNT - 1);

			Microsoft::WRL::ComPtr<ID3D11InputLayout> oldLayout;
			Microsoft::WRL::ComPtr<ID3D11Buffer> oldVertexBuffer;
			Microsoft::WRL::ComPtr<ID3D11Buffer> oldIndexBuffer;
			Microsoft::WRL::ComPtr<ID3D11Buffer> oldConstantBuffer;
			Microsoft::WRL::ComPtr<ID3D11VertexShader> oldVertexShader;
			Microsoft::WRL::ComPtr<ID3D11PixelShader> oldPixelShader;
			Microsoft::WRL::ComPtr<ID3D11GeometryShader> oldGeometryShader;
			Microsoft::WRL::ComPtr<ID3D11HullShader> oldHullShader;
			Microsoft::WRL::ComPtr<ID3D11DomainShader> oldDomainShader;
			Microsoft::WRL::ComPtr<ID3D11BlendState> oldBlendState;
			Microsoft::WRL::ComPtr<ID3D11DepthStencilState> oldDepthState;
			Microsoft::WRL::ComPtr<ID3D11RasterizerState> oldRasterizerState;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> oldSrv;
			Microsoft::WRL::ComPtr<ID3D11SamplerState> oldSampler;
			D3D11_PRIMITIVE_TOPOLOGY oldTopology{};
			DXGI_FORMAT oldIndexFormat{};
			UINT oldVertexStride = 0;
			UINT oldVertexOffset = 0;
			UINT oldIndexOffset = 0;
			UINT oldSampleMask = 0;
			UINT oldStencilReference = 0;
			float oldBlendFactor[4]{};

			context->IAGetInputLayout(oldLayout.GetAddressOf());
			context->IAGetPrimitiveTopology(&oldTopology);
			context->IAGetVertexBuffers(0, 1, oldVertexBuffer.GetAddressOf(), &oldVertexStride, &oldVertexOffset);
			context->IAGetIndexBuffer(oldIndexBuffer.GetAddressOf(), &oldIndexFormat, &oldIndexOffset);
			context->VSGetConstantBuffers(0, 1, oldConstantBuffer.GetAddressOf());
			context->VSGetShader(oldVertexShader.GetAddressOf(), nullptr, nullptr);
			context->PSGetShader(oldPixelShader.GetAddressOf(), nullptr, nullptr);
			context->GSGetShader(oldGeometryShader.GetAddressOf(), nullptr, nullptr);
			context->HSGetShader(oldHullShader.GetAddressOf(), nullptr, nullptr);
			context->DSGetShader(oldDomainShader.GetAddressOf(), nullptr, nullptr);
			context->OMGetBlendState(oldBlendState.GetAddressOf(), oldBlendFactor, &oldSampleMask);
			context->OMGetDepthStencilState(oldDepthState.GetAddressOf(), &oldStencilReference);
			context->RSGetState(oldRasterizerState.GetAddressOf());
			context->PSGetShaderResources(0, 1, oldSrv.GetAddressOf());
			context->PSGetSamplers(0, 1, oldSampler.GetAddressOf());

			const float sx = (std::max)(0.001f, localMax.x - localMin.x);
			const float sy = (std::max)(0.001f, localMax.y - localMin.y);
			const float sz = (std::max)(0.001f, localMax.z - localMin.z);
			const DirectX::XMMATRIX world =
				DirectX::XMMatrixScaling(sx * 1.002f, sy * 1.002f, sz * 1.002f) *
				DirectX::XMMatrixTranslation(
					static_cast<float>(block.x) + localMin.x - sx * 0.001f,
					static_cast<float>(block.y) + localMin.y - sy * 0.001f,
					static_cast<float>(block.z) + localMin.z - sz * 0.001f
				);

			overlayData data{};
			DirectX::XMStoreFloat4x4(
				&data.worldViewProjection,
				DirectX::XMMatrixTranspose(
					world *
					DirectX::XMLoadFloat4x4(&view) *
					DirectX::XMLoadFloat4x4(&projection)
				)
			);
			data.stageIndex = static_cast<float>(stage);
			_constantBuffer.update(context, data);
			_constantBuffer.bindVS(context, 0);
			_constantBuffer.bindPS(context, 0);
			_program.bindShaders(context);
			context->GSSetShader(nullptr, nullptr, 0);
			context->HSSetShader(nullptr, nullptr, 0);
			context->DSSetShader(nullptr, nullptr, 0);
			context->OMSetBlendState(_blendState.Get(), nullptr, 0xffffffffu);
			context->OMSetDepthStencilState(_depthState.Get(), 0);
			context->RSSetState(_rasterizerState.Get());
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			const UINT stride = sizeof(float) * 5;
			const UINT offset = 0;
			ID3D11Buffer* vb = _vertexBuffer.Get();
			context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
			context->IASetIndexBuffer(_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
			ID3D11ShaderResourceView* srv = _srv.Get();
			context->PSSetShaderResources(0, 1, &srv);
			ID3D11SamplerState* sampler = _sampler.Get();
			context->PSSetSamplers(0, 1, &sampler);
			context->DrawIndexed(36, 0, 0);

			ID3D11Buffer* oldVb = oldVertexBuffer.Get();
			ID3D11Buffer* oldCb = oldConstantBuffer.Get();
			ID3D11ShaderResourceView* oldSrvPtr = oldSrv.Get();
			ID3D11SamplerState* oldSamplerPtr = oldSampler.Get();
			context->IASetInputLayout(oldLayout.Get());
			context->IASetPrimitiveTopology(oldTopology);
			context->IASetVertexBuffers(0, 1, &oldVb, &oldVertexStride, &oldVertexOffset);
			context->IASetIndexBuffer(oldIndexBuffer.Get(), oldIndexFormat, oldIndexOffset);
			context->VSSetConstantBuffers(0, 1, &oldCb);
			context->VSSetShader(oldVertexShader.Get(), nullptr, 0);
			context->PSSetShader(oldPixelShader.Get(), nullptr, 0);
			context->GSSetShader(oldGeometryShader.Get(), nullptr, 0);
			context->HSSetShader(oldHullShader.Get(), nullptr, 0);
			context->DSSetShader(oldDomainShader.Get(), nullptr, 0);
			context->OMSetBlendState(oldBlendState.Get(), oldBlendFactor, oldSampleMask);
			context->OMSetDepthStencilState(oldDepthState.Get(), oldStencilReference);
			context->RSSetState(oldRasterizerState.Get());
			context->PSSetShaderResources(0, 1, &oldSrvPtr);
			context->PSSetSamplers(0, 1, &oldSamplerPtr);
		}

	private:
		struct overlayData {
			DirectX::XMFLOAT4X4 worldViewProjection{};
			float stageIndex = 0.0f;
			DirectX::XMFLOAT3 padding{};
		};

		void loadStages(ID3D11Device* device) {
			std::vector<DirectX::ScratchImage> images;
			images.reserve(STAGE_COUNT);
			DirectX::TexMetadata meta{};
			for (int i = 0; i < STAGE_COUNT; ++i) {
				const std::string path = "assets/textures/block/destroy_stage_" + std::to_string(i) + ".png";
				DirectX::ScratchImage image;
				DirectX::TexMetadata imageMeta{};
				const HRESULT hr = loadImageFromFile(toWide(path), image, imageMeta);
				if (FAILED(hr))
					throw std::runtime_error("Failed loading " + path);
				if (i == 0) meta = imageMeta;
				images.push_back(std::move(image));
			}

			DirectX::ScratchImage arrayImage;
			if (FAILED(arrayImage.Initialize2D(
					meta.format, meta.width, meta.height, STAGE_COUNT, 1)))
				throw std::runtime_error("Failed creating destroy stage array");

			for (int i = 0; i < STAGE_COUNT; ++i) {
				const DirectX::Image* src = images[static_cast<size_t>(i)].GetImage(0, 0, 0);
				const DirectX::Image* dst = arrayImage.GetImage(0, static_cast<size_t>(i), 0);
				if (!src || !dst || FAILED(DirectX::CopyRectangle(
						*src,
						DirectX::Rect(0, 0, static_cast<size_t>(meta.width), static_cast<size_t>(meta.height)),
						*dst, DirectX::TEX_FILTER_DEFAULT, 0, 0)))
					throw std::runtime_error("Failed packing destroy stage array");
			}

			Microsoft::WRL::ComPtr<ID3D11Resource> resource;
			if (FAILED(DirectX::CreateTexture(device, arrayImage.GetImages(), arrayImage.GetImageCount(),
					arrayImage.GetMetadata(), resource.GetAddressOf())))
				throw std::runtime_error("Failed creating destroy stage texture");

			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.Format = meta.format;
			srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
			srv.Texture2DArray.MipLevels = 1;
			srv.Texture2DArray.ArraySize = STAGE_COUNT;
			if (FAILED(device->CreateShaderResourceView(resource.Get(), &srv, _srv.GetAddressOf())))
				throw std::runtime_error("Failed creating destroy stage SRV");
		}

		shaderProgram _program;
		constantBuffer<overlayData> _constantBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> _vertexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> _indexBuffer;
		Microsoft::WRL::ComPtr<ID3D11BlendState> _blendState;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> _depthState;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> _rasterizerState;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> _sampler;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _srv;
	};

}
