#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/client.h>

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <assets/cpuAsset.h>
#include <core/playerPose.h>
#include <core/animationSystem.h>
#include <core/shader.h>
#include <core/texture.h>
#include <renderer/buffer.h>

#include <cmath>

namespace ac {
	inline heldItemStyle heldItemStyleFor(const blockDefinition* definition, uint32_t count) {
		if (!definition || count == 0) return heldItemStyle::none;
		return definition->heldStyle();
	}

	inline bool isToolHeldStyle(heldItemStyle style) {
		return style == heldItemStyle::tool || style == heldItemStyle::axe ||
			style == heldItemStyle::sword;
	}

	struct humanoidAnimationState {
		float animationTime = 0.0f;
		float useAnimationRemaining = 0.0f;
		float useAnimationDuration = 0.42f;
		float usePitch = 0.85f;
		float useYaw = 0.0f;
		float useRoll = 0.20f;
		float useMoveY = 1.0f;
		float useMoveZ = 1.0f;
		float smoothedMovement = 0.0f;
		float smoothedPitch = 0.0f;
		float airborneMovement = 0.0f;
		float airborneTime = 0.0f;
		float idleTime = 0.0f;
		float toolRaise = 0.0f;
		float swimBlend = 0.0f;
		float crouchBlend = 0.0f;
		std::vector<animationTimingSegment> useTiming;
		animationCurve useCurve;
		bool toolUsing = false;
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
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> _viewmodelDepth;
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
			const playerPoseConfig& pose = playerPose();
			animation clip;
			clip._duration = 1.0f;
			clip._ticksPerSecond = 1.0f;
			clip._loop = true;
			clip._timing = pose.walk.timing;
			clip._timeCurve = pose.walk.timeCurve;
			for (const auto [bone, phase] : std::array<std::pair<uint32_t, float>, 4>{ {
				{ 2u, pose.walk.armSwing }, { 3u, -pose.walk.armSwing },
				{ 4u, -pose.walk.legSwing }, { 5u, pose.walk.legSwing }
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
				const float normalized = clip._duration > 0.0f ? time / clip._duration : 0.0f;
				const float phase = !clip._timeCurve.empty()
					? std::clamp(clip._timeCurve.evaluate(normalized - std::floor(normalized)), 0.0f, 1.0f)
					: remapAnimationPhase(normalized, clip._timing);
				const animationTransform sampled = sampleAnimationChannel(channel, phase * clip._duration);
				return XMLoadFloat4(&sampled.rotation);
			}
			return XMQuaternionIdentity();
		}

		static float useSwingAmount(const humanoidAnimationState& state) {
			if (state.useAnimationRemaining <= 0.0f || state.useAnimationDuration <= 0.0f)
				return 0.0f;
			const float progress = std::clamp(
				1.0f - state.useAnimationRemaining / state.useAnimationDuration, 0.0f, 1.0f);
			const float timedProgress = state.useCurve.empty()
				? remapAnimationPhase(progress, state.useTiming)
				: std::clamp(state.useCurve.evaluate(progress), 0.0f, 1.0f);
			return std::sin(timedProgress * DirectX::XM_PI);
		}

		static DirectX::XMMATRIX rotationXYZ(float radX, float radY, float radZ) {
			using namespace DirectX;
			return XMMatrixRotationX(radX) *
				XMMatrixRotationY(radY) *
				XMMatrixRotationZ(radZ);
		}

		static const heldItemPose& heldPose(heldItemStyle style) {
			const playerPoseConfig& pose = playerPose();
			switch (style) {
			case heldItemStyle::tool:
			case heldItemStyle::axe:
			case heldItemStyle::sword: return pose.tool;
			case heldItemStyle::rod: return pose.rod;
			case heldItemStyle::cross: return pose.cross;
			case heldItemStyle::sprite: return pose.sprite;
			case heldItemStyle::slab: return pose.slab;
			default: return pose.block;
			}
		}

		static DirectX::XMMATRIX itemLocalTransform(heldItemStyle style, bool firstPerson) {
			using namespace DirectX;
			const heldItemPose& item = heldPose(style);
			const poseScale scale = firstPerson ? item.scaleFirst : item.scaleThird;
			return XMMatrixTranslation(
				item.translation.x - 0.5f,
				item.translation.y - 0.5f,
				item.translation.z - 0.5f) *
				XMMatrixScaling(scale.x, scale.y, scale.z) *
				rotationXYZ(item.rotationDeg.x, item.rotationDeg.y, item.rotationDeg.z);
		}

		static DirectX::XMFLOAT3 heldFistOffset(heldItemStyle style, bool firstPerson) {
			(void)firstPerson;
			const poseVec3& fist = heldPose(style).fist;
			return { fist.x, fist.y, fist.z };
		}

		static DirectX::XMMATRIX cameraWorldFromView(const DirectX::XMFLOAT4X4& view) {
			using namespace DirectX;
			XMMATRIX inverse = XMMatrixInverse(nullptr, XMLoadFloat4x4(&view));
			if (XMMatrixIsNaN(inverse) || XMMatrixIsInfinite(inverse))
				return XMMatrixIdentity();
			return inverse;
		}

		static DirectX::XMMATRIX swimOrientation(float yaw, float pitch) {
			using namespace DirectX;
			const float cy = std::cos(yaw);
			const float sy = std::sin(yaw);
			const float cp = std::cos(pitch);
			const float sp = std::sin(pitch);
			XMVECTOR look = XMVector3Normalize(XMVectorSet(cp * cy, sp, cp * sy, 0.0f));
			XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
			XMVECTOR right = XMVector3Cross(worldUp, look);
			if (XMVectorGetX(XMVector3LengthSq(right)) < 1.0e-4f)
				right = XMVectorSet(-sy, 0.0f, cy, 0.0f);
			right = XMVector3Normalize(right);
			XMVECTOR belly = XMVector3Normalize(XMVector3Cross(right, look));
			XMMATRIX orient = XMMatrixIdentity();
			orient.r[0] = XMVectorSetW(right, 0.0f);
			orient.r[1] = XMVectorSetW(look, 0.0f);
			orient.r[2] = XMVectorSetW(belly, 0.0f);
			orient.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
			return orient;
		}

		static float swimEase(float t) {
			t = std::clamp(t, 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}

		static DirectX::XMVECTOR swimArmRotation(float u, bool left) {
			using namespace DirectX;
			const auto& swim = playerPose().swim;
			const float side = left ? -1.0f : 1.0f;
			const XMVECTOR qForward = XMQuaternionRotationRollPitchYaw(
				XM_PI, side * swim.armYaw, -side * swim.armSpread);
			const XMVECTOR qWide = XMQuaternionRotationRollPitchYaw(
				0.0f, 0.0f, side * (XM_PI + swim.armSweep));
			const XMVECTOR qBack = XMQuaternionRotationRollPitchYaw(
				swim.armTuck, side * swim.armYaw * 0.25f, side * swim.armSpread * 0.35f);

			const float holdForward = swim.holdForward;
			const float sweepEnd = (std::max)(holdForward + 0.01f, swim.sweepEnd);
			const float holdBack = (std::max)(sweepEnd + 0.01f, swim.holdBack);

			if (u < holdForward)
				return qForward;
			if (u < sweepEnd) {
				const float t = swimEase((u - holdForward) / (sweepEnd - holdForward));
				if (t < 0.5f)
					return XMQuaternionSlerp(qForward, qWide, t * 2.0f);
				return XMQuaternionSlerp(qWide, qBack, t * 2.0f - 1.0f);
			}
			if (u < holdBack)
				return qBack;
			const float recover = swimEase((u - holdBack) / (std::max)(0.01f, 1.0f - holdBack));
			return XMQuaternionRotationRollPitchYaw(
				recover * XM_PI + (1.0f - recover) * swim.armTuck,
				side * swim.armYaw * recover,
				-side * swim.armSpread * recover);
		}

		static DirectX::XMVECTOR swimBoneRotation(uint32_t id, float animationTime) {
			using namespace DirectX;
			const auto& swim = playerPose().swim;
			const float cycle = animationTime * XM_2PI;
			float u = std::fmod(animationTime, 1.0f);
			if (u < 0.0f) u += 1.0f;
			const float kick = std::sin(cycle * 2.0f);
			switch (id) {
			case 0u:
				return XMQuaternionRotationRollPitchYaw(swim.bodyPitch, 0.0f, 0.0f);
			case 1u:
				return XMQuaternionRotationRollPitchYaw(swim.headPitch, 0.0f, 0.0f);
			case 2u:
				return swimArmRotation(u, false);
			case 3u:
				return swimArmRotation(u, true);
			case 4u:
				return XMQuaternionRotationRollPitchYaw(
					swim.legPitch + kick * swim.legKick,
					kick * 0.04f,
					swim.legRoll + kick * swim.legRollKick);
			case 5u:
				return XMQuaternionRotationRollPitchYaw(
					swim.legPitch - kick * swim.legKick,
					-kick * 0.04f,
					-swim.legRoll - kick * swim.legRollKick);
			default:
				return XMQuaternionIdentity();
			}
		}

		static DirectX::XMMATRIX playerWorldMatrix(
			const DirectX::XMFLOAT3& cameraPosition,
			float yaw,
			float pitch,
			bool crouching,
			float swimBlend,
			bool /*thirdPerson*/
		) {
			using namespace DirectX;
			const float swimAmount = std::clamp(swimBlend, 0.0f, 1.0f);
			const float swimSmooth = swimAmount * swimAmount * (3.0f - 2.0f * swimAmount);
			const XMMATRIX standOrient = XMMatrixRotationY(XM_PIDIV2 - yaw);
			const auto& swim = playerPose().swim;
			if (swimSmooth <= 0.001f) {
				const float feetBelowEye = crouching ? swim.crouchFeet : swim.standFeet;
				return standOrient * XMMatrixTranslation(
					cameraPosition.x,
					cameraPosition.y - feetBelowEye,
					cameraPosition.z
				);
			}
			const float eyePivot = swim.eyePivotBase + swim.eyePivotLift * swimSmooth;
			const XMVECTOR standQ = XMQuaternionRotationMatrix(standOrient);
			const XMVECTOR swimQ = XMQuaternionRotationMatrix(swimOrientation(yaw, pitch));
			const XMMATRIX orient = XMMatrixRotationQuaternion(
				XMQuaternionSlerp(standQ, swimQ, swimSmooth));
			return XMMatrixTranslation(0.0f, -eyePivot, 0.0f) *
				orient *
				XMMatrixTranslation(cameraPosition.x, cameraPosition.y, cameraPosition.z);
		}

		static DirectX::XMMATRIX viewmodelLocal(
			const humanoidAnimationState& state,
			heldItemStyle style
		) {
			using namespace DirectX;
			const auto& view = playerPose().viewmodel;
			const float swing = useSwingAmount(state);
			const float walk = state.smoothedMovement;
			const float bob = std::sin(state.animationTime * XM_2PI);
			const float idle = std::sin(state.idleTime * view.idleRate);
			const float idle2 = std::sin(state.idleTime * view.idle2Rate + view.idle2Phase);
			const float airborne = (std::min)(state.airborneTime * view.airborneRate, 1.0f);
			(void)style;

			const XMMATRIX pose =
				XMMatrixRotationX(view.poseDeg.x) *
				XMMatrixRotationY(view.poseDeg.y) *
				XMMatrixRotationZ(view.poseDeg.z);
			const XMMATRIX extraMotion = XMMatrixRotationRollPitchYaw(
				-swing * (view.swingPitch + state.usePitch * view.swingPitchUse) +
					bob * walk * view.walkBobPitch + idle * view.idlePitch - airborne * view.airbornePitch,
				swing * (view.swingYaw + state.useYaw) + idle2 * view.idleYaw,
				-swing * (view.swingRoll + state.useRoll) + bob * walk * view.walkBobRoll + idle * view.idleRoll
			);
			const XMMATRIX extraMove = XMMatrixTranslation(
				view.walkX * bob * walk + idle * view.idleX,
				-view.walkY * std::abs(bob) * walk - swing * view.swingY * state.useMoveY +
					idle2 * view.idleY - airborne * view.airborneY,
				swing * view.swingZ * state.useMoveZ + idle * view.idleZ
			);
			return pose * extraMotion * extraMove *
				XMMatrixTranslation(view.offset.x, view.offset.y, view.offset.z);
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
			float expansion = 0.0f,
			bool includeTop = true,
			bool includeBottom = true
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
			// Inset the caps so coplanar joints (neck, hips) cannot z-fight, while
			// still sealing each cuboid so shadows cannot leak through an open end.
			constexpr float capInset = 0.002f;
			if (includeTop) {
				const float topY = maximum.y - capInset;
				appendFace(vertices, indices, {{
					{ minimum.x, topY, minimum.z }, { minimum.x, topY, maximum.z },
					{ maximum.x, topY, maximum.z }, { maximum.x, topY, minimum.z }
				}}, { 0.0f, 1.0f, 0.0f }, x + d, y, x + d + w, y + d, bone);
			}
			if (includeBottom) {
				const float bottomY = minimum.y + capInset;
				appendFace(vertices, indices, {{
					{ minimum.x, bottomY, maximum.z }, { minimum.x, bottomY, minimum.z },
					{ maximum.x, bottomY, minimum.z }, { maximum.x, bottomY, maximum.z }
				}}, { 0.0f, -1.0f, 0.0f }, x + d + w, y, x + d + w + w, y + d, bone);
			}
		}

		enum class modelPiece { body, head, firstPersonArm };

		static gpuModel createModel(ID3D11Device* device, modelPiece piece) {
			std::vector<vertex> vertices;
			std::vector<uint32_t> indices;
			if (piece == modelPiece::head) {
				appendBox(vertices, indices, { -0.25f, 1.50f, -0.25f }, { 0.25f, 2.00f, 0.25f }, { 0, 0, 8, 8, 8 }, 0.0f, true, true);
				appendBox(vertices, indices, { -0.25f, 1.50f, -0.25f }, { 0.25f, 2.00f, 0.25f }, { 32, 0, 8, 8, 8 }, 0.03125f, true, true);
			}
			else if (piece == modelPiece::body) {
				// Base body, right leg, left leg, right arm, left arm.
				appendBox(vertices, indices, { -0.25f, 0.75f, -0.125f }, { 0.25f, 1.50f, 0.125f }, { 16, 16, 8, 12, 4 }, 0.0f, true, true);
				appendBox(vertices, indices, { -0.25f, 0.00f, -0.125f }, { 0.00f, 0.75f, 0.125f }, { 0, 16, 4, 12, 4 });
				appendBox(vertices, indices, { 0.00f, 0.00f, -0.125f }, { 0.25f, 0.75f, 0.125f }, { 16, 48, 4, 12, 4 });
				appendBox(vertices, indices, { -0.50f, 0.75f, -0.125f }, { -0.25f, 1.50f, 0.125f }, { 40, 16, 4, 12, 4 });
				appendBox(vertices, indices, { 0.25f, 0.75f, -0.125f }, { 0.50f, 1.50f, 0.125f }, { 32, 48, 4, 12, 4 });

				// Jacket, trousers, and sleeves use the alpha-capable second layer.
				constexpr float layer = 0.015625f;
				appendBox(vertices, indices, { -0.25f, 0.75f, -0.125f }, { 0.25f, 1.50f, 0.125f }, { 16, 32, 8, 12, 4 }, layer, true, true);
				appendBox(vertices, indices, { -0.25f, 0.00f, -0.125f }, { 0.00f, 0.75f, 0.125f }, { 0, 32, 4, 12, 4 }, layer);
				appendBox(vertices, indices, { 0.00f, 0.00f, -0.125f }, { 0.25f, 0.75f, 0.125f }, { 0, 48, 4, 12, 4 }, layer);
				appendBox(vertices, indices, { -0.50f, 0.75f, -0.125f }, { -0.25f, 1.50f, 0.125f }, { 40, 32, 4, 12, 4 }, layer);
				appendBox(vertices, indices, { 0.25f, 0.75f, -0.125f }, { 0.50f, 1.50f, 0.125f }, { 48, 48, 4, 12, 4 }, layer);
			}
			else {
				// Exact third-person right arm (4x12x4 + sleeve), same UVs and bone.
				appendBox(vertices, indices, { -0.50f, 0.75f, -0.125f }, { -0.25f, 1.50f, 0.125f }, { 40, 16, 4, 12, 4 });
				appendBox(vertices, indices, { -0.50f, 0.75f, -0.125f }, { -0.25f, 1.50f, 0.125f }, { 40, 32, 4, 12, 4 }, 0.015625f);
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
			bool swimming,
			const humanoidAnimationState& state,
			heldItemStyle itemStyle = heldItemStyle::none,
			bool firstPerson = false
		) const {
			using namespace DirectX;
			(void)crouching;
			(void)swimming;
			const playerPoseConfig& pose = playerPose();
			const float swimAmount = std::clamp(state.swimBlend, 0.0f, 1.0f);
			const float swimSmooth = swimAmount * swimAmount * (3.0f - 2.0f * swimAmount);
			const float moving = swimSmooth > 0.5f ? 1.0f : state.smoothedMovement;
			const float useSwing = useSwingAmount(state);
			const float stroke = std::sin(state.animationTime * XM_2PI);
			const float hold = (std::max)(0.0f, (std::min)(1.0f, state.toolRaise));
			const float holdSmooth = hold * hold * (3.0f - 2.0f * hold);
			const bool twoHand = (itemStyle == heldItemStyle::tool || itemStyle == heldItemStyle::axe) &&
				swimSmooth < pose.swim.twoHandLimit && holdSmooth > 0.001f;
			const float chop = useSwing * holdSmooth;
			const float axePitch = itemStyle == heldItemStyle::axe ? state.usePitch : 0.0f;
			const float axeYaw = itemStyle == heldItemStyle::axe ? state.useYaw : 0.0f;
			const float axeRoll = itemStyle == heldItemStyle::axe ? state.useRoll : 0.0f;
			boneData bones{};
			const std::array<XMFLOAT3, 6> pivots = {{
				{ 0, 1.125f, 0 }, { 0, 1.5f, 0 }, { -.375f, 1.5f, 0 },
				{ .375f, 1.5f, 0 }, { -.125f, .75f, 0 }, { .125f, .75f, 0 }
			}};
			for (uint32_t id = 0; id < 6; ++id) {
				const float armWalk = (twoHand && (id == 2u || id == 3u))
					? moving * (1.0f - holdSmooth * pose.toolRaise.walkDampen) : moving;
				XMVECTOR rotation = XMQuaternionSlerp(
					XMQuaternionIdentity(),
					sampleRotation(_walkAnimation, id, state.animationTime),
					armWalk
				);
				if (!grounded && swimSmooth < 0.5f && id >= 2u)
					rotation = XMQuaternionMultiply(rotation, XMQuaternionRotationRollPitchYaw(
						id < 4u ? pose.airborne.armPitch : pose.airborne.legPitch, 0, 0));
				if (id == 1u)
					rotation = XMQuaternionMultiply(rotation, XMQuaternionRotationRollPitchYaw(
						-state.smoothedPitch * pose.head.pitchFollow * (1.0f - swimSmooth), 0, 0));
				else if (twoHand && id == 2u) {
					const armReadyPose& ready = firstPerson ? pose.twoHand.rightFirst : pose.twoHand.rightThird;
					const float bob = stroke * moving * pose.toolRaise.bob * holdSmooth;
					rotation = XMQuaternionMultiply(rotation,
						XMQuaternionRotationRollPitchYaw(
							holdSmooth * (ready.pitch + bob) + chop * (ready.chop + axePitch),
							holdSmooth * ready.yaw + chop * (ready.chopYaw + axeYaw),
							holdSmooth * ready.roll - chop * (ready.chopRoll + axeRoll)));
				}
				else if (twoHand && id == 3u) {
					const armReadyPose& ready = firstPerson ? pose.twoHand.leftFirst : pose.twoHand.leftThird;
					const float bob = stroke * moving * pose.toolRaise.bob * holdSmooth;
					rotation = XMQuaternionMultiply(rotation,
						XMQuaternionRotationRollPitchYaw(
							holdSmooth * (ready.pitch + bob) + chop * (ready.chop + axePitch),
							holdSmooth * ready.yaw - chop * (ready.chopYaw + axeYaw),
							holdSmooth * ready.roll + chop * (ready.chopRoll + axeRoll)));
				}
				else if (id == 2u && useSwing > 0.0f && swimSmooth < pose.swim.twoHandLimit)
					rotation = XMQuaternionMultiply(rotation,
						XMQuaternionRotationRollPitchYaw(
							-useSwing * state.usePitch,
							useSwing * 0.12f,
							-useSwing * state.useRoll));
				if (swimSmooth > 0.001f) {
					XMVECTOR swimRotation = swimBoneRotation(id, state.animationTime);
					if (firstPerson && id == 1u)
						swimRotation = XMQuaternionIdentity();
					rotation = XMQuaternionSlerp(rotation, swimRotation, swimSmooth);
				}
				XMMATRIX local = XMMatrixTranslation(-pivots[id].x, -pivots[id].y, -pivots[id].z) *
					XMMatrixRotationQuaternion(rotation) * XMMatrixTranslation(pivots[id].x, pivots[id].y, pivots[id].z);
				const float crouch = std::clamp(state.crouchBlend, 0.0f, 1.0f) * (1.0f - swimSmooth);
				if (crouch > 0.0001f && id <= 3u) {
					// Rotate the torso, head, and arms as one connected upper-body rig
					// around the hips, then lower it into the shortened collision pose.
					local *= XMMatrixTranslation(0.0f, -0.75f, 0.0f) *
						XMMatrixRotationX(pose.crouch.torsoPitch * crouch) *
						XMMatrixTranslation(0.0f, 0.75f, 0.0f) *
						XMMatrixTranslation(
							0.0f, pose.crouch.upperBodyY * crouch,
							pose.crouch.upperBodyZ * crouch);
				}
				else if (crouch > 0.0001f && id >= 4u) {
					local *= XMMatrixTranslation(-pivots[id].x, -pivots[id].y, -pivots[id].z) *
						XMMatrixRotationX(pose.crouch.legPitch * crouch) *
						XMMatrixTranslation(pivots[id].x, pivots[id].y, pivots[id].z) *
						XMMatrixTranslation(0.0f, 0.0f, pose.crouch.legZ * crouch);
				}
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

			_program.initVertexShader(device, L"assets/shader/player.hlsl", "vertexMain", "vs_5_0");
			_program.initPixelShader(device, L"assets/shader/player.hlsl", "pixelMain", "ps_5_0");
			_shadowProgram.initVertexShader(device, L"assets/shader/playerShadow.hlsl", "vertexMain", "vs_5_0");
			_shadowProgram.initPixelShader(device, L"assets/shader/playerShadow.hlsl", "pixelMain", "ps_5_0");
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
			// A Minecraft skin is an atlas, not a tileable surface. Mips combine
			// adjacent limbs/faces and visibly corrupt their colours at a distance.
			sampler.MinLOD = sampler.MaxLOD = 0.0f;
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

			D3D11_DEPTH_STENCIL_DESC viewmodelDepth{};
			viewmodelDepth.DepthEnable = FALSE;
			viewmodelDepth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			viewmodelDepth.DepthFunc = D3D11_COMPARISON_ALWAYS;
			DX_CHECK(device->CreateDepthStencilState(&viewmodelDepth, _viewmodelDepth.GetAddressOf()));

			D3D11_RASTERIZER_DESC rasterizer{};
			rasterizer.FillMode = D3D11_FILL_SOLID;
			rasterizer.CullMode = D3D11_CULL_NONE;
			rasterizer.DepthClipEnable = TRUE;
			DX_CHECK(device->CreateRasterizerState(&rasterizer, _rasterizer.GetAddressOf()));
		}

		void triggerUseAnimation(humanoidAnimationState& state, heldItemStyle style = heldItemStyle::none) {
			const auto& use = playerPose().use;
			if (state.useAnimationRemaining <= use.retrigger) {
				if (isToolHeldStyle(style)) {
					state.useAnimationDuration = use.toolDuration;
					state.usePitch = use.toolPitch;
					state.useRoll = use.toolRoll;
					state.useTiming = use.toolTiming;
					state.useCurve = use.toolCurve;
				}
				else {
					state.useAnimationDuration = use.handDuration;
					state.usePitch = use.handPitch;
					state.useRoll = use.handRoll;
					state.useTiming = use.handTiming;
					state.useCurve = use.handCurve;
				}
				state.useYaw = 0.0f;
				state.useMoveY = 1.0f;
				state.useMoveZ = 1.0f;
				state.useAnimationRemaining = state.useAnimationDuration;
			}
		}

		void triggerAttackAnimation(humanoidAnimationState& state, heldItemStyle style = heldItemStyle::none) {
			const auto& attack = playerPose().attack;
			const auto& use = playerPose().use;
			if (state.useAnimationRemaining <= use.retrigger) {
				if (style == heldItemStyle::axe) {
					state.useAnimationDuration = attack.axeDuration;
					state.usePitch = attack.axePitch;
					state.useYaw = attack.axeYaw;
					state.useRoll = attack.axeRoll;
					state.useMoveY = attack.axeMoveY;
					state.useMoveZ = attack.axeMoveZ;
					state.useTiming = attack.axeTiming;
					state.useCurve = attack.axeCurve;
				}
				else if (style == heldItemStyle::sword) {
					state.useAnimationDuration = attack.swordDuration;
					state.usePitch = attack.swordPitch;
					state.useYaw = attack.swordYaw;
					state.useRoll = attack.swordRoll;
					state.useMoveY = attack.swordMoveY;
					state.useMoveZ = attack.swordMoveZ;
					state.useTiming = attack.swordTiming;
					state.useCurve = attack.swordCurve;
				}
				else if (style == heldItemStyle::tool) {
					state.useAnimationDuration = attack.toolDuration;
					state.usePitch = attack.toolPitch;
					state.useYaw = 0.0f;
					state.useRoll = attack.toolRoll;
					state.useMoveY = 1.0f;
					state.useMoveZ = 1.0f;
					state.useTiming = attack.toolTiming;
					state.useCurve = attack.toolCurve;
				}
				else {
					state.useAnimationDuration = attack.handDuration;
					state.usePitch = attack.handPitch;
					state.useYaw = 0.0f;
					state.useRoll = attack.handRoll;
					state.useMoveY = 1.0f;
					state.useMoveZ = 1.0f;
					state.useTiming = attack.handTiming;
					state.useCurve = attack.handCurve;
				}
				state.useAnimationRemaining = state.useAnimationDuration;
			}
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
			bool swimming,
			bool thirdPerson,
			const DirectX::XMFLOAT4X4& view,
			float deltaTime,
			humanoidAnimationState& state,
			heldItemStyle itemStyle,
			DirectX::XMFLOAT4X4* heldItemWorld,
			DirectX::XMFLOAT4X4* viewmodelWorld = nullptr
		) {
			using namespace DirectX;
			(void)view;
			const playerPoseConfig& pose = playerPose();
			const float swimVisual = std::clamp(state.swimBlend, 0.0f, 1.0f);
			const float planarSpeed = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
			const float swimSpeed = std::sqrt(
				velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z);
			const float speed = swimVisual > 0.5f ? swimSpeed : planarSpeed;
			const float referenceSpeed = swimming ? pose.walk.referenceSwim :
				(sprinting ? pose.walk.referenceSprint : (crouching ? pose.walk.referenceCrouch : pose.walk.referenceWalk));
			const float speedMovement = (std::min)(speed / referenceSpeed, 1.0f);
			if (grounded && swimVisual < 0.5f) {
				state.airborneMovement = speedMovement;
				state.airborneTime = 0.0f;
			}
			else {
				state.airborneTime += (std::max)(deltaTime, 0.0f);
			}
			const float airborneGait = (std::max)(speedMovement, state.airborneMovement * pose.walk.airborneKeep);
			const float targetMovement = swimVisual > 0.5f ? 1.0f :
				(grounded ? speedMovement : (std::max)(airborneGait, pose.walk.airborneMin));
			const float poseResponse = 1.0f - std::exp(-pose.walk.poseResponse * (std::max)(deltaTime, 0.0f));
			state.smoothedMovement += (targetMovement - state.smoothedMovement) * poseResponse;
			state.smoothedPitch += (pitch - state.smoothedPitch) * poseResponse;
			const float landStride = speed * (crouching ? pose.walk.strideCrouch :
				(sprinting ? pose.walk.strideSprint : pose.walk.strideWalk));
			const float swimStride = (std::max)(speed * pose.swim.strideSpeed, pose.swim.strideMin);
			const float strideRate = landStride + (swimStride - landStride) * swimVisual;
			const float airborneStrideRate = (grounded || swimVisual > 0.2f)
				? strideRate
				: (std::max)(strideRate, pose.walk.airborneStrideMin);
			state.animationTime += (std::max)(deltaTime, 0.0f) * airborneStrideRate;
			state.idleTime += (std::max)(deltaTime, 0.0f);
			state.useAnimationRemaining = (std::max)(0.0f, state.useAnimationRemaining - deltaTime);
			{
				const float dt = (std::max)(deltaTime, 0.0f);
				const bool raising = (itemStyle == heldItemStyle::tool ||
					itemStyle == heldItemStyle::axe) && state.toolUsing && !swimming;
				const float seconds = raising ? pose.toolRaise.raiseSeconds : pose.toolRaise.lowerSeconds;
				const float target = raising ? 1.0f : 0.0f;
				if (target > state.toolRaise)
					state.toolRaise = (std::min)(1.0f, state.toolRaise + dt / seconds);
				else if (target < state.toolRaise)
					state.toolRaise = (std::max)(0.0f, state.toolRaise - dt / seconds);
			}
			boneData bones = buildBones(pitch, grounded, crouching, swimming, state, itemStyle, !thirdPerson);
			const XMMATRIX playerWorld = playerWorldMatrix(
				cameraPosition, yaw, pitch, crouching, state.swimBlend, thirdPerson);
			XMFLOAT4X4 transform;
			XMStoreFloat4x4(&transform, XMMatrixTranspose(playerWorld));

			if (heldItemWorld) {
				heldItemWorld->m[0][0] = 0.0f;
			}
			if (viewmodelWorld) {
				viewmodelWorld->m[0][0] = 0.0f;
			}

			if (thirdPerson) {
				_program.bindShaders(context);
				_objectBuffer.update(context, { transform });
				_objectBuffer.bindVS(context, 0);
				_boneBuffer.bindVS(context, 6);
				_skin.bind(context, 0);
				context->PSSetSamplers(0, 1, _sampler.GetAddressOf());
				context->OMSetBlendState(_blend.Get(), nullptr, 0xffffffffu);
				context->RSSetState(_rasterizer.Get());
				context->GSSetShader(nullptr, nullptr, 0);
				context->HSSetShader(nullptr, nullptr, 0);
				context->DSSetShader(nullptr, nullptr, 0);
				context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context->OMSetDepthStencilState(_depth.Get(), 0);
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
				_program.bindShaders(context);
				_objectBuffer.update(context, { transform });
				_objectBuffer.bindVS(context, 0);
				_boneBuffer.bindVS(context, 6);
				_skin.bind(context, 0);
				context->PSSetSamplers(0, 1, _sampler.GetAddressOf());
				context->OMSetBlendState(_blend.Get(), nullptr, 0xffffffffu);
				context->RSSetState(_rasterizer.Get());
				context->GSSetShader(nullptr, nullptr, 0);
				context->HSSetShader(nullptr, nullptr, 0);
				context->DSSetShader(nullptr, nullptr, 0);
				context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context->OMSetDepthStencilState(_depth.Get(), 0);
				bones.meshPart = 3u;
				_boneBuffer.update(context, bones);
				_body.bind(context);
				_body.draw(context);
			}
			if (heldItemWorld && itemStyle != heldItemStyle::none) {
				const XMMATRIX bone = XMMatrixTranspose(XMLoadFloat4x4(&bones.transforms[2]));
				const XMFLOAT3 fist = heldFistOffset(itemStyle, false);
				const XMMATRIX itemWorld =
					itemLocalTransform(itemStyle, thirdPerson ? false : true) *
					XMMatrixTranslation(fist.x, fist.y, fist.z) *
					bone *
					playerWorld;
				XMStoreFloat4x4(heldItemWorld, XMMatrixTranspose(itemWorld));
			}
		}

		void drawViewmodel(
			ID3D11DeviceContext* context,
			const DirectX::XMFLOAT4X4& worldTransposed
		) {
			boneData bones{};
			bones.meshPart = 2u;
			_program.bindShaders(context);
			_objectBuffer.update(context, { worldTransposed });
			_objectBuffer.bindVS(context, 0);
			_boneBuffer.update(context, bones);
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
			_firstPersonArm.bind(context);
			_firstPersonArm.draw(context);
		}

		void renderShadow(
			ID3D11DeviceContext* context,
			const DirectX::XMFLOAT3& position,
			float yaw,
			float pitch,
			bool grounded,
			bool crouching,
			bool swimming,
			const humanoidAnimationState& state,
			heldItemStyle itemStyle = heldItemStyle::none
		) {
			using namespace DirectX;
			boneData bones = buildBones(pitch, grounded, crouching, swimming, state, itemStyle, false);
			XMFLOAT4X4 transform;
			XMStoreFloat4x4(&transform, XMMatrixTranspose(
				playerWorldMatrix(position, yaw, pitch, crouching, state.swimBlend, true)
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
