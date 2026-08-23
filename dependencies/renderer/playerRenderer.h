#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include <assetManager/cpuAsset.h>
#include <header/shader.h>
#include <header/texture.h>
#include <renderer/buffer.h>

namespace ac {
	struct humanoidAnimationState {
		float animationTime = 0.0f;
		float useAnimationRemaining = 0.0f;
		float smoothedMovement = 0.0f;
		float smoothedPitch = 0.0f;
		float airborneMovement = 0.0f;
		float airborneTime = 0.0f;
	};

	// Renders the classic (4-pixel arm) Minecraft 64x64 player layout.  The
	// geometry is authored in world units at 1 texture pixel = 1/16 block.
	// This class owns the shared GPU asset; per-entity state lives in the
	// dynamic renderer.
	class playerRenderer {
		struct objectData {
			DirectX::XMFLOAT4X4 transform;
		};
		struct boneData {
			std::array<DirectX::XMFLOAT4X4, 6> transforms;
			uint32_t meshPart = 0;
			DirectX::XMUINT3 padding{};
		};

		struct uvBox {
			float x;
			float y;
			float width;
			float height;
			float depth;
		};

		shaderProgram _program;
		shaderProgram _shadowProgram;
		gpuModel _body;
		gpuModel _head;
		gpuModel _firstPersonArm;
		constantBuffer<objectData> _objectBuffer;
		constantBuffer<boneData> _boneBuffer;
		texture _skin;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> _sampler;
		Microsoft::WRL::ComPtr<ID3D11BlendState> _blend;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> _depth;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> _rasterizer;
		animation _walkAnimation;

		static keyframe rotationKey(float time, float angle) {
			keyframe key;
			key._time = time;
			DirectX::XMStoreFloat4(&key._rotation,
				DirectX::XMQuaternionRotationRollPitchYaw(angle, 0.0f, 0.0f));
			return key;
		}

		static animation makeWalkAnimation() {
			animation clip;
			clip._duration = 1.0f;
			clip._ticksPerSecond = 1.0f;
			clip._loop = true;
			for (const auto [bone, phase] : std::array<std::pair<uint32_t, float>, 4>{ {
				{ 2u, .78f }, { 3u, -.78f }, { 4u, -.72f }, { 5u, .72f }
			} }) {
				animationChannel channel;
				channel._boneId = bone;
				channel._keyframes = {
					rotationKey(0.0f, phase), rotationKey(.25f, 0.0f),
					rotationKey(.5f, -phase), rotationKey(.75f, 0.0f), rotationKey(1.0f, phase)
				};
				clip._channels.push_back(std::move(channel));
			}
			return clip;
		}

		static DirectX::XMVECTOR sampleRotation(const animation& clip, uint32_t bone, float time) {
			using namespace DirectX;
			for (const animationChannel& channel : clip._channels) {
				if (channel._boneId != bone || channel._keyframes.empty()) continue;
				const float local = std::fmod(time, clip._duration);
				for (size_t i = 1; i < channel._keyframes.size(); ++i) {
					if (local > channel._keyframes[i]._time) continue;
					const keyframe& a = channel._keyframes[i - 1];
					const keyframe& b = channel._keyframes[i];
					const float blend = (local - a._time) / (std::max)(b._time - a._time, .0001f);
					return XMQuaternionSlerp(XMLoadFloat4(&a._rotation), XMLoadFloat4(&b._rotation), blend);
				}
			}
			return XMQuaternionIdentity();
		}

		static DirectX::XMFLOAT2 uv(float x, float y) {
			return { x / 64.0f, y / 64.0f };
		}

		static void appendFace(
			std::vector<vertex>& vertices,
			std::vector<uint32_t>& indices,
			const std::array<DirectX::XMFLOAT3, 4>& positions,
			const DirectX::XMFLOAT3& normal,
			float u0, float v0, float u1, float v1,
			uint32_t bone
		) {
			const uint32_t first = static_cast<uint32_t>(vertices.size());
			vertices.push_back({ positions[0], normal, uv(u0, v1), bone, 3u, 1.0f });
			vertices.push_back({ positions[1], normal, uv(u0, v0), bone, 3u, 1.0f });
			vertices.push_back({ positions[2], normal, uv(u1, v0), bone, 3u, 1.0f });
			vertices.push_back({ positions[3], normal, uv(u1, v1), bone, 3u, 1.0f });
			indices.insert(indices.end(), {
				first, first + 1u, first + 2u,
				first, first + 2u, first + 3u
			});
		}

