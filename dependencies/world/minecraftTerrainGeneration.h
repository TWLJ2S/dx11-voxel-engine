#pragma once

#include "chunk.h"

namespace ac {

    enum TERRAIN_BIOME : uint8_t {
        BIOME_PLAINS = 0,
        BIOME_HIGHLANDS,
        BIOME_ARID,
        BIOME_ALPINE,
        BIOME_OCEAN,
        BIOME_FOREST
    };

    struct biomeSample {
        TERRAIN_BIOME _biome = BIOME_PLAINS;
        float _temperature = 0.0f;
        float _moisture = 0.0f;
        float _continentalness = 0.0f;
        float _erosion = 0.0f;
        float _weirdness = 0.0f;
        float _highlandWeight = 0.0f;
        float _alpineWeight = 0.0f;
        float _aridWeight = 0.0f;
    };

    // A clean-room, Minecraft-style Overworld generator. It follows the same
    // broad pipeline (climate router -> 3D density -> aquifers -> surface rules
    // -> features) without depending on Minecraft source or world data.
    class terrainGenerator {
    private:
        struct columnSample {
            biomeSample biome;
            float surfaceHeight = 64.0f;
            float mountainWeight = 0.0f;
            float oceanWeight = 0.0f;
            float riverWeight = 0.0f;
            float aquiferLevel = 24.0f;
        };

        uint64_t _seed;

        static constexpr blockId AIR_BLOCK = 0;
        static constexpr blockId STONE_BLOCK = 1;
        static constexpr blockId DIRT_BLOCK = 2;
        static constexpr blockId GRASS_BLOCK = 3;
        static constexpr blockId BEDROCK_BLOCK = 4;
        static constexpr blockId WATER_BLOCK = 5;
        static constexpr blockId SAND_BLOCK = 6;
        static constexpr blockId SNOW_BLOCK = 7;
        static constexpr blockId DEEPSLATE_BLOCK = 8;
        static constexpr blockId GRAVEL_BLOCK = 9;
        static constexpr blockId LOG_BLOCK = 10;
        static constexpr blockId LEAVES_BLOCK = 11;

        static constexpr int32_t SEA_LEVEL = 63;
        static constexpr int32_t HORIZONTAL_SAMPLE_RATE = 4;
        static constexpr int32_t VERTICAL_SAMPLE_RATE = 8;
        static constexpr int32_t DENSITY_WIDTH = CHUNK_WIDTH / HORIZONTAL_SAMPLE_RATE + 1;
        static constexpr int32_t DENSITY_LENGTH = CHUNK_LENGTH / HORIZONTAL_SAMPLE_RATE + 1;
        static constexpr int32_t DENSITY_HEIGHT =
            (CHUNK_HEIGHT + VERTICAL_SAMPLE_RATE - 1) / VERTICAL_SAMPLE_RATE + 1;

        using blockVolume = std::array<blockId, CHUNK_VOLUME>;
        using columnGrid = std::array<columnSample, CHUNK_WIDTH * CHUNK_LENGTH>;
        using coarseColumnGrid = std::array<columnSample, DENSITY_WIDTH * DENSITY_LENGTH>;
        using densityGrid = std::array<float,
            DENSITY_WIDTH * DENSITY_LENGTH * DENSITY_HEIGHT>;

        static constexpr size_t blockIndex(int32_t x, int32_t y, int32_t z) {
            return static_cast<size_t>(x) + CHUNK_WIDTH *
                (static_cast<size_t>(z) + CHUNK_LENGTH * static_cast<size_t>(y));
        }

        static constexpr size_t columnIndex(int32_t x, int32_t z) {
            return static_cast<size_t>(x) + CHUNK_WIDTH * static_cast<size_t>(z);
        }

        static constexpr size_t densityIndex(int32_t x, int32_t y, int32_t z) {
            return static_cast<size_t>(x) + DENSITY_WIDTH *
                (static_cast<size_t>(z) + DENSITY_LENGTH * static_cast<size_t>(y));
        }

        static uint64_t mix(uint64_t value) {
            value += 0x9E3779B97F4A7C15ULL;
            value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
            value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
            return value ^ (value >> 31);
        }

        static uint64_t hash(uint64_t seed, int32_t x, int32_t z) {
            uint64_t value = mix(seed);
            value ^= mix(static_cast<uint64_t>(static_cast<int64_t>(x)) + 0x632BE59BD9B4E019ULL);
            value ^= mix(static_cast<uint64_t>(static_cast<int64_t>(z)) + 0x9E3779B97F4A7C15ULL);
            return mix(value);
        }

        static uint64_t hash(uint64_t seed, int32_t x, int32_t y, int32_t z) {
            uint64_t value = hash(seed, x, z);
            value ^= mix(static_cast<uint64_t>(static_cast<int64_t>(y)) + 0xD1B54A32D192ED03ULL);
            return mix(value);
        }

        static double noiseOffset(uint64_t seed) {
            return static_cast<double>(
                static_cast<int64_t>(mix(seed) & 0x1FFFFFULL) - 0x100000LL
            );
        }

        static float fade(float value) {
            return value * value * value * (value * (value * 6.0f - 15.0f) + 10.0f);
        }

        static float lerp(float a, float b, float amount) {
            return a + (b - a) * amount;
        }

