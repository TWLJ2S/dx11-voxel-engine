#pragma once

#include <rapidjson/document.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace ac {
	enum class animationInterpolation : uint8_t {
		step,
		linear,
		cubic
	};

	enum class animationExtrapolation : uint8_t {
		clamp,
		loop,
		pingPong
	};

	struct animationCurveKey {
		float time = 0.0f;
		float value = 0.0f;
		// Tangents are value units per second. They are only used by cubic keys.
		float inTangent = 0.0f;
		float outTangent = 0.0f;
		animationInterpolation interpolation = animationInterpolation::linear;
	};

	// A compact, engine-independent scalar curve. Curves are suitable for time
	// warping, blend weights, events/parameters, and individual transform axes.
	// Keys are immutable while sampling, so one curve can safely be shared by
	// every instance of a clip.
	struct animationCurve {
		std::vector<animationCurveKey> keys;
		animationExtrapolation preExtrapolation = animationExtrapolation::clamp;
		animationExtrapolation postExtrapolation = animationExtrapolation::clamp;

		bool empty() const { return keys.empty(); }

		float evaluate(float time) const {
			if (keys.empty()) return 0.0f;
			if (keys.size() == 1u) return keys.front().value;
			const float first = keys.front().time;
			const float last = keys.back().time;
			const float length = last - first;
			auto extrapolate = [&](animationExtrapolation mode) {
				if (length <= 0.0f || mode == animationExtrapolation::clamp)
					return std::clamp(time, first, last);
				float phase = std::fmod(time - first, length);
				if (phase < 0.0f) phase += length;
				if (mode == animationExtrapolation::pingPong) {
					const float cycle = std::floor((time - first) / length);
					if (std::abs(std::fmod(cycle, 2.0f)) >= 1.0f) phase = length - phase;
				}
				return first + phase;
			};
			if (time < first) time = extrapolate(preExtrapolation);
			else if (time > last) time = extrapolate(postExtrapolation);

			auto upper = std::upper_bound(keys.begin(), keys.end(), time,
				[](float value, const animationCurveKey& key) { return value < key.time; });
			if (upper == keys.begin()) return keys.front().value;
			if (upper == keys.end()) return keys.back().value;
			const animationCurveKey& a = *(upper - 1);
			const animationCurveKey& b = *upper;
			const float duration = b.time - a.time;
			if (duration <= 0.0f || a.interpolation == animationInterpolation::step)
				return a.value;
			const float t = std::clamp((time - a.time) / duration, 0.0f, 1.0f);
			if (a.interpolation == animationInterpolation::linear)
				return a.value + (b.value - a.value) * t;
			const float t2 = t * t;
			const float t3 = t2 * t;
			return (2.0f * t3 - 3.0f * t2 + 1.0f) * a.value +
				(t3 - 2.0f * t2 + t) * duration * a.outTangent +
				(-2.0f * t3 + 3.0f * t2) * b.value +
				(t3 - t2) * duration * b.inTangent;
		}
	};

	inline animationInterpolation animationInterpolationFromString(
		const std::string& value, const std::string& source) {
		if (value == "step" || value == "hold") return animationInterpolation::step;
		if (value == "linear") return animationInterpolation::linear;
		if (value == "cubic" || value == "hermite") return animationInterpolation::cubic;
		throw std::runtime_error(source + ": unknown curve interpolation '" + value + "'");
	}

	inline animationExtrapolation animationExtrapolationFromString(
		const std::string& value, const std::string& source) {
		if (value == "clamp") return animationExtrapolation::clamp;
		if (value == "loop") return animationExtrapolation::loop;
		if (value == "pingPong" || value == "pingpong") return animationExtrapolation::pingPong;
		throw std::runtime_error(source + ": unknown curve extrapolation '" + value + "'");
	}

	inline animationCurve loadAnimationCurve(
		const rapidjson::Value& value, const std::string& source) {
		const rapidjson::Value* keyArray = &value;
		animationCurve result;
		if (value.IsObject()) {
			if (!value.HasMember("keys"))
				throw std::runtime_error(source + ": curve object requires a keys array");
			keyArray = &value["keys"];
			auto readExtrapolation = [&](const char* name, animationExtrapolation& output) {
				if (!value.HasMember(name)) return;
				if (!value[name].IsString())
					throw std::runtime_error(source + ": curve extrapolation must be a string");
				output = animationExtrapolationFromString(value[name].GetString(), source);
			};
			readExtrapolation("pre", result.preExtrapolation);
			readExtrapolation("post", result.postExtrapolation);
		}
		if (!keyArray->IsArray() || keyArray->Empty())
			throw std::runtime_error(source + ": curve keys must be a non-empty array");
		result.keys.reserve(keyArray->Size());
		for (const rapidjson::Value& entry : keyArray->GetArray()) {
			if (!entry.IsObject() || !entry.HasMember("time") || !entry["time"].IsNumber() ||
				!entry.HasMember("value") || !entry["value"].IsNumber())
				throw std::runtime_error(source + ": each curve key requires numeric time and value");
			animationCurveKey key;
			key.time = entry["time"].GetFloat();
			key.value = entry["value"].GetFloat();
			if (!std::isfinite(key.time) || !std::isfinite(key.value))
				throw std::runtime_error(source + ": curve key values must be finite");
			if (entry.HasMember("inTangent")) {
				if (!entry["inTangent"].IsNumber()) throw std::runtime_error(source + ": inTangent must be numeric");
				key.inTangent = entry["inTangent"].GetFloat();
			}
			if (entry.HasMember("outTangent")) {
				if (!entry["outTangent"].IsNumber()) throw std::runtime_error(source + ": outTangent must be numeric");
				key.outTangent = entry["outTangent"].GetFloat();
			}
			if (entry.HasMember("interpolation")) {
				if (!entry["interpolation"].IsString()) throw std::runtime_error(source + ": interpolation must be a string");
				key.interpolation = animationInterpolationFromString(entry["interpolation"].GetString(), source);
			}
			result.keys.push_back(key);
		}
		std::stable_sort(result.keys.begin(), result.keys.end(),
			[](const animationCurveKey& a, const animationCurveKey& b) { return a.time < b.time; });
		for (size_t i = 1; i < result.keys.size(); ++i)
			if (result.keys[i].time <= result.keys[i - 1].time)
				throw std::runtime_error(source + ": curve key times must be unique");
		return result;
	}

	enum class animationEasing : uint8_t {
		linear,
		smoothStep,
		easeIn,
		easeOut,
		hold
	};

	struct animationTimingSegment {
		float duration = 1.0f;
		float speed = 1.0f;
		animationEasing easing = animationEasing::linear;
	};

	inline float animationEase(float value, animationEasing easing) {
		const float t = std::clamp(value, 0.0f, 1.0f);
		switch (easing) {
		case animationEasing::smoothStep: return t * t * (3.0f - 2.0f * t);
		case animationEasing::easeIn: return t * t;
		case animationEasing::easeOut: return 1.0f - (1.0f - t) * (1.0f - t);
		case animationEasing::hold: return t >= 1.0f ? 1.0f : 0.0f;
		default: return t;
		}
	}

	// Maps a uniformly advancing, normalized loop phase through authored timing
	// segments. A zero-speed segment is a true pause; unequal speeds and easing
	// change motion cadence without changing the loop's overall duration.
	inline float remapAnimationPhase(
		float phase,
		const std::vector<animationTimingSegment>& timing
	) {
		if (timing.empty()) return phase - std::floor(phase);
		float totalDuration = 0.0f;
		float totalTravel = 0.0f;
		for (const animationTimingSegment& segment : timing) {
			totalDuration += (std::max)(segment.duration, 0.0f);
			totalTravel += (std::max)(segment.duration, 0.0f) * (std::max)(segment.speed, 0.0f);
		}
		if (totalDuration <= 0.0f || totalTravel <= 0.0f) return 0.0f;

		const float wrapped = phase - std::floor(phase);
		const float timelineTime = wrapped * totalDuration;
		float elapsed = 0.0f;
		float travelled = 0.0f;
		for (size_t index = 0; index < timing.size(); ++index) {
			const animationTimingSegment& segment = timing[index];
			const float duration = (std::max)(segment.duration, 0.0f);
			const float speed = (std::max)(segment.speed, 0.0f);
			if (timelineTime <= elapsed + duration || index + 1u == timing.size()) {
				const float local = duration > 0.0f
					? (timelineTime - elapsed) / duration : 1.0f;
				return std::clamp(
					(travelled + duration * speed * animationEase(local, segment.easing)) /
						totalTravel,
					0.0f, 1.0f);
			}
			elapsed += duration;
			travelled += duration * speed;
		}
		return 0.0f;
	}

	inline float remapAnimationTime(
		float time,
		const std::vector<animationTimingSegment>& timing
	) {
		const float completedLoops = std::floor(time);
		return completedLoops + remapAnimationPhase(time, timing);
	}

	inline animationEasing animationEasingFromString(
		const std::string& value,
		const std::string& source
	) {
		if (value == "linear") return animationEasing::linear;
		if (value == "smoothStep") return animationEasing::smoothStep;
		if (value == "easeIn") return animationEasing::easeIn;
		if (value == "easeOut") return animationEasing::easeOut;
		if (value == "hold") return animationEasing::hold;
		throw std::runtime_error(source + ": unknown animation easing '" + value + "'");
	}

	inline std::vector<animationTimingSegment> loadAnimationTiming(
		const rapidjson::Value& value,
		const std::string& source
	) {
		if (!value.IsArray() || value.Empty())
			throw std::runtime_error(source + ": timing must be a non-empty array");
		std::vector<animationTimingSegment> result;
		result.reserve(value.Size());
		for (const rapidjson::Value& entry : value.GetArray()) {
			if (!entry.IsObject() || !entry.HasMember("duration") ||
				!entry["duration"].IsNumber())
				throw std::runtime_error(source + ": each timing segment requires a numeric duration");
			animationTimingSegment segment;
			segment.duration = entry["duration"].GetFloat();
			if (segment.duration <= 0.0f)
				throw std::runtime_error(source + ": timing duration must be positive");
			if (entry.HasMember("speed")) {
				if (!entry["speed"].IsNumber() || entry["speed"].GetFloat() < 0.0f)
					throw std::runtime_error(source + ": timing speed must be zero or positive");
				segment.speed = entry["speed"].GetFloat();
			}
			if (entry.HasMember("easing")) {
				if (!entry["easing"].IsString())
					throw std::runtime_error(source + ": timing easing must be a string");
				segment.easing = animationEasingFromString(entry["easing"].GetString(), source);
			}
			result.push_back(segment);
		}
		return result;
	}
}
