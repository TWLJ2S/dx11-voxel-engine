#define _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING
#define _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
#define NOMINMAX

#include <header/window.h>
#include <header/graphics.h>
#include <header/applicationPipeline.h>
#include <header/player.h>
#include <header/debug.h>
#include <header/shader.h>
#include <header/texture.h>
#include <assetManager/assetManager.h>
#include <world/worldStreamer.h>
#include <world/gpuChunkMesher.h>

#include <renderer/buffer.h>
#include <renderer/light.h>
#include <renderer/staticRenderer.h>
#include <renderer/dynamicRenderer.h>
#include <ui/ui.h>

#include <DirectXMath.h>
#include <DirectXCollision.h>
#include <wrl/client.h>

#include <memory>
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <cstdlib>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace dx = DirectX;

namespace {
	struct voxelCoordinate {
		int32_t x = 0;
		int32_t y = 0;
		int32_t z = 0;

		bool operator==(const voxelCoordinate& other) const {
			return x == other.x && y == other.y && z == other.z;
		}
	};

	struct voxelCoordinateHash {
		size_t operator()(const voxelCoordinate& position) const {
			uint32_t hash = static_cast<uint32_t>(position.x) * 73856093u ^
				static_cast<uint32_t>(position.y) * 19349663u ^
				static_cast<uint32_t>(position.z) * 83492791u;
			hash ^= hash >> 16u;
			return hash;
		}
	};

	uint32_t packVoxelRadiance(const dx::XMFLOAT3& color) {
		auto channel = [](float value) {
			return static_cast<uint32_t>(std::lround(std::clamp(value * 127.5f, 0.0f, 255.0f)));
		};
		return channel(color.x) | (channel(color.y) << 8u) | (channel(color.z) << 16u);
	}

	uint32_t maxPackedRadiance(uint32_t left, uint32_t right) {
		const uint32_t r = std::max(left & 255u, right & 255u);
		const uint32_t g = std::max((left >> 8u) & 255u, (right >> 8u) & 255u);
		const uint32_t b = std::max((left >> 16u) & 255u, (right >> 16u) & 255u);
		return r | (g << 8u) | (b << 16u);
	}

	class gpuFrameTimer {
		struct queryFrame {
			Microsoft::WRL::ComPtr<ID3D11Query> disjoint;
			Microsoft::WRL::ComPtr<ID3D11Query> start;
			Microsoft::WRL::ComPtr<ID3D11Query> end;
			bool pending = false;
		};
		std::array<queryFrame, 4> _frames{};
		size_t _writeIndex = 0;
		bool _recording = false;
		float _milliseconds = 0.0f;

	public:
		void create(ID3D11Device* device) {
			D3D11_QUERY_DESC timestamp{ D3D11_QUERY_TIMESTAMP, 0u };
			D3D11_QUERY_DESC disjoint{ D3D11_QUERY_TIMESTAMP_DISJOINT, 0u };
			for (queryFrame& frame : _frames) {
				DX_CHECK(device->CreateQuery(&disjoint, frame.disjoint.GetAddressOf()));
				DX_CHECK(device->CreateQuery(&timestamp, frame.start.GetAddressOf()));
				DX_CHECK(device->CreateQuery(&timestamp, frame.end.GetAddressOf()));
			}
		}

		void resolve(ID3D11DeviceContext* context) {
			for (queryFrame& frame : _frames) {
				if (!frame.pending) continue;
				D3D11_QUERY_DATA_TIMESTAMP_DISJOINT frequency{};
				if (context->GetData(frame.disjoint.Get(), &frequency, sizeof(frequency), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
					continue;
				uint64_t start = 0;
				uint64_t end = 0;
				if (context->GetData(frame.start.Get(), &start, sizeof(start), 0u) == S_OK &&
					context->GetData(frame.end.Get(), &end, sizeof(end), 0u) == S_OK &&
					!frequency.Disjoint && frequency.Frequency != 0u && end >= start) {
					_milliseconds = static_cast<float>(
						static_cast<double>(end - start) * 1000.0 / static_cast<double>(frequency.Frequency));
				}
				frame.pending = false;
			}
		}

		void begin(ID3D11DeviceContext* context) {
			resolve(context);
			for (size_t attempt = 0; attempt < _frames.size(); ++attempt) {
				queryFrame& frame = _frames[_writeIndex];
				if (!frame.pending) {
					context->Begin(frame.disjoint.Get());
					context->End(frame.start.Get());
					_recording = true;
					return;
				}
				_writeIndex = (_writeIndex + 1u) % _frames.size();
			}
		}

		void end(ID3D11DeviceContext* context) {
			if (!_recording) return;
			queryFrame& frame = _frames[_writeIndex];
			context->End(frame.end.Get());
			context->End(frame.disjoint.Get());
			frame.pending = true;
			_recording = false;
			_writeIndex = (_writeIndex + 1u) % _frames.size();
		}

		float milliseconds() const { return _milliseconds; }
	};

	struct staticPipeline {
		ac::shaderProgram program;
		ac::shaderProgram faceProgram;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
		Microsoft::WRL::ComPtr<ID3D11BlendState> opaqueBlend;
		Microsoft::WRL::ComPtr<ID3D11BlendState> translucentBlend;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> opaqueDepth;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> translucentDepth;

		staticPipeline(ID3D11Device* device) {
			program.initVertexShader(
				device,
				L"asset/shader/staticVertex.hlsl",
				"main",
				"vs_5_0"
			);
			program.initPixelShader(
				device,
				L"asset/shader/staticPixel.hlsl",
				"main",
				"ps_5_0"
			);
			faceProgram.initVertexShader(device, L"asset/shader/chunkFaceVertex.hlsl", "main", "vs_5_0");
			faceProgram.initPixelShader(device, L"asset/shader/staticPixel.hlsl", "main", "ps_5_0");

			program.initInputLayout(device, {
				{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ac::vertex, _position), D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"NORMAL",	 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ac::vertex, _normal),	 D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,	 0, offsetof(ac::vertex, _uv),		 D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"MATID",	 0, DXGI_FORMAT_R32_UINT,		 0, offsetof(ac::vertex, _material), D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"AO",		 0, DXGI_FORMAT_R32_UINT,		 0, offsetof(ac::vertex, _ao),		 D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"OPACITY",  0, DXGI_FORMAT_R32_FLOAT,		 0, offsetof(ac::vertex, _opacity),  D3D11_INPUT_PER_VERTEX_DATA, 0}
			});

			D3D11_SAMPLER_DESC d{};
			d.Filter = D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT;
			d.AddressU = d.AddressV = d.AddressW =
				D3D11_TEXTURE_ADDRESS_WRAP;
			d.MaxLOD = D3D11_FLOAT32_MAX;

			DX_CHECK(
				device->CreateSamplerState(
					&d,
					sampler.GetAddressOf()
				)
			);

			D3D11_BLEND_DESC blend{};
			blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			DX_CHECK(device->CreateBlendState(&blend, opaqueBlend.GetAddressOf()));

			blend.RenderTarget[0].BlendEnable = TRUE;
			blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
			blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
			blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
			blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
			blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			DX_CHECK(device->CreateBlendState(&blend, translucentBlend.GetAddressOf()));

			D3D11_DEPTH_STENCIL_DESC depth{};
			depth.DepthEnable = TRUE;
			depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
			depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
			DX_CHECK(device->CreateDepthStencilState(&depth, opaqueDepth.GetAddressOf()));

			depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			DX_CHECK(device->CreateDepthStencilState(&depth, translucentDepth.GetAddressOf()));
		}

		void bind(ID3D11DeviceContext* ctx) {
			program.bindShaders(ctx);
			bindOpaqueState(ctx);
		}

		void bindFaces(ID3D11DeviceContext* ctx) {
			faceProgram.bindShaders(ctx);
			bindOpaqueState(ctx);
		}

		void bindOpaqueState(ID3D11DeviceContext* ctx) {
			ctx->PSSetSamplers(
				0,
				1,
				sampler.GetAddressOf()
			);
			ctx->OMSetBlendState(opaqueBlend.Get(), nullptr, 0xffffffff);
			ctx->OMSetDepthStencilState(opaqueDepth.Get(), 0);
		}

		void bindTranslucent(ID3D11DeviceContext* ctx) {
			ctx->OMSetBlendState(translucentBlend.Get(), nullptr, 0xffffffff);
			ctx->OMSetDepthStencilState(translucentDepth.Get(), 0);
		}
	};

	struct pointShadowMap {
		static constexpr UINT resolution = 512;

		struct shadowData {
			dx::XMFLOAT4X4 viewProjection;
			dx::XMFLOAT3 lightPosition;
			float lightRadius;
			uint32_t entityOnly;
			dx::XMUINT3 padding{};
		};

		ac::shaderProgram program;
		ac::shaderProgram faceProgram;
		ac::constantBuffer<shadowData> buffer;
		ac::constantBuffer<ac::objectData> faceObjectBuffer;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		std::array<Microsoft::WRL::ComPtr<ID3D11DepthStencilView>, 6> depthViews;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderView;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> hardSampler;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthState;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerState;

		pointShadowMap(ID3D11Device* device) {
			program.initVertexShader(device, L"asset/shader/pointShadowVertex.hlsl", "main", "vs_5_0");
			program.initPixelShader(device, L"asset/shader/pointShadowPixel.hlsl", "main", "ps_5_0");
			faceProgram.initVertexShader(device, L"asset/shader/chunkFaceShadowVertex.hlsl", "main", "vs_5_0");
			faceProgram.initPixelShader(device, L"asset/shader/pointShadowPixel.hlsl", "main", "ps_5_0");
			program.initInputLayout(device, {
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ac::vertex, _position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ac::vertex, _normal), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(ac::vertex, _uv), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "MATID", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(ac::vertex, _material), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "AO", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(ac::vertex, _ao), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "OPACITY", 0, DXGI_FORMAT_R32_FLOAT, 0, offsetof(ac::vertex, _opacity), D3D11_INPUT_PER_VERTEX_DATA, 0 }
			});
			buffer.create(device);
			faceObjectBuffer.create(device);

			D3D11_TEXTURE2D_DESC textureDesc{};
			textureDesc.Width = resolution;
			textureDesc.Height = resolution;
			textureDesc.MipLevels = 1;
			textureDesc.ArraySize = 6;
			textureDesc.Format = DXGI_FORMAT_R32_TYPELESS;
			textureDesc.SampleDesc.Count = 1;
			textureDesc.Usage = D3D11_USAGE_DEFAULT;
			textureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
			textureDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
			DX_CHECK(device->CreateTexture2D(&textureDesc, nullptr, texture.GetAddressOf()));

			for (UINT face = 0; face < 6; ++face) {
				D3D11_DEPTH_STENCIL_VIEW_DESC viewDesc{};
				viewDesc.Format = DXGI_FORMAT_D32_FLOAT;
				viewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
				viewDesc.Texture2DArray.MipSlice = 0;
				viewDesc.Texture2DArray.FirstArraySlice = face;
				viewDesc.Texture2DArray.ArraySize = 1;
				DX_CHECK(device->CreateDepthStencilView(texture.Get(), &viewDesc, depthViews[face].GetAddressOf()));
			}

			D3D11_SHADER_RESOURCE_VIEW_DESC resourceDesc{};
			resourceDesc.Format = DXGI_FORMAT_R32_FLOAT;
			resourceDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
			resourceDesc.TextureCube.MostDetailedMip = 0;
			resourceDesc.TextureCube.MipLevels = 1;
			DX_CHECK(device->CreateShaderResourceView(texture.Get(), &resourceDesc, shaderView.GetAddressOf()));

			D3D11_SAMPLER_DESC samplerDesc{};
			samplerDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
			samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
			samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
			DX_CHECK(device->CreateSamplerState(&samplerDesc, sampler.GetAddressOf()));
			samplerDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
			DX_CHECK(device->CreateSamplerState(&samplerDesc, hardSampler.GetAddressOf()));

			D3D11_DEPTH_STENCIL_DESC depthDesc{};
			depthDesc.DepthEnable = TRUE;
			depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
			depthDesc.DepthFunc = D3D11_COMPARISON_LESS;
			DX_CHECK(device->CreateDepthStencilState(&depthDesc, depthState.GetAddressOf()));