        static float smoothStep(float low, float high, float value) {
            if (low == high) return value < low ? 0.0f : 1.0f;
            const float amount = std::clamp((value - low) / (high - low), 0.0f, 1.0f);
            return amount * amount * (3.0f - 2.0f * amount);
        }

        struct tunnelPoint {
            float x;
            float y;
            float z;
        };

        static float hashUnit(uint64_t value) {
            return static_cast<float>((value >> 40) & 0xFFFFFFULL) / 16777215.0f;
        }

        tunnelPoint tunnelAnchor(int32_t gridX, int32_t gridZ) const {
            constexpr float CELL_SIZE = 64.0f;
            const uint64_t anchorHash = hash(_seed + 1600, gridX, gridZ);
            return {
                (static_cast<float>(gridX) + 0.5f) * CELL_SIZE +
                    (hashUnit(mix(anchorHash + 1)) - 0.5f) * 38.0f,
                12.0f + hashUnit(mix(anchorHash + 2)) * 36.0f,
                (static_cast<float>(gridZ) + 0.5f) * CELL_SIZE +
                    (hashUnit(mix(anchorHash + 3)) - 0.5f) * 38.0f
            };
        }

        static float distanceToTunnelSegment(
            float x, float y, float z,
            const tunnelPoint& start,
            const tunnelPoint& end
        ) {
            const float dx = end.x - start.x;
            const float dy = end.y - start.y;
            const float dz = end.z - start.z;
            const float lengthSquared = dx * dx + dy * dy + dz * dz;
            const float projection = lengthSquared > 0.0f
                ? std::clamp(((x - start.x) * dx + (y - start.y) * dy +
                    (z - start.z) * dz) / lengthSquared, 0.0f, 1.0f)
                : 0.0f;
            const float offsetX = x - (start.x + dx * projection);
            const float offsetY = y - (start.y + dy * projection);
            const float offsetZ = z - (start.z + dz * projection);
            return std::sqrt(offsetX * offsetX + offsetY * offsetY + offsetZ * offsetZ);
        }

		struct tunnelCurveControls {
			tunnelPoint first;
			tunnelPoint second;
		};

		tunnelCurveControls tunnelControls(
			const tunnelPoint& start,
			const tunnelPoint& end,
			int32_t originX,
			int32_t originZ,
			uint32_t direction
		) const {
			const uint64_t curveHash = hash(_seed + 1800 + direction * 37u, originX, originZ);
			return {
				{
					lerp(start.x, end.x, 1.0f / 3.0f) + (hashUnit(mix(curveHash + 1u)) - 0.5f) * 48.0f,
					lerp(start.y, end.y, 1.0f / 3.0f) + (hashUnit(mix(curveHash + 2u)) - 0.5f) * 40.0f,
					lerp(start.z, end.z, 1.0f / 3.0f) + (hashUnit(mix(curveHash + 3u)) - 0.5f) * 48.0f
				},
				{
					lerp(start.x, end.x, 2.0f / 3.0f) + (hashUnit(mix(curveHash + 4u)) - 0.5f) * 48.0f,
					lerp(start.y, end.y, 2.0f / 3.0f) + (hashUnit(mix(curveHash + 5u)) - 0.5f) * 40.0f,
					lerp(start.z, end.z, 2.0f / 3.0f) + (hashUnit(mix(curveHash + 6u)) - 0.5f) * 48.0f
				}
			};
		}

		static tunnelPoint cubicTunnelPoint(
			const tunnelPoint& start,
			const tunnelPoint& firstControl,
			const tunnelPoint& secondControl,
			const tunnelPoint& end,
			float amount
		) {
			const float inverse = 1.0f - amount;
			return {
				inverse * inverse * inverse * start.x + 3.0f * inverse * inverse * amount * firstControl.x +
					3.0f * inverse * amount * amount * secondControl.x + amount * amount * amount * end.x,
				inverse * inverse * inverse * start.y + 3.0f * inverse * inverse * amount * firstControl.y +
					3.0f * inverse * amount * amount * secondControl.y + amount * amount * amount * end.y,
				inverse * inverse * inverse * start.z + 3.0f * inverse * inverse * amount * firstControl.z +
					3.0f * inverse * amount * amount * secondControl.z + amount * amount * amount * end.z
			};
		}

