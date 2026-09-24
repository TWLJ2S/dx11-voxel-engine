#pragma once

#include <assets/cpuAsset.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ac {
	struct animationTransform {
		DirectX::XMFLOAT3 position{};
		DirectX::XMFLOAT4 rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
		DirectX::XMFLOAT3 scale{ 1.0f, 1.0f, 1.0f };
	};

	using animationPose = std::vector<animationTransform>;

	inline float resolveAnimationTime(float time, float duration, bool loop) {
		if (duration <= 0.0f) return 0.0f;
		if (!loop) return std::clamp(time, 0.0f, duration);
		float wrapped = std::fmod(time, duration);
		if (wrapped < 0.0f) wrapped += duration;
		return wrapped;
	}

	inline DirectX::XMFLOAT3 animationHermite(
		const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& outTangent,
		const DirectX::XMFLOAT3& b, const DirectX::XMFLOAT3& inTangent,
		float t, float duration) {
		const float t2 = t * t;
		const float t3 = t2 * t;
		const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
		const float h10 = t3 - 2.0f * t2 + t;
		const float h01 = -2.0f * t3 + 3.0f * t2;
		const float h11 = t3 - t2;
		return {
			h00 * a.x + h10 * duration * outTangent.x + h01 * b.x + h11 * duration * inTangent.x,
			h00 * a.y + h10 * duration * outTangent.y + h01 * b.y + h11 * duration * inTangent.y,
			h00 * a.z + h10 * duration * outTangent.z + h01 * b.z + h11 * duration * inTangent.z
		};
	}

	inline animationTransform sampleAnimationChannel(
		const animationChannel& channel, float time) {
		using namespace DirectX;
		if (channel._keyframes.empty()) return {};
		if (channel._keyframes.size() == 1u || time <= channel._keyframes.front()._time) {
			const keyframe& key = channel._keyframes.front();
			return { key._position, key._rotation, key._scale };
		}
		auto upper = std::upper_bound(channel._keyframes.begin(), channel._keyframes.end(), time,
			[](float value, const keyframe& key) { return value < key._time; });
		if (upper == channel._keyframes.end()) {
			const keyframe& key = channel._keyframes.back();
			return { key._position, key._rotation, key._scale };
		}
		const keyframe& a = *(upper - 1);
		const keyframe& b = *upper;
		const float duration = (std::max)(b._time - a._time, 0.000001f);
		float blend = std::clamp((time - a._time) / duration, 0.0f, 1.0f);
		if (a._interpolation == animationInterpolation::step) blend = 0.0f;
		animationTransform result;
		if (a._interpolation == animationInterpolation::cubic) {
			result.position = animationHermite(a._position, a._positionOutTangent,
				b._position, b._positionInTangent, blend, duration);
			result.scale = animationHermite(a._scale, a._scaleOutTangent,
				b._scale, b._scaleInTangent, blend, duration);
			// Smooth quaternion interpolation avoids Euler discontinuities while
			// retaining the ease-in/out expected from cubic transform keys.
			blend = blend * blend * (3.0f - 2.0f * blend);
		}
		else {
			XMStoreFloat3(&result.position, XMVectorLerp(XMLoadFloat3(&a._position), XMLoadFloat3(&b._position), blend));
			XMStoreFloat3(&result.scale, XMVectorLerp(XMLoadFloat3(&a._scale), XMLoadFloat3(&b._scale), blend));
		}
		XMVECTOR qa = XMQuaternionNormalize(XMLoadFloat4(&a._rotation));
		XMVECTOR qb = XMQuaternionNormalize(XMLoadFloat4(&b._rotation));
		if (XMVectorGetX(XMVector4Dot(qa, qb)) < 0.0f) qb = XMVectorNegate(qb);
		XMStoreFloat4(&result.rotation, XMQuaternionNormalize(XMQuaternionSlerp(qa, qb, blend)));
		return result;
	}

	inline void sampleAnimationClip(
		const animation& clip, float time, animationPose& output, size_t boneCount) {
		output.assign(boneCount, {});
		float local = resolveAnimationTime(time * clip._ticksPerSecond, clip._duration, clip._loop);
		if (!clip._timeCurve.empty()) {
			const float normalized = clip._duration > 0.0f ? local / clip._duration : 0.0f;
			local = std::clamp(clip._timeCurve.evaluate(normalized), 0.0f, 1.0f) * clip._duration;
		}
		else if (!clip._timing.empty()) {
			const float normalized = clip._duration > 0.0f ? local / clip._duration : 0.0f;
			local = remapAnimationPhase(normalized, clip._timing) * clip._duration;
		}
		for (const animationChannel& channel : clip._channels)
			if (channel._boneId < output.size())
				output[channel._boneId] = sampleAnimationChannel(channel, local);
	}

	inline animationTransform blendAnimationTransform(
		const animationTransform& a, const animationTransform& b, float weight) {
		using namespace DirectX;
		weight = std::clamp(weight, 0.0f, 1.0f);
		animationTransform result;
		XMStoreFloat3(&result.position, XMVectorLerp(XMLoadFloat3(&a.position), XMLoadFloat3(&b.position), weight));
		XMStoreFloat3(&result.scale, XMVectorLerp(XMLoadFloat3(&a.scale), XMLoadFloat3(&b.scale), weight));
		XMVECTOR qa = XMQuaternionNormalize(XMLoadFloat4(&a.rotation));
		XMVECTOR qb = XMQuaternionNormalize(XMLoadFloat4(&b.rotation));
		if (XMVectorGetX(XMVector4Dot(qa, qb)) < 0.0f) qb = XMVectorNegate(qb);
		XMStoreFloat4(&result.rotation, XMQuaternionNormalize(XMQuaternionSlerp(qa, qb, weight)));
		return result;
	}

	inline void blendAnimationPoses(
		const animationPose& a, const animationPose& b, float weight,
		animationPose& output, const std::vector<float>* boneMask = nullptr) {
		const size_t count = (std::max)(a.size(), b.size());
		output.resize(count);
		for (size_t i = 0; i < count; ++i) {
			const animationTransform left = i < a.size() ? a[i] : animationTransform{};
			const animationTransform right = i < b.size() ? b[i] : animationTransform{};
			const float maskedWeight = boneMask && i < boneMask->size() ? weight * (*boneMask)[i] : weight;
			output[i] = blendAnimationTransform(left, right, maskedWeight);
		}
	}
}
