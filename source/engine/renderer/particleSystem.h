#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <DirectXTex/DirectXTex.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include <core/debug.h>
#include <core/shader.h>
#include <core/texture.h>

namespace ac {

	struct particle {
		DirectX::XMFLOAT3 position{};
		DirectX::XMFLOAT3 velocity{};
		DirectX::XMFLOAT4 color{ 1, 1, 1, 1 };
		float life = 0.0f;
		float maxLife = 1.0f;
		float size = 0.08f;
		float verticalScale = 1.0f;
		float gravity = 11.0f;
		float collisionY = -(std::numeric_limits<float>::max)();
		uint32_t textureIndex = 0;
		bool precipitation = false;
	};

	struct particleVertex {
		DirectX::XMFLOAT3 position;
		DirectX::XMFLOAT4 color;
		DirectX::XMFLOAT2 uv;
		uint32_t textureIndex;
	};

	class particleSystem {
	private:
		static constexpr size_t MAX_PARTICLES = 4096;
		static constexpr uint32_t GENERIC_FRAME_COUNT = 8;
		std::vector<particle> _particles;
		std::vector<particleVertex> _vertices;
		Microsoft::WRL::ComPtr<ID3D11Buffer> _vertexBuffer;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _textureArray;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> _sampler;
		Microsoft::WRL::ComPtr<ID3D11BlendState> _blend;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> _depth;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> _raster;
		shaderProgram _shader;
		std::mt19937 _rng{ 0xC0FFEEu };
		bool _ready = false;
		size_t _spawnCursor = 0;
		size_t _liveCount = 0;

		particle* spawn() {
			if (_liveCount >= _particles.size()) return nullptr;
			for (size_t n = 0; n < _particles.size(); ++n) {
				particle& p = _particles[_spawnCursor];
				_spawnCursor = (_spawnCursor + 1) % _particles.size();
				if (p.life <= 0.0f) {
					++_liveCount;
					p = particle{};
					return &p;
				}
			}
			return nullptr;
		}

		bool loadParticleTextures(ID3D11Device* device) {
			std::vector<DirectX::ScratchImage> images;
			images.reserve(GENERIC_FRAME_COUNT);
			std::vector<DirectX::Image> slices;
			slices.reserve(GENERIC_FRAME_COUNT);
			DirectX::TexMetadata metadata{};
			for (uint32_t i = 0; i < GENERIC_FRAME_COUNT; ++i) {
				const std::wstring path =
					L"assets/textures/particle/generic_" + std::to_wstring(i) + L".png";
				DirectX::ScratchImage image;
				DirectX::TexMetadata meta{};
				if (FAILED(loadImageFromFile(path, image, meta)))
					return false;
				if (i == 0) metadata = meta;
				if (meta.width != metadata.width || meta.height != metadata.height)
					return false;
				images.push_back(std::move(image));
				slices.push_back(*images.back().GetImage(0, 0, 0));
			}

			DirectX::ScratchImage arrayImage;
			metadata.arraySize = GENERIC_FRAME_COUNT;
			metadata.depth = 1;
			metadata.mipLevels = 1;
			metadata.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;
			if (FAILED(arrayImage.Initialize2D(
					metadata.format,
					metadata.width,
					metadata.height,
					GENERIC_FRAME_COUNT,
					1)))
				return false;
			for (uint32_t i = 0; i < GENERIC_FRAME_COUNT; ++i) {
				const DirectX::Image* dest = arrayImage.GetImage(0, i, 0);
				if (!dest || FAILED(DirectX::CopyRectangle(
						slices[i],
						DirectX::Rect(0, 0, static_cast<size_t>(metadata.width), static_cast<size_t>(metadata.height)),
						*dest,
						DirectX::TEX_FILTER_DEFAULT,
						0,
						0)))
					return false;
			}

			Microsoft::WRL::ComPtr<ID3D11Resource> resource;
			if (FAILED(DirectX::CreateTexture(
					device,
					arrayImage.GetImages(),
					arrayImage.GetImageCount(),
					arrayImage.GetMetadata(),
					resource.GetAddressOf())))
				return false;

			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.Format = metadata.format;
			srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
			srv.Texture2DArray.MipLevels = 1;
			srv.Texture2DArray.ArraySize = GENERIC_FRAME_COUNT;
			return SUCCEEDED(device->CreateShaderResourceView(
				resource.Get(), &srv, _textureArray.GetAddressOf()));
		}