		float curvedTunnelField(
			float x, float y, float z,
			const tunnelPoint& start,
			const tunnelPoint& end,
			int32_t originX,
			int32_t originZ,
			uint32_t direction
		) const {
			constexpr int32_t CURVE_STEPS = 8;
			constexpr float PI = 3.14159265358979323846f;
			const tunnelCurveControls controls = tunnelControls(start, end, originX, originZ, direction);
			const uint64_t shapeHash = hash(_seed + 1700 + direction * 53u, originX, originZ);
			const float startWidth = 3.0f + hashUnit(mix(shapeHash + 1u)) * 4.5f;
			const float endWidth = 3.0f + hashUnit(mix(shapeHash + 2u)) * 4.5f;
			const float startHeight = 1.8f + hashUnit(mix(shapeHash + 3u)) * 4.4f;
			const float endHeight = 1.8f + hashUnit(mix(shapeHash + 4u)) * 4.4f;
			const float widthWave = (hashUnit(mix(shapeHash + 5u)) - 0.5f) * 3.5f;
			const float heightWave = (hashUnit(mix(shapeHash + 6u)) - 0.5f) * 3.0f;
			float field = 0.0f;
			tunnelPoint previous = start;
			for (int32_t step = 1; step <= CURVE_STEPS; ++step) {
				const float segmentEnd = static_cast<float>(step) / CURVE_STEPS;
				const tunnelPoint current = cubicTunnelPoint(
					start, controls.first, controls.second, end, segmentEnd);
				const float dx = current.x - previous.x;
				const float dy = current.y - previous.y;
				const float dz = current.z - previous.z;
				const float lengthSquared = dx * dx + dy * dy + dz * dz;
				const float projection = lengthSquared > 0.0f
					? std::clamp(((x - previous.x) * dx + (y - previous.y) * dy +
						(z - previous.z) * dz) / lengthSquared, 0.0f, 1.0f)
					: 0.0f;
				const float amount = (static_cast<float>(step - 1) + projection) / CURVE_STEPS;
				const float wave = std::sin(amount * PI);
				const float horizontalRadius = std::max(2.5f, lerp(startWidth, endWidth, amount) + wave * widthWave);
				const float verticalRadius = std::max(1.6f, lerp(startHeight, endHeight, amount) + wave * heightWave);
				const float offsetX = x - (previous.x + dx * projection);
				const float offsetY = y - (previous.y + dy * projection);
				const float offsetZ = z - (previous.z + dz * projection);
				const float normalizedDistance = std::sqrt(
					(offsetX * offsetX + offsetZ * offsetZ) / (horizontalRadius * horizontalRadius) +
					(offsetY * offsetY) / (verticalRadius * verticalRadius));
				field = std::max(field, 1.0f - smoothStep(0.72f, 1.08f, normalizedDistance));
				previous = current;
			}
			return field;
		}

        float connectedTunnelField(int32_t worldX, int32_t y, int32_t worldZ) const {
            constexpr int32_t CELL_SIZE = 64;
            const int32_t cellX = static_cast<int32_t>(std::floor(
                static_cast<double>(worldX) / static_cast<double>(CELL_SIZE)));
            const int32_t cellZ = static_cast<int32_t>(std::floor(
                static_cast<double>(worldZ) / static_cast<double>(CELL_SIZE)));
            const tunnelPoint center = tunnelAnchor(cellX, cellZ);
            const tunnelPoint east = tunnelAnchor(cellX + 1, cellZ);
            const tunnelPoint west = tunnelAnchor(cellX - 1, cellZ);
            const tunnelPoint north = tunnelAnchor(cellX, cellZ + 1);
            const tunnelPoint south = tunnelAnchor(cellX, cellZ - 1);
            const float pointX = static_cast<float>(worldX);
            const float pointY = static_cast<float>(y);
            const float pointZ = static_cast<float>(worldZ);
            float field = 0.0f;

			// Two independently displaced controls allow S-shaped bends on every
			// axis. Width and height vary separately along each tunnel.
			auto carveSegment = [&](const tunnelPoint& start, const tunnelPoint& end,
				int32_t originX, int32_t originZ, uint32_t direction) {
				field = std::max(field, curvedTunnelField(
					pointX, pointY, pointZ, start, end, originX, originZ, direction));
			};
			carveSegment(center, east, cellX, cellZ, 0u);
			carveSegment(center, north, cellX, cellZ, 1u);
			carveSegment(west, center, cellX - 1, cellZ, 0u);
			carveSegment(south, center, cellX, cellZ - 1, 1u);
            return field;
        }

        static float gradient2(uint64_t value, float x, float z) {
            switch (value & 7ULL) {
            case 0: return x;
            case 1: return -x;
            case 2: return z;
            case 3: return -z;
            case 4: return (x + z) * 0.70710678f;
            case 5: return (x - z) * 0.70710678f;
            case 6: return (-x + z) * 0.70710678f;
            default: return (-x - z) * 0.70710678f;
            }
        }

        static float gradient3(uint64_t value, float x, float y, float z) {
            switch (value % 12ULL) {
            case 0: return x + y;
            case 1: return -x + y;
            case 2: return x - y;
            case 3: return -x - y;
            case 4: return x + z;
            case 5: return -x + z;
            case 6: return x - z;
            case 7: return -x - z;
            case 8: return y + z;
            case 9: return -y + z;
            case 10: return y - z;
            default: return -y - z;
            }
        }

        static float noise2D(double x, double z, uint64_t seed) {
            const int32_t x0 = static_cast<int32_t>(std::floor(x));
            const int32_t z0 = static_cast<int32_t>(std::floor(z));
            const float localX = static_cast<float>(x - x0);
            const float localZ = static_cast<float>(z - z0);
            const float u = fade(localX);
            const float v = fade(localZ);

            const float n00 = gradient2(hash(seed, x0, z0), localX, localZ);
            const float n10 = gradient2(hash(seed, x0 + 1, z0), localX - 1.0f, localZ);
            const float n01 = gradient2(hash(seed, x0, z0 + 1), localX, localZ - 1.0f);
            const float n11 = gradient2(hash(seed, x0 + 1, z0 + 1), localX - 1.0f, localZ - 1.0f);
            return std::clamp(lerp(lerp(n00, n10, u), lerp(n01, n11, u), v) * 1.7f, -1.0f, 1.0f);
        }