		static void appendBox(
			std::vector<vertex>& vertices,
			std::vector<uint32_t>& indices,
			DirectX::XMFLOAT3 minimum,
			DirectX::XMFLOAT3 maximum,
			const uvBox& atlas,
			float expansion = 0.0f
		) {
			uint32_t bone = 0u;
			if (minimum.y >= 1.49f) bone = 1u;
			else if (maximum.y <= .76f) bone = maximum.x <= .01f ? 4u : 5u;
			else if (maximum.x <= -.24f) bone = 2u;
			else if (minimum.x >= .24f) bone = 3u;
			minimum.x -= expansion; minimum.y -= expansion; minimum.z -= expansion;
			maximum.x += expansion; maximum.y += expansion; maximum.z += expansion;

			const float x = atlas.x;
			const float y = atlas.y;
			const float w = atlas.width;
			const float h = atlas.height;
			const float d = atlas.depth;

			// Minecraft's unfolded cuboid strip is: right, front, left, back.
			appendFace(vertices, indices, {{
				{ minimum.x, minimum.y, maximum.z }, { minimum.x, maximum.y, maximum.z },
				{ minimum.x, maximum.y, minimum.z }, { minimum.x, minimum.y, minimum.z }
			}}, { -1.0f, 0.0f, 0.0f }, x, y + d, x + d, y + d + h, bone);
			appendFace(vertices, indices, {{
				{ maximum.x, minimum.y, maximum.z }, { maximum.x, maximum.y, maximum.z },
				{ minimum.x, maximum.y, maximum.z }, { minimum.x, minimum.y, maximum.z }
			}}, { 0.0f, 0.0f, 1.0f }, x + d, y + d, x + d + w, y + d + h, bone);
			appendFace(vertices, indices, {{
				{ maximum.x, minimum.y, minimum.z }, { maximum.x, maximum.y, minimum.z },
				{ maximum.x, maximum.y, maximum.z }, { maximum.x, minimum.y, maximum.z }
			}}, { 1.0f, 0.0f, 0.0f }, x + d + w, y + d, x + d + w + d, y + d + h, bone);
			appendFace(vertices, indices, {{
				{ minimum.x, minimum.y, minimum.z }, { minimum.x, maximum.y, minimum.z },
				{ maximum.x, maximum.y, minimum.z }, { maximum.x, minimum.y, minimum.z }
			}}, { 0.0f, 0.0f, -1.0f }, x + d + w + d, y + d, x + d + w + d + w, y + d + h, bone);
			appendFace(vertices, indices, {{
				{ minimum.x, maximum.y, minimum.z }, { minimum.x, maximum.y, maximum.z },
				{ maximum.x, maximum.y, maximum.z }, { maximum.x, maximum.y, minimum.z }
			}}, { 0.0f, 1.0f, 0.0f }, x + d, y, x + d + w, y + d, bone);
			appendFace(vertices, indices, {{
				{ minimum.x, minimum.y, maximum.z }, { minimum.x, minimum.y, minimum.z },
				{ maximum.x, minimum.y, minimum.z }, { maximum.x, minimum.y, maximum.z }
			}}, { 0.0f, -1.0f, 0.0f }, x + d + w, y, x + d + w + w, y + d, bone);
		}

		enum class modelPiece { body, head, firstPersonArm };