	public:
		void create(ID3D11Device* device) {
			_particles.resize(MAX_PARTICLES);
			_vertices.resize(MAX_PARTICLES * 6);
			_shader.initVertexShader(device, L"assets/shader/particle.hlsl", "vertexMain", "vs_5_0");
			_shader.initPixelShader(device, L"assets/shader/particle.hlsl", "pixelMain", "ps_5_0");

			D3D11_INPUT_ELEMENT_DESC layout[] = {
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXINDEX", 0, DXGI_FORMAT_R32_UINT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			};
			_shader.initInputLayout(device, {
				layout[0], layout[1], layout[2], layout[3]
			});

			D3D11_BUFFER_DESC bd{};
			bd.Usage = D3D11_USAGE_DYNAMIC;
			bd.ByteWidth = static_cast<UINT>(sizeof(particleVertex) * MAX_PARTICLES * 6);
			bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			DX_CHECK(device->CreateBuffer(&bd, nullptr, _vertexBuffer.GetAddressOf()));

			if (!loadParticleTextures(device))
				throw std::runtime_error("Failed to load particle textures from assets/textures/particle");

			D3D11_SAMPLER_DESC sampler{};
			sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler.MaxLOD = D3D11_FLOAT32_MAX;
			DX_CHECK(device->CreateSamplerState(&sampler, _sampler.GetAddressOf()));

			D3D11_BLEND_DESC blend{};
			blend.RenderTarget[0].BlendEnable = TRUE;
			blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
			blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
			blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
			blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
			blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			DX_CHECK(device->CreateBlendState(&blend, _blend.GetAddressOf()));

			D3D11_DEPTH_STENCIL_DESC depth{};
			depth.DepthEnable = TRUE;
			depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
			DX_CHECK(device->CreateDepthStencilState(&depth, _depth.GetAddressOf()));

			D3D11_RASTERIZER_DESC raster{};
			raster.FillMode = D3D11_FILL_SOLID;
			raster.CullMode = D3D11_CULL_NONE;
			raster.DepthClipEnable = TRUE;
			DX_CHECK(device->CreateRasterizerState(&raster, _raster.GetAddressOf()));
			_ready = true;
		}

		void emitBurst(
			const DirectX::XMFLOAT3& center,
			const DirectX::XMFLOAT4& color,
			int count,
			float speed = 3.5f,
			float life = 0.7f,
			float size = 0.14f
		) {
			std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
			std::uniform_real_distribution<float> lifeJitter(0.55f, 1.0f);
			std::uniform_int_distribution<uint32_t> frame(0, GENERIC_FRAME_COUNT - 1);
			for (int i = 0; i < count; ++i) {
				particle* p = spawn();
				if (!p) return;
				DirectX::XMFLOAT3 dir{ unit(_rng), unit(_rng) * 0.85f + 0.45f, unit(_rng) };
				const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
				if (len > 0.001f) {
					dir.x /= len; dir.y /= len; dir.z /= len;
				}
				const float s = speed * (0.45f + 0.55f * lifeJitter(_rng));
				p->position = {
					center.x + unit(_rng) * 0.18f,
					center.y + unit(_rng) * 0.18f,
					center.z + unit(_rng) * 0.18f
				};
				p->velocity = { dir.x * s, dir.y * s, dir.z * s };
				p->color = color;
				p->maxLife = life * lifeJitter(_rng);
				p->life = p->maxLife;
				p->size = size * (0.75f + 0.55f * lifeJitter(_rng));
				p->verticalScale = 1.0f;
				p->gravity = 11.0f;
				p->textureIndex = frame(_rng);
			}
		}

		void emitTrail(
			const DirectX::XMFLOAT3& origin,
			const DirectX::XMFLOAT3& direction,
			const DirectX::XMFLOAT4& color,
			int count,
			float speed = 1.4f
		) {
			std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
			std::uniform_int_distribution<uint32_t> frame(0, GENERIC_FRAME_COUNT - 1);
			DirectX::XMFLOAT3 dir = direction;
			const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
			if (len > 0.001f) {
				dir.x /= len; dir.y /= len; dir.z /= len;
			}
			for (int i = 0; i < count; ++i) {
				particle* p = spawn();
				if (!p) return;
				p->position = {
					origin.x + unit(_rng) * 0.12f,
					origin.y + 0.05f,
					origin.z + unit(_rng) * 0.12f
				};
				p->velocity = {
					-dir.x * speed + unit(_rng) * 0.35f,
					0.35f + unit(_rng) * 0.45f,
					-dir.z * speed + unit(_rng) * 0.35f
				};
				p->color = color;
				p->maxLife = 0.35f + 0.25f * ((unit(_rng) + 1.0f) * 0.5f);
				p->life = p->maxLife;
				p->size = 0.06f + 0.04f * ((unit(_rng) + 1.0f) * 0.5f);
				p->verticalScale = 1.0f;
				p->gravity = 11.0f;
				p->textureIndex = frame(_rng);
			}
		}

		void emitPrecipitation(
			const DirectX::XMFLOAT3& eye,
			bool snow,
			bool storm,
			float intensity,
			float dt,
			float weatherTime,
			const std::function<bool(float, float, float, float&)>& precipitationColumn
		) {
			if (intensity <= 0.02f) return;
			std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
			std::uniform_int_distribution<uint32_t> frame(0, GENERIC_FRAME_COUNT - 1);
			const float stormBoost = storm ? 1.18f : 1.0f;
			const int count = std::clamp(static_cast<int>(std::ceil(
				(snow ? 34.0f : 58.0f) * intensity * stormBoost *
				std::max(dt, 0.008f) * 60.0f)), 1, 76);
			const float radius = snow ? 21.0f : (storm ? 22.0f : 19.0f);
			const float gust = storm ? 5.0f : (snow ? 1.35f : 0.7f);
			const float windX = std::sin(weatherTime * 0.31f) * gust;
			const float windZ = std::cos(weatherTime * 0.23f) * gust * 0.7f;
			for (int i = 0; i < count; ++i) {
				const DirectX::XMFLOAT3 position{
					eye.x + unit(_rng) * radius,
					eye.y + (snow ? 10.0f : 12.0f) + unit(_rng) * 3.0f,
					eye.z + unit(_rng) * radius
				};
				float collisionY = -(std::numeric_limits<float>::max)();
				if (precipitationColumn &&
					!precipitationColumn(position.x, position.y, position.z, collisionY))
					continue;
				particle* p = spawn();
				if (!p) return;
				p->position = position;
				p->collisionY = collisionY;
				p->precipitation = true;
				if (snow) {
					p->velocity = {
						windX + unit(_rng) * 1.8f,
						-2.7f + unit(_rng) * 0.7f,
						windZ + unit(_rng) * 1.8f
					};
					p->color = { 0.95f, 0.975f, 1.0f, 0.96f };
					p->maxLife = 2.4f + 0.8f * ((unit(_rng) + 1.0f) * 0.5f);
					p->size = 0.10f + 0.07f * ((unit(_rng) + 1.0f) * 0.5f);
					p->verticalScale = 1.25f;
					p->gravity = 1.6f;
				}
				else {
					p->velocity = {
						windX + unit(_rng) * 1.0f,
						(storm ? -25.0f : -21.0f) + unit(_rng) * 3.5f,
						windZ + unit(_rng) * 1.0f
					};
					p->color = storm
						? DirectX::XMFLOAT4{ 0.70f, 0.80f, 0.96f, 0.82f }
						: DirectX::XMFLOAT4{ 0.65f, 0.78f, 0.96f, 0.72f };
					p->maxLife = 0.85f + 0.25f * ((unit(_rng) + 1.0f) * 0.5f);
					p->size = 0.045f + 0.025f * ((unit(_rng) + 1.0f) * 0.5f);
					p->verticalScale = storm ? 8.0f : 6.0f;
					p->gravity = 8.0f;
				}
				p->life = p->maxLife;
				p->textureIndex = frame(_rng);
			}
		}

		void update(float dt) {
			if (_liveCount == 0) return;
			size_t live = 0;
			for (particle& p : _particles) {
				if (p.life <= 0.0f) continue;
				p.life -= dt;
				if (p.life <= 0.0f) {
					p.life = 0.0f;
					continue;
				}
				p.velocity.y -= p.gravity * dt;
				p.velocity.x *= (1.0f - 1.8f * dt);
				p.velocity.z *= (1.0f - 1.8f * dt);
				p.position.x += p.velocity.x * dt;
				p.position.y += p.velocity.y * dt;
				p.position.z += p.velocity.z * dt;
				if (p.precipitation && p.position.y <= p.collisionY) {
					p.life = 0.0f;
					continue;
				}
				const float fade = std::clamp(p.life / std::max(p.maxLife, 0.001f), 0.0f, 1.0f);
				p.color.w = std::min(p.color.w, fade * fade);
				++live;
			}
			_liveCount = live;
		}

		void draw(
			ID3D11DeviceContext* context,
			ID3D11Buffer* cameraBuffer,
			const DirectX::XMFLOAT3& cameraRight,
			const DirectX::XMFLOAT3& cameraUp
		) {
			if (!_ready || _liveCount == 0) return;

			auto normalize = [](DirectX::XMFLOAT3 v) {
				const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
				if (len > 1e-5f) {
					v.x /= len; v.y /= len; v.z /= len;
				}
				return v;
			};
			const DirectX::XMFLOAT3 right = normalize(cameraRight);
			const DirectX::XMFLOAT3 up = normalize(cameraUp);

			UINT vertexCount = 0;
			for (const particle& p : _particles) {
				if (p.life <= 0.0f) continue;
				const float hs = p.size * 0.5f;
				const DirectX::XMFLOAT3 r{
					right.x * hs, right.y * hs, right.z * hs };
				const DirectX::XMFLOAT3 u{
					up.x * hs * p.verticalScale,
					up.y * hs * p.verticalScale,
					up.z * hs * p.verticalScale };
				const DirectX::XMFLOAT3 corners[4] = {
					{ p.position.x - r.x - u.x, p.position.y - r.y - u.y, p.position.z - r.z - u.z },
					{ p.position.x + r.x - u.x, p.position.y + r.y - u.y, p.position.z + r.z - u.z },
					{ p.position.x - r.x + u.x, p.position.y - r.y + u.y, p.position.z - r.z + u.z },
					{ p.position.x + r.x + u.x, p.position.y + r.y + u.y, p.position.z + r.z + u.z },
				};
				const DirectX::XMFLOAT2 uvs[4] = { {0,1},{1,1},{0,0},{1,0} };
				const int indices[6] = { 0, 1, 2, 2, 1, 3 };
				for (int i = 0; i < 6; ++i) {
					const int c = indices[i];
					_vertices[vertexCount++] = { corners[c], p.color, uvs[c], p.textureIndex };
				}
			}
			if (vertexCount == 0) return;

			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (FAILED(context->Map(_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
				return;
			std::memcpy(mapped.pData, _vertices.data(), sizeof(particleVertex) * vertexCount);
			context->Unmap(_vertexBuffer.Get(), 0);

			UINT stride = sizeof(particleVertex);
			UINT offset = 0;

			Microsoft::WRL::ComPtr<ID3D11BlendState> oldBlend;
			Microsoft::WRL::ComPtr<ID3D11DepthStencilState> oldDepth;
			Microsoft::WRL::ComPtr<ID3D11RasterizerState> oldRaster;
			float oldBlendFactor[4] = { 0, 0, 0, 0 };
			UINT oldSampleMask = 0;
			UINT oldStencilRef = 0;
			context->OMGetBlendState(oldBlend.GetAddressOf(), oldBlendFactor, &oldSampleMask);
			context->OMGetDepthStencilState(oldDepth.GetAddressOf(), &oldStencilRef);
			context->RSGetState(oldRaster.GetAddressOf());

			_shader.bindShaders(context);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->IASetVertexBuffers(0, 1, _vertexBuffer.GetAddressOf(), &stride, &offset);
			context->VSSetConstantBuffers(1, 1, &cameraBuffer);
			context->PSSetShaderResources(0, 1, _textureArray.GetAddressOf());
			context->PSSetSamplers(0, 1, _sampler.GetAddressOf());
			float blendFactor[4] = { 0, 0, 0, 0 };
			context->OMSetBlendState(_blend.Get(), blendFactor, 0xffffffff);
			context->OMSetDepthStencilState(_depth.Get(), 0);
			context->RSSetState(_raster.Get());
			context->Draw(vertexCount, 0);
			ID3D11ShaderResourceView* nullSrv = nullptr;
			context->PSSetShaderResources(0, 1, &nullSrv);
			context->OMSetBlendState(oldBlend.Get(), oldBlendFactor, oldSampleMask);
			context->OMSetDepthStencilState(oldDepth.Get(), oldStencilRef);
			context->RSSetState(oldRaster.Get());
		}
	};

}