        static float noise3D(double x, double y, double z, uint64_t seed) {
            const int32_t x0 = static_cast<int32_t>(std::floor(x));
            const int32_t y0 = static_cast<int32_t>(std::floor(y));
            const int32_t z0 = static_cast<int32_t>(std::floor(z));
            const float localX = static_cast<float>(x - x0);
            const float localY = static_cast<float>(y - y0);
            const float localZ = static_cast<float>(z - z0);
            const float u = fade(localX);
            const float v = fade(localY);
            const float w = fade(localZ);

            auto corner = [&](int32_t dx, int32_t dy, int32_t dz) {
                return gradient3(
                    hash(seed, x0 + dx, y0 + dy, z0 + dz),
                    localX - static_cast<float>(dx),
                    localY - static_cast<float>(dy),
                    localZ - static_cast<float>(dz)
                );
            };

            const float lower = lerp(
                lerp(corner(0, 0, 0), corner(1, 0, 0), u),
                lerp(corner(0, 0, 1), corner(1, 0, 1), u),
                w
            );
            const float upper = lerp(
                lerp(corner(0, 1, 0), corner(1, 1, 0), u),
                lerp(corner(0, 1, 1), corner(1, 1, 1), u),
                w
            );
            return std::clamp(lerp(lower, upper, v), -1.0f, 1.0f);
        }

        static float fractal2D(
            int32_t worldX,
            int32_t worldZ,
            double frequency,
            int octaves,
            uint64_t seed
        ) {
            double x = (static_cast<double>(worldX) + noiseOffset(seed)) * frequency;
            double z = (static_cast<double>(worldZ) + noiseOffset(seed + 1)) * frequency;
            float amplitude = 1.0f;
            float value = 0.0f;
            float total = 0.0f;

            for (int octave = 0; octave < octaves; ++octave) {
                value += noise2D(x, z, seed + static_cast<uint64_t>(octave) * 0x9E3779B97F4A7C15ULL) * amplitude;
                total += amplitude;
                x *= 2.0;
                z *= 2.0;
                amplitude *= 0.5f;
            }
            return value / total;
        }

        static float fractal3D(
            int32_t worldX,
            int32_t y,
            int32_t worldZ,
            double horizontalFrequency,
            double verticalFrequency,
            int octaves,
            uint64_t seed
        ) {
            double x = (static_cast<double>(worldX) + noiseOffset(seed)) * horizontalFrequency;
            double fy = (static_cast<double>(y) + noiseOffset(seed + 1)) * verticalFrequency;
            double z = (static_cast<double>(worldZ) + noiseOffset(seed + 2)) * horizontalFrequency;
            float amplitude = 1.0f;
            float value = 0.0f;
            float total = 0.0f;

            for (int octave = 0; octave < octaves; ++octave) {
                value += noise3D(x, fy, z, seed + static_cast<uint64_t>(octave) * 0xD1B54A32D192ED03ULL) * amplitude;
                total += amplitude;
                x *= 2.0;
                fy *= 2.0;
                z *= 2.0;
                amplitude *= 0.5f;
            }
            return value / total;
        }

        static float peaksAndValleys(float weirdness) {
            return 1.0f - std::abs(std::abs(weirdness) * 3.0f - 2.0f);
        }

        static float continentalHeight(float continentalness) {
            if (continentalness < -0.62f)
                return lerp(28.0f, 43.0f, smoothStep(-1.0f, -0.62f, continentalness));
            if (continentalness < -0.30f)
                return lerp(43.0f, 57.0f, smoothStep(-0.62f, -0.30f, continentalness));
            if (continentalness < -0.12f)
                return lerp(57.0f, 63.0f, smoothStep(-0.30f, -0.12f, continentalness));
            if (continentalness < 0.18f)
                return lerp(64.0f, 72.0f, smoothStep(-0.12f, 0.18f, continentalness));
            if (continentalness < 0.55f)
                return lerp(72.0f, 86.0f, smoothStep(0.18f, 0.55f, continentalness));
            return lerp(86.0f, 98.0f, smoothStep(0.55f, 1.0f, continentalness));
        }