		static gpuModel createModel(ID3D11Device* device, modelPiece piece) {
			std::vector<vertex> vertices;
			std::vector<uint32_t> indices;
			if (piece == modelPiece::head) {
				appendBox(vertices, indices, { -0.25f, 1.50f, -0.25f }, { 0.25f, 2.00f, 0.25f }, { 0, 0, 8, 8, 8 });
				appendBox(vertices, indices, { -0.25f, 1.50f, -0.25f }, { 0.25f, 2.00f, 0.25f }, { 32, 0, 8, 8, 8 }, 0.03125f);
			}
			else if (piece == modelPiece::body) {
				// Base body, right leg, left leg, right arm, left arm.
				appendBox(vertices, indices, { -0.25f, 0.75f, -0.125f }, { 0.25f, 1.50f, 0.125f }, { 16, 16, 8, 12, 4 });
				appendBox(vertices, indices, { -0.25f, 0.00f, -0.125f }, { 0.00f, 0.75f, 0.125f }, { 0, 16, 4, 12, 4 });
				appendBox(vertices, indices, { 0.00f, 0.00f, -0.125f }, { 0.25f, 0.75f, 0.125f }, { 16, 48, 4, 12, 4 });
				appendBox(vertices, indices, { -0.50f, 0.75f, -0.125f }, { -0.25f, 1.50f, 0.125f }, { 40, 16, 4, 12, 4 });
				appendBox(vertices, indices, { 0.25f, 0.75f, -0.125f }, { 0.50f, 1.50f, 0.125f }, { 32, 48, 4, 12, 4 });

				// Jacket, trousers, and sleeves use the alpha-capable second layer.
				constexpr float layer = 0.015625f;
				appendBox(vertices, indices, { -0.25f, 0.75f, -0.125f }, { 0.25f, 1.50f, 0.125f }, { 16, 32, 8, 12, 4 }, layer);
				appendBox(vertices, indices, { -0.25f, 0.00f, -0.125f }, { 0.00f, 0.75f, 0.125f }, { 0, 32, 4, 12, 4 }, layer);
				appendBox(vertices, indices, { 0.00f, 0.00f, -0.125f }, { 0.25f, 0.75f, 0.125f }, { 0, 48, 4, 12, 4 }, layer);
				appendBox(vertices, indices, { -0.50f, 0.75f, -0.125f }, { -0.25f, 1.50f, 0.125f }, { 40, 32, 4, 12, 4 }, layer);
				appendBox(vertices, indices, { 0.25f, 0.75f, -0.125f }, { 0.50f, 1.50f, 0.125f }, { 48, 48, 4, 12, 4 }, layer);
			}
			else {
				// A compact view-model arm replaces the full torso in first person.
				// It keeps the correct 1:3 classic-arm proportions without filling
				// the lower half of the screen.
				appendBox(vertices, indices, { -0.10f, -0.30f, -0.10f }, { 0.10f, 0.30f, 0.10f }, { 40, 16, 4, 12, 4 });
				appendBox(vertices, indices, { -0.10f, -0.30f, -0.10f }, { 0.10f, 0.30f, 0.10f }, { 40, 32, 4, 12, 4 }, 0.0125f);
			}

			gpuModel model;
			model._vertex.create(device, vertices);
			model._index.create(device, indices);
			return model;
		}

		boneData buildBones(
			float pitch,
			bool grounded,
			bool crouching,
			const humanoidAnimationState& state
		) const {
			using namespace DirectX;
			const float moving = state.smoothedMovement;
			const float useProgress = state.useAnimationRemaining > 0
				? 1.0f - state.useAnimationRemaining / .42f
				: 0.0f;
			const float useSwing = std::sin(useProgress * XM_PI);
			boneData bones{};
			const std::array<XMFLOAT3, 6> pivots = {{
				{ 0, 1.125f, 0 }, { 0, 1.5f, 0 }, { -.375f, 1.5f, 0 },
				{ .375f, 1.5f, 0 }, { -.125f, .75f, 0 }, { .125f, .75f, 0 }
			}};
			for (uint32_t id = 0; id < 6; ++id) {
				XMVECTOR rotation = XMQuaternionSlerp(
					XMQuaternionIdentity(),
					sampleRotation(_walkAnimation, id, state.animationTime),
					moving
				);
				if (crouching && id <= 3u)
					rotation = XMQuaternionMultiply(rotation, XMQuaternionRotationRollPitchYaw(.28f, 0, 0));
				else if (crouching && id >= 4u)
					rotation = XMQuaternionMultiply(rotation, XMQuaternionRotationRollPitchYaw(-.20f, 0, 0));
				if (!grounded && id >= 2u)
					rotation = XMQuaternionMultiply(rotation, XMQuaternionRotationRollPitchYaw(id < 4u ? -.14f : .18f, 0, 0));
				if (id == 1u)
					rotation = XMQuaternionMultiply(rotation, XMQuaternionRotationRollPitchYaw(-state.smoothedPitch * .75f, 0, 0));
				else if (id == 2u && useSwing > 0.0f)
					rotation = XMQuaternionMultiply(rotation, XMQuaternionRotationRollPitchYaw(-useSwing * .85f, 0, 0));
				const XMMATRIX local = XMMatrixTranslation(-pivots[id].x, -pivots[id].y, -pivots[id].z) *
					XMMatrixRotationQuaternion(rotation) * XMMatrixTranslation(pivots[id].x, pivots[id].y, pivots[id].z);
				XMStoreFloat4x4(&bones.transforms[id], XMMatrixTranspose(local));
			}
			return bones;
		}

