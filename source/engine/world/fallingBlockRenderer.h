#pragma once

#include <assets/assetManager.h>
#include <assets/cpuAsset.h>
#include <assets/gpuAsset.h>
#include <core/debug.h>
#include <core/shader.h>
#include <core/texture.h>
#include <renderer/buffer.h>
#include <renderer/staticRenderer.h>
#include <world/fallingBlockPhysics.h>

#include <d3d11.h>
#include <DirectXMath.h>

#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace ac {

	// Builds and draws unit cubes textured via per-vertex block materials (terrain path).
	class fallingBlockRenderer {
	public:
		void create(ID3D11Device* device, const modelManager* models = nullptr) {
			_device = device;
			_models = models;
			_heldProgram.initVertexShader(device, L"assets/shader/itemIcon.hlsl", "vertexMain", "vs_5_0");
			_heldProgram.initPixelShader(device, L"assets/shader/itemIcon.hlsl", "pixelMain", "ps_5_0");
			_heldProgram.initInputLayout(device, {
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(vertex, _position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(vertex, _normal), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(vertex, _uv), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "MATID", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(vertex, _material), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "AO", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(vertex, _ao), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "OPACITY", 0, DXGI_FORMAT_R32_FLOAT, 0, offsetof(vertex, _opacity), D3D11_INPUT_PER_VERTEX_DATA, 0 }
			});

			D3D11_SAMPLER_DESC sampler{};
			sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler.MaxLOD = 0.0f;
			DX_CHECK(device->CreateSamplerState(&sampler, _heldSampler.GetAddressOf()));

			D3D11_BLEND_DESC blend{};
			blend.RenderTarget[0].BlendEnable = FALSE;
			blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			DX_CHECK(device->CreateBlendState(&blend, _heldBlend.GetAddressOf()));
		}

		void clearMeshes() {
			_meshes.clear();
			_heldMeshes.clear();
		}

		void draw(
			ID3D11DeviceContext* context,
			constantBuffer<objectData>& objectBuffer,
			staticAssetManager& blocks,
			const std::vector<fallingBlockEntity>& entities
		) {
			if (!_device || !context || entities.empty()) return;

			namespace dx = DirectX;
			_drawList.clear();
			_drawList.reserve(entities.size());

			for (const fallingBlockEntity& entity : entities) {
				const uint32_t type = blockType(entity.id);
				const blockDefinition* definition = blocks.get(type);
				if (!definition) continue;
				gpuModel* mesh = meshFor(definition);
				if (!mesh) continue;
				_drawList.push_back({ &entity, mesh, type });
			}
			if (_drawList.empty()) return;

			std::sort(_drawList.begin(), _drawList.end(),
				[](const drawItem& a, const drawItem& b) {
					return a.type < b.type;
				});

			gpuModel* bound = nullptr;
			for (const drawItem& item : _drawList) {
				if (item.mesh != bound) {
					item.mesh->bind(context);
					bound = item.mesh;
				}
				dx::XMFLOAT4X4 transform{};
				dx::XMStoreFloat4x4(
					&transform,
					dx::XMMatrixTranspose(dx::XMMatrixTranslation(
						item.entity->px - 0.5f,
						item.entity->y,
						item.entity->pz - 0.5f)));
				objectBuffer.update(context, { transform });
				objectBuffer.bindVS(context, 0);
				item.mesh->draw(context);
			}
		}

		template <typename ItemRange>
		void drawItems(
			ID3D11DeviceContext* context,
			constantBuffer<objectData>& objectBuffer,
			staticAssetManager& blocks,
			const ItemRange& items,
			float timeSeconds,
			float size = 0.25f
		) {
			if (!_device || !context) return;
			namespace dx = DirectX;

			struct itemDraw {
				gpuModel* mesh = nullptr;
				uint32_t type = 0;
				float x = 0.0f;
				float y = 0.0f;
				float z = 0.0f;
				float yaw = 0.0f;
			};
			std::vector<itemDraw> list;
			list.reserve(items.size());
			for (const auto& entity : items) {
				const uint32_t type = blockType(entity.itemId);
				const blockDefinition* definition = blocks.get(type);
				if (!definition) continue;
				gpuModel* mesh = meshFor(definition);
				if (!mesh) continue;
				list.push_back({
					mesh, type, entity.x, entity.y, entity.z, entity.yaw
				});
			}
			if (list.empty()) return;

			std::sort(list.begin(), list.end(),
				[](const itemDraw& a, const itemDraw& b) { return a.type < b.type; });

			gpuModel* bound = nullptr;
			for (const itemDraw& item : list) {
				if (item.mesh != bound) {
					item.mesh->bind(context);
					bound = item.mesh;
				}
				const float bob = 0.06f * std::sin(timeSeconds * 3.2f + item.x + item.z);
				const float half = size * 0.5f;
				dx::XMFLOAT4X4 transform{};
				dx::XMStoreFloat4x4(
					&transform,
					dx::XMMatrixTranspose(
						dx::XMMatrixScaling(size, size, size) *
						dx::XMMatrixRotationY(item.yaw) *
						dx::XMMatrixTranslation(
							item.x - half,
							item.y + bob,
							item.z - half)));
				objectBuffer.update(context, { transform });
				objectBuffer.bindVS(context, 0);
				item.mesh->draw(context);
			}
		}

		void drawHeld(
			ID3D11DeviceContext* context,
			constantBuffer<objectData>& objectBuffer,
			const blockDefinition* definition,
			heldItemStyle style,
			const DirectX::XMFLOAT4X4& worldTransposed,
			bool /*overlay*/
		) {
			if (!_device || !context || !definition || style == heldItemStyle::none) return;
			gpuModel* mesh = heldMeshFor(definition, style);
			if (!mesh) return;
			ensureOverlayDepth();

			Microsoft::WRL::ComPtr<ID3D11DepthStencilState> previousDepth;
			Microsoft::WRL::ComPtr<ID3D11RasterizerState> previousRaster;
			Microsoft::WRL::ComPtr<ID3D11BlendState> previousBlend;
			FLOAT blendFactor[4]{};
			UINT sampleMask = 0xffffffffu;
			UINT stencil = 0;
			context->OMGetDepthStencilState(previousDepth.GetAddressOf(), &stencil);
			context->RSGetState(previousRaster.GetAddressOf());
			context->OMGetBlendState(previousBlend.GetAddressOf(), blendFactor, &sampleMask);

			context->GSSetShader(nullptr, nullptr, 0);
			context->HSSetShader(nullptr, nullptr, 0);
			context->DSSetShader(nullptr, nullptr, 0);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			_heldProgram.bindShaders(context);
			context->PSSetSamplers(0, 1, _heldSampler.GetAddressOf());
			context->OMSetBlendState(_heldBlend.Get(), nullptr, 0xffffffffu);
			context->RSSetState(_heldRasterizer.Get());
			context->OMSetDepthStencilState(_overlayDepth.Get(), 0);

			mesh->bind(context);
			objectBuffer.update(context, { worldTransposed });
			objectBuffer.bindVS(context, 0);
			mesh->draw(context);

			if (previousRaster)
				context->RSSetState(previousRaster.Get());
			if (previousDepth)
				context->OMSetDepthStencilState(previousDepth.Get(), stencil);
			context->OMSetBlendState(previousBlend.Get(), blendFactor, sampleMask);
		}

	private:
		struct drawItem {
			const fallingBlockEntity* entity = nullptr;
			gpuModel* mesh = nullptr;
			uint32_t type = 0;
		};

		ID3D11Device* _device = nullptr;
		const modelManager* _models = nullptr;
		std::unordered_map<uint32_t, gpuModel> _meshes;
		std::unordered_map<uint32_t, gpuModel> _heldMeshes;
		std::vector<drawItem> _drawList;
		shaderProgram _heldProgram;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> _heldSampler;
		Microsoft::WRL::ComPtr<ID3D11BlendState> _heldBlend;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> _overlayDepth;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> _heldRasterizer;

		void ensureOverlayDepth() {
			if (!_device) return;
			if (!_overlayDepth) {
				D3D11_DEPTH_STENCIL_DESC depth{};
				depth.DepthEnable = TRUE;
				depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
				depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
				_device->CreateDepthStencilState(&depth, _overlayDepth.GetAddressOf());
			}
			if (!_heldRasterizer) {
				D3D11_RASTERIZER_DESC rasterizer{};
				rasterizer.FillMode = D3D11_FILL_SOLID;
				rasterizer.CullMode = D3D11_CULL_NONE;
				rasterizer.DepthClipEnable = TRUE;
				_device->CreateRasterizerState(&rasterizer, _heldRasterizer.GetAddressOf());
			}
		}

		static void pushQuad(
			std::vector<vertex>& vertices,
			std::vector<uint32_t>& indices,
			const DirectX::XMFLOAT3& n,
			const DirectX::XMFLOAT3& p0,
			const DirectX::XMFLOAT3& p1,
			const DirectX::XMFLOAT3& p2,
			const DirectX::XMFLOAT3& p3,
			uint32_t material,
			float opacity
		) {
			const uint32_t base = static_cast<uint32_t>(vertices.size());
			const DirectX::XMFLOAT2 uvs[4] = { {0, 1}, {1, 1}, {1, 0}, {0, 0} };
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

		gpuModel* meshFor(const blockDefinition* definition) {
			if (!definition || !_device) return nullptr;
			if ((definition->_model == MODEL_LEVER || isDiodeModel(definition->_model)) && _models) {
				const auto* lever = _models->get(definition->_model);
				if (lever && !lever->_vertex.empty() && !lever->_index.empty())
					return heldMeshFor(definition, heldItemStyle::block);
			}
			const auto existing = _meshes.find(definition->_id);
			if (existing != _meshes.end()) return &existing->second;

			std::vector<vertex> vertices;
			std::vector<uint32_t> indices;
			vertices.reserve(24);
			indices.reserve(36);
			const float opacity = definition->_opacity;
			const uint32_t west = definition->materialForFace(BLOCK_FACE_WEST);
			const uint32_t east = definition->materialForFace(BLOCK_FACE_EAST);
			const uint32_t down = definition->materialForFace(BLOCK_FACE_DOWN);
			const uint32_t up = definition->materialForFace(BLOCK_FACE_UP);
			const uint32_t north = definition->materialForFace(BLOCK_FACE_NORTH);
			const uint32_t south = definition->materialForFace(BLOCK_FACE_SOUTH);

			// Unit cube [0,1]^3 — vertex order matches chunkMesher face winding.
			pushQuad(vertices, indices, { -1, 0, 0 },
				{ 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 1 }, { 0, 1, 0 }, west, opacity);
			pushQuad(vertices, indices, { 1, 0, 0 },
				{ 1, 0, 1 }, { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, east, opacity);
			pushQuad(vertices, indices, { 0, -1, 0 },
				{ 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 }, { 0, 0, 1 }, down, opacity);
			pushQuad(vertices, indices, { 0, 1, 0 },
				{ 1, 1, 0 }, { 0, 1, 0 }, { 0, 1, 1 }, { 1, 1, 1 }, up, opacity);
			pushQuad(vertices, indices, { 0, 0, -1 },
				{ 1, 0, 0 }, { 0, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, north, opacity);
			pushQuad(vertices, indices, { 0, 0, 1 },
				{ 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 }, south, opacity);

			gpuModel model;
			model._vertex.create(_device, vertices);
			model._index.create(_device, indices);
			auto [it, _] = _meshes.emplace(definition->_id, std::move(model));
			return &it->second;
		}

		gpuModel* heldMeshFor(const blockDefinition* definition, heldItemStyle style) {
			if (!definition || !_device) return nullptr;
			const auto existing = _heldMeshes.find(definition->_id);
			if (existing != _heldMeshes.end()) return &existing->second;

			std::vector<vertex> vertices;
			std::vector<uint32_t> indices;
			const float opacity = 1.0f;
			const uint32_t west = definition->materialForFace(BLOCK_FACE_WEST);
			const model* prototype = nullptr;
			if (_models && !definition->_item && definition->_model != MODEL_CUBE)
				prototype = _models->get(definition->_model);
			if (prototype && !prototype->_vertex.empty() && !prototype->_index.empty()) {
				vertices = prototype->_vertex;
				indices = prototype->_index;
				for (vertex& v : vertices) {
					v._material = isDiodeModel(definition->_model) ? definition->materialForFace(v._material) : definition->_model == MODEL_LEVER
						? definition->materialForFace(v._material == 1u ? BLOCK_FACE_UP : BLOCK_FACE_WEST)
						: definition->materialForNormal(v._normal);
					v._opacity = definition->_opacity;
					if (v._ao == 0u) v._ao = 0xFFu;
				}
			}

			if (vertices.empty()) {
				if (style == heldItemStyle::block)
					return meshFor(definition);
				if (style == heldItemStyle::slab) {
					appendBox(vertices, indices, { 0, 0, 0 }, { 1, 0.5f, 1 }, definition, opacity);
				}
				else if (style == heldItemStyle::rod) {
					appendBox(vertices, indices, { 0.42f, 0.0f, 0.42f }, { 0.58f, 1.0f, 0.58f },
						west, opacity);
				}
				else if (style == heldItemStyle::cross) {
					appendCross(vertices, indices, west, opacity);
				}
				else if (!appendExtrudedSprite(vertices, indices, definition, west, opacity)) {
					appendSpriteCard(vertices, indices, west, opacity);
				}
			}

			if (vertices.empty()) return meshFor(definition);
			gpuModel model;
			model._vertex.create(_device, vertices);
			model._index.create(_device, indices);
			auto [it, _] = _heldMeshes.emplace(definition->_id, std::move(model));
			return &it->second;
		}

		void appendBox(
			std::vector<vertex>& vertices,
			std::vector<uint32_t>& indices,
			DirectX::XMFLOAT3 minimum,
			DirectX::XMFLOAT3 maximum,
			uint32_t material,
			float opacity
		) {
			pushQuad(vertices, indices, { -1, 0, 0 },
				{ minimum.x, minimum.y, minimum.z }, { minimum.x, minimum.y, maximum.z },
				{ minimum.x, maximum.y, maximum.z }, { minimum.x, maximum.y, minimum.z },
				material, opacity);
			pushQuad(vertices, indices, { 1, 0, 0 },
				{ maximum.x, minimum.y, maximum.z }, { maximum.x, minimum.y, minimum.z },
				{ maximum.x, maximum.y, minimum.z }, { maximum.x, maximum.y, maximum.z },
				material, opacity);
			pushQuad(vertices, indices, { 0, -1, 0 },
				{ minimum.x, minimum.y, minimum.z }, { maximum.x, minimum.y, minimum.z },
				{ maximum.x, minimum.y, maximum.z }, { minimum.x, minimum.y, maximum.z },
				material, opacity);
			pushQuad(vertices, indices, { 0, 1, 0 },
				{ maximum.x, maximum.y, minimum.z }, { minimum.x, maximum.y, minimum.z },
				{ minimum.x, maximum.y, maximum.z }, { maximum.x, maximum.y, maximum.z },
				material, opacity);
			pushQuad(vertices, indices, { 0, 0, -1 },
				{ maximum.x, minimum.y, minimum.z }, { minimum.x, minimum.y, minimum.z },
				{ minimum.x, maximum.y, minimum.z }, { maximum.x, maximum.y, minimum.z },
				material, opacity);
			pushQuad(vertices, indices, { 0, 0, 1 },
				{ minimum.x, minimum.y, maximum.z }, { maximum.x, minimum.y, maximum.z },
				{ maximum.x, maximum.y, maximum.z }, { minimum.x, maximum.y, maximum.z },
				material, opacity);
		}

		void appendBox(
			std::vector<vertex>& vertices,
			std::vector<uint32_t>& indices,
			DirectX::XMFLOAT3 minimum,
			DirectX::XMFLOAT3 maximum,
			const blockDefinition* definition,
			float opacity
		) {
			const uint32_t west = definition->materialForFace(BLOCK_FACE_WEST);
			const uint32_t east = definition->materialForFace(BLOCK_FACE_EAST);
			const uint32_t down = definition->materialForFace(BLOCK_FACE_DOWN);
			const uint32_t up = definition->materialForFace(BLOCK_FACE_UP);
			const uint32_t north = definition->materialForFace(BLOCK_FACE_NORTH);
			const uint32_t south = definition->materialForFace(BLOCK_FACE_SOUTH);
			pushQuad(vertices, indices, { -1, 0, 0 },
				{ minimum.x, minimum.y, minimum.z }, { minimum.x, minimum.y, maximum.z },
				{ minimum.x, maximum.y, maximum.z }, { minimum.x, maximum.y, minimum.z },
				west, opacity);
			pushQuad(vertices, indices, { 1, 0, 0 },
				{ maximum.x, minimum.y, maximum.z }, { maximum.x, minimum.y, minimum.z },
				{ maximum.x, maximum.y, minimum.z }, { maximum.x, maximum.y, maximum.z },
				east, opacity);
			pushQuad(vertices, indices, { 0, -1, 0 },
				{ minimum.x, minimum.y, minimum.z }, { maximum.x, minimum.y, minimum.z },
				{ maximum.x, minimum.y, maximum.z }, { minimum.x, minimum.y, maximum.z },
				down, opacity);
			pushQuad(vertices, indices, { 0, 1, 0 },
				{ maximum.x, maximum.y, minimum.z }, { minimum.x, maximum.y, minimum.z },
				{ minimum.x, maximum.y, maximum.z }, { maximum.x, maximum.y, maximum.z },
				up, opacity);
			pushQuad(vertices, indices, { 0, 0, -1 },
				{ maximum.x, minimum.y, minimum.z }, { minimum.x, minimum.y, minimum.z },
				{ minimum.x, maximum.y, minimum.z }, { maximum.x, maximum.y, minimum.z },
				north, opacity);
			pushQuad(vertices, indices, { 0, 0, 1 },
				{ minimum.x, minimum.y, maximum.z }, { maximum.x, minimum.y, maximum.z },
				{ maximum.x, maximum.y, maximum.z }, { minimum.x, maximum.y, maximum.z },
				south, opacity);
		}

		void appendCross(
			std::vector<vertex>& vertices,
			std::vector<uint32_t>& indices,
			uint32_t material,
			float opacity
		) {
			pushQuad(vertices, indices, { 0.0f, 0.0f, 1.0f },
				{ 0, 0, 0.5f }, { 1, 0, 0.5f }, { 1, 1, 0.5f }, { 0, 1, 0.5f }, material, opacity);
			pushQuad(vertices, indices, { 0.0f, 0.0f, -1.0f },
				{ 1, 0, 0.5f }, { 0, 0, 0.5f }, { 0, 1, 0.5f }, { 1, 1, 0.5f }, material, opacity);
			pushQuad(vertices, indices, { 1.0f, 0.0f, 0.0f },
				{ 0.5f, 0, 1 }, { 0.5f, 0, 0 }, { 0.5f, 1, 0 }, { 0.5f, 1, 1 }, material, opacity);
			pushQuad(vertices, indices, { -1.0f, 0.0f, 0.0f },
				{ 0.5f, 0, 0 }, { 0.5f, 0, 1 }, { 0.5f, 1, 1 }, { 0.5f, 1, 0 }, material, opacity);
		}

		void appendSpriteCard(
			std::vector<vertex>& vertices,
			std::vector<uint32_t>& indices,
			uint32_t material,
			float opacity
		) {
			constexpr float z0 = 0.5f - (1.0f / 32.0f);
			constexpr float z1 = 0.5f + (1.0f / 32.0f);
			pushQuad(vertices, indices, { 0, 0, 1 },
				{ 0, 0, z1 }, { 1, 0, z1 }, { 1, 1, z1 }, { 0, 1, z1 }, material, opacity);
			pushQuad(vertices, indices, { 0, 0, -1 },
				{ 1, 0, z0 }, { 0, 0, z0 }, { 0, 1, z0 }, { 1, 1, z0 }, material, opacity);
			pushQuad(vertices, indices, { 0, -1, 0 },
				{ 0, 0, z0 }, { 1, 0, z0 }, { 1, 0, z1 }, { 0, 0, z1 }, material, opacity);
			pushQuad(vertices, indices, { 0, 1, 0 },
				{ 0, 1, z1 }, { 1, 1, z1 }, { 1, 1, z0 }, { 0, 1, z0 }, material, opacity);
			pushQuad(vertices, indices, { -1, 0, 0 },
				{ 0, 0, z0 }, { 0, 0, z1 }, { 0, 1, z1 }, { 0, 1, z0 }, material, opacity);
			pushQuad(vertices, indices, { 1, 0, 0 },
				{ 1, 0, z1 }, { 1, 0, z0 }, { 1, 1, z0 }, { 1, 1, z1 }, material, opacity);
		}

		void pushQuadUv(
			std::vector<vertex>& vertices,
			std::vector<uint32_t>& indices,
			const DirectX::XMFLOAT3& n,
			const DirectX::XMFLOAT3& p0,
			const DirectX::XMFLOAT3& p1,
			const DirectX::XMFLOAT3& p2,
			const DirectX::XMFLOAT3& p3,
			DirectX::XMFLOAT2 uv0,
			DirectX::XMFLOAT2 uv1,
			DirectX::XMFLOAT2 uv2,
			DirectX::XMFLOAT2 uv3,
			uint32_t material,
			float opacity
		) {
			const uint32_t base = static_cast<uint32_t>(vertices.size());
			const DirectX::XMFLOAT3 positions[4] = { p0, p1, p2, p3 };
			const DirectX::XMFLOAT2 uvs[4] = { uv0, uv1, uv2, uv3 };
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

		bool appendExtrudedSprite(
			std::vector<vertex>& vertices,
			std::vector<uint32_t>& indices,
			const blockDefinition* definition,
			uint32_t material,
			float opacity
		) {
			const std::string& path = definition->_texture.empty()
				? definition->textureForFace(BLOCK_FACE_WEST)
				: definition->_texture;
			if (path.empty() || !std::filesystem::exists(path)) return false;

			scratchTexture loaded;
			try {
				loaded = loadScratchTextureFromFile(path);
			}
			catch (...) {
				return false;
			}
			const DirectX::Image* source = loaded.image.GetImage(0, 0, 0);
			if (!source) return false;

			DirectX::ScratchImage converted;
			const DirectX::Image* image = source;
			if (source->format != DXGI_FORMAT_R8G8B8A8_UNORM &&
				source->format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
				if (FAILED(DirectX::Convert(
					*source,
					DXGI_FORMAT_R8G8B8A8_UNORM,
					DirectX::TEX_FILTER_DEFAULT,
					DirectX::TEX_THRESHOLD_DEFAULT,
					converted)))
					return false;
				image = converted.GetImage(0, 0, 0);
				if (!image) return false;
			}

			const int width = static_cast<int>(image->width);
			int height = static_cast<int>(image->height);
			if (width <= 0 || height <= 0) return false;
			if (height > width && (height % width) == 0)
				height = width;
			if (width > 32 || height > 32) {
				appendSpriteCard(vertices, indices, material, opacity);
				return true;
			}

			auto opaque = [&](int x, int y) {
				if (x < 0 || y < 0 || x >= width || y >= height) return false;
				const uint8_t* pixel = image->pixels + static_cast<size_t>(y) * image->rowPitch +
					static_cast<size_t>(x) * 4u;
				return pixel[3] > 8;
			};

			const float z0 = 0.5f - (1.0f / 32.0f);
			const float z1 = 0.5f + (1.0f / 32.0f);
			const float invW = 1.0f / static_cast<float>(width);
			const float invH = 1.0f / static_cast<float>(height);

			for (int y = 0; y < height; ++y) {
				for (int x = 0; x < width; ++x) {
					if (!opaque(x, y)) continue;
					const float x0 = static_cast<float>(x) * invW;
					const float x1 = static_cast<float>(x + 1) * invW;
					const float yTop = 1.0f - static_cast<float>(y) * invH;
					const float yBot = 1.0f - static_cast<float>(y + 1) * invH;
					const float u0 = x0;
					const float u1 = x1;
					const float v0 = static_cast<float>(y) * invH;
					const float v1 = static_cast<float>(y + 1) * invH;

					pushQuadUv(vertices, indices, { 0, 0, 1 },
						{ x0, yBot, z1 }, { x1, yBot, z1 }, { x1, yTop, z1 }, { x0, yTop, z1 },
						{ u0, v1 }, { u1, v1 }, { u1, v0 }, { u0, v0 }, material, opacity);
					pushQuadUv(vertices, indices, { 0, 0, -1 },
						{ x1, yBot, z0 }, { x0, yBot, z0 }, { x0, yTop, z0 }, { x1, yTop, z0 },
						{ u1, v1 }, { u0, v1 }, { u0, v0 }, { u1, v0 }, material, opacity);

					const DirectX::XMFLOAT2 uvCenter = { (u0 + u1) * 0.5f, (v0 + v1) * 0.5f };
					if (!opaque(x - 1, y))
						pushQuadUv(vertices, indices, { -1, 0, 0 },
							{ x0, yBot, z0 }, { x0, yBot, z1 }, { x0, yTop, z1 }, { x0, yTop, z0 },
							uvCenter, uvCenter, uvCenter, uvCenter, material, opacity);
					if (!opaque(x + 1, y))
						pushQuadUv(vertices, indices, { 1, 0, 0 },
							{ x1, yBot, z1 }, { x1, yBot, z0 }, { x1, yTop, z0 }, { x1, yTop, z1 },
							uvCenter, uvCenter, uvCenter, uvCenter, material, opacity);
					if (!opaque(x, y + 1))
						pushQuadUv(vertices, indices, { 0, -1, 0 },
							{ x0, yBot, z0 }, { x1, yBot, z0 }, { x1, yBot, z1 }, { x0, yBot, z1 },
							uvCenter, uvCenter, uvCenter, uvCenter, material, opacity);
					if (!opaque(x, y - 1))
						pushQuadUv(vertices, indices, { 0, 1, 0 },
							{ x0, yTop, z1 }, { x1, yTop, z1 }, { x1, yTop, z0 }, { x0, yTop, z0 },
							uvCenter, uvCenter, uvCenter, uvCenter, material, opacity);
				}
			}
			return !vertices.empty();
		}
	};

}