        columnSample sampleColumn(int32_t worldX, int32_t worldZ) const {
            columnSample result;
            biomeSample& biome = result.biome;

            biome._continentalness = fractal2D(worldX, worldZ, 0.00072, 4, _seed + 100);
            biome._erosion = fractal2D(worldX, worldZ, 0.00165, 4, _seed + 200);
            biome._weirdness = fractal2D(worldX, worldZ, 0.00320, 3, _seed + 300);
            biome._temperature = fractal2D(worldX, worldZ, 0.00058, 3, _seed + 400);
            biome._moisture = fractal2D(worldX, worldZ, 0.00066, 3, _seed + 500);

            const float ridge = std::clamp(peaksAndValleys(biome._weirdness), 0.0f, 1.0f);
            const float inland = smoothStep(-0.18f, 0.32f, biome._continentalness);
            const float lowErosion = 1.0f - smoothStep(-0.45f, 0.42f, biome._erosion);
            result.mountainWeight = std::clamp(inland * lowErosion * smoothStep(0.10f, 0.82f, ridge), 0.0f, 1.0f);
            result.oceanWeight = 1.0f - smoothStep(-0.46f, -0.12f, biome._continentalness);

            const float riverNoise = std::abs(fractal2D(worldX, worldZ, 0.00145, 3, _seed + 600));
            result.riverWeight =
                (1.0f - smoothStep(0.018f, 0.070f, riverNoise)) *
                smoothStep(-0.20f, 0.03f, biome._continentalness) *
                (1.0f - result.mountainWeight * 0.75f);

            const float rollingHills = fractal2D(worldX, worldZ, 0.0075, 4, _seed + 700);
            const float detail = fractal2D(worldX, worldZ, 0.025, 2, _seed + 800);
            float height = continentalHeight(biome._continentalness);
            height += rollingHills * lerp(3.0f, 13.0f, 1.0f - smoothStep(-0.25f, 0.55f, biome._erosion));
            height += detail * lerp(0.8f, 3.5f, result.mountainWeight);
            height += result.mountainWeight * result.mountainWeight * 82.0f;
            height = lerp(height, static_cast<float>(SEA_LEVEL - 3), result.riverWeight * 0.88f);
            result.surfaceHeight = std::clamp(height, 8.0f, static_cast<float>(CHUNK_HEIGHT - 10));

            const float aquiferRegion = fractal2D(worldX, worldZ, 0.0011, 2, _seed + 925);
            result.aquiferLevel = aquiferRegion > 0.28f
                ? 14.0f + fractal2D(worldX, worldZ, 0.0028, 2, _seed + 900) * 7.0f
                : -1.0f;

            biome._highlandWeight = smoothStep(0.18f, 0.62f, result.mountainWeight);
            biome._alpineWeight = std::max(
                smoothStep(0.55f, 0.88f, result.mountainWeight),
                smoothStep(120.0f, 165.0f, result.surfaceHeight)
            );
            biome._aridWeight = smoothStep(0.18f, 0.62f,
                biome._temperature - biome._moisture * 0.72f);

            if (result.surfaceHeight < SEA_LEVEL - 1 || result.oceanWeight > 0.58f)
                biome._biome = BIOME_OCEAN;
            else if (biome._alpineWeight > 0.52f)
                biome._biome = BIOME_ALPINE;
            else if (biome._aridWeight > 0.54f)
                biome._biome = BIOME_ARID;
            else if (biome._highlandWeight > 0.40f)
                biome._biome = BIOME_HIGHLANDS;
            else if (biome._moisture > 0.08f)
                biome._biome = BIOME_FOREST;
            else
                biome._biome = BIOME_PLAINS;

            return result;
        }

		static columnSample interpolateColumn(
			const columnSample& a,
			const columnSample& b,
			const columnSample& c,
			const columnSample& d,
			float tx,
			float tz
		) {
			auto blend = [&](float columnSample::* member) {
				return lerp(lerp(a.*member, b.*member, tx), lerp(c.*member, d.*member, tx), tz);
			};
			auto blendBiome = [&](float biomeSample::* member) {
				return lerp(
					lerp(a.biome.*member, b.biome.*member, tx),
					lerp(c.biome.*member, d.biome.*member, tx),
					tz
				);
			};

			columnSample result;
			result.surfaceHeight = blend(&columnSample::surfaceHeight);
			result.mountainWeight = blend(&columnSample::mountainWeight);
			result.oceanWeight = blend(&columnSample::oceanWeight);
			result.riverWeight = blend(&columnSample::riverWeight);
			result.aquiferLevel = blend(&columnSample::aquiferLevel);
			result.biome._temperature = blendBiome(&biomeSample::_temperature);
			result.biome._moisture = blendBiome(&biomeSample::_moisture);
			result.biome._continentalness = blendBiome(&biomeSample::_continentalness);
			result.biome._erosion = blendBiome(&biomeSample::_erosion);
			result.biome._weirdness = blendBiome(&biomeSample::_weirdness);
			result.biome._highlandWeight = blendBiome(&biomeSample::_highlandWeight);
			result.biome._alpineWeight = blendBiome(&biomeSample::_alpineWeight);
			result.biome._aridWeight = blendBiome(&biomeSample::_aridWeight);

			if (result.surfaceHeight < SEA_LEVEL - 1 || result.oceanWeight > 0.58f)
				result.biome._biome = BIOME_OCEAN;
			else if (result.biome._alpineWeight > 0.52f)
				result.biome._biome = BIOME_ALPINE;
			else if (result.biome._aridWeight > 0.54f)
				result.biome._biome = BIOME_ARID;
			else if (result.biome._highlandWeight > 0.40f)
				result.biome._biome = BIOME_HIGHLANDS;
			else if (result.biome._moisture > 0.08f)
				result.biome._biome = BIOME_FOREST;
			else
				result.biome._biome = BIOME_PLAINS;
			return result;
		}