	public:
		playerRenderer(ID3D11Device* device, const std::string& skinPath)
			: _body(createModel(device, modelPiece::body)),
			  _head(createModel(device, modelPiece::head)),
			  _firstPersonArm(createModel(device, modelPiece::firstPersonArm)),
			  _skin(loadTextureFromFile(device, skinPath)),
			  _walkAnimation(makeWalkAnimation()) {
			if (_skin._width != 64u || _skin._height != 64u)
				throw std::runtime_error("Player skin must be a 64x64 Minecraft skin: " + skinPath);

			_program.initVertexShader(device, L"asset/shader/player.hlsl", "vertexMain", "vs_5_0");
			_program.initPixelShader(device, L"asset/shader/player.hlsl", "pixelMain", "ps_5_0");
			_shadowProgram.initVertexShader(device, L"asset/shader/playerShadow.hlsl", "vertexMain", "vs_5_0");
			_shadowProgram.initPixelShader(device, L"asset/shader/playerShadow.hlsl", "pixelMain", "ps_5_0");
			_program.initInputLayout(device, {
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(vertex, _position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(vertex, _normal), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(vertex, _uv), D3D11_INPUT_PER_VERTEX_DATA, 0 }
				,{ "MATID", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(vertex, _material), D3D11_INPUT_PER_VERTEX_DATA, 0 }
			});
			_shadowProgram.initInputLayout(device, {
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(vertex, _position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(vertex, _normal), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(vertex, _uv), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "MATID", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(vertex, _material), D3D11_INPUT_PER_VERTEX_DATA, 0 }
			});
			_objectBuffer.create(device);
			_boneBuffer.create(device);

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
			depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
			depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
			DX_CHECK(device->CreateDepthStencilState(&depth, _depth.GetAddressOf()));

			D3D11_RASTERIZER_DESC rasterizer{};
			rasterizer.FillMode = D3D11_FILL_SOLID;
			rasterizer.CullMode = D3D11_CULL_NONE;
			rasterizer.DepthClipEnable = TRUE;
			DX_CHECK(device->CreateRasterizerState(&rasterizer, _rasterizer.GetAddressOf()));
		}

		void triggerUseAnimation(humanoidAnimationState& state) {
			// Do not restart midway through the return stroke; that caused an
			// obvious pose pop when clicks arrived close together.
			if (state.useAnimationRemaining <= .05f)
				state.useAnimationRemaining = .42f;
		}