			D3D11_RASTERIZER_DESC rasterizerDesc{};
			rasterizerDesc.FillMode = D3D11_FILL_SOLID;
			rasterizerDesc.CullMode = D3D11_CULL_BACK;
			rasterizerDesc.DepthClipEnable = TRUE;
			DX_CHECK(device->CreateRasterizerState(&rasterizerDesc, rasterizerState.GetAddressOf()));
		}

		void render(
			ID3D11DeviceContext* context,
			ac::staticRenderer& renderer,
			ac::dynamicRenderer& dynamicRenderer,
			std::unordered_map<ac::worldChunkKey, ac::worldChunkGPU, ac::worldChunkKeyHash>& chunks,
			const ac::gpuLight& light,
			bool includeWorldCasters = true
		) {
			Microsoft::WRL::ComPtr<ID3D11RenderTargetView> previousTarget;
			Microsoft::WRL::ComPtr<ID3D11DepthStencilView> previousDepth;
			context->OMGetRenderTargets(1, previousTarget.GetAddressOf(), previousDepth.GetAddressOf());
			UINT viewportCount = 1;
			D3D11_VIEWPORT previousViewport{};
			context->RSGetViewports(&viewportCount, &previousViewport);
			Microsoft::WRL::ComPtr<ID3D11RasterizerState> previousRasterizer;
			context->RSGetState(previousRasterizer.GetAddressOf());

			ID3D11ShaderResourceView* nullResource = nullptr;
			context->PSSetShaderResources(65, 1, &nullResource);
			D3D11_VIEWPORT shadowViewport{ 0.0f, 0.0f, static_cast<float>(resolution), static_cast<float>(resolution), 0.0f, 1.0f };
			context->RSSetViewports(1, &shadowViewport);
			context->RSSetState(rasterizerState.Get());
			context->OMSetDepthStencilState(depthState.Get(), 0);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			program.bindShaders(context);
			// Write radial distance rather than face-projected depth. Radial depth
			// stays continuous when filtered samples cross cubemap face boundaries.

			const dx::XMVECTOR eye = dx::XMLoadFloat3(&light.position);
			const dx::XMFLOAT3 directions[6] = {
				{ 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
				{ 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
			};
			const dx::XMFLOAT3 upVectors[6] = {
				{ 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, -1 },
				{ 0, 0, 1 }, { 0, 1, 0 }, { 0, 1, 0 }
			};
			const dx::XMMATRIX projection = dx::XMMatrixPerspectiveFovLH(
				dx::XM_PIDIV2,
				1.0f,
				0.05f,
				light.radius
			);

			for (UINT face = 0; face < 6; ++face) {
				context->OMSetRenderTargets(0, nullptr, depthViews[face].Get());
				context->ClearDepthStencilView(depthViews[face].Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

				const dx::XMMATRIX view = dx::XMMatrixLookToLH(
					eye,
					dx::XMLoadFloat3(&directions[face]),
					dx::XMLoadFloat3(&upVectors[face])
				);
				dx::BoundingFrustum localShadowFrustum;
				dx::BoundingFrustum worldShadowFrustum;
				dx::BoundingFrustum::CreateFromMatrix(localShadowFrustum, projection);
				localShadowFrustum.Transform(
					worldShadowFrustum,
					dx::XMMatrixInverse(nullptr, view)
				);
				shadowData data{};
				dx::XMStoreFloat4x4(&data.viewProjection, dx::XMMatrixTranspose(view * projection));
				data.lightPosition = light.position;
				data.lightRadius = light.radius;
				data.entityOnly = includeWorldCasters ? 0u : 1u;
				buffer.update(context, data);
				buffer.bindVS(context, 4);
				buffer.bindPS(context, 4);

				if (includeWorldCasters) for (auto& pair : chunks) {
					ac::worldChunkGPU& worldChunk = pair.second;
					if (!worldChunk._chunk)
						continue;
					bool hasOpaqueSection = false;
					for (const ac::gpuModel& section : worldChunk._opaqueSections)
						hasOpaqueSection |= section._index.count() != 0;
					if (worldChunk._gpuFaces.count(false) == 0 && !hasOpaqueSection)
						continue;

					const auto& position = worldChunk._chunk->_position;
					const float minX = static_cast<float>(position.x * CHUNK_WIDTH);
					const float minZ = static_cast<float>(position.z * CHUNK_LENGTH);
					const float closestX = std::clamp(light.position.x, minX, minX + CHUNK_WIDTH);
					const float closestY = std::clamp(light.position.y, 0.0f, static_cast<float>(CHUNK_HEIGHT));
					const float closestZ = std::clamp(light.position.z, minZ, minZ + CHUNK_LENGTH);
					const float dx = closestX - light.position.x;
					const float dy = closestY - light.position.y;
					const float dz = closestZ - light.position.z;
					if (dx * dx + dy * dy + dz * dz > light.radius * light.radius)
						continue;

					const dx::BoundingBox chunkBounds(
						{
							minX + CHUNK_WIDTH * 0.5f,
							CHUNK_HEIGHT * 0.5f,
							minZ + CHUNK_LENGTH * 0.5f
						},
						{
							CHUNK_WIDTH * 0.5f,
							CHUNK_HEIGHT * 0.5f,
							CHUNK_LENGTH * 0.5f
						}
					);
					if (worldShadowFrustum.Contains(chunkBounds) == dx::DISJOINT)
						continue;

					dx::XMFLOAT4X4 transform;
					dx::XMStoreFloat4x4(&transform, dx::XMMatrixTranspose(dx::XMMatrixTranslation(minX, 0.0f, minZ)));
					if (worldChunk._hasGpuMesh) {
						renderer.render();
						faceProgram.bindShaders(context);
						faceObjectBuffer.update(context, { transform });
						faceObjectBuffer.bindVS(context, 0);
						worldChunk._gpuFaces.draw(context, false);
					}
					else {
						program.bindShaders(context);
						for (ac::gpuModel& section : worldChunk._opaqueSections)
							if (section._index.count() != 0)
								renderer.submit({ &section, nullptr, transform });
					}
				}
				renderer.render();
				// Generic dynamic meshes use the regular point-shadow vertex format.
				// Humanoids then bind their skinned shadow shader so animated limbs and
				// alpha-cutout skin layers cast their actual silhouettes.
				program.bindShaders(context);
				dynamicRenderer.renderMeshShadows(light.position, light.radius);
				dynamicRenderer.renderHumanoidShadows(light.position, light.radius);
				ID3D11ShaderResourceView* nullFace = nullptr;
				context->VSSetShaderResources(66, 1, &nullFace);
			}

			context->OMSetRenderTargets(1, previousTarget.GetAddressOf(), previousDepth.Get());
			context->RSSetViewports(1, &previousViewport);
			context->RSSetState(previousRasterizer.Get());
		}

		void bind(ID3D11DeviceContext* context) {
			context->PSSetShaderResources(65, 1, shaderView.GetAddressOf());
			context->PSSetSamplers(1, 1, sampler.GetAddressOf());
			context->PSSetSamplers(2, 1, hardSampler.GetAddressOf());
			// b4 is reused by voxel-light settings during the world pass. Mirror the
			// active shadow light into b6 so receivers can still evaluate this map.
			buffer.bindPS(context, 6);
		}

		void disable(ID3D11DeviceContext* context) {
			// A zero radius makes receiver sampling return fully visible while
			// retaining a valid bound constant buffer and cubemap resource.
			buffer.update(context, shadowData{});
		}
	};

	class blockOutlineRenderer {
	private:
		struct outlineVertex {
			dx::XMFLOAT3 position;
		};

		struct outlineData {
			dx::XMFLOAT4X4 worldViewProjection;
			dx::XMFLOAT4 color;
		};

		ac::shaderProgram _program;
		ac::constantBuffer<outlineData> _constantBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> _vertexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> _indexBuffer;
		Microsoft::WRL::ComPtr<ID3D11BlendState> _blendState;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> _depthState;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> _rasterizerState;

	public:
		void create(ID3D11Device* device) {
			_program.initVertexShader(device, L"asset/shader/blockOutline.hlsl", "vertexMain", "vs_5_0");
			_program.initPixelShader(device, L"asset/shader/blockOutline.hlsl", "pixelMain", "ps_5_0");
			_program.initInputLayout(device, {
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(outlineVertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0 }
			});
			_constantBuffer.create(device);

			constexpr std::array<outlineVertex, 8> vertices = {
				outlineVertex{ { 0.0f, 0.0f, 0.0f } }, outlineVertex{ { 1.0f, 0.0f, 0.0f } },
				outlineVertex{ { 1.0f, 1.0f, 0.0f } }, outlineVertex{ { 0.0f, 1.0f, 0.0f } },
				outlineVertex{ { 0.0f, 0.0f, 1.0f } }, outlineVertex{ { 1.0f, 0.0f, 1.0f } },
				outlineVertex{ { 1.0f, 1.0f, 1.0f } }, outlineVertex{ { 0.0f, 1.0f, 1.0f } }
			};
			constexpr std::array<uint16_t, 24> indices = {
				0, 1, 1, 2, 2, 3, 3, 0,
				4, 5, 5, 6, 6, 7, 7, 4,
				0, 4, 1, 5, 2, 6, 3, 7
			};

			D3D11_BUFFER_DESC vertexDescription{};
			vertexDescription.ByteWidth = sizeof(vertices);
			vertexDescription.Usage = D3D11_USAGE_IMMUTABLE;
			vertexDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			D3D11_SUBRESOURCE_DATA vertexData{ vertices.data() };
			DX_CHECK(device->CreateBuffer(&vertexDescription, &vertexData, _vertexBuffer.GetAddressOf()));

			D3D11_BUFFER_DESC indexDescription{};
			indexDescription.ByteWidth = sizeof(indices);
			indexDescription.Usage = D3D11_USAGE_IMMUTABLE;
			indexDescription.BindFlags = D3D11_BIND_INDEX_BUFFER;
			D3D11_SUBRESOURCE_DATA indexData{ indices.data() };
			DX_CHECK(device->CreateBuffer(&indexDescription, &indexData, _indexBuffer.GetAddressOf()));

			D3D11_BLEND_DESC blend{};
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
		}

		void render(
			ID3D11DeviceContext* context,
			const dx::XMINT3& block,
			const dx::XMFLOAT4X4& view,
			const dx::XMFLOAT4X4& projection
		) {
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

			const dx::XMMATRIX world = dx::XMMatrixScaling(1.006f, 1.006f, 1.006f) *
				dx::XMMatrixTranslation(
					static_cast<float>(block.x) - 0.003f,
					static_cast<float>(block.y) - 0.003f,
					static_cast<float>(block.z) - 0.003f
				);
			outlineData data{};
			dx::XMStoreFloat4x4(
				&data.worldViewProjection,
				dx::XMMatrixTranspose(world * dx::XMLoadFloat4x4(&view) * dx::XMLoadFloat4x4(&projection))
			);
			data.color = { 0.65f, 0.88f, 1.0f, 1.0f };
			_constantBuffer.update(context, data);
			_constantBuffer.bindVS(context, 0);
			_program.bindShaders(context);
			context->GSSetShader(nullptr, nullptr, 0);
			context->HSSetShader(nullptr, nullptr, 0);
			context->DSSetShader(nullptr, nullptr, 0);
			context->OMSetBlendState(_blendState.Get(), nullptr, 0xffffffffu);
			context->OMSetDepthStencilState(_depthState.Get(), 0);
			context->RSSetState(_rasterizerState.Get());
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
			const UINT stride = sizeof(outlineVertex);
			const UINT offset = 0;
			ID3D11Buffer* vertexBuffer = _vertexBuffer.Get();
			context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
			context->IASetIndexBuffer(_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
			context->DrawIndexed(24, 0, 0);

			ID3D11Buffer* oldVertexBufferPointer = oldVertexBuffer.Get();
			ID3D11Buffer* oldConstantBufferPointer = oldConstantBuffer.Get();
			context->IASetInputLayout(oldLayout.Get());
			context->IASetPrimitiveTopology(oldTopology);
			context->IASetVertexBuffers(0, 1, &oldVertexBufferPointer, &oldVertexStride, &oldVertexOffset);
			context->IASetIndexBuffer(oldIndexBuffer.Get(), oldIndexFormat, oldIndexOffset);
			context->VSSetConstantBuffers(0, 1, &oldConstantBufferPointer);
			context->VSSetShader(oldVertexShader.Get(), nullptr, 0);
			context->PSSetShader(oldPixelShader.Get(), nullptr, 0);
			context->GSSetShader(oldGeometryShader.Get(), nullptr, 0);
			context->HSSetShader(oldHullShader.Get(), nullptr, 0);
			context->DSSetShader(oldDomainShader.Get(), nullptr, 0);
			context->OMSetBlendState(oldBlendState.Get(), oldBlendFactor, oldSampleMask);
			context->OMSetDepthStencilState(oldDepthState.Get(), oldStencilReference);
			context->RSSetState(oldRasterizerState.Get());
		}
	};

	class voxelPipeline final : public ac::applicationPipeline {
	private:
		ac::window& _window;
		ac::graphicsContext& _graphicsSettings;
		ID3D11Device* _device;
		ID3D11DeviceContext* _context;
		staticPipeline& _pipeline;
		pointShadowMap& _shadowMap;
		ac::staticRenderer& _renderer;
		ac::blockTextureSet& _blockTextures;
		ac::staticAssetManager& _blocks;
		ac::world& _world;
		ac::worldStreamer& _streamer;
		ac::player& _player;
		ac::dynamicRenderer& _dynamicRenderer;
		ac::dynamicEntityHandle _playerEntity;
		ac::constantBuffer<ac::cameraData>& _cameraBuffer;
		ac::structuredBuffer<ac::gpuLight>& _lights;
		std::vector<ac::gpuLight>& _lightData;
		ac::constantBuffer<ac::lightBufferData>& _lightBuffer;
		ac::structuredBuffer<uint32_t>& _lightOccluders;
		std::vector<uint32_t>& _lightOccluderData;
		ac::constantBuffer<ac::lightOcclusionBufferData>& _lightOcclusionBuffer;
		ac::structuredBuffer<ac::gpuVoxelLight> _voxelLights;
		ac::constantBuffer<ac::voxelLightBufferData> _voxelLightBuffer;
		ac::constantBuffer<ac::chunkLightBufferData> _chunkLightBuffer;
		std::vector<ac::gpuVoxelLight> _voxelLightTable =
			std::vector<ac::gpuVoxelLight>(ac::VOXEL_LIGHT_TABLE_SIZE);
		std::vector<ac::gpuVoxelLight> _voxelLightScratch =
			std::vector<ac::gpuVoxelLight>(ac::VOXEL_LIGHT_TABLE_SIZE);
		ac::constantBuffer<ac::objectData> _gpuObjectBuffer;
		gpuFrameTimer _lightingTimer;
		blockOutlineRenderer _outlineRenderer;
		ac::uiRenderer _uiRenderer;
		ac::uiCanvas _hud;
		ac::uiInputState _uiInput;
		std::array<ac::uiElementId, 9> _hotbarSlots{};
		std::array<ac::uiElementId, 9> _hotbarIcons{};
		std::array<ac::blockId, 9> _hotbarBlocks{};
		ac::uiElementId _hotbarSelectionOutline = 0;
		std::array<ac::uiElementId, 27> _inventorySlots{};
		std::array<ac::uiElementId, 27> _inventoryIcons{};
		std::array<ac::blockId, 27> _inventoryBlocks{};
		std::array<ac::uiElementId, 4> _crosshairElements{};
		std::vector<ac::uiElementId> _inventoryElements;
		ac::uiElementId _inventoryDimmer = 0;
		ac::uiElementId _inventorySelectionOutline = 0;
		ac::uiElementId _dragGhost = 0;
		ac::uiElementId _fpsPanel = 0;
		ac::uiElementId _gpuTimePanel = 0;
		ac::uiElementId _settingsPanel = 0;
		ac::uiElementId _settingsTitle = 0;
		ac::uiElementId _vsyncButton = 0;
		ac::uiElementId _fpsLimitLabel = 0;
		ac::uiElementId _fpsLimitDecrease = 0;
		ac::uiElementId _fpsLimitIncrease = 0;
		ac::uiElementId _lightingQualityButton = 0;
		float _fpsElapsed = 0.0f;
		uint32_t _fpsFrameCount = 0;
		bool _vsyncEnabled = true;
		uint32_t _lightingQuality = 1u;
		bool _voxelLightingHasData = false;
		inline static constexpr std::array<int, 7> FPS_LIMITS = { 30, 60, 120, 144, 240, 300, 0 };
		size_t _fpsLimitIndex = 5;
		size_t _inventoryBlockCount = 0;
		size_t _inventoryCursor = 0;
		int _selectedHotbarSlot = 0;
		ac::blockId _selectedBlock = 1;
		bool _inventoryOpen = false;
		ac::cameraData _cameraData{};
		dx::XMFLOAT4X4 _renderView{};
		dx::XMFLOAT4X4 _renderProjection{};
		dx::XMFLOAT3 _renderAimOrigin{};
		dx::XMFLOAT3 _renderAimDirection{ 0.0f, 0.0f, 1.0f };
		bool _renderAimValid = false;
		dx::XMINT3 _renderedTargetBlock{};
		dx::XMINT3 _renderedTargetAdjacent{};
		ac::blockId _renderedTargetId = 0;
		bool _renderedTargetValid = false;
		float _bodycamRoll = 0.0f;
		float _bodycamPitchLean = 0.0f;
		float _bodycamMotion = 0.0f;
		float _bodycamClock = 0.0f;
		float _bodycamForwardLag = 0.0f;
		float _bodycamVerticalLag = 0.0f;
		float _crouchCameraOffset = 0.0f;
		float _previousBodycamYaw = 0.0f;
		bool _bodycamInitialized = false;
		bool _spectatorMode = false;
		bool _spawnPending = true;
		uint64_t _lightOcclusionSignature = UINT64_MAX;
		uint64_t _lightOcclusionBlockRevision = UINT64_MAX;
		uint64_t _blockLightingRevision = UINT64_MAX;
		uint64_t _pendingBlockLightingRevision = UINT64_MAX;
		std::chrono::steady_clock::time_point _pendingBlockLightingSince{};
		std::chrono::steady_clock::time_point _lastBlockLightingBuild{};
		std::vector<ac::gpuLight> _propagatedBlockSources;
		std::vector<ac::gpuLight> _pendingBlockSources;
		bool _pendingBlockLightingAffected = false;
		bool _pendingBlockLightingStreaming = false;
		std::vector<uint8_t> _blockOcclusionLookup = std::vector<uint8_t>(256, 2u);
		bool _cursorCaptured = true;
		// F5 cycles: first person, rear shoulder, front-facing third person.
		uint8_t _cameraMode = 0u;

		bool solidBlock(int32_t x, int32_t y, int32_t z) const {
			if (y < 0) return true;
			const ac::blockId id = _streamer.blockAt(x, y, z);
			if (id == 0u) return false;
			const ac::blockDefinition* definition = _blocks.get(id);
			return definition && definition->_occludes;
		}

		std::optional<dx::XMFLOAT3> initialSpawnPoint() const {
			const dx::XMFLOAT3 current = _player.getCamera()._gpuData._position;
			const int32_t chunkX = static_cast<int32_t>(std::floor(current.x / static_cast<float>(CHUNK_WIDTH)));
			const int32_t chunkZ = static_cast<int32_t>(std::floor(current.z / static_cast<float>(CHUNK_LENGTH)));
			int32_t bestY = -1;
			int32_t bestX = 0;
			int32_t bestZ = 0;
			for (int32_t z = chunkZ * CHUNK_LENGTH; z < (chunkZ + 1) * CHUNK_LENGTH; ++z) {
				for (int32_t x = chunkX * CHUNK_WIDTH; x < (chunkX + 1) * CHUNK_WIDTH; ++x) {
					for (int32_t y = CHUNK_HEIGHT - 3; y >= 0; --y) {
						if (!solidBlock(x, y, z)) continue;
						if (solidBlock(x, y + 1, z) || solidBlock(x, y + 2, z)) break;
						if (y > bestY) { bestY = y; bestX = x; bestZ = z; }
						break;
					}
				}
			}
			if (bestY < 0) return std::nullopt; // The initial chunk is not available yet.
			return dx::XMFLOAT3{
				static_cast<float>(bestX) + .5f,
				static_cast<float>(bestY) + 2.62f,
				static_cast<float>(bestZ) + .5f
			};
		}

		void updateDynamicEntities() {
			const dx::XMFLOAT3 playerPosition = _player.getCamera()._gpuData._position;
			_dynamicRenderer.setHumanoidPose(
				_playerEntity,
				playerPosition,
				_player.getVelocity(),
				_player.getYaw(),
				_player.getPitch(),
				_player.isGrounded(),
				_player.isCrouching(),
				_player.isSprinting(),
				_cameraMode != 0u
			);
			if (ac::dynamicEntity* entity = _dynamicRenderer.get(_playerEntity))
				entity->visible = !_spectatorMode && !_spawnPending;
		}

		bool cameraPositionClear(const dx::XMFLOAT3& position, float radius = 0.18f) const {
			// Treat touching a voxel face as clear. Without this inset, a camera
			// exactly flush with a wall or ceiling repeatedly changed obstruction
			// state as floating-point movement settled against the block.
			constexpr float epsilon = .001f;
			const int32_t minX = static_cast<int32_t>(std::floor(position.x - radius + epsilon));
			const int32_t minY = static_cast<int32_t>(std::floor(position.y - radius + epsilon));
			const int32_t minZ = static_cast<int32_t>(std::floor(position.z - radius + epsilon));
			const int32_t maxX = static_cast<int32_t>(std::floor(position.x + radius - epsilon));
			const int32_t maxY = static_cast<int32_t>(std::floor(position.y + radius - epsilon));
			const int32_t maxZ = static_cast<int32_t>(std::floor(position.z + radius - epsilon));
			for (int32_t z = minZ; z <= maxZ; ++z) {
				for (int32_t y = minY; y <= maxY; ++y) {
					for (int32_t x = minX; x <= maxX; ++x) {
						const ac::blockId id = _streamer.blockAt(x, y, z);
						if (id == 0u) continue;
						const ac::blockDefinition* definition = _blocks.get(id);
						if (definition && definition->_occludes)
							return false;
					}
				}
			}
			return true;
		}

		dx::XMFLOAT3 unobstructedCameraPosition(
			const dx::XMFLOAT3& anchor,
			const dx::XMFLOAT3& desired,
			float radius = 0.18f
		) const {
			const dx::XMVECTOR start = dx::XMLoadFloat3(&anchor);
			const dx::XMVECTOR offset = dx::XMVectorSubtract(dx::XMLoadFloat3(&desired), start);
			const float distance = dx::XMVectorGetX(dx::XMVector3Length(offset));
			if (distance <= 0.001f) return anchor;

			const uint32_t steps = (std::max)(1u, static_cast<uint32_t>(std::ceil(distance / 0.01f)));
			dx::XMFLOAT3 safe = anchor;
			for (uint32_t step = 1u; step <= steps; ++step) {
				const float amount = static_cast<float>(step) / static_cast<float>(steps);
				dx::XMFLOAT3 candidate;
				dx::XMStoreFloat3(&candidate, dx::XMVectorMultiplyAdd(offset, dx::XMVectorReplicate(amount), start));
				if (!cameraPositionClear(candidate, radius)) break;
				safe = candidate;
			}
			return safe;
		}

		void rebuildVoxelLighting(const std::vector<ac::gpuLight>& sources) {
			std::unordered_map<voxelCoordinate, uint32_t, voxelCoordinateHash> radiance;
			radiance.reserve(sources.size() * 8192u);

			const auto occludes = [&](const voxelCoordinate& position) {
				const ac::blockId id = _streamer.blockAt(position.x, position.y, position.z);
				if (id == 0u) return false;
				const ac::blockDefinition* definition = _blocks.get(id);
				return definition && definition->_occludes && definition->_emission.intensity <= 0.0f;
			};
			const auto visibleRay = [&](const dx::XMFLOAT3& start, const dx::XMFLOAT3& end) {
				voxelCoordinate voxel{
					static_cast<int32_t>(std::floor(start.x)),
					static_cast<int32_t>(std::floor(start.y)),
					static_cast<int32_t>(std::floor(start.z))
				};
				const voxelCoordinate endVoxel{
					static_cast<int32_t>(std::floor(end.x)),
					static_cast<int32_t>(std::floor(end.y)),
					static_cast<int32_t>(std::floor(end.z))
				};
				const dx::XMFLOAT3 ray{ end.x - start.x, end.y - start.y, end.z - start.z };
				const int32_t stepX = ray.x >= 0.0f ? 1 : -1;
				const int32_t stepY = ray.y >= 0.0f ? 1 : -1;
				const int32_t stepZ = ray.z >= 0.0f ? 1 : -1;
				const float infinity = (std::numeric_limits<float>::infinity)();
				const float deltaX = std::abs(ray.x) > 1e-6f ? std::abs(1.0f / ray.x) : infinity;
				const float deltaY = std::abs(ray.y) > 1e-6f ? std::abs(1.0f / ray.y) : infinity;
				const float deltaZ = std::abs(ray.z) > 1e-6f ? std::abs(1.0f / ray.z) : infinity;
				float nextX = std::abs(ray.x) > 1e-6f
					? ((stepX > 0 ? static_cast<float>(voxel.x + 1) : static_cast<float>(voxel.x)) - start.x) / ray.x
					: infinity;
				float nextY = std::abs(ray.y) > 1e-6f
					? ((stepY > 0 ? static_cast<float>(voxel.y + 1) : static_cast<float>(voxel.y)) - start.y) / ray.y
					: infinity;
				float nextZ = std::abs(ray.z) > 1e-6f
					? ((stepZ > 0 ? static_cast<float>(voxel.z + 1) : static_cast<float>(voxel.z)) - start.z) / ray.z
					: infinity;

				for (uint32_t iteration = 0; iteration < 64u && !(voxel == endVoxel); ++iteration) {
					const float nearest = std::min({ nextX, nextY, nextZ });
					if (std::abs(nextX - nearest) < 1e-5f) { voxel.x += stepX; nextX += deltaX; }
					if (std::abs(nextY - nearest) < 1e-5f) { voxel.y += stepY; nextY += deltaY; }
					if (std::abs(nextZ - nearest) < 1e-5f) { voxel.z += stepZ; nextZ += deltaZ; }
					if (voxel == endVoxel) return true;
					if (occludes(voxel)) return false;
				}
				return true;
			};

			const auto emitterVisibility = [&](const ac::gpuLight& source, const voxelCoordinate& target) {
				const dx::XMFLOAT3 receiver{
					static_cast<float>(target.x) + 0.5f,
					static_cast<float>(target.y) + 0.5f,
					static_cast<float>(target.z) + 0.5f
				};
				const dx::XMFLOAT3 fromEmitter{
					receiver.x - source.position.x,
					receiver.y - source.position.y,
					receiver.z - source.position.z
				};
				const dx::XMFLOAT3 absolute{
					std::abs(fromEmitter.x), std::abs(fromEmitter.y), std::abs(fromEmitter.z)
				};
				std::array<dx::XMFLOAT3, 5> samples{};
				const float u = 0.34f;
				if (absolute.x >= absolute.y && absolute.x >= absolute.z) {
					const float face = source.position.x + (fromEmitter.x >= 0.0f ? source.halfExtent.x : -source.halfExtent.x);
					samples = { dx::XMFLOAT3{face, source.position.y, source.position.z},
						dx::XMFLOAT3{face, source.position.y-u, source.position.z-u}, dx::XMFLOAT3{face, source.position.y+u, source.position.z+u},
						dx::XMFLOAT3{face, source.position.y-u, source.position.z+u}, dx::XMFLOAT3{face, source.position.y+u, source.position.z-u} };
				}
				else if (absolute.y >= absolute.z) {
					const float face = source.position.y + (fromEmitter.y >= 0.0f ? source.halfExtent.y : -source.halfExtent.y);
					samples = { dx::XMFLOAT3{source.position.x, face, source.position.z},
						dx::XMFLOAT3{source.position.x-u, face, source.position.z-u}, dx::XMFLOAT3{source.position.x+u, face, source.position.z+u},
						dx::XMFLOAT3{source.position.x-u, face, source.position.z+u}, dx::XMFLOAT3{source.position.x+u, face, source.position.z-u} };
				}
				else {
					const float face = source.position.z + (fromEmitter.z >= 0.0f ? source.halfExtent.z : -source.halfExtent.z);
					samples = { dx::XMFLOAT3{source.position.x, source.position.y, face},
						dx::XMFLOAT3{source.position.x-u, source.position.y-u, face}, dx::XMFLOAT3{source.position.x+u, source.position.y+u, face},
						dx::XMFLOAT3{source.position.x-u, source.position.y+u, face}, dx::XMFLOAT3{source.position.x+u, source.position.y-u, face} };
				}
				const uint32_t sampleCount = _lightingQuality == 0u ? 1u : (_lightingQuality == 1u ? 3u : 5u);
				uint32_t visibleSamples = 0u;
				for (uint32_t sample = 0; sample < sampleCount; ++sample)
					visibleSamples += visibleRay(receiver, samples[sample]) ? 1u : 0u;
				const float directVisibility = static_cast<float>(visibleSamples) / sampleCount;
				// Retain a small indirect component so fully shadowed areas do not
				// become unnaturally black while still reading clearly as occluded.
				return 0.08f + directVisibility * 0.92f;
			};

			struct propagationNode {
				voxelCoordinate position;
				uint16_t distance = 0;
			};
			constexpr std::array<voxelCoordinate, 6> directions = {
				voxelCoordinate{-1, 0, 0}, voxelCoordinate{1, 0, 0},
				voxelCoordinate{0, -1, 0}, voxelCoordinate{0, 1, 0},
				voxelCoordinate{0, 0, -1}, voxelCoordinate{0, 0, 1}
			};

			for (const ac::gpuLight& source : sources) {
				const int radius = std::max(1, static_cast<int>(std::ceil(source.radius)));
				const voxelCoordinate origin = {
					static_cast<int32_t>(std::floor(source.position.x)),
					static_cast<int32_t>(std::floor(source.position.y)),
					static_cast<int32_t>(std::floor(source.position.z))
				};
				std::vector<propagationNode> queue;
				queue.reserve(static_cast<size_t>(radius * radius * radius * 2));
				std::unordered_set<voxelCoordinate, voxelCoordinateHash> visited;
				visited.reserve(queue.capacity());
				queue.push_back({ origin, 0u });
				visited.insert(origin);

				for (size_t head = 0; head < queue.size(); ++head) {
					const propagationNode node = queue[head];
					if (node.distance > radius || (node.distance != 0u && occludes(node.position)))
						continue;

					const float normalized = static_cast<float>(node.distance) / static_cast<float>(radius);
					// Quadratic decay keeps the configured reach but makes glowstone lose
					// intensity much more clearly as distance increases.
					const float remainingLight = (std::max)(0.0f, 1.0f - normalized);
					const float attenuation = std::pow(remainingLight, 1.65f);
					const float visibility = node.distance == 0u ? 1.0f : emitterVisibility(source, node.position);
					const uint32_t packed = packVoxelRadiance({
						source.color.x * source.intensity * attenuation * visibility,
						source.color.y * source.intensity * attenuation * visibility,
						source.color.z * source.intensity * attenuation * visibility
					});
					if (packed != 0u) {
						auto [entry, inserted] = radiance.emplace(node.position, packed);
						if (!inserted) entry->second = maxPackedRadiance(entry->second, packed);
					}

					if (node.distance == radius) continue;
					for (const voxelCoordinate& direction : directions) {
						voxelCoordinate next = {
							node.position.x + direction.x,
							node.position.y + direction.y,
							node.position.z + direction.z
						};
						if (next.y < 0 || next.y >= CHUNK_HEIGHT || !visited.insert(next).second)
							continue;
						queue.push_back({ next, static_cast<uint16_t>(node.distance + 1u) });
					}
				}
			}

			std::fill(_voxelLightScratch.begin(), _voxelLightScratch.end(), ac::gpuVoxelLight{});
			const uint32_t tableMask = ac::VOXEL_LIGHT_TABLE_SIZE - 1u;
			for (const auto& [position, packedColor] : radiance) {
				uint32_t slot = static_cast<uint32_t>(voxelCoordinateHash{}(position)) & tableMask;
				for (uint32_t probe = 0; probe < 24u; ++probe) {
					ac::gpuVoxelLight& target = _voxelLightScratch[slot];
					if (target.packedColor == 0u) {
						target.position = { position.x, position.y, position.z };
						target.packedColor = packedColor;
						break;
					}
					slot = (slot + 1u) & tableMask;
				}
			}

			auto differs = [](const ac::gpuVoxelLight& left, const ac::gpuVoxelLight& right) {
				return left.position.x != right.position.x || left.position.y != right.position.y ||
					left.position.z != right.position.z || left.packedColor != right.packedColor;
			};
			size_t changed = 0;
			for (size_t index = 0; index < _voxelLightTable.size(); ++index)
				changed += differs(_voxelLightTable[index], _voxelLightScratch[index]);
			if (changed > _voxelLightTable.size() / 4u) {
				_voxelLights.update(_context, _voxelLightScratch.data(), ac::VOXEL_LIGHT_TABLE_SIZE);
			}
			else if (changed != 0u) {
				constexpr size_t MAX_UNCHANGED_GAP = 32u;
				std::vector<std::pair<UINT, UINT>> ranges;
				ranges.reserve(128);
				size_t index = 0;
				while (index < _voxelLightTable.size()) {
					while (index < _voxelLightTable.size() &&
						!differs(_voxelLightTable[index], _voxelLightScratch[index])) ++index;
					if (index == _voxelLightTable.size()) break;
					const size_t first = index++;
					size_t lastChanged = first;
					while (index < _voxelLightTable.size()) {
						if (differs(_voxelLightTable[index], _voxelLightScratch[index]))
							lastChanged = index;
						else if (index - lastChanged > MAX_UNCHANGED_GAP)
							break;
						++index;
					}
					const UINT count = static_cast<UINT>(lastChanged - first + 1u);
					ranges.emplace_back(static_cast<UINT>(first), count);
					if (ranges.size() > 128u) break;
					index = lastChanged + 1u;
				}
				if (ranges.size() > 128u) {
					_voxelLights.update(_context, _voxelLightScratch.data(), ac::VOXEL_LIGHT_TABLE_SIZE);
				}
				else {
					for (const auto& [first, count] : ranges)
						_voxelLights.updateRange(_context, _voxelLightScratch.data() + first, first, count);
				}
			}
			_voxelLightTable.swap(_voxelLightScratch);
			_voxelLightingHasData = !radiance.empty();
			_voxelLightBuffer.update(_context, {
				tableMask,
				_voxelLightingHasData ? 1u : 0u,
				_lightingQuality,
				0u
			});
		}

		std::vector<ac::gpuLight> collectBlockLightSources() const {
			std::vector<ac::gpuLight> sources;
			for (const auto& [chunkKey, worldChunk] : _streamer.chunks()) {
				(void)chunkKey;
				for (const auto& emitter : worldChunk._emitters) {
					const ac::blockDefinition* definition = _blocks.get(emitter.id);
					if (!definition) continue;
					const ac::blockLight& emission = definition->_emission;
					sources.push_back({
						{ static_cast<float>(emitter.position.x) + 0.5f,
						  static_cast<float>(emitter.position.y) + 0.5f,
						  static_cast<float>(emitter.position.z) + 0.5f },
						emission.radius, emission.color, emission.intensity,
						{ 0.49f, 0.49f, 0.49f }, ac::GPU_LIGHT_MESH
					});
				}
			}
			return sources;
		}

		static bool blockLightCanReach(const ac::gpuLight& light, const dx::XMINT3& block) {
			const float distance =
				std::abs(light.position.x - (static_cast<float>(block.x) + 0.5f)) +
				std::abs(light.position.y - (static_cast<float>(block.y) + 0.5f)) +
				std::abs(light.position.z - (static_cast<float>(block.z) + 0.5f));
			return distance <= light.radius + 1.0f;
		}

		void updateBlockLights() {
			const uint64_t revision = _streamer.blockRevision();
			if (revision == _blockLightingRevision)
				return;
			const auto now = std::chrono::steady_clock::now();
			if (revision != _pendingBlockLightingRevision) {
				const bool hadPending = _pendingBlockLightingRevision != _blockLightingRevision;
				if (!hadPending) _pendingBlockLightingSince = now;
				_pendingBlockLightingRevision = revision;
				_pendingBlockSources = collectBlockLightSources();
				dx::XMINT3 localizedPosition{};
				const bool localizedEdit = _streamer.localizedBlockChange(revision, localizedPosition);
				if (localizedEdit) {
					auto reaches = [&](const std::vector<ac::gpuLight>& sources) {
						return std::any_of(sources.begin(), sources.end(), [&](const ac::gpuLight& light) {
							return blockLightCanReach(light, localizedPosition);
						});
					};
					_pendingBlockLightingAffected |= reaches(_pendingBlockSources) || reaches(_propagatedBlockSources);
				}
				else {
					_pendingBlockLightingAffected = true;
					_pendingBlockLightingStreaming = true;
				}
			}
			if (!_pendingBlockLightingAffected) {
				_blockLightingRevision = revision;
				_pendingBlockLightingRevision = revision;
				_propagatedBlockSources = _pendingBlockSources;
				return;
			}
			const auto delay = _pendingBlockLightingStreaming
				? std::chrono::milliseconds(400) : std::chrono::milliseconds(75);
			const auto elapsedFrom = _pendingBlockLightingStreaming
				? _pendingBlockLightingSince : _lastBlockLightingBuild;
			if (elapsedFrom.time_since_epoch().count() != 0 && now - elapsedFrom < delay)
				return;

			_blockLightingRevision = revision;
			_pendingBlockLightingRevision = revision;
			_lastBlockLightingBuild = now;
			_propagatedBlockSources = _pendingBlockSources;
			_pendingBlockLightingAffected = false;
			_pendingBlockLightingStreaming = false;

			// Block emitters use the cached propagated field. The separate dynamic
			// list remains available for moving point and mesh lights.
			if (!_lightData.empty())
				_lights.update(_context, _lightData.data(), static_cast<UINT>(_lightData.size()));
			_lightBuffer.update(_context, {
				static_cast<uint32_t>(_lightData.size()),
				{ 0, 0, 0 }
			});
			if (_lightData.empty())
				_chunkLightBuffer.update(_context, {});
			rebuildVoxelLighting(_propagatedBlockSources);
		}

		ac::chunkLightBufferData chunkLightsFor(const dx::XMINT3& chunkPosition) const {
			ac::chunkLightBufferData result{};
			uint32_t* indices = reinterpret_cast<uint32_t*>(result.indices.data());
			const dx::XMFLOAT3 minimum = {
				static_cast<float>(chunkPosition.x * CHUNK_WIDTH),
				static_cast<float>(chunkPosition.y * CHUNK_HEIGHT),
				static_cast<float>(chunkPosition.z * CHUNK_LENGTH)
			};
			const dx::XMFLOAT3 maximum = {
				minimum.x + CHUNK_WIDTH,
				minimum.y + CHUNK_HEIGHT,
				minimum.z + CHUNK_LENGTH
			};

			const uint32_t maximumLights = _lightingQuality == 0u ? 16u :
				(_lightingQuality == 1u ? 32u : 64u);
			for (uint32_t index = 0; index < _lightData.size() && result.count < maximumLights; ++index) {
				const ac::gpuLight& light = _lightData[index];
				const dx::XMFLOAT3 extent = light.type == ac::GPU_LIGHT_POINT
					? dx::XMFLOAT3{} : light.halfExtent;
				const float closestX = std::clamp(light.position.x, minimum.x - extent.x, maximum.x + extent.x);
				const float closestY = std::clamp(light.position.y, minimum.y - extent.y, maximum.y + extent.y);
				const float closestZ = std::clamp(light.position.z, minimum.z - extent.z, maximum.z + extent.z);
				const float dx = light.position.x - closestX;
				const float dy = light.position.y - closestY;
				const float dz = light.position.z - closestZ;
				if (dx * dx + dy * dy + dz * dz > light.radius * light.radius)
					continue;
				indices[result.count++] = index;
			}
			return result;
		}

		void bindChunkLights(const dx::XMINT3& chunkPosition) {
			if (_lightData.empty()) return;
			_chunkLightBuffer.update(_context, chunkLightsFor(chunkPosition));
			_chunkLightBuffer.bindPS(_context, 5);
		}

		ID3D11ShaderResourceView* blockIcon(ac::blockId id) const {
			const ac::blockDefinition* definition = _blocks.get(id);
			if (!definition)
				return nullptr;
			ac::texture* item = _blockTextures.get(definition->materialForFace(ac::BLOCK_FACE_UP));
			return item ? item->_shaderResourceView.Get() : nullptr;
		}

		void refreshHotbarIcons() {
			for (size_t slot = 0; slot < _hotbarIcons.size(); ++slot) {
				auto& icon = _hud.get(_hotbarIcons[slot]);
				icon.texture = blockIcon(_hotbarBlocks[slot]);
				icon.visible = icon.texture != nullptr;
			}
			_selectedBlock = _hotbarBlocks[_selectedHotbarSlot];
		}

		void updateFps(float deltaTime) {
			_fpsElapsed += std::max(deltaTime, 0.0f);
			++_fpsFrameCount;
			if (_fpsElapsed < 0.25f)
				return;
			const int fps = static_cast<int>(std::lround(_fpsFrameCount / _fpsElapsed));
			_hud.get(_fpsPanel).text = "FPS: " + std::to_string(fps);
			const int gpuTenths = static_cast<int>(std::lround(_lightingTimer.milliseconds() * 10.0f));
			_hud.get(_gpuTimePanel).text = "WORLD: " + std::to_string(gpuTenths / 10) + "." +
				std::to_string(std::abs(gpuTenths % 10)) + " MS";
			_fpsElapsed = 0.0f;
			_fpsFrameCount = 0;
		}

		void refreshSettingsText() {
			_hud.get(_vsyncButton).text = _vsyncEnabled ? "VSYNC: ON" : "VSYNC: OFF";
			const int limit = FPS_LIMITS[_fpsLimitIndex];
			_hud.get(_fpsLimitLabel).text = limit == 0
				? "LIMIT: UNLIMITED"
				: "LIMIT: " + std::to_string(limit);
			static constexpr std::array<const char*, 3> qualityNames = { "LOW", "BALANCED", "HIGH" };
			_hud.get(_lightingQualityButton).text =
				"LIGHTING: " + std::string(qualityNames[_lightingQuality]);
		}

		void applyLightingQuality() {
			_voxelLightBuffer.update(_context, {
				ac::VOXEL_LIGHT_TABLE_SIZE - 1u,
				_voxelLightingHasData ? 1u : 0u,
				_lightingQuality,
				0u
			});
			refreshSettingsText();
		}

		void applyFpsLimit() {
			const int limit = FPS_LIMITS[_fpsLimitIndex];
			if (limit == 0) _window.setFocusedDeltaTimeLimit(0);
			else _window.setFocusedFpsLimit(static_cast<UINT>(limit));
			refreshSettingsText();
		}

		void setInventoryOpen(bool open) {
			_inventoryOpen = open;
			for (ac::uiElementId id : _inventoryElements)
				_hud.get(id).visible = open;
			for (size_t slot = 0; slot < _inventoryIcons.size(); ++slot)
				_hud.get(_inventoryIcons[slot]).visible =
					open && _hud.get(_inventoryIcons[slot]).texture != nullptr;
			_hud.get(_inventorySelectionOutline).visible = open && _inventoryBlockCount != 0;
			_hud.get(_dragGhost).visible = false;
			for (ac::uiElementId id : _crosshairElements)
				_hud.get(id).visible = !open;

			_window.setInputMode(GLFW_CURSOR, open ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
			_window.setUpdateDeltaCursor(!open);
			_cursorCaptured = !open;
		}

		void buildHud() {
			auto addPanel = [this](
				DirectX::XMFLOAT2 position,
				DirectX::XMFLOAT2 size,
				DirectX::XMFLOAT4 color,
				ac::uiAnchor anchor,
				bool interactive = false
			) {
				ac::uiElement element{};
				element.position = position;
				element.size = size;
				element.pivot = { 0.5f, 0.5f };
				element.color = color;
				element.anchor = anchor;
				element.interactive = interactive;
				return _hud.add(element);
			};

			addPanel({ 0.0f, -36.0f }, { 414.0f, 58.0f }, { 0.025f, 0.035f, 0.05f, 0.82f }, ac::uiAnchor::bottomCenter);
			_hotbarSelectionOutline = addPanel(
				{ -176.0f, -36.0f },
				{ 48.0f, 48.0f },
				{ 0.25f, 0.72f, 1.0f, 0.95f },
				ac::uiAnchor::bottomCenter
			);
			for (int slot = 0; slot < 9; ++slot) {
				const float x = static_cast<float>((slot - 4) * 44);
				_hotbarSlots[slot] = addPanel(
					{ x, -36.0f },
					{ 40.0f, 40.0f },
					slot == 0
						? DirectX::XMFLOAT4{ 0.12f, 0.18f, 0.24f, 0.96f }
						: DirectX::XMFLOAT4{ 0.075f, 0.09f, 0.12f, 0.9f },
					ac::uiAnchor::bottomCenter,
					true
				);

				_hotbarIcons[slot] = addPanel(
					{ x, -36.0f },
					{ 30.0f, 30.0f },
					{ 1.0f, 1.0f, 1.0f, 1.0f },
					ac::uiAnchor::bottomCenter
				);
			}

			_crosshairElements[0] = addPanel({ 0.0f, 0.0f }, { 24.0f, 4.0f }, { 0.0f, 0.0f, 0.0f, 0.7f }, ac::uiAnchor::center);
			_crosshairElements[1] = addPanel({ 0.0f, 0.0f }, { 4.0f, 24.0f }, { 0.0f, 0.0f, 0.0f, 0.7f }, ac::uiAnchor::center);
			_crosshairElements[2] = addPanel({ 0.0f, 0.0f }, { 20.0f, 2.0f }, { 0.95f, 0.98f, 1.0f, 0.95f }, ac::uiAnchor::center);
			_crosshairElements[3] = addPanel({ 0.0f, 0.0f }, { 2.0f, 20.0f }, { 0.95f, 0.98f, 1.0f, 0.95f }, ac::uiAnchor::center);

			const std::vector<uint32_t> blockIds = _blocks.ids();
			_inventoryBlockCount = std::min<size_t>(_inventoryBlocks.size(), blockIds.size());
			for (size_t slot = 0; slot < _hotbarBlocks.size() && slot < blockIds.size(); ++slot)
				_hotbarBlocks[slot] = blockIds[slot];
			for (size_t slot = 0; slot < _inventoryBlockCount; ++slot)
				_inventoryBlocks[slot] = blockIds[slot];

			auto addInventoryElement = [&](ac::uiElementId id) {
				_inventoryElements.push_back(id);
				_hud.get(id).visible = false;
				return id;
			};

			_inventoryDimmer = addInventoryElement(addPanel(
				{ 0.0f, 0.0f },
				{ 1.0f, 1.0f },
				{ 0.0f, 0.0f, 0.0f, 0.48f },
				ac::uiAnchor::center
			));
			addInventoryElement(addPanel(
				{ 0.0f, 0.0f },
				{ 414.0f, 150.0f },
				{ 0.025f, 0.035f, 0.05f, 0.97f },
				ac::uiAnchor::center
			));
			_inventorySelectionOutline = addInventoryElement(addPanel(
				{ -176.0f, -44.0f },
				{ 48.0f, 48.0f },
				{ 0.25f, 0.72f, 1.0f, 1.0f },
				ac::uiAnchor::center
			));

			for (size_t slot = 0; slot < _inventorySlots.size(); ++slot) {
				const float x = static_cast<float>((static_cast<int>(slot % 9) - 4) * 44);
				const float y = static_cast<float>((static_cast<int>(slot / 9) - 1) * 44);
				_inventorySlots[slot] = addInventoryElement(addPanel(
					{ x, y },
					{ 40.0f, 40.0f },
					{ 0.075f, 0.09f, 0.12f, 0.96f },
					ac::uiAnchor::center,
					slot < _inventoryBlockCount
				));
				_inventoryIcons[slot] = addInventoryElement(addPanel(
					{ x, y },
					{ 30.0f, 30.0f },
					{ 1.0f, 1.0f, 1.0f, 1.0f },
					ac::uiAnchor::center
				));
				auto& icon = _hud.get(_inventoryIcons[slot]);
				icon.texture = blockIcon(_inventoryBlocks[slot]);
				if (!icon.texture)
					icon.visible = false;
			}
			_dragGhost = addInventoryElement(addPanel(
				{ 0.0f, 0.0f },
				{ 34.0f, 34.0f },
				{ 1.0f, 1.0f, 1.0f, 0.88f },
				ac::uiAnchor::topLeft
			));

			auto configureSettingsElement = [&](ac::uiElementId id, DirectX::XMFLOAT2 offset,
				const std::string& text, float fontSize = 16.0f) {
				auto& element = _hud.get(id);
				element.pivot = { 0.0f, 0.0f };
				element.text = text;
				element.fontSize = fontSize;
				element.textOffset = offset;
				element.textColor = { 0.9f, 0.96f, 1.0f, 1.0f };
			};
			_settingsPanel = addInventoryElement(addPanel(
				{ -252.0f, 12.0f }, { 240.0f, 178.0f },
				{ 0.025f, 0.035f, 0.05f, 0.96f }, ac::uiAnchor::topRight
			));
			_hud.get(_settingsPanel).pivot = { 0.0f, 0.0f };
			_settingsTitle = addInventoryElement(addPanel(
				{ -238.0f, 22.0f }, { 210.0f, 26.0f },
				{ 0, 0, 0, 0 }, ac::uiAnchor::topRight
			));
			configureSettingsElement(_settingsTitle, { 0.0f, 1.0f }, "DISPLAY", 18.0f);
			_vsyncButton = addInventoryElement(addPanel(
				{ -238.0f, 54.0f }, { 210.0f, 32.0f },
				{ 0.08f, 0.12f, 0.16f, 0.98f }, ac::uiAnchor::topRight, true
			));
			configureSettingsElement(_vsyncButton, { 10.0f, 4.0f }, "VSYNC: ON");
			_fpsLimitDecrease = addInventoryElement(addPanel(
				{ -238.0f, 96.0f }, { 34.0f, 32.0f },
				{ 0.08f, 0.12f, 0.16f, 0.98f }, ac::uiAnchor::topRight, true
			));
			configureSettingsElement(_fpsLimitDecrease, { 11.0f, 4.0f }, "-");
			_fpsLimitLabel = addInventoryElement(addPanel(
				{ -198.0f, 96.0f }, { 130.0f, 32.0f },
				{ 0.055f, 0.075f, 0.10f, 0.98f }, ac::uiAnchor::topRight
			));
			configureSettingsElement(_fpsLimitLabel, { 7.0f, 5.0f }, "LIMIT: 300", 12.0f);
			_fpsLimitIncrease = addInventoryElement(addPanel(
				{ -62.0f, 96.0f }, { 34.0f, 32.0f },
				{ 0.08f, 0.12f, 0.16f, 0.98f }, ac::uiAnchor::topRight, true
			));
			configureSettingsElement(_fpsLimitIncrease, { 10.0f, 4.0f }, "+");
			_lightingQualityButton = addInventoryElement(addPanel(
				{ -238.0f, 138.0f }, { 210.0f, 32.0f },
				{ 0.08f, 0.12f, 0.16f, 0.98f }, ac::uiAnchor::topRight, true
			));
			configureSettingsElement(_lightingQualityButton, { 10.0f, 5.0f }, "LIGHTING: BALANCED", 13.0f);

			_fpsPanel = addPanel(
				{ 12.0f, 12.0f },
				{ 116.0f, 30.0f },
				{ 0.02f, 0.03f, 0.045f, 0.76f },
				ac::uiAnchor::topLeft
			);
			auto& fpsPanel = _hud.get(_fpsPanel);
			fpsPanel.pivot = { 0.0f, 0.0f };
			fpsPanel.text = "FPS: --";
			fpsPanel.fontSize = 17.0f;
			fpsPanel.textOffset = { 8.0f, 3.0f };
			fpsPanel.textColor = { 0.88f, 0.95f, 1.0f, 1.0f };

			_gpuTimePanel = addPanel(
				{ 12.0f, 46.0f },
				{ 150.0f, 26.0f },
				{ 0.02f, 0.03f, 0.045f, 0.70f },
				ac::uiAnchor::topLeft
			);
			auto& gpuPanel = _hud.get(_gpuTimePanel);
			gpuPanel.pivot = { 0.0f, 0.0f };
			gpuPanel.text = "WORLD: --.- MS";
			gpuPanel.fontSize = 14.0f;
			gpuPanel.textOffset = { 7.0f, 3.0f };
			gpuPanel.textColor = { 0.72f, 0.88f, 1.0f, 1.0f };

			refreshHotbarIcons();
			refreshSettingsText();
		}

		void updateHudInput() {
			_uiInput.update(_window);
			_hud.get(_inventoryDimmer).size = {
				static_cast<float>(_window.getWidth()),
				static_cast<float>(_window.getHeight())
			};
			_hud.updateInput(
				_uiInput,
				static_cast<float>(_window.getWidth()),
				static_cast<float>(_window.getHeight())
			);

			if (_inventoryOpen && _hud.wasClicked(_vsyncButton)) {
				_vsyncEnabled = !_vsyncEnabled;
				_graphicsSettings.setVsync(_vsyncEnabled);
				refreshSettingsText();
			}
			if (_inventoryOpen && _hud.wasClicked(_lightingQualityButton)) {
				_lightingQuality = (_lightingQuality + 1u) % 3u;
				applyLightingQuality();
			}
			if (_inventoryOpen && _hud.wasClicked(_fpsLimitDecrease) && _fpsLimitIndex > 0) {
				--_fpsLimitIndex;
				applyFpsLimit();
			}
			if (_inventoryOpen && _hud.wasClicked(_fpsLimitIncrease) && _fpsLimitIndex + 1 < FPS_LIMITS.size()) {
				++_fpsLimitIndex;
				applyFpsLimit();
			}

			for (int slot = 0; slot < 9; ++slot) {
				if (_uiInput.keyPressed(GLFW_KEY_1 + slot) || _hud.wasClicked(_hotbarSlots[slot]))
					_selectedHotbarSlot = slot;
			}
			if (!_inventoryOpen && _uiInput.scrollDelta.y != 0.0f) {
				const int wheelSteps = std::max(1, static_cast<int>(std::lround(std::abs(_uiInput.scrollDelta.y))));
				const int direction = _uiInput.scrollDelta.y > 0.0f ? -1 : 1;
				_selectedHotbarSlot = (_selectedHotbarSlot + direction * wheelSteps) % 9;
				if (_selectedHotbarSlot < 0) _selectedHotbarSlot += 9;
			}

			if (_inventoryOpen && _inventoryBlockCount != 0) {
				if (_uiInput.keyPressed(GLFW_KEY_LEFT) && _inventoryCursor > 0)
					--_inventoryCursor;
				if (_uiInput.keyPressed(GLFW_KEY_RIGHT) && _inventoryCursor + 1 < _inventoryBlockCount)
					++_inventoryCursor;
				if (_uiInput.keyPressed(GLFW_KEY_UP) && _inventoryCursor >= 9)
					_inventoryCursor -= 9;
				if (_uiInput.keyPressed(GLFW_KEY_DOWN) && _inventoryCursor + 9 < _inventoryBlockCount)
					_inventoryCursor += 9;

				for (size_t slot = 0; slot < _inventoryBlockCount; ++slot) {
					if (_hud.wasClicked(_inventorySlots[slot])) {
						_inventoryCursor = slot;
						_selectedBlock = _inventoryBlocks[slot];
						_hotbarBlocks[_selectedHotbarSlot] = _selectedBlock;
						refreshHotbarIcons();
					}
				}
				if (_uiInput.keyPressed(GLFW_KEY_ENTER) || _uiInput.keyPressed(GLFW_KEY_KP_ENTER)) {
					_selectedBlock = _inventoryBlocks[_inventoryCursor];
					_hotbarBlocks[_selectedHotbarSlot] = _selectedBlock;
					refreshHotbarIcons();
				}

				if (_hud.dragSource() && _hud.dropTarget()) {
					const ac::uiElementId source = *_hud.dragSource();
					const ac::uiElementId target = *_hud.dropTarget();
					for (size_t targetSlot = 0; targetSlot < _hotbarSlots.size(); ++targetSlot) {
						if (target != _hotbarSlots[targetSlot])
							continue;
						for (size_t sourceSlot = 0; sourceSlot < _inventoryBlockCount; ++sourceSlot) {
							if (source == _inventorySlots[sourceSlot]) {
								_hotbarBlocks[targetSlot] = _inventoryBlocks[sourceSlot];
								_selectedHotbarSlot = static_cast<int>(targetSlot);
								refreshHotbarIcons();
							}
						}
						for (size_t sourceSlot = 0; sourceSlot < _hotbarSlots.size(); ++sourceSlot) {
							if (source == _hotbarSlots[sourceSlot] && sourceSlot != targetSlot) {
								std::swap(_hotbarBlocks[sourceSlot], _hotbarBlocks[targetSlot]);
								_selectedHotbarSlot = static_cast<int>(targetSlot);
								refreshHotbarIcons();
							}
						}
					}
				}

				auto& outline = _hud.get(_inventorySelectionOutline);
				outline.position = {
					static_cast<float>((static_cast<int>(_inventoryCursor % 9) - 4) * 44),
					static_cast<float>((static_cast<int>(_inventoryCursor / 9) - 1) * 44)
				};
				for (size_t slot = 0; slot < _inventorySlots.size(); ++slot) {
					auto& element = _hud.get(_inventorySlots[slot]);
					if (_hud.isActive(_inventorySlots[slot]))
						element.color = { 0.18f, 0.30f, 0.40f, 0.99f };
					else if (_hud.isHovered(_inventorySlots[slot]))
						element.color = { 0.13f, 0.21f, 0.29f, 0.98f };
					else
						element.color = { 0.075f, 0.09f, 0.12f, 0.96f };
				}
			}

			auto& dragGhost = _hud.get(_dragGhost);
			dragGhost.visible = false;
			if (_inventoryOpen && _hud.active()) {
				ac::blockId draggedBlock = 0;
				for (size_t slot = 0; slot < _inventoryBlockCount; ++slot)
					if (*_hud.active() == _inventorySlots[slot]) draggedBlock = _inventoryBlocks[slot];
				for (size_t slot = 0; slot < _hotbarSlots.size(); ++slot)
					if (*_hud.active() == _hotbarSlots[slot]) draggedBlock = _hotbarBlocks[slot];
				if (draggedBlock != 0) {
					dragGhost.texture = blockIcon(draggedBlock);
					dragGhost.position = _uiInput.mousePosition;
					dragGhost.visible = dragGhost.texture != nullptr;
				}
			}

			_selectedBlock = _hotbarBlocks[_selectedHotbarSlot];

			_hud.get(_hotbarSelectionOutline).position.x =
				static_cast<float>((_selectedHotbarSlot - 4) * 44);
			for (int slot = 0; slot < 9; ++slot) {
				auto& element = _hud.get(_hotbarSlots[slot]);
				if (_hud.isActive(_hotbarSlots[slot]))
					element.color = { 0.18f, 0.30f, 0.40f, 0.98f };
				else if (_hud.isHovered(_hotbarSlots[slot]))
					element.color = { 0.13f, 0.21f, 0.29f, 0.96f };
				else if (slot == _selectedHotbarSlot)
					element.color = { 0.12f, 0.18f, 0.24f, 0.96f };
				else
					element.color = { 0.075f, 0.09f, 0.12f, 0.9f };
			}
		}

		struct blockRayHit {
			dx::XMINT3 block{};
			dx::XMINT3 adjacent{};
			ac::blockId id = 0;
		};

		std::optional<blockRayHit> raycastBlock(float reach = 8.0f) const {
			const dx::XMFLOAT3 origin = _renderAimValid
				? _renderAimOrigin
				: _player.getCamera()._gpuData._position;
			const dx::XMFLOAT3 direction = _renderAimValid
				? _renderAimDirection
				: _player.getLookDirection();
			dx::XMINT3 cell = {
				static_cast<int32_t>(std::floor(origin.x)),
				static_cast<int32_t>(std::floor(origin.y)),
				static_cast<int32_t>(std::floor(origin.z))
			};
			dx::XMINT3 previous = cell;
			const int32_t stepX = direction.x > 0.0f ? 1 : (direction.x < 0.0f ? -1 : 0);
			const int32_t stepY = direction.y > 0.0f ? 1 : (direction.y < 0.0f ? -1 : 0);
			const int32_t stepZ = direction.z > 0.0f ? 1 : (direction.z < 0.0f ? -1 : 0);
			const float infinity = (std::numeric_limits<float>::infinity)();
			const float deltaX = stepX == 0 ? infinity : std::abs(1.0f / direction.x);
			const float deltaY = stepY == 0 ? infinity : std::abs(1.0f / direction.y);
			const float deltaZ = stepZ == 0 ? infinity : std::abs(1.0f / direction.z);
			float maxX = stepX > 0 ? (static_cast<float>(cell.x + 1) - origin.x) / direction.x
				: (stepX < 0 ? (origin.x - static_cast<float>(cell.x)) / -direction.x : infinity);
			float maxY = stepY > 0 ? (static_cast<float>(cell.y + 1) - origin.y) / direction.y
				: (stepY < 0 ? (origin.y - static_cast<float>(cell.y)) / -direction.y : infinity);
			float maxZ = stepZ > 0 ? (static_cast<float>(cell.z + 1) - origin.z) / direction.z
				: (stepZ < 0 ? (origin.z - static_cast<float>(cell.z)) / -direction.z : infinity);
			float distance = 0.0f;

			while (distance <= reach) {
				const ac::blockId id = _streamer.blockAt(cell.x, cell.y, cell.z);
				if (id != 0)
					return blockRayHit{ cell, previous, id };
				previous = cell;
				if (maxX <= maxY && maxX <= maxZ) {
					cell.x += stepX;
					distance = maxX;
					maxX += deltaX;
				}
				else if (maxY <= maxZ) {
					cell.y += stepY;
					distance = maxY;
					maxY += deltaY;
				}
				else {
					cell.z += stepZ;
					distance = maxZ;
					maxZ += deltaZ;
				}
			}
			return std::nullopt;
		}

		void updateBlockInteraction() {
			if (_inventoryOpen || !_cursorCaptured)
				return;
			if (!_uiInput.mousePressed(GLFW_MOUSE_BUTTON_LEFT) &&
				!_uiInput.mousePressed(GLFW_MOUSE_BUTTON_RIGHT))
				return;
			_dynamicRenderer.triggerUseAnimation(_playerEntity);

			// Use the exact voxel that produced the outline in the last presented
			// frame. Recasting after movement/look input can select a neighboring
			// voxel at an edge even though the player clicked the outlined block.
			const std::optional<blockRayHit> hit = _renderedTargetValid
				? std::optional<blockRayHit>{ blockRayHit{
					_renderedTargetBlock,
					_renderedTargetAdjacent,
					_renderedTargetId
				} }
				: raycastBlock();
			if (!hit)
				return;
			if (_uiInput.mousePressed(GLFW_MOUSE_BUTTON_LEFT)) {
				_streamer.setBlock(hit->block.x, hit->block.y, hit->block.z, 0, _device, _context);
				return;
			}

			if (_selectedBlock != 0 &&
				(_spectatorMode || !_player.occupiesBlock(hit->adjacent.x, hit->adjacent.y, hit->adjacent.z))) {
				_streamer.setBlock(
					hit->adjacent.x,
					hit->adjacent.y,
					hit->adjacent.z,
					_selectedBlock,
					_device,
					_context
				);
			}
		}

		void updateLightOcclusionVolumes() {
			uint64_t signature = static_cast<uint64_t>(_lightData.size()) * 0x9e3779b97f4a7c15ULL;
			for (const ac::gpuLight& light : _lightData) {
				const int32_t x = static_cast<int32_t>(std::floor(light.position.x));
				const int32_t y = static_cast<int32_t>(std::floor(light.position.y));
				const int32_t z = static_cast<int32_t>(std::floor(light.position.z));
				const uint64_t positionHash =
					static_cast<uint64_t>(static_cast<uint32_t>(x)) * 0x9e3779b1u ^
					static_cast<uint64_t>(static_cast<uint32_t>(y)) * 0x85ebca77u ^
					static_cast<uint64_t>(static_cast<uint32_t>(z)) * 0xc2b2ae3du;
				signature ^= positionHash + 0x9e3779b97f4a7c15ULL + (signature << 6) + (signature >> 2);
			}
			const uint64_t blockRevision = _streamer.blockRevision();
			const bool lightsChanged = signature != _lightOcclusionSignature;
			if (!lightsChanged && blockRevision == _lightOcclusionBlockRevision)
				return;
			_lightOcclusionSignature = signature;
			_lightOcclusionBlockRevision = blockRevision;

			ac::lightOcclusionBufferData volumeData{};
			volumeData.size = ac::LIGHT_OCCLUSION_SIZE;
			const int32_t halfSize = static_cast<int32_t>(ac::LIGHT_OCCLUSION_SIZE / 2u);
			std::vector<uint8_t> rebuild(_lightData.size(), 1u);
			dx::XMINT3 changedBlock{};
			if (!lightsChanged && _streamer.localizedBlockChange(blockRevision, changedBlock)) {
				for (size_t lightIndex = 0; lightIndex < _lightData.size(); ++lightIndex) {
					const ac::gpuLight& light = _lightData[lightIndex];
					const dx::XMINT3 origin = {
						static_cast<int32_t>(std::floor(light.position.x)) - halfSize,
						static_cast<int32_t>(std::floor(light.position.y)) - halfSize,
						static_cast<int32_t>(std::floor(light.position.z)) - halfSize
					};
					rebuild[lightIndex] = changedBlock.x >= origin.x &&
						changedBlock.y >= origin.y && changedBlock.z >= origin.z &&
						changedBlock.x < origin.x + static_cast<int32_t>(ac::LIGHT_OCCLUSION_SIZE) &&
						changedBlock.y < origin.y + static_cast<int32_t>(ac::LIGHT_OCCLUSION_SIZE) &&
						changedBlock.z < origin.z + static_cast<int32_t>(ac::LIGHT_OCCLUSION_SIZE);
				}
			}
			const auto blockOccludesLight = [&](ac::blockId id) {
				if (id == 0u)
					return false;
				const ac::blockDefinition* definition = _blocks.get(id);
				if (definition && definition->_emission.intensity > 0.0f)
					return false;
				if (id < _blockOcclusionLookup.size()) {
					uint8_t& cached = _blockOcclusionLookup[id];
					if (cached == 2u) {
						cached = definition && definition->_occludes ? 1u : 0u;
					}
					return cached == 1u;
				}
				return definition && definition->_occludes;
			};

			for (size_t lightIndex = 0; lightIndex < _lightData.size(); ++lightIndex) {
				const ac::gpuLight& light = _lightData[lightIndex];
				const dx::XMINT3 origin = {
					static_cast<int32_t>(std::floor(light.position.x)) - halfSize,
					static_cast<int32_t>(std::floor(light.position.y)) - halfSize,
					static_cast<int32_t>(std::floor(light.position.z)) - halfSize
				};
				volumeData.origins[lightIndex] = { origin.x, origin.y, origin.z, 0 };
				if (!rebuild[lightIndex])
					continue;
				const size_t firstWord = lightIndex * ac::LIGHT_OCCLUSION_WORDS;
				std::fill_n(_lightOccluderData.begin() + firstWord, ac::LIGHT_OCCLUSION_WORDS, 0u);
				const int32_t endX = origin.x + static_cast<int32_t>(ac::LIGHT_OCCLUSION_SIZE);
				const int32_t endY = origin.y + static_cast<int32_t>(ac::LIGHT_OCCLUSION_SIZE);
				const int32_t endZ = origin.z + static_cast<int32_t>(ac::LIGHT_OCCLUSION_SIZE);

			for (const auto& pair : _streamer.chunks()) {
				const ac::chunk* chunk = pair.second._chunk.get();
				if (!chunk) continue;

				const int32_t chunkX = chunk->_position.x * CHUNK_WIDTH;
				const int32_t chunkZ = chunk->_position.z * CHUNK_LENGTH;
				const int32_t minX = std::max(origin.x, chunkX);
				const int32_t maxX = std::min(endX, chunkX + CHUNK_WIDTH);
				const int32_t minY = std::max(origin.y, 0);
				const int32_t maxY = std::min(endY, CHUNK_HEIGHT);
				const int32_t minZ = std::max(origin.z, chunkZ);
				const int32_t maxZ = std::min(endZ, chunkZ + CHUNK_LENGTH);

				for (int32_t y = minY; y < maxY; ++y) {
					for (int32_t z = minZ; z < maxZ; ++z) {
						for (int32_t x = minX; x < maxX; ++x) {
							const ac::blockId id = chunk->getBlock(
								static_cast<uint32_t>(x - chunkX),
								static_cast<uint32_t>(y),
								static_cast<uint32_t>(z - chunkZ)
							);
							if (!blockOccludesLight(id))
								continue;

							const uint32_t localX = static_cast<uint32_t>(x - origin.x);
							const uint32_t localY = static_cast<uint32_t>(y - origin.y);
							const uint32_t localZ = static_cast<uint32_t>(z - origin.z);
							const uint32_t localIndex = localX + ac::LIGHT_OCCLUSION_SIZE *
								(localZ + ac::LIGHT_OCCLUSION_SIZE * localY);
							const uint32_t index = static_cast<uint32_t>(lightIndex) * ac::LIGHT_OCCLUSION_VOXELS + localIndex;
							_lightOccluderData[index >> 5] |= 1u << (index & 31u);
						}
					}
				}
			}
			}

			_lightOccluders.update(
				_context,
				_lightOccluderData.data(),
				static_cast<UINT>(_lightOccluderData.size())
			);

			_lightOcclusionBuffer.update(_context, volumeData);
		}

		bool onUpdate(const ac::frameContext& frame) override {
			updateHudInput();
			updateFps(frame.deltaTime);
			if (_uiInput.keyPressed(GLFW_KEY_F5)) {
				_cameraMode = static_cast<uint8_t>((_cameraMode + 1u) % 3u);
				_bodycamInitialized = false;
			}
			if (_uiInput.keyPressed(GLFW_KEY_F6)) {
				_spectatorMode = !_spectatorMode;
				_player.clearVelocity();
				if (_spectatorMode) _cameraMode = 0u;
				_bodycamInitialized = false;
			}
			bool suppressWorldClick = false;

			// Prime streaming at the requested start chunk, then activate physics
			// only after a safe terrain-relative spawn can be resolved.
			_streamer.update(_player.getCamera()._gpuData._position, _device, _context);
			if (_spawnPending) {
				if (const std::optional<dx::XMFLOAT3> spawn = initialSpawnPoint()) {
					_player.teleport(*spawn);
					_spawnPending = false;
				}
			}

			if (_uiInput.keyPressed(GLFW_KEY_E)) {
				setInventoryOpen(!_inventoryOpen);
				suppressWorldClick = true;
			}
			else if (_inventoryOpen && _uiInput.keyPressed(GLFW_KEY_ESCAPE)) {
				setInventoryOpen(false);
				suppressWorldClick = true;
			}
			else if (!_inventoryOpen && _cursorCaptured &&
				_window.getKeyPress(GLFW_KEY_ESCAPE)) {

				_window.setInputMode(
					GLFW_CURSOR,
					GLFW_CURSOR_NORMAL
				);

				_window.setUpdateDeltaCursor(false);
				_cursorCaptured = false;
			}
			else if (!_inventoryOpen && !_cursorCaptured &&
				_uiInput.mousePressed(GLFW_MOUSE_BUTTON_LEFT) &&
				!_hud.active()) {

				_window.setInputMode(
					GLFW_CURSOR,
					GLFW_CURSOR_DISABLED
				);

				_window.setUpdateDeltaCursor(true);
				_cursorCaptured = true;
				suppressWorldClick = true;
			}

			if (!_inventoryOpen && !_spawnPending) {
				_player.update(
					_window.getGlfwWindow(),
					{
						static_cast<float>(_window.getDeltaCursorX()),
						static_cast<float>(_window.getDeltaCursorY())
					},
					{ 0.05f, 0.05f, 0.05f },
					19.0f,
					frame.deltaTime,
					[this](int32_t x, int32_t y, int32_t z) {
						if (y < 0) return true;
						const ac::blockId id = _streamer.blockAt(x, y, z);
						if (id == 0u) return false;
						const ac::blockDefinition* definition = _blocks.get(id);
						return definition && definition->_occludes;
					},
					_spectatorMode
				);
			}
			if (!suppressWorldClick)
				updateBlockInteraction();

			const auto& playerPosition =
				_player.getCamera()._gpuData._position;
			updateDynamicEntities();

			_streamer.update(playerPosition, _device, _context);
			updateBlockLights();
			if (!_lightData.empty())
				updateLightOcclusionVolumes();

			return true;
		}

		void onRender(const ac::frameContext& frame) override {
			_lightingTimer.begin(_context);
			const auto pointLight = std::find_if(_lightData.begin(), _lightData.end(), [](const ac::gpuLight& light) {
				return light.type == ac::GPU_LIGHT_POINT;
			});
			if (pointLight != _lightData.end()) {
				_shadowMap.render(_context, _renderer, _dynamicRenderer, _streamer.chunks(), *pointLight, true);
			}
			else if (!_spawnPending) {
				// Block emitters are propagated into the voxel-light field rather than
				// the dynamic point-light list. Select the strongest nearby real emitter
				// so the entity shadow direction and reach come from world lighting.
				const dx::XMFLOAT3 playerEye = _player.getCamera()._gpuData._position;
				const std::vector<ac::gpuLight>& blockSources = !_propagatedBlockSources.empty()
					? _propagatedBlockSources
					: _pendingBlockSources;
				const ac::gpuLight* shadowSource = nullptr;
				float bestScore = (std::numeric_limits<float>::max)();
				for (const ac::gpuLight& source : blockSources) {
					const float x = playerEye.x - source.position.x;
					const float y = playerEye.y - source.position.y;
					const float z = playerEye.z - source.position.z;
					const float distanceSquared = x * x + y * y + z * z;
					const float reach = source.radius + 1.1f;
					if (source.radius <= 0.0f || source.intensity <= 0.0f || distanceSquared > reach * reach)
						continue;
					const float score = distanceSquared /
						((std::max)(source.intensity * source.radius * source.radius, .001f));
					if (score < bestScore) {
						bestScore = score;
						shadowSource = &source;
					}
				}
				if (shadowSource)
					_shadowMap.render(_context, _renderer, _dynamicRenderer, _streamer.chunks(), *shadowSource, false);
				else
					_shadowMap.disable(_context);
			}
			else {
				_shadowMap.disable(_context);
			}

			const bool gpuFaces = _streamer.gpuMeshingEnabled();
			if (gpuFaces) _pipeline.bindFaces(_context);
			else _pipeline.bind(_context);
			_shadowMap.bind(_context);

			_blockTextures.bind(_context);

			const dx::XMFLOAT3 playerEye = _player.getCamera()._gpuData._position;
			const float crouchEyeOffset = (!_spectatorMode && _player.isCrouching()) ? -.12f : 0.0f;
			const dx::XMFLOAT3 visualPlayerEye = {
				playerEye.x,
				playerEye.y + crouchEyeOffset,
				playerEye.z
			};
			_cameraData._position = visualPlayerEye;

			_cameraData._view =
				_player.getCamera()._gpuData._view;
			const dx::XMFLOAT3 look = _player.getLookDirection();
			if (!_spectatorMode && _cameraMode == 0u) {
				const float dt = (std::max)(frame.deltaTime, .0001f);
				const float yaw = _player.getYaw();
				if (!_bodycamInitialized) {
					_previousBodycamYaw = yaw;
					_bodycamInitialized = true;
				}
				float yawDelta = yaw - _previousBodycamYaw;
				while (yawDelta > dx::XM_PI) yawDelta -= dx::XM_2PI;
				while (yawDelta < -dx::XM_PI) yawDelta += dx::XM_2PI;
				_previousBodycamYaw = yaw;

				const dx::XMVECTOR worldUp = dx::XMVectorSet(0, 1, 0, 0);
				const dx::XMVECTOR lookVector = dx::XMVector3Normalize(dx::XMLoadFloat3(&look));
				// Keep the bodycam basis horizontal. Building the right axis from an
				// almost-vertical look vector becomes unstable near the pitch clamp.
				const dx::XMFLOAT3 bodyForward = { std::cos(yaw), 0.0f, std::sin(yaw) };
				const dx::XMVECTOR bodyForwardVector = dx::XMLoadFloat3(&bodyForward);
				const dx::XMVECTOR rightVector = dx::XMVector3Normalize(dx::XMVector3Cross(worldUp, bodyForwardVector));
				dx::XMFLOAT3 right;
				dx::XMStoreFloat3(&right, rightVector);
				const dx::XMFLOAT3 velocity = _player.getVelocity();
				const float speed = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
				const float targetMotion = (std::min)(speed / 5.0f, 1.0f);
				const float response = 1.0f - std::exp(-8.0f * dt);
				const float tiltResponse = 1.0f - std::exp(-14.0f * dt);
				_bodycamMotion += (targetMotion - _bodycamMotion) * response;
				_bodycamClock += dt * (4.0f + _bodycamMotion * 3.0f);

				const float lateralSpeed = velocity.x * right.x + velocity.z * right.z;
				const float forwardSpeed = velocity.x * bodyForward.x + velocity.z * bodyForward.z;
				const float turnRate = std::clamp(yawDelta / dt, -5.0f, 5.0f);
				const float walkingRoll = std::sin(_bodycamClock) * .007f * _bodycamMotion;
				const float targetRoll = std::clamp(-turnRate * .035f - lateralSpeed * .010f + walkingRoll, -.18f, .18f);
				_bodycamRoll += (targetRoll - _bodycamRoll) * tiltResponse;
				float targetPitchLean = std::clamp(
					-forwardSpeed * .010f,
					-.14f,
					.14f
				);
				// The input pitch is clamped in player.h, but bodycam lean is applied
				// afterward. Clamp their sum as well so jumping or moving cannot rotate
				// the final view through vertical and flip its up/right basis.
				constexpr float maximumRenderedPitch = 89.0f * (dx::XM_PI / 180.0f);
				const float playerPitch = _player.getPitch();
				targetPitchLean = std::clamp(
					targetPitchLean,
					-maximumRenderedPitch - playerPitch,
					 maximumRenderedPitch - playerPitch
				);
				_bodycamPitchLean += (targetPitchLean - _bodycamPitchLean) * tiltResponse;
				_bodycamPitchLean = std::clamp(
					_bodycamPitchLean,
					-maximumRenderedPitch - playerPitch,
					 maximumRenderedPitch - playerPitch
				);
				if (_player.justLanded()) {
					_bodycamPitchLean *= .75f;
					_bodycamRoll *= .90f;
				}

				// Place the lens just beyond the front of the face. Use yaw only so
				// looking up or down cannot pull the camera into the torso.
				constexpr float faceCameraOffset = .29f;
				const dx::XMFLOAT3 faceForward = { std::cos(yaw), 0.0f, std::sin(yaw) };
				const dx::XMFLOAT3 desiredEye = {
					visualPlayerEye.x + faceForward.x * faceCameraOffset,
					visualPlayerEye.y,
					visualPlayerEye.z + faceForward.z * faceCameraOffset
				};
				// Keep the camera probe inside the symmetric movement hitbox instead of
				// extending player collision into neighboring block corners.
				const dx::XMFLOAT3 eye = unobstructedCameraPosition(visualPlayerEye, desiredEye, .005f);
				const dx::XMVECTOR leanedLook = dx::XMVector3Normalize(dx::XMVector3Rotate(
					lookVector,
					dx::XMQuaternionRotationAxis(rightVector, _bodycamPitchLean)
				));
				const dx::XMVECTOR tiltedUp = dx::XMVector3Rotate(
					worldUp,
					dx::XMQuaternionRotationAxis(leanedLook, _bodycamRoll)
				);
				_cameraData._position = eye;
				dx::XMStoreFloat4x4(
					&_cameraData._view,
					dx::XMMatrixLookToLH(dx::XMLoadFloat3(&eye), leanedLook, tiltedUp)
				);
			}
			else if (!_spectatorMode) {
				const bool frontView = _cameraMode == 2u;
				const dx::XMVECTOR up = dx::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
				const dx::XMVECTOR rightVector = dx::XMVector3Normalize(
					dx::XMVector3Cross(up, dx::XMLoadFloat3(&look))
				);
				dx::XMFLOAT3 right;
				dx::XMStoreFloat3(&right, rightVector);
				const dx::XMFLOAT3 desiredEye = {
					visualPlayerEye.x + look.x * (frontView ? 3.25f : -3.25f) + right.x * (frontView ? 0.0f : .72f),
					visualPlayerEye.y + look.y * (frontView ? 3.25f : -3.25f) + .22f,
					visualPlayerEye.z + look.z * (frontView ? 3.25f : -3.25f) + right.z * (frontView ? 0.0f : .72f)
				};
				const dx::XMFLOAT3 eye = unobstructedCameraPosition(visualPlayerEye, desiredEye);
				const dx::XMFLOAT3 focus = {
					visualPlayerEye.x + look.x * (frontView ? 0.0f : 8.0f),
					visualPlayerEye.y + look.y * (frontView ? 0.0f : 8.0f) - (frontView ? .18f : 0.0f),
					visualPlayerEye.z + look.z * (frontView ? 0.0f : 8.0f)
				};
				_cameraData._position = eye;
				dx::XMStoreFloat4x4(
					&_cameraData._view,
					dx::XMMatrixLookAtLH(
						dx::XMLoadFloat3(&eye),
						dx::XMLoadFloat3(&focus),
						up
					)
				);
			}

			_cameraData._projection =
				_player.getCamera()._gpuData._projection;
			_renderView = _cameraData._view;
			_renderProjection = _cameraData._projection;
			// The crosshair is fixed at screen center, so its world ray must use
			// the final leaned/rolled camera basis rather than the raw player aim.
			const dx::XMMATRIX inverseRenderView = dx::XMMatrixInverse(
				nullptr,
				dx::XMLoadFloat4x4(&_renderView)
			);
			dx::XMStoreFloat3(
				&_renderAimDirection,
				dx::XMVector3Normalize(inverseRenderView.r[2])
			);
			// First-person visuals may retract or sway around the face. Start the
			// interaction ray at the physical eye so that crossing a voxel edge
			// cannot change the DDA's initial/adjacent cell.
			_renderAimOrigin = (!_spectatorMode && _cameraMode == 0u)
				? playerEye
				: _cameraData._position;
			_renderAimValid = true;

			dx::BoundingFrustum viewFrustum;
			dx::BoundingFrustum worldFrustum;
			dx::BoundingFrustum::CreateFromMatrix(
				viewFrustum,
				dx::XMLoadFloat4x4(&_cameraData._projection)
			);
			viewFrustum.Transform(
				worldFrustum,
				dx::XMMatrixInverse(nullptr, dx::XMLoadFloat4x4(&_cameraData._view))
			);

			dx::XMStoreFloat4x4(
				&_cameraData._view,
				dx::XMMatrixTranspose(
					dx::XMLoadFloat4x4(&_cameraData._view)
				)
			);

			dx::XMStoreFloat4x4(
				&_cameraData._projection,
				dx::XMMatrixTranspose(
					dx::XMLoadFloat4x4(&_cameraData._projection)
				)
			);

			_cameraBuffer.update(_context, _cameraData);
			_cameraBuffer.bindVS(_context, 1);
			_cameraBuffer.bindPS(_context, 1);

			_lights.bindPS(_context, 64);
			_lightBuffer.bindPS(_context, 2);
			_lightOccluders.bindPS(_context, 67);
			_lightOcclusionBuffer.bindPS(_context, 3);
			_voxelLights.bindPS(_context, 68);
			_voxelLightBuffer.bindPS(_context, 4);
			_chunkLightBuffer.bindPS(_context, 5);

			_context->IASetPrimitiveTopology(
				D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST
			);

			struct translucentChunk {
				ac::gpuModel* _model = nullptr;
				ac::gpuChunkFaceMesh* _gpuFaces = nullptr;
				dx::XMFLOAT4X4 _transform = {};
				dx::XMINT3 _chunkPosition = {};
				float _distanceSquared = 0.0f;
			};
			std::vector<translucentChunk> translucent;
			translucent.reserve(_streamer.chunks().size() * 2u);

			for (auto& pair : _streamer.chunks()) {
				ac::worldChunkGPU& worldChunk = pair.second;

				if (!worldChunk._chunk)
					continue;

				auto& position = worldChunk._chunk->_position;
				const dx::BoundingBox chunkBounds(
					{
						static_cast<float>(position.x * CHUNK_WIDTH) + CHUNK_WIDTH * 0.5f,
						CHUNK_HEIGHT * 0.5f,
						static_cast<float>(position.z * CHUNK_LENGTH) + CHUNK_LENGTH * 0.5f
					},
					{ CHUNK_WIDTH * 0.5f, CHUNK_HEIGHT * 0.5f, CHUNK_LENGTH * 0.5f }
				);
				if (worldFrustum.Contains(chunkBounds) == dx::DISJOINT)
					continue;

				dx::XMFLOAT4X4 transform;

				dx::XMStoreFloat4x4(
					&transform,
					dx::XMMatrixTranspose(
						dx::XMMatrixTranslation(
							static_cast<float>(position.x * CHUNK_WIDTH),
							static_cast<float>(position.y * CHUNK_HEIGHT),
							static_cast<float>(position.z * CHUNK_LENGTH)
						)
					)
				);
				bindChunkLights(position);

				if (worldChunk._hasGpuMesh && worldChunk._gpuFaces.count(false) != 0) {
					_gpuObjectBuffer.update(_context, { transform });
					_gpuObjectBuffer.bindVS(_context, 0);
					worldChunk._gpuFaces.draw(_context, false);
				}
				else {
					_gpuObjectBuffer.update(_context, { transform });
					_gpuObjectBuffer.bindVS(_context, 0);
					for (ac::gpuModel& section : worldChunk._opaqueSections) {
						if (section._index.count() == 0) continue;
						section.bind(_context);
						section.draw(_context);
					}
				}

				if (worldChunk._hasGpuMesh && worldChunk._gpuFaces.count(true) != 0) {
					const float centerX = static_cast<float>(position.x * CHUNK_WIDTH) + CHUNK_WIDTH * 0.5f;
					const float centerY = static_cast<float>(position.y * CHUNK_HEIGHT) + CHUNK_HEIGHT * 0.5f;
					const float centerZ = static_cast<float>(position.z * CHUNK_LENGTH) + CHUNK_LENGTH * 0.5f;
					const float dx = centerX - _cameraData._position.x;
					const float dy = centerY - _cameraData._position.y;
					const float dz = centerZ - _cameraData._position.z;
					translucent.push_back({
						nullptr,
						&worldChunk._gpuFaces,
						transform,
						position,
						dx * dx + dy * dy + dz * dz
					});
				}
				else if (!worldChunk._hasGpuMesh) {
					const float centerX = static_cast<float>(position.x * CHUNK_WIDTH) + CHUNK_WIDTH * 0.5f;
					const float centerZ = static_cast<float>(position.z * CHUNK_LENGTH) + CHUNK_LENGTH * 0.5f;
					for (uint32_t sectionIndex = 0; sectionIndex < CHUNK_SUBCHUNKS; ++sectionIndex) {
						ac::gpuModel& section = worldChunk._translucentSections[sectionIndex];
						if (section._index.count() == 0) continue;
						const float centerY = sectionIndex * SUBCHUNK_HEIGHT + SUBCHUNK_HEIGHT * 0.5f;
						const float dx = centerX - _cameraData._position.x;
						const float dy = centerY - _cameraData._position.y;
						const float dz = centerZ - _cameraData._position.z;
						translucent.push_back({
							&section, nullptr, transform, position,
							dx * dx + dy * dy + dz * dz
						});
					}
				}
			}

			_renderer.render();
			_dynamicRenderer.renderHumanoids(
				_player.getCamera()._gpuData._position,
				_renderView,
				frame.deltaTime
			);

			// Dynamic assets use their own shader and samplers. Restore the world
			// pipeline before drawing water and other translucent blocks.
			if (gpuFaces) _pipeline.bindFaces(_context);
			else _pipeline.bind(_context);
			_shadowMap.bind(_context);
			_blockTextures.bind(_context);

			std::sort(translucent.begin(), translucent.end(), [](const translucentChunk& a, const translucentChunk& b) {
				return a._distanceSquared > b._distanceSquared;
				});
			_pipeline.bindTranslucent(_context);

			for (const translucentChunk& draw : translucent) {
				bindChunkLights(draw._chunkPosition);
				if (draw._gpuFaces) {
					_gpuObjectBuffer.update(_context, { draw._transform });
					_gpuObjectBuffer.bindVS(_context, 0);
					draw._gpuFaces->draw(_context, true);
				}
				else if (draw._model) {
					_gpuObjectBuffer.update(_context, { draw._transform });
					_gpuObjectBuffer.bindVS(_context, 0);
					draw._model->bind(_context);
					draw._model->draw(_context);
				}
			}
			_renderer.render();
			_lightingTimer.end(_context);
			ID3D11ShaderResourceView* nullFace = nullptr;
			_context->VSSetShaderResources(66, 1, &nullFace);
			_renderedTargetValid = false;
			if (!_inventoryOpen && _cursorCaptured) {
				if (const std::optional<blockRayHit> target = raycastBlock()) {
					_renderedTargetBlock = target->block;
					_renderedTargetAdjacent = target->adjacent;
					_renderedTargetId = target->id;
					_renderedTargetValid = true;
					_outlineRenderer.render(
						_context,
						target->block,
						_renderView,
						_renderProjection
					);
				}
			}
			_uiRenderer.render(
				_context,
				_hud,
				static_cast<float>(_window.getWidth()),
				static_cast<float>(_window.getHeight())
			);
		}

		void onShutdown() override {
			_streamer.stop();
			_world.saveWorld();
		}

	public:
		voxelPipeline(
			ac::window& window,
			ac::graphicsContext& graphics,
			ID3D11Device* device,
			ID3D11DeviceContext* context,
			staticPipeline& pipeline,
			pointShadowMap& shadowMap,
			ac::staticRenderer& renderer,
			ac::blockTextureSet& blockTextures,
			ac::staticAssetManager& blocks,
			ac::world& world,
			ac::worldStreamer& streamer,
			ac::player& player,
			ac::dynamicRenderer& dynamicRenderer,
			ac::constantBuffer<ac::cameraData>& cameraBuffer,
			ac::structuredBuffer<ac::gpuLight>& lights,
			std::vector<ac::gpuLight>& lightData,
			ac::constantBuffer<ac::lightBufferData>& lightBuffer,
			ac::structuredBuffer<uint32_t>& lightOccluders,
			std::vector<uint32_t>& lightOccluderData,
			ac::constantBuffer<ac::lightOcclusionBufferData>& lightOcclusionBuffer
		) :
			applicationPipeline(window, graphics),
			_window(window),
			_graphicsSettings(graphics),
			_device(device),
			_context(context),
			_pipeline(pipeline),
			_shadowMap(shadowMap),
			_renderer(renderer),
			_blockTextures(blockTextures),
			_blocks(blocks),
			_world(world),
			_streamer(streamer),
			_player(player),
			_dynamicRenderer(dynamicRenderer),
			_cameraBuffer(cameraBuffer),
			_lights(lights),
			_lightData(lightData),
			_lightBuffer(lightBuffer),
			_lightOccluders(lightOccluders),
			_lightOccluderData(lightOccluderData),
			_lightOcclusionBuffer(lightOcclusionBuffer) {

			_gpuObjectBuffer.create(device);
			_lightingTimer.create(device);
			_voxelLights.create(device, ac::VOXEL_LIGHT_TABLE_SIZE, _voxelLightTable.data());
			_voxelLightBuffer.create(device);
			_voxelLightBuffer.update(context, {});
			_chunkLightBuffer.create(device);
			_chunkLightBuffer.update(context, {});
			_outlineRenderer.create(device);
			_uiRenderer.create(device);
			buildHud();
			setClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			_window.setInputMode(GLFW_CURSOR, GLFW_CURSOR_DISABLED);
			_window.setUpdateDeltaCursor(true);
			_playerEntity = _dynamicRenderer.createHumanoid({}, { 1.0f, 1.0f, 1.0f, 1.0f }, false);
		}
	};
}

int main() {
	if (!glfwInit())
		return -1;

	ac::window window(
		1280,
		720,
		"DX11 Open World"
	);

	ac::graphicsContext gfx(
		window.getHwnd(),
		window.getWidth(),
		window.getHeight(),
#ifdef _DEBUG
		true
#else
		false
#endif
	);

	if (FAILED(gfx.getResult())) {
		glfwTerminate();
		return -1;
	}

	auto* device = gfx.getDevice();
	auto* context = gfx.getContext();

	staticPipeline pipeline(device);
	pointShadowMap shadowMap(device);
	ac::staticRenderer renderer(device, context);

	ac::textureLibrary textures;


	auto cubeData =
		ac::loadModelFromJson(
			"asset/model/cube.json"
		);

	auto cubeModel =
		std::make_unique<ac::model>();

	cubeModel->_vertex =
		std::move(cubeData._vertex);

	cubeModel->_index =
		std::move(cubeData._index);

	ac::modelManager models;

	models.load(
		std::move(cubeModel),
		ac::MODEL_CUBE
	);

	ac::staticAssetManager blocks;

	try {
		blocks.loadValidated("asset/block", models);
	}
	catch (const std::exception& error) {
		std::cerr << "Failed to load block registry: " << error.what() << '\\n';
		glfwTerminate();
		return -1;
	}

	ac::blockTextureSet blockTextures;
	try {
		blockTextures.load(device, textures, blocks.texturePaths());
		blockTextures.loadEmissions(device, blocks);
	}
	catch (const std::exception& error) {
		std::cerr << "Failed to load block textures: " << error.what() << '\n';
		glfwTerminate();
		return -1;
	}

	ac::world world(
		"world",
		1ULL
	);

	char* gpuTerrainValue = nullptr;
	size_t gpuTerrainLength = 0;
	_dupenv_s(&gpuTerrainValue, &gpuTerrainLength, "AC_GPU_TERRAIN");
	const bool enableGpuTerrain = gpuTerrainValue != nullptr;
	std::free(gpuTerrainValue);
	// GPU meshing is the normal renderer path. Keeping it behind an opt-in
	// environment variable made ordinary Release launches silently use the
	// deferred CPU remesh queue, which is visible as placement latency.
	char* cpuMeshingValue = nullptr;
	size_t cpuMeshingLength = 0;
	_dupenv_s(&cpuMeshingValue, &cpuMeshingLength, "AC_CPU_MESHING");
	const bool enableGpuMeshing = cpuMeshingValue == nullptr;
	std::free(cpuMeshingValue);
	ac::worldStreamer streamer(
		&world,
		&blocks,
		&models,
		device,
		enableGpuTerrain,
		enableGpuMeshing,
		16,
		18
	);

	ac::player player(
		{ 8.0f, 100.0f, -12.0f },
		{ 0, 0, 0 },
		1.48352986f,
		window.getAspect(),
		0.01f
	);
	ac::dynamicRenderer dynamicAssets(device, context);
	dynamicAssets.loadHumanoidAsset(device, "asset/texture/player.png");

	ac::constantBuffer<ac::cameraData> cameraBuffer;
	cameraBuffer.create(device);

	ac::structuredBuffer<ac::gpuLight> lights;
	lights.create(device, 1024);

	std::vector<ac::gpuLight> lightData;

	ac::constantBuffer<ac::lightBufferData> lightBuffer;
	lightBuffer.create(device);

	lightBuffer.update(
		context,
		{ 0, { 0, 0, 0 } }
	);

	std::vector<uint32_t> lightOccluderData(ac::LIGHT_OCCLUSION_TOTAL_WORDS, 0u);
	ac::structuredBuffer<uint32_t> lightOccluders;
	lightOccluders.create(
		device,
		ac::LIGHT_OCCLUSION_TOTAL_WORDS,
		lightOccluderData.data()
	);

	ac::constantBuffer<ac::lightOcclusionBufferData> lightOcclusionBuffer;
	lightOcclusionBuffer.create(device);
	lightOcclusionBuffer.update(context, ac::lightOcclusionBufferData{});

	voxelPipeline application(
		window,
		gfx,
		device,
		context,
		pipeline,
		shadowMap,
		renderer,
		blockTextures,
		blocks,
		world,
		streamer,
		player,
		dynamicAssets,
		cameraBuffer,
		lights,
		lightData,
		lightBuffer,
		lightOccluders,
		lightOccluderData,
		lightOcclusionBuffer
	);

	const int result = application.run();

	glfwTerminate();
	return result;
}