		void buildColumnGrids(
			int32_t worldX,
			int32_t worldZ,
			coarseColumnGrid& coarse,
			columnGrid& full
		) const {
			for (int32_t z = 0; z < DENSITY_LENGTH; ++z)
				for (int32_t x = 0; x < DENSITY_WIDTH; ++x)
					coarse[static_cast<size_t>(x) + DENSITY_WIDTH * static_cast<size_t>(z)] =
						sampleColumn(worldX + x * HORIZONTAL_SAMPLE_RATE, worldZ + z * HORIZONTAL_SAMPLE_RATE);

			for (int32_t z = 0; z < CHUNK_LENGTH; ++z) {
				const int32_t cellZ = z / HORIZONTAL_SAMPLE_RATE;
				const float tz = static_cast<float>(z % HORIZONTAL_SAMPLE_RATE) / HORIZONTAL_SAMPLE_RATE;
				for (int32_t x = 0; x < CHUNK_WIDTH; ++x) {
					const int32_t cellX = x / HORIZONTAL_SAMPLE_RATE;
					const float tx = static_cast<float>(x % HORIZONTAL_SAMPLE_RATE) / HORIZONTAL_SAMPLE_RATE;
					auto at = [&](int32_t dx, int32_t dz) -> const columnSample& {
						return coarse[static_cast<size_t>(cellX + dx) +
							DENSITY_WIDTH * static_cast<size_t>(cellZ + dz)];
					};
					full[columnIndex(x, z)] = interpolateColumn(
						at(0, 0), at(1, 0), at(0, 1), at(1, 1), tx, tz
					);
				}
			}
		}

        float sampleDensity(
            const columnSample& column,
            int32_t worldX,
            int32_t y,
            int32_t worldZ
        ) const {
            const float terrainScale = lerp(14.0f, 27.0f, column.mountainWeight);
            const float terrainShape = fractal3D(worldX, y, worldZ, 0.0062, 0.0085, 4, _seed + 1100);
            const float jaggedShape = fractal3D(worldX, y, worldZ, 0.0180, 0.0150, 2, _seed + 1200);
            float density = (column.surfaceHeight - static_cast<float>(y)) / terrainScale;
            density += terrainShape * lerp(0.32f, 0.88f, column.mountainWeight);
            density += jaggedShape * column.mountainWeight * 0.26f;

            const float depth = column.surfaceHeight - static_cast<float>(y);
            const float caveWindow =
                smoothStep(5.0f, 18.0f, depth) *
                smoothStep(4.0f, 16.0f, static_cast<float>(y)) *
                (1.0f - smoothStep(150.0f, 220.0f, static_cast<float>(y)));

            if (caveWindow > 0.0f) {
                const float cheese = fractal3D(worldX, y, worldZ, 0.0140, 0.0190, 3, _seed + 1300);
                const float spaghettiA = std::abs(noise3D(
                    static_cast<double>(worldX) * 0.026,
                    static_cast<double>(y) * 0.021,
                    static_cast<double>(worldZ) * 0.026,
                    _seed + 1400
                ));
                const float spaghettiB = std::abs(noise3D(
                    static_cast<double>(worldX) * 0.026,
                    static_cast<double>(y) * 0.021,
                    static_cast<double>(worldZ) * 0.026,
                    _seed + 1500
                ));

                const float cheeseCave = smoothStep(0.34f, 0.56f, cheese) *
                    (1.0f - smoothStep(105.0f, 155.0f, static_cast<float>(y)));
                const float spaghettiDistance = std::max(spaghettiA, spaghettiB);
                const float spaghettiCave = 1.0f - smoothStep(0.055f, 0.145f, spaghettiDistance);
                const float cavernBoost = (1.0f - smoothStep(45.0f, 92.0f, static_cast<float>(y))) *
                    smoothStep(0.25f, 0.48f, cheese);
                const float connectedTunnel = connectedTunnelField(worldX, y, worldZ);
                const float cave = std::max({ cheeseCave, spaghettiCave, cavernBoost, connectedTunnel }) * caveWindow;

                if (cave > 0.0f)
                    density = lerp(density, std::min(density, -0.8f), cave);
            }

            density = lerp(1.35f, density, smoothStep(0.0f, 7.0f, static_cast<float>(y)));
            density = lerp(density, -1.0f, smoothStep(
                static_cast<float>(CHUNK_HEIGHT - 10),
                static_cast<float>(CHUNK_HEIGHT - 1),
                static_cast<float>(y)
            ));
            return density;
        }

        void buildDensityGrid(int32_t worldX, int32_t worldZ, const coarseColumnGrid& columns, densityGrid& output) const {
            for (int32_t y = 0; y < DENSITY_HEIGHT; ++y) {
                const int32_t sampleY = y * VERTICAL_SAMPLE_RATE;
                for (int32_t z = 0; z < DENSITY_LENGTH; ++z) {
                    for (int32_t x = 0; x < DENSITY_WIDTH; ++x) {
                        const int32_t sampleX = worldX + x * HORIZONTAL_SAMPLE_RATE;
                        const int32_t sampleZ = worldZ + z * HORIZONTAL_SAMPLE_RATE;
                        output[densityIndex(x, y, z)] = sampleDensity(
                            columns[static_cast<size_t>(x) + DENSITY_WIDTH * static_cast<size_t>(z)],
                            sampleX,
                            sampleY,
                            sampleZ
                        );
                    }
                }
            }
        }