		void render(
			ID3D11DeviceContext* context,
			const DirectX::XMFLOAT3& cameraPosition,
			const DirectX::XMFLOAT3& velocity,
			float yaw,
			float pitch,
			bool grounded,
			bool crouching,
			bool sprinting,
			bool thirdPerson,
			const DirectX::XMFLOAT4X4& view,
			float deltaTime,
			humanoidAnimationState& state
		) {
			using namespace DirectX;
			const float speed = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
			// Continue locomotion in the air, but reduce its amplitude so jumps
			// retain motion without looking like a full-speed grounded sprint.
			const float referenceSpeed = sprinting ? 5.75f : (crouching ? 1.6f : 4.0f);
			const float speedMovement = (std::min)(speed / referenceSpeed, 1.0f);
			if (grounded) {
				state.airborneMovement = speedMovement;
				state.airborneTime = 0.0f;
			}
			else {
				state.airborneTime += (std::max)(deltaTime, 0.0f);
			}
			// Preserve the take-off gait throughout a jump. Blending toward zero at
			// the apex made the limbs freeze in mid-air.
			const float airborneGait = (std::max)(speedMovement, state.airborneMovement * .82f);
			const float targetMovement = grounded ? speedMovement : (std::max)(airborneGait, .22f);
			const float poseResponse = 1.0f - std::exp(-7.0f * (std::max)(deltaTime, 0.0f));
			state.smoothedMovement += (targetMovement - state.smoothedMovement) * poseResponse;
			state.smoothedPitch += (pitch - state.smoothedPitch) * poseResponse;
			// Drive animation phase from actual travel speed. This prevents the
			// pose from appearing stuck when a smoothed blend value changes slowly.
			const float strideRate = speed * (crouching ? .30f : (sprinting ? .25f : .24f));
			const float airborneStrideRate = grounded
				? strideRate
				: (std::max)(strideRate, .45f);
			state.animationTime += (std::max)(deltaTime, 0.0f) * airborneStrideRate;
			state.useAnimationRemaining = (std::max)(0.0f, state.useAnimationRemaining - deltaTime);
			boneData bones = buildBones(pitch, grounded, crouching, state);
			XMFLOAT4X4 transform;
			// First- and third-person views share the attached world-space body.
			// First person omits only the head, leaving the torso, both arms, and
			// both legs visible naturally when looking down.
			XMStoreFloat4x4(&transform, XMMatrixTranspose(
				XMMatrixRotationY(XM_PIDIV2 - yaw) *
				XMMatrixTranslation(
					cameraPosition.x,
					cameraPosition.y - 1.625f - (crouching ? .25f : 0.0f),
					cameraPosition.z
				)
			));

			_program.bindShaders(context);
			_objectBuffer.update(context, { transform });
			_objectBuffer.bindVS(context, 0);
			_boneBuffer.bindVS(context, 6);
			_skin.bind(context, 0);
			context->PSSetSamplers(0, 1, _sampler.GetAddressOf());
			context->OMSetBlendState(_blend.Get(), nullptr, 0xffffffffu);
			context->OMSetDepthStencilState(_depth.Get(), 0);
			context->RSSetState(_rasterizer.Get());
			context->GSSetShader(nullptr, nullptr, 0);
			context->HSSetShader(nullptr, nullptr, 0);
			context->DSSetShader(nullptr, nullptr, 0);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			if (thirdPerson) {
				bones.meshPart = 0u;
				_boneBuffer.update(context, bones);
				_body.bind(context);
				_body.draw(context);
				bones.meshPart = 1u;
				_boneBuffer.update(context, bones);
				_head.bind(context);
				_head.draw(context);
			}
			else {
				bones.meshPart = 3u;
				_boneBuffer.update(context, bones);
				_body.bind(context);
				_body.draw(context);
			}
		}

		void renderShadow(
			ID3D11DeviceContext* context,
			const DirectX::XMFLOAT3& position,
			float yaw,
			float pitch,
			bool grounded,
			bool crouching,
			const humanoidAnimationState& state
		) {
			using namespace DirectX;
			boneData bones = buildBones(pitch, grounded, crouching, state);
			XMFLOAT4X4 transform;
			XMStoreFloat4x4(&transform, XMMatrixTranspose(
				XMMatrixRotationY(XM_PIDIV2 - yaw) *
				XMMatrixTranslation(
					position.x,
					position.y - 1.625f - (crouching ? .25f : 0.0f),
					position.z
				)
			));

			_shadowProgram.bindShaders(context);
			_objectBuffer.update(context, { transform });
			_objectBuffer.bindVS(context, 0);
			_boneBuffer.bindVS(context, 6);
			_skin.bind(context, 0);
			context->PSSetSamplers(0, 1, _sampler.GetAddressOf());
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			// Shadows always use the complete world-space humanoid, including the
			// local player's head even when its visible first-person pass omits it.
			bones.meshPart = 0u;
			_boneBuffer.update(context, bones);
			_body.bind(context);
			_body.draw(context);
			bones.meshPart = 1u;
			_boneBuffer.update(context, bones);
			_head.bind(context);
			_head.draw(context);
		}
	};

}
