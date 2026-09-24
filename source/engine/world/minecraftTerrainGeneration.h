#pragma once

#include "chunk.h"
#include "waterPhysics.h"

#include <cstring>
#include <filesystem>

namespace ac {

    struct terrainLithologyRule {
        blockId block = 0u;
        uint64_t salt = 0u;
        int32_t minimumY = 0;
        int32_t peakY = 0;
        int32_t maximumY = CHUNK_HEIGHT;
        bool peaked = false;
        float chance = 0.0f;
        int32_t cellXZ = 1;
        int32_t cellY = 1;
        float radiusMinimum = 1.0f;
        float radiusMaximum = 1.0f;
    };

    struct terrainBlockPalette {
        blockId AIR_BLOCK = 0u;
        std::unordered_map<std::string, blockId> blocks;
        std::unordered_map<std::string, float> settings;
        std::unordered_map<std::string, std::unordered_set<blockId>> blockGroups;
        std::vector<terrainLithologyRule> lithology;

        blockId require(std::string_view role) const {
            const auto it = blocks.find(std::string(role));
            if (it == blocks.end())
                throw std::runtime_error("terrain block role is not configured: " + std::string(role));
            return it->second;
        }

        bool contains(std::string_view role) const {
            return blocks.contains(std::string(role));
        }

        void set(std::string_view role, blockId id) {
            blocks[std::string(role)] = id;
        }

        size_t size() const { return blocks.size(); }

        float requireSetting(std::string_view name) const {
            const auto it = settings.find(std::string(name));
            if (it == settings.end())
                throw std::runtime_error("terrain setting is not configured: " + std::string(name));
            return it->second;
        }

        bool inGroup(std::string_view group, blockId state) const {
            const auto it = blockGroups.find(std::string(group));
            return it != blockGroups.end() && it->second.contains(blockType(state));
        }

        static terrainBlockPalette load(
            const std::filesystem::path& path,
            const staticAssetManager& registry
        ) {
            const rapidjson::Document document = jsonLoader::load(path.string());
            if (!document.IsObject() || !document.HasMember("blocks") || !document["blocks"].IsObject())
                throw std::runtime_error(path.string() + ": terrain config requires a 'blocks' object");
            const rapidjson::Value& blocks = document["blocks"];
            terrainBlockPalette result;
            for (auto member = blocks.MemberBegin(); member != blocks.MemberEnd(); ++member) {
                const std::string name(member->name.GetString(), member->name.GetStringLength());
                if (!member->value.IsString())
                    throw std::runtime_error(path.string() + ": terrain block role '" + name + "' must be a block name");
                const blockId id = static_cast<blockId>(registry.getId(member->value.GetString()));
                if (id == 0u)
                    throw std::runtime_error(path.string() + ": terrain block role '" + name + "' resolves to air");
                result.blocks.emplace(name, id);
            }
            if (result.blocks.empty())
                throw std::runtime_error(path.string() + ": terrain config requires at least one block role");
            if (!document.HasMember("settings") || !document["settings"].IsObject())
                throw std::runtime_error(path.string() + ": terrain config requires a 'settings' object");
            for (auto member = document["settings"].MemberBegin();
                member != document["settings"].MemberEnd(); ++member) {
                const std::string name(member->name.GetString(), member->name.GetStringLength());
                if (!member->value.IsNumber())
                    throw std::runtime_error(path.string() + ": terrain setting '" + name + "' must be numeric");
                result.settings.emplace(name, member->value.GetFloat());
            }
            if (!document.HasMember("blockGroups") || !document["blockGroups"].IsObject())
                throw std::runtime_error(path.string() + ": terrain config requires a 'blockGroups' object");
            for (auto group = document["blockGroups"].MemberBegin();
                group != document["blockGroups"].MemberEnd(); ++group) {
                const std::string name(group->name.GetString(), group->name.GetStringLength());
                if (!group->value.IsArray())
                    throw std::runtime_error(path.string() + ": terrain block group '" + name + "' must be an array");
                auto& ids = result.blockGroups[name];
                for (const rapidjson::Value& value : group->value.GetArray()) {
                    if (!value.IsString())
                        throw std::runtime_error(path.string() + ": terrain block groups must contain block names");
                    ids.emplace(static_cast<blockId>(registry.getId(value.GetString())));
                }
            }
            if (!document.HasMember("lithology") || !document["lithology"].IsArray())
                throw std::runtime_error(path.string() + ": terrain config requires a 'lithology' array");
            for (const rapidjson::Value& value : document["lithology"].GetArray()) {
                if (!value.IsObject() || !value.HasMember("block") || !value["block"].IsString() ||
                    !value.HasMember("salt") || !value["salt"].IsUint64() ||
                    !value.HasMember("chance") || !value["chance"].IsNumber() ||
                    !value.HasMember("height") || !value["height"].IsArray() ||
                    !value.HasMember("cell") || !value["cell"].IsArray() || value["cell"].Size() != 2u ||
                    !value.HasMember("radius") || !value["radius"].IsArray() || value["radius"].Size() != 2u)
                    throw std::runtime_error(path.string() + ": invalid terrain lithology rule");
                const rapidjson::Value& height = value["height"];
                const rapidjson::Value& cell = value["cell"];
                const rapidjson::Value& radius = value["radius"];
                if ((height.Size() != 2u && height.Size() != 3u) ||
                    !height[0].IsInt() || !height[height.Size() - 1u].IsInt() ||
                    (height.Size() == 3u && !height[1].IsInt()) ||
                    !cell[0].IsInt() || !cell[1].IsInt() ||
                    !radius[0].IsNumber() || !radius[1].IsNumber())
                    throw std::runtime_error(path.string() + ": invalid terrain lithology range");
                terrainLithologyRule rule;
                rule.block = static_cast<blockId>(registry.getId(value["block"].GetString()));
                rule.salt = value["salt"].GetUint64();
                rule.minimumY = height[0].GetInt();
                rule.maximumY = height[height.Size() - 1u].GetInt();
                rule.peaked = height.Size() == 3u;
                rule.peakY = rule.peaked ? height[1].GetInt() : rule.minimumY;
                rule.chance = value["chance"].GetFloat();
                rule.cellXZ = cell[0].GetInt();
                rule.cellY = cell[1].GetInt();
                rule.radiusMinimum = radius[0].GetFloat();
                rule.radiusMaximum = radius[1].GetFloat();
                if (rule.minimumY > rule.maximumY ||
                    (rule.peaked && (rule.peakY < rule.minimumY || rule.peakY > rule.maximumY)) ||
                    rule.chance < 0.0f || rule.cellXZ <= 0 || rule.cellY <= 0 ||
                    rule.radiusMinimum <= 0.0f || rule.radiusMaximum < rule.radiusMinimum)
                    throw std::runtime_error(path.string() + ": invalid terrain lithology values");
                result.lithology.push_back(rule);
            }
            return result;
        }
    };

    using TERRAIN_BIOME = uint32_t;

    struct terrainClimateRange {
        float minimum = 0.0f;
        float maximum = 0.0f;
        float falloff = 0.0f;
    };

    struct terrainClimateSelector {
        float weight = 1.0f;
        std::unordered_map<std::string, terrainClimateRange> ranges;
    };

    struct terrainBiomeGenerationDefinition {
        blockId topBlock = 0u;
        blockId underBlock = 0u;
        blockId floodedTopBlock = 0u;
        blockId floodedUnderBlock = 0u;
        uint32_t soilDepth = 3u;
        uint32_t floodedSoilDepth = 3u;
        std::string waterKind = "none";
        bool floodedSurface = false;
        bool globalSnow = false;
        bool alpineSurface = false;
        bool highlandRock = false;
        bool dryWeather = false;
        bool coldWeather = false;
        std::string treeKind = "none";
        std::string alternateTreeKind = "none";
        blockId treeLogBlock = 0u;
        blockId treeLeavesBlock = 0u;
        blockId alternateTreeLogBlock = 0u;
        blockId alternateTreeLeavesBlock = 0u;
        uint32_t treeChanceNumerator = 0u;
        uint32_t treeChanceDenominator = 1u;
        uint32_t alternateChanceNumerator = 0u;
        uint32_t alternateChanceDenominator = 1u;
        uint32_t treeVariants = 1u;
        float alpineRockDepth = 16.0f;
        float highlandRockDepth = 12.0f;
        std::vector<terrainClimateSelector> climateSelectors;
    };

    struct terrainBiomeDefinition {
        std::string name;
        std::string displayName;
        std::vector<std::string> aliases;
        terrainBiomeGenerationDefinition generation;
    };

    struct terrainBiomeConfig {
        std::vector<terrainBiomeDefinition> definitions;
        std::unordered_map<std::string, TERRAIN_BIOME> ids;

        static terrainBiomeConfig load(
            const std::filesystem::path& path,
            const staticAssetManager& registry
        );

        const char* displayName(TERRAIN_BIOME biome) const {
            const size_t index = static_cast<size_t>(biome);
            return index < definitions.size() ? definitions[index].displayName.c_str() : "UNKNOWN";
        }

        bool parse(const char* token, TERRAIN_BIOME& out) const {
            if (!token || !*token) return false;
            std::string lower(token);
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            const auto it = ids.find(lower);
            if (it == ids.end()) return false;
            out = it->second;
            return true;
        }

        TERRAIN_BIOME id(std::string_view name) const {
            const auto it = ids.find(std::string(name));
            return it == ids.end() ? 0u : it->second;
        }
    };