        static float interpolatedDensity(
            const densityGrid& density,
            int32_t x,
            int32_t y,
            int32_t z
        ) {
            const int32_t cellX = x / HORIZONTAL_SAMPLE_RATE;
            const int32_t cellY = y / VERTICAL_SAMPLE_RATE;
            const int32_t cellZ = z / HORIZONTAL_SAMPLE_RATE;
            const float tx = static_cast<float>(x % HORIZONTAL_SAMPLE_RATE) / HORIZONTAL_SAMPLE_RATE;
            const float ty = static_cast<float>(y % VERTICAL_SAMPLE_RATE) / VERTICAL_SAMPLE_RATE;
            const float tz = static_cast<float>(z % HORIZONTAL_SAMPLE_RATE) / HORIZONTAL_SAMPLE_RATE;

            auto at = [&](int32_t dx, int32_t dy, int32_t dz) {
                return density[densityIndex(cellX + dx, cellY + dy, cellZ + dz)];
            };

            const float lower = lerp(
                lerp(at(0, 0, 0), at(1, 0, 0), tx),
                lerp(at(0, 0, 1), at(1, 0, 1), tx),
                tz
            );
            const float upper = lerp(
                lerp(at(0, 1, 0), at(1, 1, 0), tx),
                lerp(at(0, 1, 1), at(1, 1, 1), tx),
                tz
            );
            return lerp(lower, upper, ty);
        }

        void applySurfaceRules(
            int32_t worldX,
            int32_t worldZ,
            const columnGrid& columns,
            blockVolume& blocks
        ) const {
            for (int32_t z = 0; z < CHUNK_LENGTH; ++z) {
                for (int32_t x = 0; x < CHUNK_WIDTH; ++x) {
                    const columnSample& column = columns[columnIndex(x, z)];
                    int32_t soilRemaining = 0;
                    blockId underBlock = DIRT_BLOCK;

                    for (int32_t y = CHUNK_HEIGHT - 2; y > 0; --y) {
                        blockId& block = blocks[blockIndex(x, y, z)];
                        const blockId above = blocks[blockIndex(x, y + 1, z)];

                        if (block == AIR_BLOCK || block == WATER_BLOCK) {
                            soilRemaining = 0;
                            continue;
                        }
                        if (block != STONE_BLOCK) continue;

                        const bool exposed = above == AIR_BLOCK || above == WATER_BLOCK;
                        const bool nearSurface =
                            static_cast<float>(y) >= column.surfaceHeight - 12.0f;

                        if (exposed && nearSurface) {
                            const uint64_t surfaceRandom = hash(_seed + 1600, worldX + x, y, worldZ + z);
                            soilRemaining = 3 + static_cast<int32_t>(surfaceRandom % 3ULL);
                            underBlock = DIRT_BLOCK;

                            if (y <= SEA_LEVEL) {
                                block = (surfaceRandom % 7ULL == 0ULL) ? GRAVEL_BLOCK : SAND_BLOCK;
                                underBlock = block;
                            }
                            else {
                                switch (column.biome._biome) {
                                case BIOME_ARID:
                                    block = SAND_BLOCK;
                                    underBlock = SAND_BLOCK;
                                    soilRemaining += 2;
                                    break;
                                case BIOME_ALPINE:
                                    if (y >= 116 && column.biome._temperature < 0.30f)
                                        block = SNOW_BLOCK;
                                    else
                                        block = STONE_BLOCK;
                                    underBlock = STONE_BLOCK;
                                    soilRemaining = 1;
                                    break;
                                case BIOME_HIGHLANDS:
                                    block = column.mountainWeight > 0.68f && (surfaceRandom & 3ULL) != 0ULL
                                        ? STONE_BLOCK
                                        : GRASS_BLOCK;
                                    break;
                                default:
                                    block = GRASS_BLOCK;
                                    break;
                                }
                            }
                        }
                        else if (soilRemaining > 0) {
                            block = underBlock;
                            --soilRemaining;
                        }
                    }
                }
            }
        }

        void applyUndergroundRules(
            int32_t worldX,
            int32_t worldZ,
            blockVolume& blocks
        ) const {
            for (int32_t z = 0; z < CHUNK_LENGTH; ++z) {
                for (int32_t x = 0; x < CHUNK_WIDTH; ++x) {
                    for (int32_t y = 1; y < 32; ++y) {
                        blockId& block = blocks[blockIndex(x, y, z)];
                        if (block != STONE_BLOCK) continue;

                        const uint64_t transition = hash(_seed + 1700, worldX + x, y, worldZ + z);
                        if (y < 8 || static_cast<int32_t>(transition % 24ULL) < 32 - y)
                            block = DEEPSLATE_BLOCK;
                    }

                    for (int32_t y = 0; y < 5; ++y) {
                        const uint64_t foundation = hash(_seed + 1800, worldX + x, y, worldZ + z);
                        if (y == 0 || static_cast<int32_t>(foundation % 5ULL) >= y)
                            blocks[blockIndex(x, y, z)] = BEDROCK_BLOCK;
                    }
                }
            }
        }

        static int32_t floorDiv(int32_t value, int32_t divisor) {
            int32_t quotient = value / divisor;
            if (value % divisor < 0) --quotient;
            return quotient;
        }

        int32_t featureSurface(int32_t worldX, int32_t worldZ, const columnSample& column) const {
            const int32_t center = std::clamp(
                static_cast<int32_t>(std::round(column.surfaceHeight)),
                2,
                CHUNK_HEIGHT - 3
            );
            const int32_t top = std::min(center + 18, CHUNK_HEIGHT - 2);
            const int32_t bottom = std::max(center - 18, 1);

            for (int32_t y = top; y >= bottom; --y) {
                if (sampleDensity(column, worldX, y, worldZ) > 0.0f &&
                    sampleDensity(column, worldX, y + 1, worldZ) <= 0.0f)
                    return y;
            }
            return center;
        }

