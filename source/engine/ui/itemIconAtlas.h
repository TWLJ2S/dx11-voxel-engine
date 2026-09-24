#pragma once

#include <assets/assetManager.h>
#include <assets/cpuAsset.h>
#include <assets/gpuAsset.h>
#include <core/debug.h>
#include <core/player.h>
#include <core/shader.h>
#include <core/texture.h>
#include <renderer/buffer.h>
#include <renderer/staticRenderer.h>
#include <world/chunk.h>

#include <DirectXMath.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ac {

	// Bakes isometric block previews into a texture atlas for HUD slots.
	class itemIconAtlas {
	public:
		static constexpr UINT ICON_SIZE = 64;

		struct iconUv {
			DirectX::XMFLOAT2 uvMin{ 0.0f, 0.0f };
			DirectX::XMFLOAT2 uvMax{ 1.0f, 1.0f };
			bool valid = false;
		};

		void bake(
			ID3D11Device* device,
			ID3D11DeviceContext* context,
			staticAssetManager& blocks,
			modelManager& models,
			blockTextureSet& textures
		) {
			namespace dx = DirectX;
			_device = device;
			_bakeContext = context;
			_uvs.clear();
			_icons.clear();

			_program.initVertexShader(device, L"assets/shader/itemIcon.hlsl", "vertexMain", "vs_5_0");
			_program.initPixelShader(device, L"assets/shader/itemIcon.hlsl", "pixelMain", "ps_5_0");
			_program.initInputLayout(device, {
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(vertex, _position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(vertex, _normal), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(vertex, _uv), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "MATID", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(vertex, _material), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "AO", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(vertex, _ao), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "OPACITY", 0, DXGI_FORMAT_R32_FLOAT, 0, offsetof(vertex, _opacity), D3D11_INPUT_PER_VERTEX_DATA, 0 }
			});
			_objectBuffer.create(device);
			_cameraBuffer.create(device);

			D3D11_SAMPLER_DESC sampler{};
			sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler.MaxLOD = D3D11_FLOAT32_MAX;
			DX_CHECK(device->CreateSamplerState(&sampler, _sampler.GetAddressOf()));

			D3D11_DEPTH_STENCIL_DESC depth{};
			depth.DepthEnable = TRUE;
			depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
			depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
			DX_CHECK(device->CreateDepthStencilState(&depth, _depthState.GetAddressOf()));

			D3D11_RASTERIZER_DESC raster{};
			raster.FillMode = D3D11_FILL_SOLID;
			raster.CullMode = D3D11_CULL_NONE;
			raster.DepthClipEnable = TRUE;
			DX_CHECK(device->CreateRasterizerState(&raster, _rasterState.GetAddressOf()));

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

			const std::vector<uint32_t> ids = blocks.ids();
			_icons.reserve(ids.size());
			const UINT columns = std::max(1u, static_cast<UINT>(std::ceil(std::sqrt(static_cast<float>(ids.size() + 1)))));
			const UINT rows = std::max(1u, static_cast<UINT>(std::ceil(static_cast<float>(ids.size()) / static_cast<float>(columns))));
			const UINT atlasWidth = columns * ICON_SIZE;
			const UINT atlasHeight = rows * ICON_SIZE;
			_atlasWidth = atlasWidth;
			_atlasHeight = atlasHeight;

			D3D11_TEXTURE2D_DESC atlasDesc{};
			atlasDesc.Width = atlasWidth;
			atlasDesc.Height = atlasHeight;
			atlasDesc.MipLevels = 1;
			atlasDesc.ArraySize = 1;
			atlasDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			atlasDesc.SampleDesc.Count = 1;
			atlasDesc.Usage = D3D11_USAGE_DEFAULT;
			atlasDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
			DX_CHECK(device->CreateTexture2D(&atlasDesc, nullptr, _atlas.GetAddressOf()));
			DX_CHECK(device->CreateShaderResourceView(_atlas.Get(), nullptr, _atlasSrv.GetAddressOf()));

			D3D11_TEXTURE2D_DESC tileDesc = atlasDesc;
			tileDesc.Width = ICON_SIZE;
			tileDesc.Height = ICON_SIZE;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> tile;
			Microsoft::WRL::ComPtr<ID3D11RenderTargetView> tileRtv;
			DX_CHECK(device->CreateTexture2D(&tileDesc, nullptr, tile.GetAddressOf()));
			DX_CHECK(device->CreateRenderTargetView(tile.Get(), nullptr, tileRtv.GetAddressOf()));

			D3D11_TEXTURE2D_DESC depthDesc{};
			depthDesc.Width = ICON_SIZE;
			depthDesc.Height = ICON_SIZE;
			depthDesc.MipLevels = 1;
			depthDesc.ArraySize = 1;
			depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
			depthDesc.SampleDesc.Count = 1;
			depthDesc.Usage = D3D11_USAGE_DEFAULT;
			depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> depthTex;
			Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthView;
			DX_CHECK(device->CreateTexture2D(&depthDesc, nullptr, depthTex.GetAddressOf()));
			DX_CHECK(device->CreateDepthStencilView(depthTex.Get(), nullptr, depthView.GetAddressOf()));

			// Minecraft-like GUI isometric angle.
			const dx::XMMATRIX view = dx::XMMatrixLookAtLH(
				dx::XMVectorSet(1.6f, 1.75f, 1.6f, 0.0f),
				dx::XMVectorSet(0.5f, 0.45f, 0.5f, 0.0f),
				dx::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
			const dx::XMMATRIX projection = dx::XMMatrixOrthographicLH(1.85f, 1.85f, 0.01f, 10.0f);
			cameraData camera{};
			dx::XMStoreFloat4x4(&camera._view, dx::XMMatrixTranspose(view));
			dx::XMStoreFloat4x4(&camera._projection, dx::XMMatrixTranspose(projection));
			dx::XMStoreFloat4x4(&camera._viewProjection,
				dx::XMMatrixTranspose(view * projection));
			camera._position = { 1.6f, 1.75f, 1.6f };
			_cameraBuffer.update(context, camera);

			D3D11_VIEWPORT viewport{};
			viewport.Width = static_cast<float>(ICON_SIZE);
			viewport.Height = static_cast<float>(ICON_SIZE);
			viewport.MinDepth = 0.0f;
			viewport.MaxDepth = 1.0f;

			Microsoft::WRL::ComPtr<ID3D11RenderTargetView> oldRtv;
			Microsoft::WRL::ComPtr<ID3D11DepthStencilView> oldDsv;
			context->OMGetRenderTargets(1, oldRtv.GetAddressOf(), oldDsv.GetAddressOf());
			UINT oldViewportCount = 1;
			D3D11_VIEWPORT oldViewport{};
			context->RSGetViewports(&oldViewportCount, &oldViewport);

			UINT index = 0;
			for (uint32_t id : ids) {
				const blockDefinition* definition = blocks.get(id);
				if (!definition) continue;

				const UINT col = index % columns;
				const UINT row = index / columns;
				const UINT destX = col * ICON_SIZE;
				const UINT destY = row * ICON_SIZE;

				bool wrote = false;
				if (shouldUseFlatIcon(definition)) {
					const std::string& path = flatIconPath(definition);
					wrote = blitFlatIcon(device, context, path, destX, destY);
				}

				if (!wrote) {
					gpuModel mesh;
					buildMesh(definition, models, mesh);
					if (mesh._index.count() == 0) {
						++index;
						continue;
					}

					const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
					context->OMSetRenderTargets(1, tileRtv.GetAddressOf(), depthView.Get());
					context->ClearRenderTargetView(tileRtv.Get(), clear);
					context->ClearDepthStencilView(depthView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
					context->RSSetViewports(1, &viewport);

					ID3D11ShaderResourceView* nullSrvs[10] = {};
					context->PSSetShaderResources(1, 10, nullSrvs);
					ID3D11SamplerState* nullSamplers[4] = {};
					context->PSSetSamplers(1, 4, nullSamplers);
					ID3D11Buffer* nullCBs[8] = {};
					context->PSSetConstantBuffers(2, 8, nullCBs);
					context->GSSetShader(nullptr, nullptr, 0);
					context->HSSetShader(nullptr, nullptr, 0);
					context->DSSetShader(nullptr, nullptr, 0);

					_program.bindShaders(context);
					_cameraBuffer.bindVS(context, 1);
					_cameraBuffer.bindPS(context, 1);
					textures.bind(context);
					context->PSSetSamplers(0, 1, _sampler.GetAddressOf());
					context->OMSetBlendState(_blendState.Get(), nullptr, 0xffffffffu);
					context->OMSetDepthStencilState(_depthState.Get(), 0);
					context->RSSetState(_rasterState.Get());
					context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

					dx::XMFLOAT4X4 transform{};
					dx::XMStoreFloat4x4(&transform, dx::XMMatrixTranspose(dx::XMMatrixIdentity()));
					_objectBuffer.update(context, { transform });
					_objectBuffer.bindVS(context, 0);
					mesh.bind(context);
					mesh.draw(context);

					D3D11_BOX box{};
					box.left = 0; box.top = 0; box.front = 0;
					box.right = ICON_SIZE; box.bottom = ICON_SIZE; box.back = 1;
					context->CopySubresourceRegion(
						_atlas.Get(), 0,
						destX, destY, 0,
						tile.Get(), 0, &box);
				}

				iconUv uv;
				uv.uvMin = {
					static_cast<float>(destX) / static_cast<float>(atlasWidth),
					static_cast<float>(destY) / static_cast<float>(atlasHeight)
				};
				uv.uvMax = {
					static_cast<float>(destX + ICON_SIZE) / static_cast<float>(atlasWidth),
					static_cast<float>(destY + ICON_SIZE) / static_cast<float>(atlasHeight)
				};
				uv.valid = true;
				_uvs[id] = uv;
				++index;
			}

			context->OMSetRenderTargets(1, oldRtv.GetAddressOf(), oldDsv.Get());
			if (oldViewportCount > 0)
				context->RSSetViewports(oldViewportCount, &oldViewport);
		}

		ID3D11ShaderResourceView* srv() const { return _atlasSrv.Get(); }
		UINT atlasWidth() const { return _atlasWidth; }
		UINT atlasHeight() const { return _atlasHeight; }

		iconUv uvFor(blockId id) const {
			const auto found = _uvs.find(blockType(id));
			if (found == _uvs.end()) return {};
			return found->second;
		}

		bool ready() const { return _atlasSrv != nullptr; }

	private:
		shaderProgram _program;
		constantBuffer<objectData> _objectBuffer;
		constantBuffer<cameraData> _cameraBuffer;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> _atlas;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _atlasSrv;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> _sampler;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> _depthState;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> _rasterState;
		Microsoft::WRL::ComPtr<ID3D11BlendState> _blendState;
		std::unordered_map<uint32_t, iconUv> _uvs;
		std::vector<uint32_t> _icons;
		UINT _atlasWidth = 0;
		UINT _atlasHeight = 0;

		static void pushQuad(
			std::vector<vertex>& vertices,
			std::vector<uint32_t>& indices,
			const DirectX::XMFLOAT3& n,
			const DirectX::XMFLOAT3& p0,
			const DirectX::XMFLOAT3& p1,
			const DirectX::XMFLOAT3& p2,
			const DirectX::XMFLOAT3& p3,
			uint32_t material,
			float opacity,
			DirectX::XMFLOAT2 uv0 = { 0, 1 },
			DirectX::XMFLOAT2 uv1 = { 1, 1 },
			DirectX::XMFLOAT2 uv2 = { 1, 0 },
			DirectX::XMFLOAT2 uv3 = { 0, 0 }
		) {
			const uint32_t base = static_cast<uint32_t>(vertices.size());
			const DirectX::XMFLOAT2 uvs[4] = { uv0, uv1, uv2, uv3 };
			const DirectX::XMFLOAT3 positions[4] = { p0, p1, p2, p3 };
			for (int i = 0; i < 4; ++i) {
				vertex v{};
				v._position = positions[i];
				v._normal = n;
				v._uv = uvs[i];
				v._material = material;
				v._ao = 0xFFu;
				v._opacity = opacity;
				vertices.push_back(v);
			}
			indices.push_back(base + 0);
			indices.push_back(base + 1);
			indices.push_back(base + 2);
			indices.push_back(base + 0);
			indices.push_back(base + 2);
			indices.push_back(base + 3);
		}

		static void appendCube(
			std::vector<vertex>& vertices,
			std::vector<uint32_t>& indices,
			const blockDefinition* definition
		) {
			const float opacity = definition->_opacity;
			const uint32_t west = definition->materialForFace(BLOCK_FACE_WEST);
			const uint32_t east = definition->materialForFace(BLOCK_FACE_EAST);
			const uint32_t down = definition->materialForFace(BLOCK_FACE_DOWN);
			const uint32_t up = definition->materialForFace(BLOCK_FACE_UP);
			const uint32_t north = definition->materialForFace(BLOCK_FACE_NORTH);
			const uint32_t south = definition->materialForFace(BLOCK_FACE_SOUTH);
			pushQuad(vertices, indices, { -1, 0, 0 }, { 0,0,0 }, { 0,0,1 }, { 0,1,1 }, { 0,1,0 }, west, opacity);
			pushQuad(vertices, indices, { 1, 0, 0 }, { 1,0,1 }, { 1,0,0 }, { 1,1,0 }, { 1,1,1 }, east, opacity);
			pushQuad(vertices, indices, { 0, -1, 0 }, { 0,0,0 }, { 1,0,0 }, { 1,0,1 }, { 0,0,1 }, down, opacity);
			pushQuad(vertices, indices, { 0, 1, 0 }, { 1,1,0 }, { 0,1,0 }, { 0,1,1 }, { 1,1,1 }, up, opacity);
			pushQuad(vertices, indices, { 0, 0, -1 }, { 1,0,0 }, { 0,0,0 }, { 0,1,0 }, { 1,1,0 }, north, opacity);
			pushQuad(vertices, indices, { 0, 0, 1 }, { 0,0,1 }, { 1,0,1 }, { 1,1,1 }, { 0,1,1 }, south, opacity);
		}

		void buildMesh(
			const blockDefinition* definition,
			modelManager& models,
			gpuModel& out
		) {
			std::vector<vertex> vertices;
			std::vector<uint32_t> indices;
			const model* prototype = nullptr;
			uint32_t modelId = definition->_model;
			if (modelId == MODEL_FENCE)
				prototype = models.get(MODEL_FENCE);
			else if (isDetailModel(modelId))
				prototype = models.get(modelId);

			if (prototype && !prototype->_vertex.empty()) {
				vertices = prototype->_vertex;
				indices = prototype->_index;
				for (vertex& v : vertices) {
					v._material = isDiodeModel(modelId) ? definition->materialForFace(v._material) : modelId == MODEL_LEVER
						? definition->materialForFace(v._material == 1u ? BLOCK_FACE_UP : BLOCK_FACE_WEST)
						: definition->materialForNormal(v._normal);
					v._opacity = definition->_opacity;
					if (v._ao == 0) v._ao = 0xFFu;
				}
			}
			else {
				appendCube(vertices, indices, definition);
			}

			out._vertex.upload(_device, _bakeContext, vertices);
			out._index.upload(_device, _bakeContext, indices);
		}

		ID3D11Device* _device = nullptr;
		ID3D11DeviceContext* _bakeContext = nullptr;

		static bool shouldUseFlatIcon(const blockDefinition* definition) {
			if (!definition) return false;
			if (definition->_model == MODEL_LEVER) return false;
			if (definition->_flatIcon) return true;
			if (definition->_item) return true;
			if (definition->_texture.find("/item/") != std::string::npos)
				return true;
			// Plants, torches, thin plates, panes look wrong as 3D cubes in the HUD.
			if (definition->_model == MODEL_CROSS ||
				definition->_model == MODEL_TORCH ||
				definition->_model == MODEL_THIN ||
				definition->_model == MODEL_PANE)
				return true;
			if (definition->_renderMode == RENDER_MODE_CUTOUT &&
				!definition->_solid &&
				!definition->_occludes) {
				return true;
			}
			return false;
		}

		static const std::string& flatIconPath(const blockDefinition* definition) {
			if (!definition->_texture.empty())
				return definition->_texture;
			return definition->textureForFace(BLOCK_FACE_WEST);
		}

		bool blitFlatIcon(
			ID3D11Device* device,
			ID3D11DeviceContext* context,
			const std::string& path,
			UINT destX,
			UINT destY
		) {
			if (path.empty() || !device || !context || !_atlas)
				return false;
			try {
				scratchTexture scratch = loadScratchTextureFromFile(path);
				const DirectX::Image* source = scratch.image.GetImage(0, 0, 0);
				if (!source) return false;

				DirectX::ScratchImage resized;
				HRESULT hr = DirectX::Resize(
					*source,
					ICON_SIZE,
					ICON_SIZE,
					DirectX::TEX_FILTER_POINT,
					resized);
				if (FAILED(hr)) return false;

				const DirectX::Image* resizedImage = resized.GetImage(0, 0, 0);
				if (!resizedImage) return false;

				D3D11_TEXTURE2D_DESC desc{};
				desc.Width = ICON_SIZE;
				desc.Height = ICON_SIZE;
				desc.MipLevels = 1;
				desc.ArraySize = 1;
				desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				desc.SampleDesc.Count = 1;
				desc.Usage = D3D11_USAGE_DEFAULT;
				desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

				D3D11_SUBRESOURCE_DATA init{};
				init.pSysMem = resizedImage->pixels;
				init.SysMemPitch = static_cast<UINT>(resizedImage->rowPitch);
				init.SysMemSlicePitch = static_cast<UINT>(resizedImage->slicePitch);

				Microsoft::WRL::ComPtr<ID3D11Texture2D> temp;
				hr = device->CreateTexture2D(&desc, &init, temp.GetAddressOf());
				if (FAILED(hr) || !temp) return false;

				context->CopySubresourceRegion(
					_atlas.Get(), 0,
					destX, destY, 0,
					temp.Get(), 0, nullptr);
				return true;
			}
			catch (...) {
				return false;
			}
		}
	};

}