    struct biomeSample {
        TERRAIN_BIOME _biome = 0u;
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
			float lakeWeight = 0.0f;
			float lakeSurface = 68.0f;
        };

        uint64_t _seed;
        terrainBlockPalette _blocks;
        terrainBiomeConfig _biomes;
        int32_t _seaLevel = static_cast<int32_t>(_blocks.requireSetting("seaLevel"));

        blockId roleBlock(std::string_view role) const { return _blocks.require(role); }
        float setting(std::string_view name) const { return _blocks.requireSetting(name); }
        static constexpr int32_t HORIZONTAL_SAMPLE_RATE = 4;
        // A four-block Y lattice preserves genuine 3D shelves and overhangs;
        // the previous eight-block spacing smoothed most of them back into a
        // conventional height field in the CPU fallback.
        static constexpr int32_t VERTICAL_SAMPLE_RATE = 4;
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

        // Sparse, varied openings that connect the terrain surface to the
        // 64-cell tunnel network. Mountain-weighted openings sit below the
        // column top so they intersect cliffs and hillsides rather than always
        // reading as sinkholes viewed from above.
        float oneSurfaceEntrance(
            float x,
            float y,
            float z,
            float surfaceHeight,
            float mountainWeight,
            int32_t cellX,
            int32_t cellZ
        ) const {
            constexpr float CELL_SIZE = 64.0f;
            constexpr float PI = 3.14159265358979323846f;
            const uint64_t entranceHash = hash(_seed + 2100, cellX, cellZ);
            // Roughly one third fewer entrances than the old 42% gate.
            if (hashUnit(mix(entranceHash)) > 0.28f)
                return 0.0f;

            const float mouthX = (static_cast<float>(cellX) + 0.5f) * CELL_SIZE +
                (hashUnit(mix(entranceHash + 1)) - 0.5f) * 28.0f;
            const float mouthZ = (static_cast<float>(cellZ) + 0.5f) * CELL_SIZE +
                (hashUnit(mix(entranceHash + 3)) - 0.5f) * 28.0f;
            const uint32_t style = static_cast<uint32_t>(
                hashUnit(mix(entranceHash + 6)) * 3.9999f);
            const float sizeA = hashUnit(mix(entranceHash + 4));
            const float sizeB = hashUnit(mix(entranceHash + 5));
            float mouthWidth = 4.5f + sizeA * 3.5f;
            float mouthHeight = mouthWidth * (0.78f + sizeB * 0.22f);
            if (style == 1u) {
                mouthWidth = 3.4f + sizeA * 3.8f;
                mouthHeight = 4.5f + sizeB * 5.0f;
            }
            else if (style == 2u) {
                mouthWidth = 1.9f + sizeA * 2.3f;
                mouthHeight = 7.0f + sizeB * 7.0f;
            }
            else if (style == 3u) {
                mouthWidth = 7.0f + sizeA * 5.0f;
                mouthHeight = 3.0f + sizeB * 3.2f;
            }

            const float yaw = hashUnit(mix(entranceHash + 7)) * PI * 2.0f;
            const float dirX = std::cos(yaw);
            const float dirZ = std::sin(yaw);
            const float run = 14.0f + hashUnit(mix(entranceHash + 8)) * 28.0f;
            float drop = 12.0f + hashUnit(mix(entranceHash + 9)) * 16.0f;
            if (style == 1u) drop = 5.0f + hashUnit(mix(entranceHash + 9)) * 9.0f;
            else if (style == 2u) drop = 8.0f + hashUnit(mix(entranceHash + 9)) * 18.0f;
            else if (style == 3u) drop = 2.0f + hashUnit(mix(entranceHash + 9)) * 7.0f;

            // Lower the portal centre on mountainous columns, but keep the
            // offset proportional to its height so even low, broad mouths
            // still break through the hillside instead of being buried.
            const float sideDepth = style == 0u ? -2.0f :
                mouthHeight * (0.24f +
                    std::clamp(mountainWeight, 0.0f, 1.0f) * 0.42f) +
                hashUnit(mix(entranceHash + 10)) * 0.75f;
            const tunnelPoint mouth{ mouthX, surfaceHeight - sideDepth, mouthZ };
            const tunnelPoint bend{
                mouthX + dirX * run,
                mouth.y - drop,
                mouthZ + dirZ * run
            };
            const tunnelPoint anchor = tunnelAnchor(cellX, cellZ);
            const tunnelPoint bottom{ anchor.x, std::max(12.0f, anchor.y), anchor.z };
            const float shaftWidth = 2.7f + hashUnit(mix(entranceHash + 11)) * 2.8f;
            const float shaftHeight = 2.2f + hashUnit(mix(entranceHash + 12)) * 3.8f;
            const float pad = std::max(mouthWidth, mouthHeight) + 2.0f;
            if (x < std::min({ mouth.x, bend.x, bottom.x }) - pad ||
                x > std::max({ mouth.x, bend.x, bottom.x }) + pad ||
                z < std::min({ mouth.z, bend.z, bottom.z }) - pad ||
                z > std::max({ mouth.z, bend.z, bottom.z }) + pad) {
                return 0.0f;
            }

            auto segmentField = [&](const tunnelPoint& start, const tunnelPoint& end,
                float startWidth, float startHeight, float endWidth, float endHeight,
                float shapeWave) {
                const float dx = end.x - start.x;
                const float dy = end.y - start.y;
                const float dz = end.z - start.z;
                const float lengthSquared = dx * dx + dy * dy + dz * dz;
                const float projection = lengthSquared > 0.0f
                    ? std::clamp(((x - start.x) * dx + (y - start.y) * dy +
                        (z - start.z) * dz) / lengthSquared, 0.0f, 1.0f)
                    : 0.0f;
                const float wave = 1.0f + shapeWave * std::sin(
                    projection * PI * (style == 2u ? 3.0f : 2.0f));
                const float width = std::max(1.4f,
                    lerp(startWidth, endWidth, projection) * wave);
                const float height = std::max(1.4f,
                    lerp(startHeight, endHeight, projection) / std::max(wave, 0.55f));
                const float ox = x - (start.x + dx * projection);
                const float oy = y - (start.y + dy * projection);
                const float oz = z - (start.z + dz * projection);
                const float normalizedDistance = std::sqrt(
                    (ox * ox + oz * oz) / (width * width) +
                    (oy * oy) / (height * height));
                return 1.0f - smoothStep(0.70f, 1.10f, normalizedDistance);
            };

            const float shapeWave = (hashUnit(mix(entranceHash + 13)) - 0.5f) * 0.42f;
            float field = segmentField(
                mouth, bend, mouthWidth, mouthHeight,
                shaftWidth * 1.20f, shaftHeight * 1.20f, shapeWave);
            field = std::max(field, segmentField(
                bend, bottom, shaftWidth * 1.20f, shaftHeight * 1.20f,
                shaftWidth, shaftHeight, -shapeWave * 0.65f));

            if (style == 0u) {
                const float planar = std::sqrt(
                    (x - mouthX) * (x - mouthX) + (z - mouthZ) * (z - mouthZ));
                const float bowl =
                    (1.0f - smoothStep(mouthWidth * 0.45f, mouthWidth * 1.15f, planar)) *
                    (1.0f - smoothStep(mouthHeight * 0.55f, mouthHeight * 1.30f,
                        std::abs(y - surfaceHeight)));
                field = std::max(field, bowl);
            }
            else {
                // Elliptical portal cap: narrow/tall fissures, arched adits, and
                // broad low mouths retain distinct silhouettes at the surface.
                const float deltaX = x - mouthX;
                const float deltaZ = z - mouthZ;
                const float along = deltaX * dirX + deltaZ * dirZ;
                const float lateral = deltaX * -dirZ + deltaZ * dirX;
                const float vertical = y - mouth.y;
                const float portalDistance = std::sqrt(
                    lateral * lateral / (mouthWidth * mouthWidth) +
                    vertical * vertical / (mouthHeight * mouthHeight) +
                    along * along / ((mouthWidth * 1.45f) * (mouthWidth * 1.45f)));
                field = std::max(field,
                    1.0f - smoothStep(0.68f, 1.10f, portalDistance));
            }
            field *= smoothStep(8.0f, 14.0f, y);
            field *= 1.0f - smoothStep(surfaceHeight + 2.0f, surfaceHeight + 6.0f, y);
            return field;
        }

        float surfaceEntranceField(
            const columnSample& column,
            int32_t worldX,
            int32_t y,
            int32_t worldZ
        ) const {
            const std::string& biomeWater =
                _biomes.definitions[column.biome._biome].generation.waterKind;
            if (biomeWater == "ocean" || biomeWater == "lake")
                return 0.0f;
            if (column.oceanWeight > 0.35f || column.riverWeight > 0.20f ||
                column.lakeWeight > 0.16f) {
                return 0.0f;
            }
            if (column.surfaceHeight < static_cast<float>(_seaLevel) + 2.0f)
                return 0.0f;

            constexpr int32_t CELL_SIZE = 64;
            const int32_t cellX = static_cast<int32_t>(std::floor(
                static_cast<double>(worldX) / static_cast<double>(CELL_SIZE)));
            const int32_t cellZ = static_cast<int32_t>(std::floor(
                static_cast<double>(worldZ) / static_cast<double>(CELL_SIZE)));
            const float x = static_cast<float>(worldX);
            const float fy = static_cast<float>(y);
            const float z = static_cast<float>(worldZ);
            float field = 0.0f;
            for (int32_t dz = -1; dz <= 1; ++dz) {
                for (int32_t dx = -1; dx <= 1; ++dx) {
                    field = std::max(
                        field,
                        oneSurfaceEntrance(
                            x, fy, z, column.surfaceHeight, column.mountainWeight,
                            cellX + dx, cellZ + dz));
                }
            }
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
            return fractal2DAniso(worldX, worldZ, frequency, frequency, octaves, seed);
        }