        void placeTrees(int32_t worldX, int32_t worldZ, blockVolume& blocks) const {
            constexpr int32_t featureSpacing = 8;
            constexpr int32_t featureMargin = 3;
            const int32_t minCellX = floorDiv(worldX - featureMargin, featureSpacing);
            const int32_t maxCellX = floorDiv(worldX + CHUNK_WIDTH - 1 + featureMargin, featureSpacing);
            const int32_t minCellZ = floorDiv(worldZ - featureMargin, featureSpacing);
            const int32_t maxCellZ = floorDiv(worldZ + CHUNK_LENGTH - 1 + featureMargin, featureSpacing);

            auto setIfLocal = [&](int32_t x, int32_t y, int32_t z, blockId id) {
                const int32_t localX = x - worldX;
                const int32_t localZ = z - worldZ;
                if (localX < 0 || localX >= CHUNK_WIDTH ||
                    localZ < 0 || localZ >= CHUNK_LENGTH ||
                    y < 1 || y >= CHUNK_HEIGHT)
                    return;

                blockId& current = blocks[blockIndex(localX, y, localZ)];
                if (current == AIR_BLOCK || current == LEAVES_BLOCK)
                    current = id;
            };

            for (int32_t cellZ = minCellZ; cellZ <= maxCellZ; ++cellZ) {
                for (int32_t cellX = minCellX; cellX <= maxCellX; ++cellX) {
                    const uint64_t featureRandom = hash(_seed + 1900, cellX, cellZ);
                    const int32_t treeX = cellX * featureSpacing + 1 +
                        static_cast<int32_t>(featureRandom % 6ULL);
                    const int32_t treeZ = cellZ * featureSpacing + 1 +
                        static_cast<int32_t>((featureRandom >> 8) % 6ULL);
                    const columnSample column = sampleColumn(treeX, treeZ);

                    const bool forest = column.biome._biome == BIOME_FOREST;
                    const bool sparsePlainsTree = column.biome._biome == BIOME_PLAINS &&
                        ((featureRandom >> 16) & 3ULL) == 0ULL;
                    if (!forest && !sparsePlainsTree) continue;
                    if (column.riverWeight > 0.65f || column.surfaceHeight <= SEA_LEVEL + 1) continue;

                    const int32_t groundY = featureSurface(treeX, treeZ, column);
                    const int32_t treeHeight = 4 + static_cast<int32_t>((featureRandom >> 20) % 3ULL);
                    const int32_t crownY = groundY + treeHeight;
                    if (groundY <= SEA_LEVEL || crownY + 2 >= CHUNK_HEIGHT) continue;

                    for (int32_t y = groundY + 1; y <= crownY; ++y)
                        setIfLocal(treeX, y, treeZ, LOG_BLOCK);

                    for (int32_t y = crownY - 2; y <= crownY + 1; ++y) {
                        const int32_t radius = y == crownY + 1 ? 1 : 2;
                        for (int32_t dz = -radius; dz <= radius; ++dz) {
                            for (int32_t dx = -radius; dx <= radius; ++dx) {
                                if (radius == 2 && std::abs(dx) == 2 && std::abs(dz) == 2)
                                    continue;
                                setIfLocal(treeX + dx, y, treeZ + dz, LEAVES_BLOCK);
                            }
                        }
                    }
                    setIfLocal(treeX, crownY + 2, treeZ, LEAVES_BLOCK);
                }
            }
        }

    public:
        explicit terrainGenerator(uint64_t seed) : _seed(seed) {}

        uint64_t seed() const { return _seed; }

        biomeSample sampleBiome(int32_t worldX, int32_t worldZ) const {
            return sampleColumn(worldX, worldZ).biome;
        }

        void generate(chunk& c) const {
            const int32_t worldX = c._position.x * CHUNK_WIDTH;
            const int32_t worldZ = c._position.z * CHUNK_LENGTH;
			// Every entry in these scratch grids is written below; default-zeroing
			// roughly 270 KiB per generated chunk only adds memory bandwidth.
            columnGrid columns;
			coarseColumnGrid coarseColumns;
            densityGrid density;
            blockVolume blocks;

			buildColumnGrids(worldX, worldZ, coarseColumns, columns);
            buildDensityGrid(worldX, worldZ, coarseColumns, density);

            for (int32_t y = 0; y < CHUNK_HEIGHT; ++y) {
                for (int32_t z = 0; z < CHUNK_LENGTH; ++z) {
                    for (int32_t x = 0; x < CHUNK_WIDTH; ++x) {
                        const columnSample& column = columns[columnIndex(x, z)];
                        blockId block = AIR_BLOCK;
                        if (interpolatedDensity(density, x, y, z) > 0.0f) {
                            block = STONE_BLOCK;
                        }
                        else if (y <= SEA_LEVEL &&
                            ((column.surfaceHeight < SEA_LEVEL &&
                                static_cast<float>(y) >= column.surfaceHeight - 1.0f) ||
                                static_cast<float>(y) <= column.aquiferLevel)) {
                            block = WATER_BLOCK;
                        }
                        blocks[blockIndex(x, y, z)] = block;
                    }
                }
            }

            applySurfaceRules(worldX, worldZ, columns, blocks);
            applyUndergroundRules(worldX, worldZ, blocks);
            placeTrees(worldX, worldZ, blocks);
            c.buildFromDense(blocks);
            c._dirty = true;
        }
    };

}
