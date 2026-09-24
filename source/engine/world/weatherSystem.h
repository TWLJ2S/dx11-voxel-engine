#pragma once

#include <world/minecraftTerrainGeneration.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ac {

	enum class weatherKind : uint32_t {
		clear = 0,
		rain,
		storm,
		snow
	};

	struct weatherState {
		weatherKind kind = weatherKind::clear;
		float intensity = 0.0f;
		float overcast = 0.0f;
		float lightning = 0.0f;
	};

	struct cloudWeatherSample {
		float opacity = 0.0f;
		float moisture = 0.0f;
		float precipitation = 0.0f;
	};

	class weatherSystem {
	public:
		void setBiomeConfig(const terrainBiomeConfig& biomes) {
			_biomes = &biomes;
		}

		void reset(uint64_t seed, float clock = 0.0f) {
			_seed = seed;
			_clock = clock;
			_forced = false;
			_lightningTimer = 3.0f + hashUnit(seed + 17u) * 8.0f;
			_frontIntensity = 0.0f;
			_frontOvercast = 0.0f;
			_state = {};
		}

		void setForced(weatherKind kind) {
			_forced = true;
			_forcedKind = kind;
			_state.kind = kind;
			_state.intensity = kind == weatherKind::clear ? 0.0f
				: kind == weatherKind::storm ? 1.0f : 0.82f;
			_state.overcast = kind == weatherKind::clear ? 0.0f
				: kind == weatherKind::storm ? 0.95f
				: kind == weatherKind::snow ? 0.68f
				: 0.64f;
			_state.lightning = 0.0f;
			_frontIntensity = _state.intensity;
			_frontOvercast = _state.overcast;
		}

		void clearForced() {
			_forced = false;
		}

		void update(
			float deltaTime,
			TERRAIN_BIOME biome,
			float temperature,
			float surfaceHeight,
			float worldX,
			float worldZ
		) {
			const float dt = std::clamp(deltaTime, 0.0f, 0.1f);
			_clock += dt;
			_state.lightning = std::max(0.0f, _state.lightning - dt * 3.6f);

			weatherKind wanted = _forced ? _forcedKind : pickGlobal(_clock);
			if (!_forced)
				wanted = filterByBiome(wanted, biome, temperature, surfaceHeight);

			const float targetIntensity = wanted == weatherKind::clear ? 0.0f
				: wanted == weatherKind::storm ? 1.0f
				: 0.82f;
			const float targetOvercast = wanted == weatherKind::clear ? 0.0f
				: wanted == weatherKind::storm ? 0.95f
				: wanted == weatherKind::snow ? 0.68f
				: 0.64f;
			const float blend = 1.0f - std::exp(-dt * 0.78f);
			_frontIntensity += (targetIntensity - _frontIntensity) * blend;
			_frontOvercast += (targetOvercast - _frontOvercast) * blend;
			const cloudWeatherSample localCloud = cloudAt(worldX, worldZ);
			_state.intensity = localCloud.precipitation;
			_state.overcast = std::clamp(
				_frontOvercast * (0.18f + localCloud.opacity * 0.48f + localCloud.moisture * 0.42f),
				0.0f, 1.0f);
			_state.kind = wanted;

			if (_state.kind == weatherKind::storm && _state.intensity > 0.35f) {
				_lightningTimer -= dt;
				if (_lightningTimer <= 0.0f) {
					_state.lightning = 1.0f;
					_lightningTimer = 2.4f + hashUnit(
						_seed ^ static_cast<uint64_t>(_clock * 17.0f)) * 7.5f;
				}
			}
		}

		const weatherState& state() const { return _state; }
		float clock() const { return _clock; }
		float frontOvercast() const { return _frontOvercast; }
		bool forced() const { return _forced; }

		cloudWeatherSample cloudAt(float worldX, float worldZ) const {
			const float coverage = 0.34f + _frontOvercast * 0.52f;
			const float density = 0.72f + _frontOvercast * 0.58f;
			float px = (worldX + _clock * 2.15f) / 420.0f;
			float pz = (worldZ + _clock * 0.72f) / 420.0f;
			const float baseX = px;
			const float baseZ = pz;
			float broad = 0.0f;
			float weight = 0.52f;
			for (uint32_t octave = 0; octave < 5u; ++octave) {
				broad += cloudNoise(px, pz) * weight;
				const float nextX = px * 1.62f - pz * 1.18f + 17.7f;
				const float nextZ = px * 1.18f + pz * 1.62f + 17.7f;
				px = nextX;
				pz = nextZ;
				weight *= 0.49f;
			}
			const float detail = cloudFbm(baseX * 2.75f + 31.4f, baseZ * 2.75f + 31.4f) * 0.22f;
			const float threshold = 0.665f + (0.405f - 0.665f) * std::clamp(coverage, 0.0f, 1.0f);
			const float opacity = std::clamp(
				(broad + detail - threshold) * 5.2f * density, 0.0f, 1.0f);

			// Every advected cloud patch repeatedly charges, rains, then exhausts.
			// Sampling this in world space makes the rain footprint travel with it.
			const float phaseOffset = cloudNoise(baseX * 0.31f + 13.7f, baseZ * 0.31f - 8.4f);
			const float phase = fract(_clock / 110.0f + phaseOffset);
			const float charged = smoothRange(0.12f, 0.34f, phase) *
				(1.0f - smoothRange(0.72f, 0.96f, phase));
			const float raining = smoothRange(0.30f, 0.43f, phase) *
				(1.0f - smoothRange(0.70f, 0.86f, phase));
			return {
				opacity,
				opacity * charged,
				_frontIntensity * std::pow(opacity, 1.35f) * raining
			};
		}

		float precipitationAt(float worldX, float worldZ) const {
			return cloudAt(worldX, worldZ).precipitation;
		}

		static const char* name(weatherKind kind) {
			switch (kind) {
			case weatherKind::rain: return "rain";
			case weatherKind::storm: return "storm";
			case weatherKind::snow: return "snow";
			default: return "clear";
			}
		}

	private:
		uint64_t _seed = 1;
		float _clock = 0.0f;
		bool _forced = false;
		weatherKind _forcedKind = weatherKind::clear;
		float _lightningTimer = 4.0f;
		float _frontIntensity = 0.0f;
		float _frontOvercast = 0.0f;
		weatherState _state{};
		const terrainBiomeConfig* _biomes = nullptr;

		static float fract(float value) {
			return value - std::floor(value);
		}

		static float smoothRange(float low, float high, float value) {
			const float amount = std::clamp((value - low) / (high - low), 0.0f, 1.0f);
			return amount * amount * (3.0f - 2.0f * amount);
		}

		static float cloudHash(float x, float z) {
			x = fract(x * 123.34f);
			z = fract(z * 456.21f);
			const float amount = x * (x + 45.32f) + z * (z + 45.32f);
			x += amount;
			z += amount;
			return fract(x * z);
		}

		static float cloudNoise(float x, float z) {
			const float cellX = std::floor(x);
			const float cellZ = std::floor(z);
			float localX = fract(x);
			float localZ = fract(z);
			localX = localX * localX * (3.0f - 2.0f * localX);
			localZ = localZ * localZ * (3.0f - 2.0f * localZ);
			const float lower = cloudHash(cellX, cellZ) +
				(cloudHash(cellX + 1.0f, cellZ) - cloudHash(cellX, cellZ)) * localX;
			const float upper = cloudHash(cellX, cellZ + 1.0f) +
				(cloudHash(cellX + 1.0f, cellZ + 1.0f) - cloudHash(cellX, cellZ + 1.0f)) * localX;
			return lower + (upper - lower) * localZ;
		}

		static float cloudFbm(float x, float z) {
			float value = 0.0f;
			float weight = 0.52f;
			for (uint32_t octave = 0; octave < 5u; ++octave) {
				value += cloudNoise(x, z) * weight;
				const float nextX = x * 1.62f - z * 1.18f + 17.7f;
				const float nextZ = x * 1.18f + z * 1.62f + 17.7f;
				x = nextX;
				z = nextZ;
				weight *= 0.49f;
			}
			return value;
		}

		static float hashUnit(uint64_t value) {
			value ^= value >> 30;
			value *= 0xBF58476D1CE4E5B9ULL;
			value ^= value >> 27;
			value *= 0x94D049BB133111EBULL;
			value ^= value >> 31;
			return static_cast<float>(value & 0x00ffffffull) / 16777215.0f;
		}

		weatherKind pickGlobal(float clock) const {
			const uint64_t period = static_cast<uint64_t>(std::floor(clock / 78.0f));
			const float pick = hashUnit(_seed + period * 0x9E3779B97F4A7C15ULL);
			if (pick < 0.52f) return weatherKind::clear;
			if (pick < 0.84f) return weatherKind::rain;
			return weatherKind::storm;
		}

		weatherKind filterByBiome(
			weatherKind kind,
			TERRAIN_BIOME biome,
			float temperature,
			float surfaceHeight
		) {
			const terrainBiomeGenerationDefinition* generation =
				_biomes && biome < _biomes->definitions.size()
				? &_biomes->definitions[biome].generation : nullptr;
			if (generation && generation->dryWeather)
				return weatherKind::clear;
			const bool cold = (generation && generation->coldWeather) ||
				temperature < -0.12f || surfaceHeight > 140.0f;
			if (kind == weatherKind::clear)
				return weatherKind::clear;
			if (cold)
				return weatherKind::snow;
			return kind;
		}
	};

}