        // Stretch X/Z differently so hills and ranges form elongated contours
        // instead of soft circular blobs.
        static float fractal2DAniso(
            int32_t worldX,
            int32_t worldZ,
            double frequencyX,
            double frequencyZ,
            int octaves,
            uint64_t seed
        ) {
            double x = (static_cast<double>(worldX) + noiseOffset(seed)) * frequencyX;
            double z = (static_cast<double>(worldZ) + noiseOffset(seed + 1)) * frequencyZ;
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

        static float ridgedFractal2D(
            int32_t worldX,
            int32_t worldZ,
            double frequencyX,
            double frequencyZ,
            int octaves,
            uint64_t seed
        ) {
            double x = (static_cast<double>(worldX) + noiseOffset(seed)) * frequencyX;
            double z = (static_cast<double>(worldZ) + noiseOffset(seed + 1)) * frequencyZ;
            float amplitude = 1.0f;
            float value = 0.0f;
            float total = 0.0f;
            float weight = 1.0f;

            for (int octave = 0; octave < octaves; ++octave) {
                float sample = 1.0f - std::abs(noise2D(
                    x, z, seed + static_cast<uint64_t>(octave) * 0x9E3779B97F4A7C15ULL));
                sample = sample * sample * weight;
                value += sample * amplitude;
                total += amplitude;
                weight = std::clamp(sample * 2.0f, 0.0f, 1.0f);
                x *= 2.0;
                z *= 2.0;
                amplitude *= 0.5f;
            }
            return total > 0.0f ? value / total : 0.0f;
        }

        static float fractal3D(
            double worldX,
            double y,
            double worldZ,
            double horizontalFrequency,
            double verticalFrequency,
            int octaves,
            uint64_t seed
        ) {
            double x = (worldX + noiseOffset(seed)) * horizontalFrequency;
            double fy = (y + noiseOffset(seed + 1)) * verticalFrequency;
            double z = (worldZ + noiseOffset(seed + 2)) * horizontalFrequency;
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
            // Deeper oceans and taller inland plateaus within the 384-high world.
            if (continentalness < -0.70f)
                return lerp(6.0f, 28.0f, smoothStep(-1.0f, -0.70f, continentalness));
            if (continentalness < -0.35f)
                return lerp(28.0f, 52.0f, smoothStep(-0.70f, -0.35f, continentalness));
            if (continentalness < -0.05f)
                return lerp(52.0f, 64.0f, smoothStep(-0.35f, -0.05f, continentalness));
            if (continentalness < 0.25f)
                return lerp(64.0f, 86.0f, smoothStep(-0.05f, 0.25f, continentalness));
            if (continentalness < 0.55f)
                return lerp(86.0f, 112.0f, smoothStep(0.25f, 0.55f, continentalness));
            return lerp(112.0f, 138.0f, smoothStep(0.55f, 1.0f, continentalness));
        }

        // Altitude where snow begins. Cold climates snow lower; hot climates need taller peaks.
        float snowLineY(float temperature) const {
            return lerp(
                setting("snowLineCold"),
                setting("snowLineHot"),
                smoothStep(-0.55f, 0.55f, temperature));
        }

        static float climateMetric(std::string_view name, const columnSample& column) {
            if (name == "height") return column.surfaceHeight;
            if (name == "mountain") return column.mountainWeight;
            if (name == "ocean") return column.oceanWeight;
            if (name == "lake") return column.lakeWeight;
            if (name == "river") return column.riverWeight;
            if (name == "temperature") return column.biome._temperature;
            if (name == "moisture") return column.biome._moisture;
            if (name == "continentalness") return column.biome._continentalness;
            if (name == "erosion") return column.biome._erosion;
            if (name == "weirdness") return column.biome._weirdness;
            if (name == "highland") return column.biome._highlandWeight;
            if (name == "alpine") return column.biome._alpineWeight;
            if (name == "arid") return column.biome._aridWeight;
            return 0.0f;
        }

        static float climateRangeWeight(float value, const terrainClimateRange& range) {
            if (value >= range.minimum && value <= range.maximum) return 1.0f;
            if (range.falloff <= 0.0f) return 0.0f;
            if (value < range.minimum)
                return smoothStep(range.minimum - range.falloff, range.minimum, value);
            return 1.0f - smoothStep(range.maximum, range.maximum + range.falloff, value);
        }

        void fillBiomeWeights(const columnSample& column, float* weights) const {
            float total = 0.0f;
            for (uint32_t index = 0; index < _biomes.definitions.size(); ++index) {
                const terrainBiomeGenerationDefinition& generation =
                    _biomes.definitions[index].generation;
                float biomeWeight = 0.0f;
                for (const terrainClimateSelector& selector : generation.climateSelectors) {
                    float score = selector.weight;
                    for (const auto& [metric, range] : selector.ranges)
                        score *= climateRangeWeight(climateMetric(metric, column), range);
                    biomeWeight = std::max(biomeWeight, score);
                }
                weights[index] = biomeWeight;
                total += biomeWeight;
            }
            if (total <= std::numeric_limits<float>::epsilon()) {
                weights[0] = 1.0f;
                total = 1.0f;
            }
            for (uint32_t index = 0; index < _biomes.definitions.size(); ++index)
                weights[index] /= total;
        }

        TERRAIN_BIOME dominantBiome(const float* weights) const {
            uint32_t best = 0u;
            float bestWeight = -1.0f;
            for (uint32_t i = 0; i < _biomes.definitions.size(); ++i) {
                if (weights[i] > bestWeight) {
                    bestWeight = weights[i];
                    best = i;
                }
            }
            return static_cast<TERRAIN_BIOME>(best);
        }

        TERRAIN_BIOME classifyBiome(const columnSample& column) const {
            std::vector<float> weights(_biomes.definitions.size());
            fillBiomeWeights(column, weights.data());
            return dominantBiome(weights.data());
        }

		TERRAIN_BIOME blendedLandBiome(
			const columnSample& column,
			int32_t worldX,
			int32_t worldZ
		) const {
			std::vector<float> weights(_biomes.definitions.size());
			fillBiomeWeights(column, weights.data());

			uint32_t first = 0u;
			uint32_t second = 0u;
			for (uint32_t biome = 0; biome < _biomes.definitions.size(); ++biome) {
				if (weights[biome] > weights[first]) {
					second = first;
					first = biome;
				}
				else if (biome != first &&
					(second == first || weights[biome] > weights[second])) {
					second = biome;
				}
			}

			const TERRAIN_BIOME classified = classifyBiome(column);
			const TERRAIN_BIOME primary = static_cast<TERRAIN_BIOME>(first);
			const TERRAIN_BIOME alternate = static_cast<TERRAIN_BIOME>(second);
			auto isWaterBoundaryBiome = [this](TERRAIN_BIOME biome) {
				return _biomes.definitions[biome].generation.waterKind != "none";
			};
			if (isWaterBoundaryBiome(classified) || isWaterBoundaryBiome(alternate) ||
				weights[first] <= 0.0f || weights[second] <= 0.0f)
				return classified;

			const float ratio = weights[second] / weights[first];
			if (ratio < 0.28f)
				return primary;

			// A low-frequency world-space field breaks the mathematical biome contour
			// into broad, natural patches. Because it is sampled in absolute block
			// coordinates, the transition cannot restart at a chunk boundary.
			const float patch = std::clamp(
				fractal2DAniso(worldX, worldZ, 0.055, 0.047, 3, _seed + 2350) * 0.5f + 0.5f,
				0.0f,
				1.0f);
			const float alternateChance =
				(weights[second] / (weights[first] + weights[second])) *
				smoothStep(0.28f, 0.72f, ratio);
			return patch < alternateChance ? alternate : primary;
		}

        struct waterColumn {
            float surface = -1.0f;
            std::string_view kind = "none";
        };

        waterColumn columnWater(const columnSample& column) const {
            waterColumn water;
            water.surface = column.aquiferLevel;
            water.kind = "none";
            const std::string& biomeWater =
                _biomes.definitions[column.biome._biome].generation.waterKind;
            const float oceanThreshold = setting("oceanWaterThreshold");
            const float lakeThreshold = setting("lakeWaterThreshold");
            if (biomeWater == "ocean" || column.oceanWeight > oceanThreshold) {
                water.surface = std::max(water.surface, static_cast<float>(_seaLevel + 1));
                water.kind = "ocean";
            }
            if (column.oceanWeight <= oceanThreshold &&
                (biomeWater == "lake" || column.lakeWeight > lakeThreshold)) {
                if (column.lakeSurface >= water.surface) {
                    water.surface = column.lakeSurface;
                    water.kind = "lake";
                }
            }
            if (column.oceanWeight <= oceanThreshold &&
                column.lakeWeight <= lakeThreshold &&
                column.riverWeight > setting("riverWaterThreshold")) {
                const float riverSurface = static_cast<float>(_seaLevel + 1);
                if (riverSurface >= water.surface) {
                    water.surface = riverSurface;
                    water.kind = "river";
                }
            }
            if (water.kind == "none" && biomeWater == "wetland") {
                water.surface = std::max(water.surface, static_cast<float>(_seaLevel + 1));
                water.kind = "wetland";
            }
            return water;
        }

        static waterColumn mergeWater(const waterColumn& current, const waterColumn& neighbor) {
            waterColumn result = current;
            if (neighbor.surface > current.surface + 0.01f) {
                result = neighbor;
            }
            else if (neighbor.surface >= current.surface - 0.01f &&
                neighbor.kind != "none" && current.kind == "none") {
                result.kind = neighbor.kind;
                result.surface = std::max(current.surface, neighbor.surface);
            }
            return result;
        }

        columnSample sampleColumn(int32_t worldX, int32_t worldZ) const {
            columnSample result;
            biomeSample& biome = result.biome;

            // Light, high-frequency warp only — strong low-freq warp made soft climate blobs.
            const float warpX =
                fractal2DAniso(worldX, worldZ, 0.00085, 0.00135, 3, _seed + 40) * 32.0f +
                fractal2DAniso(worldX, worldZ, 0.0031, 0.0022, 2, _seed + 41) * 12.0f;
            const float warpZ =
                fractal2DAniso(worldX, worldZ, 0.00115, 0.00075, 3, _seed + 71) * 32.0f +
                fractal2DAniso(worldX, worldZ, 0.0026, 0.0034, 2, _seed + 72) * 12.0f;
            const float climateXf = static_cast<float>(worldX) + warpX;
            const float climateZf = static_cast<float>(worldZ) + warpZ;
            const int32_t climateX = static_cast<int32_t>(climateXf);
            const int32_t climateZ = static_cast<int32_t>(climateZf);

            // Mix smooth + ridged continental noise so coastlines are jagged, not oval.
            const float continentalSmooth =
                fractal2DAniso(climateX, climateZ, 0.00018, 0.00026, 5, _seed + 100);
            const float continentalRidged =
                ridgedFractal2D(climateX, climateZ, 0.00022, 0.00014, 4, _seed + 105) * 2.0f - 1.0f;
            biome._continentalness = continentalSmooth * 0.68f + continentalRidged * 0.32f;
            biome._erosion = fractal2DAniso(climateX, climateZ, 0.00038, 0.00028, 5, _seed + 200);
            biome._weirdness =
                fractal2DAniso(climateX, climateZ, 0.00072, 0.00048, 4, _seed + 300) * 0.55f +
                (ridgedFractal2D(climateX, climateZ, 0.00095, 0.00155, 4, _seed + 305) * 2.0f - 1.0f) * 0.45f;
            biome._temperature = fractal2DAniso(climateX, climateZ, 0.00016, 0.00022, 4, _seed + 400);
            biome._moisture = fractal2DAniso(climateX, climateZ, 0.00017, 0.00020, 4, _seed + 500);

            const float ridge = std::clamp(peaksAndValleys(biome._weirdness), 0.0f, 1.0f);
            const float inland = smoothStep(-0.18f, 0.32f, biome._continentalness);
            const float lowErosion = 1.0f - smoothStep(-0.45f, 0.42f, biome._erosion);
            // Sharper ridge gate → mountain chains instead of soft circular massifs.
            result.mountainWeight = std::clamp(
                inland * lowErosion * smoothStep(0.32f, 0.78f, ridge), 0.0f, 1.0f);
            result.oceanWeight = 1.0f - smoothStep(-0.80f, 0.16f, biome._continentalness);

            // Narrow river channels from abs-noise.
            const float riverNoise = std::abs(fractal2DAniso(worldX, worldZ, 0.0022, 0.0031, 4, _seed + 600));
            result.riverWeight =
                (1.0f - smoothStep(0.010f, 0.038f, riverNoise)) *
                smoothStep(-0.20f, 0.03f, biome._continentalness) *
                (1.0f - result.mountainWeight * 0.75f);

            // Elongated lake basins (ridged + anisotropic mask) instead of soft circular ponds.
            const float lakeField = ridgedFractal2D(worldX, worldZ, 0.0026, 0.0012, 4, _seed + 650);
            const float lakeMask = fractal2DAniso(worldX, worldZ, 0.0010, 0.0017, 3, _seed + 660);
            result.lakeWeight =
                smoothStep(0.62f, 0.86f, lakeField) *
                smoothStep(0.08f, 0.38f, lakeMask) *
                smoothStep(-0.08f, 0.28f, biome._continentalness) *
                (1.0f - result.mountainWeight) *
                (1.0f - result.riverWeight) *
                (1.0f - smoothStep(0.22f, 0.36f, result.oceanWeight));
            result.lakeSurface = 68.0f +
                fractal2DAniso(worldX, worldZ, 0.0011, 0.0007, 2, _seed + 675) * 4.0f;

            const float rollingHills = fractal2DAniso(worldX, worldZ, 0.0065, 0.0105, 4, _seed + 700);
            const float broadValleys = fractal2DAniso(worldX, worldZ, 0.0032, 0.0020, 3, _seed + 730);
            const float detail = fractal2DAniso(worldX, worldZ, 0.024, 0.017, 2, _seed + 800);
            const float rangeRidges = ridgedFractal2D(worldX, worldZ, 0.00135, 0.00225, 5, _seed + 760);
            float height = continentalHeight(biome._continentalness);
            const float relief = 1.0f - smoothStep(-0.20f, 0.55f, biome._erosion);

            // Plains / desert / savanna / wetland stay flat; mountains keep full relief.
            const float aridness = smoothStep(0.15f, 0.55f,
                biome._temperature - biome._moisture * 0.72f);
            const float dryness = 1.0f - smoothStep(-0.08f, 0.28f, biome._moisture);
            const float flatClimate = std::max(aridness, dryness * (1.0f - aridness * 0.35f));
            const float lowlandFlat = std::clamp(
                (1.0f - result.mountainWeight) *
                smoothStep(-0.20f, 0.50f, biome._erosion) *
                lerp(0.45f, 1.0f, flatClimate),
                0.0f, 1.0f);

            height += rollingHills * lerp(4.0f, 22.0f, relief) * lerp(1.0f, 0.08f, lowlandFlat);
            height += broadValleys * lerp(-6.0f, 8.0f, inland) * lerp(1.0f, 0.10f, lowlandFlat);
            height += detail * lerp(0.6f, 3.5f, result.mountainWeight) * lerp(1.0f, 0.25f, lowlandFlat);
            height += result.mountainWeight * lerp(28.0f, 105.0f, ridge);
            height += result.mountainWeight * rangeRidges * lerp(18.0f, 78.0f, ridge);

            // Pull flat biomes toward a gentle plains elevation band.
            const float flatTarget = lerp(66.0f, 74.0f, inland * 0.55f);
            height = lerp(height, flatTarget, lowlandFlat * 0.62f * (1.0f - result.oceanWeight));

            // Ease land down through a beach shelf into the seafloor so ocean
            // meetings are a ramp, not a biome-shaped cliff.
            const float shoreBlend = smoothStep(0.04f, 0.58f, result.oceanWeight);
            const float deepBlend = smoothStep(0.32f, 0.90f, result.oceanWeight);
            const float shelfHeight = lerp(
                static_cast<float>(_seaLevel) + 5.0f,
                static_cast<float>(_seaLevel) + 1.0f,
                shoreBlend);
            const float seafloorHeight = continentalHeight(biome._continentalness);
            height = lerp(height, shelfHeight, shoreBlend * (1.0f - deepBlend * 0.85f));
            height = lerp(height, seafloorHeight, deepBlend);

            height = lerp(height, static_cast<float>(_seaLevel - 3), result.riverWeight * 0.88f);
            const float lakeShore = smoothStep(0.10f, 0.36f, result.lakeWeight);
            const float lakeDeep = smoothStep(0.32f, 0.72f, result.lakeWeight);
            height = lerp(height, result.lakeSurface - 1.5f, lakeShore * (1.0f - lakeDeep));
            height = lerp(height, result.lakeSurface - 6.0f, lakeDeep);
            const float wetlandWant =
                (1.0f - result.mountainWeight) *
                (1.0f - result.oceanWeight) *
                (1.0f - result.lakeWeight) *
                (1.0f - result.riverWeight) *
                smoothStep(-0.06f, 0.30f, biome._moisture) *
                (1.0f - smoothStep(0.08f, 0.48f, biome._temperature));
            height = lerp(
                height,
                static_cast<float>(_seaLevel) - 0.5f,
                std::clamp(wetlandWant * 1.65f, 0.0f, 1.0f));
            result.surfaceHeight = std::clamp(height, 4.0f, static_cast<float>(CHUNK_HEIGHT - 10));

            const float aquiferRegion = fractal2D(worldX, worldZ, 0.0011, 2, _seed + 925);
            result.aquiferLevel = aquiferRegion > 0.28f
                ? 8.0f + fractal2D(worldX, worldZ, 0.0028, 2, _seed + 900) * 10.0f
                : -1.0f;

            biome._highlandWeight = smoothStep(0.14f, 0.55f, result.mountainWeight);
            biome._alpineWeight = std::max(
                smoothStep(0.40f, 0.78f, result.mountainWeight),
                smoothStep(130.0f, 190.0f, result.surfaceHeight)
            );
            biome._aridWeight = smoothStep(0.18f, 0.62f,
                biome._temperature - biome._moisture * 0.72f);

            biome._biome = classifyBiome(result);
            return result;
        }

		columnSample interpolateColumn(
			const columnSample& a,
			const columnSample& b,
			const columnSample& c,
			const columnSample& d,
			float tx,
			float tz
		) const {
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
			result.lakeWeight = blend(&columnSample::lakeWeight);
			result.lakeSurface = blend(&columnSample::lakeSurface);
			result.biome._temperature = blendBiome(&biomeSample::_temperature);
			result.biome._moisture = blendBiome(&biomeSample::_moisture);
			result.biome._continentalness = blendBiome(&biomeSample::_continentalness);
			result.biome._erosion = blendBiome(&biomeSample::_erosion);
			result.biome._weirdness = blendBiome(&biomeSample::_weirdness);
			result.biome._highlandWeight = blendBiome(&biomeSample::_highlandWeight);
			result.biome._alpineWeight = blendBiome(&biomeSample::_alpineWeight);
			result.biome._aridWeight = blendBiome(&biomeSample::_aridWeight);

			result.biome._biome = classifyBiome(result);
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
					columnSample sample = interpolateColumn(
						at(0, 0), at(1, 0), at(0, 1), at(1, 1), tx, tz
					);
					sample.biome._biome = blendedLandBiome(
						sample, worldX + x, worldZ + z);
					full[columnIndex(x, z)] = sample;
				}
			}
		}

        float sampleDensity(
            const columnSample& column,
            int32_t worldX,
            int32_t y,
            int32_t worldZ
        ) const {
            const float terrainScale = lerp(18.0f, 52.0f, column.mountainWeight);
            const float warpX = fractal3D(worldX, y, worldZ, 0.0034, 0.0038, 2, _seed + 1010);
            const float warpY = fractal3D(worldX, y, worldZ, 0.0037, 0.0030, 2, _seed + 1020);
            const float warpZ = fractal3D(worldX, y, worldZ, 0.0026, 0.0036, 2, _seed + 1030);
            const double warpedX = static_cast<double>(worldX) + warpX * 14.0;
            const double warpedY = static_cast<double>(y) + warpY * 10.0;
            const double warpedZ = static_cast<double>(worldZ) + warpZ * 18.0;

            const float terrainShape = fractal3D(
                warpedX, warpedY, warpedZ, 0.0085, 0.0110, 3, _seed + 1100);
            const float detailShape = fractal3D(
                warpedX, warpedY, warpedZ, 0.0240, 0.0300, 2, _seed + 1200);
            const float ridgeShape = 1.0f - std::abs(fractal3D(
                warpedX, warpedY, warpedZ, 0.0110, 0.0160, 3, _seed + 1250));

            // The column height now supplies only a broad vertical bias. The
            // solid/air boundary itself is an XYZ isosurface, allowing cliffs to
            // undercut and mountain mass to form shelves, arches, and overhangs.
            const float verticalBias =
                (column.surfaceHeight - static_cast<float>(y)) / terrainScale;
            float density = verticalBias * 0.68f;
            density += terrainShape * lerp(0.20f, 0.58f, column.mountainWeight);
            density += detailShape * lerp(0.06f, 0.20f, column.mountainWeight);
            density += (ridgeShape * 2.0f - 1.0f) * column.mountainWeight * 0.22f;

            const float depth = column.surfaceHeight - static_cast<float>(y);
            const float caveWindow =
                smoothStep(4.0f, 22.0f, depth) *
                smoothStep(2.0f, 12.0f, static_cast<float>(y)) *
                (1.0f - smoothStep(200.0f, 280.0f, static_cast<float>(y)));

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

            const float entrance = surfaceEntranceField(column, worldX, y, worldZ);
            if (entrance > 0.0f)
                density = lerp(density, std::min(density, -0.85f), entrance);

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

        struct surfaceChoice {
            blockId top;
            blockId under;
            int32_t soil;
        };

        static bool lakeDirtFloor(uint64_t seed, int32_t worldX, int32_t worldZ) {
            // Coarse enough that a whole lake stays one bed (old 1000-block
            // cells split basins across chunk borders).
            const int32_t cellX = static_cast<int32_t>(std::floor(static_cast<float>(worldX) * 0.00015f));
            const int32_t cellZ = static_cast<int32_t>(std::floor(static_cast<float>(worldZ) * 0.00015f));
            return (hash(seed + 2111, cellX, cellZ) & 1ULL) != 0ULL;
        }

        static bool oceanGravelFloor(uint64_t seed) {
            return (hash(seed + 2100, 0, 0) & 1ULL) != 0ULL;
        }

        surfaceChoice waterBed(std::string_view kind, uint64_t seed, int32_t worldX, int32_t worldZ) const {
            surfaceChoice choice{ roleBlock("grass"), roleBlock("dirt"), 3 };
            choice.soil = 4;
            if (kind == "lake") {
                const bool dirt = lakeDirtFloor(seed, worldX, worldZ);
                choice.top = dirt ? roleBlock("dirt") : roleBlock("gravel");
                choice.under = choice.top;
            }
            else if (kind == "river") {
                choice.top = roleBlock("gravel");
                choice.under = roleBlock("gravel");
            }
            else {
                const bool gravel = oceanGravelFloor(seed);
                choice.top = gravel ? roleBlock("gravel") : roleBlock("sand");
                choice.under = choice.top;
                choice.soil = 5;
            }
            return choice;
        }

        surfaceChoice surfaceForBiome(
            TERRAIN_BIOME biome,
            const columnSample& column,
            int32_t y,
            int32_t worldX,
            int32_t worldZ,
            std::string_view waterKind,
            bool flooded
        ) const {
            // Floor follows the water that actually covers the column, then
            // the biome. One block type for the whole soil layer so hillsides
            // do not stripe sandstone or coarse dirt through the surface.
            if (flooded && (waterKind == "ocean" || waterKind == "lake" || waterKind == "river"))
                return waterBed(waterKind, _seed, worldX, worldZ);
            const terrainBiomeGenerationDefinition& definition =
                _biomes.definitions[static_cast<size_t>(biome)].generation;
            if (definition.waterKind == "ocean" || definition.waterKind == "lake")
                return waterBed(definition.waterKind, _seed, worldX, worldZ);

            surfaceChoice choice{
                definition.topBlock,
                definition.underBlock,
                static_cast<int32_t>(definition.soilDepth)
            };
            if (definition.floodedSurface && (flooded || waterKind == "wetland")) {
                choice.top = definition.floodedTopBlock;
                choice.under = definition.floodedUnderBlock;
                choice.soil = static_cast<int32_t>(definition.floodedSoilDepth);
            }
            if (definition.alpineSurface) {
                const float snowLine = snowLineY(column.biome._temperature);
                if (static_cast<float>(y) >= snowLine) {
                    choice.top = roleBlock("snow");
                    choice.under = roleBlock("snow");
                }
                else if (static_cast<float>(y) >= snowLine - definition.alpineRockDepth) {
                    choice.top = roleBlock("stone");
                    choice.under = roleBlock("stone");
                }
            }

            if (!flooded && definition.globalSnow) {
                const float snowLine = snowLineY(column.biome._temperature);
                if (static_cast<float>(y) >= snowLine) {
                    choice.top = roleBlock("snow");
                    choice.under = roleBlock("snow");
                    choice.soil = 2;
                }
                else if (definition.highlandRock &&
                    static_cast<float>(y) >= snowLine - definition.highlandRockDepth) {
                    choice.top = roleBlock("stone");
                    choice.under = roleBlock("stone");
                    choice.soil = 2;
                }
            }
            return choice;
        }

        void applySurfaceRules(
            int32_t worldX,
            int32_t worldZ,
            const columnGrid& columns,
            const std::array<waterColumn, CHUNK_WIDTH * CHUNK_LENGTH>& waters,
            blockVolume& blocks
        ) const {
            for (int32_t z = 0; z < CHUNK_LENGTH; ++z) {
                for (int32_t x = 0; x < CHUNK_WIDTH; ++x) {
                    const columnSample& column = columns[columnIndex(x, z)];
                    const waterColumn& water = waters[columnIndex(x, z)];
                    int32_t soilRemaining = 0;
                    blockId underBlock = roleBlock("dirt");

                    for (int32_t y = CHUNK_HEIGHT - 2; y > 0; --y) {
                        blockId& block = blocks[blockIndex(x, y, z)];
                        const blockId above = blocks[blockIndex(x, y + 1, z)];

                        if (block == _blocks.AIR_BLOCK || blockType(block) == roleBlock("water")) {
                            soilRemaining = 0;
                            continue;
                        }
                        if (block != roleBlock("stone")) continue;

                        const bool exposed = above == _blocks.AIR_BLOCK || blockType(above) == roleBlock("water");
                        const bool nearSurface =
                            static_cast<float>(y) >= column.surfaceHeight - 12.0f;
                        const uint64_t surfaceRandom = hash(_seed + 1600, worldX + x, y, worldZ + z);

                        if (exposed && nearSurface) {
                            const bool flooded =
                                blockType(above) == roleBlock("water") ||
                                column.surfaceHeight + 0.5f < water.surface;
                            const surfaceChoice choice = surfaceForBiome(
                                column.biome._biome,
                                column,
                                y,
                                worldX + x,
                                worldZ + z,
                                water.kind,
                                flooded);
                            block = choice.top;
                            underBlock = choice.under;
                            soilRemaining = choice.soil + static_cast<int32_t>(surfaceRandom % 3ULL);
                        }
                        else if (soilRemaining > 0) {
                            block = underBlock;
                            --soilRemaining;
                        }
                    }
                }
            }
        }

        blockId lithologyAt(int32_t worldX, int32_t y, int32_t worldZ, blockId host) const {
            (void)host;

            // Triangle density in [lo, hi] peaking at peak. Used so each ore
            // has a geological depth preference instead of a flat band.
            auto triangle = [](int32_t height, int32_t lo, int32_t peak, int32_t hi) -> float {
                if (height <= lo || height >= hi) return 0.0f;
                if (height == peak) return 1.0f;
                if (height < peak)
                    return static_cast<float>(height - lo) / static_cast<float>(peak - lo);
                return static_cast<float>(hi - height) / static_cast<float>(hi - peak);
            };

            // Coarse-cell ellipsoid blobs so ores/stone variants clump into
            // irregular pockets instead of salt-and-pepper noise.
            auto inBlob = [&](
                uint64_t salt,
                int32_t cellXZ,
                int32_t cellY,
                float spawnChance,
                float radiusMin,
                float radiusMax
            ) -> bool {
                if (spawnChance <= 0.0f) return false;
                const int32_t cx = floorDiv(worldX, cellXZ);
                const int32_t cy = floorDiv(y, cellY);
                const int32_t cz = floorDiv(worldZ, cellXZ);
                const uint64_t cell = hash(_seed + salt, cx, cy, cz);
                if (static_cast<float>(cell % 10000ULL) >= spawnChance * 10000.0f)
                    return false;

                const float ox = static_cast<float>((cell >> 8) % 997ULL) / 997.0f;
                const float oy = static_cast<float>((cell >> 18) % 991ULL) / 991.0f;
                const float oz = static_cast<float>((cell >> 28) % 983ULL) / 983.0f;
                const float stretch = 0.55f + 0.90f * static_cast<float>((cell >> 38) % 1000ULL) / 1000.0f;
                const float rx = radiusMin + (radiusMax - radiusMin) *
                    static_cast<float>((cell >> 4) % 1000ULL) / 1000.0f;
                const float ry = (radiusMin * 0.55f + (radiusMax - radiusMin) * 0.45f *
                    static_cast<float>((cell >> 14) % 1000ULL) / 1000.0f) * stretch;
                const float rz = radiusMin + (radiusMax - radiusMin) *
                    static_cast<float>((cell >> 24) % 1000ULL) / 1000.0f;

                const float centerX = static_cast<float>(cx * cellXZ) + ox * static_cast<float>(cellXZ);
                const float centerY = static_cast<float>(cy * cellY) + oy * static_cast<float>(cellY);
                const float centerZ = static_cast<float>(cz * cellXZ) + oz * static_cast<float>(cellXZ);
                const float dx = (static_cast<float>(worldX) + 0.5f - centerX) / std::max(rx, 0.35f);
                const float dy = (static_cast<float>(y) + 0.5f - centerY) / std::max(ry, 0.25f);
                const float dz = (static_cast<float>(worldZ) + 0.5f - centerZ) / std::max(rz, 0.35f);
                // Mild hash warp keeps blob edges from reading as perfect ellipses.
                const float edgeNoise = static_cast<float>(
                    hash(cell + 91ULL, worldX, y, worldZ) % 1000ULL) / 1000.0f;
                return dx * dx + dy * dy + dz * dz < 1.0f + (edgeNoise - 0.5f) * 0.45f;
            };

            for (const terrainLithologyRule& rule : _blocks.lithology) {
                if (y < rule.minimumY || y > rule.maximumY) continue;
                const float verticalWeight = rule.peaked
                    ? triangle(y, rule.minimumY, rule.peakY, rule.maximumY)
                    : 1.0f;
                if (inBlob(
                    rule.salt,
                    rule.cellXZ,
                    rule.cellY,
                    rule.chance * verticalWeight,
                    rule.radiusMinimum,
                    rule.radiusMaximum))
                    return rule.block;
            }
            return roleBlock("stone");
        }

        void applyUndergroundRules(
            int32_t worldX,
            int32_t worldZ,
            blockVolume& blocks
        ) const {
            for (int32_t z = 0; z < CHUNK_LENGTH; ++z) {
                for (int32_t x = 0; x < CHUNK_WIDTH; ++x) {
                    for (int32_t y = 1; y < CHUNK_HEIGHT - 1; ++y) {
                        blockId& block = blocks[blockIndex(x, y, z)];
                        if (block == roleBlock("stone"))
                            block = lithologyAt(worldX + x, y, worldZ + z, block);
                    }

                    for (int32_t y = 0; y < 5; ++y) {
                        const uint64_t foundation = hash(_seed + 1800, worldX + x, y, worldZ + z);
                        if (y == 0 || static_cast<int32_t>(foundation % 5ULL) >= y)
                            blocks[blockIndex(x, y, z)] = roleBlock("bedrock");
                    }
                }
            }
        }

        static int32_t floorDiv(int32_t value, int32_t divisor) {
            int32_t quotient = value / divisor;
            if (value % divisor < 0) --quotient;
            return quotient;
        }

        bool isTreeGroundBlock(blockId id) const {
            return _blocks.inGroup("treeGround", id);
        }

        bool isTreeLeafBlock(blockId id) const {
            return _blocks.inGroup("treeLeaves", id);
        }

        int32_t findTreeGround(
            const blockVolume& blocks,
            int32_t worldX,
            int32_t worldZ,
            int32_t treeX,
            int32_t treeZ,
            int32_t searchTop = CHUNK_HEIGHT - 2
        ) const {
            const int32_t localX = treeX - worldX;
            const int32_t localZ = treeZ - worldZ;
            if (localX < 0 || localX >= CHUNK_WIDTH ||
                localZ < 0 || localZ >= CHUNK_LENGTH)
                return -1;

            const int32_t startY = std::clamp(searchTop, _seaLevel + 1, CHUNK_HEIGHT - 2);
            for (int32_t y = startY; y >= _seaLevel + 1; --y) {
                const blockId ground = blocks[blockIndex(localX, y, localZ)];
                const blockId above = blocks[blockIndex(localX, y + 1, localZ)];
                if (!isTreeGroundBlock(ground))
                    continue;
                if (blockType(above) == roleBlock("water"))
                    continue;
                if (above != _blocks.AIR_BLOCK && !isTreeLeafBlock(above))
                    continue;

                bool supported = true;
                for (int32_t depth = 0; depth < 3; ++depth) {
                    const blockId below = blocks[blockIndex(localX, y - depth, localZ)];
                    if (below == _blocks.AIR_BLOCK || blockType(below) == roleBlock("water")) {
                        supported = false;
                        break;
                    }
                }
                if (!supported)
                    continue;
                return y;
            }
            return -1;
        }

        void placeTrees(int32_t worldX, int32_t worldZ, const columnGrid& columns, blockVolume& blocks) const {
            const int32_t featureSpacing = static_cast<int32_t>(setting("treeFeatureSpacing"));
            const int32_t featureMargin = static_cast<int32_t>(setting("treeFeatureMargin"));
            const int32_t minCellX = floorDiv(worldX - featureMargin, featureSpacing);
            const int32_t maxCellX = floorDiv(worldX + CHUNK_WIDTH - 1 + featureMargin, featureSpacing);
            const int32_t minCellZ = floorDiv(worldZ - featureMargin, featureSpacing);
            const int32_t maxCellZ = floorDiv(worldZ + CHUNK_LENGTH - 1 + featureMargin, featureSpacing);

            auto setIfLocal = [&](int32_t x, int32_t y, int32_t z, blockId id, bool replaceLeavesOnly = false) {
                const int32_t localX = x - worldX;
                const int32_t localZ = z - worldZ;
                if (localX < 0 || localX >= CHUNK_WIDTH ||
                    localZ < 0 || localZ >= CHUNK_LENGTH ||
                    y < 1 || y >= CHUNK_HEIGHT)
                    return;

                blockId& current = blocks[blockIndex(localX, y, localZ)];
                const blockId type = blockType(current);
                const bool leaf = _blocks.inGroup("treeLeaves", type);
                auto isLog = [&](blockId value) { return _blocks.inGroup("treeLogs", value); };
                if (current == _blocks.AIR_BLOCK || leaf)
                    current = id;
                else if (!replaceLeavesOnly && isLog(type) && isLog(id))
                    current = id;
            };

            auto placeSquareCanopyLayer = [&](int32_t cx, int32_t y, int32_t cz, int32_t radius, blockId leafId) {
                for (int32_t dz = -radius; dz <= radius; ++dz) {
                    for (int32_t dx = -radius; dx <= radius; ++dx) {
                        if (std::abs(dx) == radius && std::abs(dz) == radius)
                            continue;
                        setIfLocal(cx + dx, y, cz + dz, leafId, true);
                    }
                }
            };

            auto placeLayeredCanopy = [&](int32_t cx, int32_t cz, int32_t trunkTop, blockId leafId,
                const std::pair<int32_t, int32_t>* layers, int32_t layerCount) {
                for (int32_t layer = 0; layer < layerCount; ++layer)
                    placeSquareCanopyLayer(cx, trunkTop + layers[layer].first, cz, layers[layer].second, leafId);
            };

            for (int32_t cellZ = minCellZ; cellZ <= maxCellZ; ++cellZ) {
                for (int32_t cellX = minCellX; cellX <= maxCellX; ++cellX) {
                    const uint64_t featureRandom = hash(_seed + 1900, cellX, cellZ);
                    const int32_t treeX = cellX * featureSpacing + 2 +
                        static_cast<int32_t>(featureRandom % 6ULL);
                    const int32_t treeZ = cellZ * featureSpacing + 2 +
                        static_cast<int32_t>((featureRandom >> 8) % 6ULL);
                    const int32_t localX = treeX - worldX;
                    const int32_t localZ = treeZ - worldZ;
                    if (localX < 0 || localX >= CHUNK_WIDTH ||
                        localZ < 0 || localZ >= CHUNK_LENGTH)
                        continue;
                    const columnSample& column = columns[columnIndex(localX, localZ)];
                    const TERRAIN_BIOME biome = column.biome._biome;
                    const terrainBiomeGenerationDefinition& definition =
                        _biomes.definitions[static_cast<size_t>(biome)].generation;
                    if (definition.treeKind == "none" || definition.treeChanceNumerator == 0u ||
                        ((featureRandom >> 16) % definition.treeChanceDenominator) >=
                            definition.treeChanceNumerator)
                        continue;
                    std::string_view kind = definition.treeKind;
                    blockId logId = definition.treeLogBlock;
                    blockId leafId = definition.treeLeavesBlock;
                    if (definition.alternateTreeKind != "none" &&
                        definition.alternateChanceNumerator > 0u &&
                        ((featureRandom >> 24) % definition.alternateChanceDenominator) <
                            definition.alternateChanceNumerator) {
                        kind = definition.alternateTreeKind;
						logId = definition.alternateTreeLogBlock;
						leafId = definition.alternateTreeLeavesBlock;
					}
					const uint32_t variantCount = definition.treeVariants;
					const uint32_t variant = static_cast<uint32_t>((featureRandom >> 30) & 3ULL) %
						variantCount;
					if (column.riverWeight > setting("treeRiverLimit") ||
						column.lakeWeight > setting("treeLakeLimit") ||
						column.surfaceHeight <= _seaLevel + 1) continue;

                    const int32_t searchTop =
                        static_cast<int32_t>(std::ceil(column.surfaceHeight)) + 2;
                    const int32_t groundY = findTreeGround(
                        blocks, worldX, worldZ, treeX, treeZ, searchTop);
                    if (groundY < 0) continue;
                    // No trees on snowcaps / rocky alpine bands.
                    const float snowLine = snowLineY(column.biome._temperature);
                    if (static_cast<float>(groundY) >= snowLine - 4.0f) continue;
                    if (definition.alpineSurface &&
                        static_cast<float>(groundY) >= snowLine - definition.alpineRockDepth - 2.0f)
                        continue;

                    int32_t height = 5;
                    if (kind == "birch") {
                        height = 6 + static_cast<int32_t>((featureRandom >> 20) % 4ULL);
                    }
                    else if (kind == "spruce") {
                        height = 7 + static_cast<int32_t>((featureRandom >> 20) % 6ULL);
                    }
                    else if (kind == "jungle") {
                        height = 9 + static_cast<int32_t>((featureRandom >> 20) % 6ULL);
                    }
                    else if (kind == "acacia") {
                        height = 5 + static_cast<int32_t>((featureRandom >> 20) % 3ULL);
                    }
                    else if (kind == "dark_oak") {
                        height = 4 + static_cast<int32_t>((featureRandom >> 20) % 2ULL);
                    }
                    else {
                        height = 4 + static_cast<int32_t>((featureRandom >> 20) % 4ULL);
                    }
					if (variant == 1u) height += 2;
					else if (variant == 2u) height += (kind == "spruce" || kind == "jungle") ? 3 : 1;
					else if (variant == 3u && kind == "acacia") height += 2;
                    if (groundY + height + 4 >= CHUNK_HEIGHT) continue;

                    const int32_t trunkTop = groundY + height;
                    const bool thickJungle = kind == "jungle" && variant == 3u;
                    const bool darkOakTrunk = kind == "dark_oak";
                    if (darkOakTrunk) {
                        bool stableFootprint = true;
                        for (int32_t dx = 0; dx < 2 && stableFootprint; ++dx) {
                            for (int32_t dz = 0; dz < 2; ++dz) {
                                if (findTreeGround(
                                        blocks, worldX, worldZ, treeX + dx, treeZ + dz, searchTop) != groundY) {
                                    stableFootprint = false;
                                    break;
                                }
                            }
                        }
                        if (!stableFootprint)
                            continue;
                    }
                    for (int32_t y = groundY + 1; y <= trunkTop; ++y) {
                        if (darkOakTrunk) {
                            setIfLocal(treeX, y, treeZ, logId);
                            setIfLocal(treeX + 1, y, treeZ, logId);
                            setIfLocal(treeX, y, treeZ + 1, logId);
                            setIfLocal(treeX + 1, y, treeZ + 1, logId);
                        }
                        else {
                            setIfLocal(treeX, y, treeZ, logId);
                            if (thickJungle) {
                                setIfLocal(treeX + 1, y, treeZ, logId);
                                setIfLocal(treeX, y, treeZ + 1, logId);
                                setIfLocal(treeX + 1, y, treeZ + 1, logId);
                            }
                        }
                    }

					const int32_t branchX = ((featureRandom >> 13) & 1ULL) != 0ULL ? 1 : -1;
					const int32_t branchZ = ((featureRandom >> 14) & 1ULL) != 0ULL ? 1 : -1;

                    if (kind == "spruce") {
						const int32_t layers = variant == 1u ? 5 : (variant == 2u ? 6 : 4);
						const int32_t baseRadius = variant == 1u ? 2 : (variant == 2u ? 4 : 3);
                        for (int32_t layer = 0; layer < layers; ++layer) {
							const int32_t radius = std::max(1, baseRadius - layer / 2);
                            const int32_t y = trunkTop - (layers - 1 - layer);
                            for (int32_t dz = -radius; dz <= radius; ++dz) {
                                for (int32_t dx = -radius; dx <= radius; ++dx) {
                                    if (std::abs(dx) == radius && std::abs(dz) == radius) continue;
                                    setIfLocal(treeX + dx, y, treeZ + dz, leafId, true);
                                }
                            }
                        }
                        setIfLocal(treeX, trunkTop + 1, treeZ, leafId, true);
                        setIfLocal(treeX, trunkTop + 2, treeZ, leafId, true);
						if (variant == 3u) {
							setIfLocal(treeX + branchX, trunkTop - 3, treeZ, logId);
							setIfLocal(treeX + branchX * 2, trunkTop - 2, treeZ, logId);
							placeSquareCanopyLayer(treeX + branchX * 2, trunkTop - 1, treeZ, 2, leafId);
						}
                    }
                    else if (kind == "acacia") {
						const int32_t reach = variant == 0u ? 0 : (variant == 3u ? 3 : 2);
						const int32_t canopyX = treeX + branchX * reach;
						const int32_t canopyZ = treeZ + (variant == 2u ? 0 : branchZ * reach);
						for (int32_t step = 1; step <= reach; ++step)
							setIfLocal(treeX + branchX * step, trunkTop - reach + step,
								treeZ + (variant == 2u ? 0 : branchZ * step), logId);
						const int32_t radius = variant == 1u ? 4 : 3;
                        for (int32_t y = trunkTop - 1; y <= trunkTop + 1; ++y) {
                            const int32_t r = y == trunkTop ? radius : radius - 1;
                            for (int32_t dz = -r; dz <= r; ++dz) {
                                for (int32_t dx = -r; dx <= r; ++dx) {
                                    if (dx * dx + dz * dz > r * r) continue;
									setIfLocal(canopyX + dx, y, canopyZ + dz, leafId, true);
                                }
                            }
                        }
						if (variant == 2u) {
							setIfLocal(treeX - branchX, trunkTop - 2, treeZ + branchZ, logId);
							setIfLocal(treeX - branchX * 2, trunkTop - 1, treeZ + branchZ * 2, logId);
							placeSquareCanopyLayer(treeX - branchX * 2, trunkTop,
								treeZ + branchZ * 2, 2, leafId);
						}
                    }
                    else if (kind == "jungle") {
						const int32_t canopyRadius = variant == 1u ? 2 : (variant == 2u ? 4 : 3);
						const std::pair<int32_t, int32_t> jungleLayers[] = {
							{ 2, std::max(1, canopyRadius - 1) }, { 1, canopyRadius },
							{ 0, canopyRadius }, { -1, std::max(1, canopyRadius - 1) }, { -2, 1 }
						};
                        const int32_t canopyX = thickJungle ? treeX + 1 : treeX;
                        const int32_t canopyZ = thickJungle ? treeZ + 1 : treeZ;
                        placeLayeredCanopy(canopyX, canopyZ, trunkTop, leafId, jungleLayers, 5);
						if (variant >= 2u) {
							for (int32_t step = 1; step <= 2; ++step)
								setIfLocal(treeX + branchX * step, trunkTop - 4 + step,
									treeZ + branchZ * step, logId);
							placeSquareCanopyLayer(treeX + branchX * 2, trunkTop - 1,
								treeZ + branchZ * 2, variant == 2u ? 2 : 1, leafId);
						}
                    }
                    else if (kind == "dark_oak") {
						const int32_t crownRadius = variant == 1u ? 4 : 3;
						const std::pair<int32_t, int32_t> darkOakLayers[] = {
							{ 1, variant == 2u ? 3 : 2 }, { 0, crownRadius }, { -1, crownRadius },
							{ -2, std::max(2, crownRadius - 1) }, { -3, 1 }
						};
                        placeLayeredCanopy(treeX + 1, treeZ + 1, trunkTop, leafId, darkOakLayers, 5);
						if (variant >= 2u) {
							setIfLocal(treeX + 1 + branchX, trunkTop - 2, treeZ + 1, logId);
							setIfLocal(treeX + 1 + branchX * 2, trunkTop - 1, treeZ + 1, logId);
							placeSquareCanopyLayer(treeX + 1 + branchX * 2, trunkTop,
								treeZ + 1, 2, leafId);
						}
                    }
                    else if (kind == "birch") {
						const int32_t radius = variant == 2u ? 3 : 2;
						const std::pair<int32_t, int32_t> birchLayers[] = {
							{ 1, variant == 1u ? 1 : 2 }, { 0, radius }, { -1, radius }, { -2, 1 }
						};
						placeLayeredCanopy(treeX, treeZ, trunkTop, leafId, birchLayers, 4);
						if (variant == 3u) {
							setIfLocal(treeX + branchX, trunkTop - 2, treeZ + branchZ, logId);
							placeSquareCanopyLayer(treeX + branchX, trunkTop - 1,
								treeZ + branchZ, 1, leafId);
						}
                    }
                    else {
						const int32_t oakRadius = variant == 1u ? 3 : 2;
						const std::pair<int32_t, int32_t> oakLayers[] = {
							{ 1, variant == 2u ? 2 : 1 }, { 0, oakRadius },
							{ -1, oakRadius }, { -2, variant == 1u ? 2 : 1 }
						};
                        placeLayeredCanopy(treeX, treeZ, trunkTop, leafId, oakLayers, 4);
						if (variant >= 2u) {
							const int32_t branchCount = variant == 3u ? 2 : 1;
							for (int32_t branch = 0; branch < branchCount; ++branch) {
								const int32_t bx = branch == 0 ? branchX : -branchX;
								const int32_t bz = branch == 0 ? branchZ : -branchZ;
								setIfLocal(treeX + bx, trunkTop - 2 - branch, treeZ + bz, logId);
								setIfLocal(treeX + bx * 2, trunkTop - 1 - branch, treeZ + bz * 2, logId);
								placeSquareCanopyLayer(treeX + bx * 2, trunkTop - branch,
									treeZ + bz * 2, 1, leafId);
							}
                        }
                    }
                }
            }
        }
    public:
        float freeWaterSurface(const columnSample& column) const {
            return columnWater(column).surface;
        }

        terrainGenerator(uint64_t seed, terrainBlockPalette blocks, terrainBiomeConfig biomes)
            : _seed(seed), _blocks(std::move(blocks)), _biomes(std::move(biomes)) {}

        uint64_t seed() const { return _seed; }

        biomeSample sampleBiome(int32_t worldX, int32_t worldZ) const {
            return sampleColumn(worldX, worldZ).biome;
        }

        float peekSurfaceHeight(int32_t worldX, int32_t worldZ) const {
            return sampleColumn(worldX, worldZ).surfaceHeight;
        }

        bool isLandSpawnColumn(int32_t worldX, int32_t worldZ) const {
            const columnSample column = sampleColumn(worldX, worldZ);
            const std::string& biomeWater =
                _biomes.definitions[column.biome._biome].generation.waterKind;
            if (biomeWater == "ocean" || biomeWater == "lake")
                return false;
            if (column.oceanWeight > setting("spawnOceanLimit") ||
                column.lakeWeight > setting("spawnLakeLimit"))
                return false;
            return column.surfaceHeight >= static_cast<float>(_seaLevel) + 3.0f;
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

            std::array<waterColumn, CHUNK_WIDTH * CHUNK_LENGTH> waters{};
            for (int32_t z = 0; z < CHUNK_LENGTH; ++z) {
                for (int32_t x = 0; x < CHUNK_WIDTH; ++x) {
                    waters[columnIndex(x, z)] =
                        columnWater(columns[columnIndex(x, z)]);
                }
            }
            // Cache one-ring outside columns once instead of resampling them
            // for every perimeter neighbor lookup during water spread.
            std::array<waterColumn, CHUNK_WIDTH> northRim{};
            std::array<waterColumn, CHUNK_WIDTH> southRim{};
            std::array<waterColumn, CHUNK_LENGTH> westRim{};
            std::array<waterColumn, CHUNK_LENGTH> eastRim{};
            for (int32_t x = 0; x < CHUNK_WIDTH; ++x) {
                northRim[static_cast<size_t>(x)] =
                    columnWater(sampleColumn(worldX + x, worldZ - 1));
                southRim[static_cast<size_t>(x)] =
                    columnWater(sampleColumn(worldX + x, worldZ + CHUNK_LENGTH));
            }
            for (int32_t z = 0; z < CHUNK_LENGTH; ++z) {
                westRim[static_cast<size_t>(z)] =
                    columnWater(sampleColumn(worldX - 1, worldZ + z));
                eastRim[static_cast<size_t>(z)] =
                    columnWater(sampleColumn(worldX + CHUNK_WIDTH, worldZ + z));
            }
            const waterColumn northWest = columnWater(sampleColumn(worldX - 1, worldZ - 1));
            const waterColumn northEast = columnWater(sampleColumn(worldX + CHUNK_WIDTH, worldZ - 1));
            const waterColumn southWest = columnWater(sampleColumn(worldX - 1, worldZ + CHUNK_LENGTH));
            const waterColumn southEast = columnWater(sampleColumn(worldX + CHUNK_WIDTH, worldZ + CHUNK_LENGTH));

            std::array<waterColumn, CHUNK_WIDTH * CHUNK_LENGTH> spreadWaters = waters;
            for (int32_t z = 0; z < CHUNK_LENGTH; ++z) {
                for (int32_t x = 0; x < CHUNK_WIDTH; ++x) {
                    waterColumn surface = waters[columnIndex(x, z)];
                    for (int32_t dz = -1; dz <= 1; ++dz) {
                        for (int32_t dx = -1; dx <= 1; ++dx) {
                            if (dx == 0 && dz == 0) continue;
                            const int32_t nx = x + dx;
                            const int32_t nz = z + dz;
                            if (nx >= 0 && nx < CHUNK_WIDTH && nz >= 0 && nz < CHUNK_LENGTH) {
                                surface = mergeWater(surface, waters[columnIndex(nx, nz)]);
                                continue;
                            }
                            waterColumn neighbor = surface;
                            if (nx < 0 && nz < 0) neighbor = northWest;
                            else if (nx >= CHUNK_WIDTH && nz < 0) neighbor = northEast;
                            else if (nx < 0 && nz >= CHUNK_LENGTH) neighbor = southWest;
                            else if (nx >= CHUNK_WIDTH && nz >= CHUNK_LENGTH) neighbor = southEast;
                            else if (nz < 0) neighbor = northRim[static_cast<size_t>(nx)];
                            else if (nz >= CHUNK_LENGTH) neighbor = southRim[static_cast<size_t>(nx)];
                            else if (nx < 0) neighbor = westRim[static_cast<size_t>(nz)];
                            else neighbor = eastRim[static_cast<size_t>(nz)];
                            surface = mergeWater(surface, neighbor);
                        }
                    }
                    spreadWaters[columnIndex(x, z)] = surface;
                }
            }

            for (int32_t y = 0; y < CHUNK_HEIGHT; ++y) {
                for (int32_t z = 0; z < CHUNK_LENGTH; ++z) {
                    for (int32_t x = 0; x < CHUNK_WIDTH; ++x) {
                        blockId block = _blocks.AIR_BLOCK;
                        if (interpolatedDensity(density, x, y, z) > 0.0f) {
                            block = roleBlock("stone");
                        }
                        else {
                            // Ocean/river/lake basins fill every generated void
                            // beneath their free surface. Neighboring columns contribute
                            // their surface so shore and cave mouths are not left dry.
                            const float waterSurface = spreadWaters[columnIndex(x, z)].surface;
                            const float fill = waterSurface - static_cast<float>(y);
                            if (fill >= 1.0f) {
                                block = withFluidLevel(roleBlock("water"), MAX_FLUID_LEVEL);
                            }
                            else if (fill > 0.0f) {
                                const uint8_t level = static_cast<uint8_t>(std::clamp(
                                    static_cast<int32_t>(std::ceil(fill * MAX_FLUID_LEVEL)),
                                    1,
                                    static_cast<int32_t>(MAX_FLUID_LEVEL)));
                                block = withFluidLevel(roleBlock("water"), level);
                            }
                        }
                        blocks[blockIndex(x, y, z)] = block;
                    }
                }
            }

            applySurfaceRules(worldX, worldZ, columns, spreadWaters, blocks);
            applyUndergroundRules(worldX, worldZ, blocks);
            placeTrees(worldX, worldZ, columns, blocks);
            waterPhysics::fillConnectedBasinWater(blocks.data(), roleBlock("water"));
            c.buildFromDense(blocks);
            c._dirty = true;
        }
    };


}
