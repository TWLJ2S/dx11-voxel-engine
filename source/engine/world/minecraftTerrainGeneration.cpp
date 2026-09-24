#include "minecraftTerrainGeneration.h"

namespace {
    const rapidjson::Value& requireObjectMember(
        const rapidjson::Value& object, const char* name, const std::string& source
    ) {
        if (!object.HasMember(name) || !object[name].IsObject())
            throw std::runtime_error(source + ": field '" + name + "' must be an object");
        return object[name];
    }

    std::string requireString(
        const rapidjson::Value& object, const char* name, const std::string& source
    ) {
        if (!object.HasMember(name) || !object[name].IsString())
            throw std::runtime_error(source + ": field '" + name + "' must be a string");
        return object[name].GetString();
    }

    uint32_t requireUint(
        const rapidjson::Value& object, const char* name, const std::string& source
    ) {
        if (!object.HasMember(name) || !object[name].IsUint())
            throw std::runtime_error(source + ": field '" + name + "' must be an unsigned integer");
        return object[name].GetUint();
    }

    void readChance(
        const rapidjson::Value& object,
        const char* field,
        uint32_t& numerator,
        uint32_t& denominator,
        const std::string& source
    ) {
        if (!object.HasMember(field)) return;
        const rapidjson::Value& value = object[field];
        if (!value.IsArray() || value.Size() != 2u || !value[0].IsUint() || !value[1].IsUint() ||
            value[1].GetUint() == 0u || value[0].GetUint() > value[1].GetUint())
            throw std::runtime_error(source + ": field '" + field + "' must be [numerator, denominator]");
        numerator = value[0].GetUint();
        denominator = value[1].GetUint();
    }
}

namespace ac {
    terrainBiomeConfig terrainBiomeConfig::load(
        const std::filesystem::path& path,
        const staticAssetManager& registry
    ) {
        const rapidjson::Document document = jsonLoader::load(path.string());
        if (!document.IsObject() || !document.HasMember("biomes") || !document["biomes"].IsArray())
            throw std::runtime_error(path.string() + ": terrain biome config requires a 'biomes' array");
        if (document.MemberCount() != 1u)
            throw std::runtime_error(path.string() + ": unknown top-level biome config field");
        const rapidjson::Value& biomes = document["biomes"];
        if (biomes.Empty())
            throw std::runtime_error(path.string() + ": expected at least one biome");

        terrainBiomeConfig result;
        result.definitions.resize(biomes.Size());
        std::unordered_set<std::string> names;
        for (rapidjson::SizeType itemIndex = 0; itemIndex < biomes.Size(); ++itemIndex) {
            const rapidjson::Value& value = biomes[itemIndex];
            const std::string source = path.string() + ".biomes[" + std::to_string(itemIndex) + "]";
            if (!value.IsObject()) throw std::runtime_error(source + ": biome must be an object");
            // IDs are assigned by document order.  This keeps saves and
            // generated terrain stable for a given pack while allowing packs
            // to add/reorder definitions without recompiling the engine.
            const uint32_t id = static_cast<uint32_t>(itemIndex);
            terrainBiomeDefinition& definition = result.definitions[id];
            definition.name = requireString(value, "name", source);
            definition.displayName = requireString(value, "displayName", source);
            if (!names.insert(definition.name).second)
                throw std::runtime_error(source + ": duplicate biome name '" + definition.name + "'");
            auto addId = [&](const std::string& name) {
                std::string lower = name;
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                if (!result.ids.emplace(lower, id).second)
                    throw std::runtime_error(source + ": duplicate biome alias '" + name + "'");
            };
            addId(definition.name);
            if (value.HasMember("aliases")) {
                if (!value["aliases"].IsArray())
                    throw std::runtime_error(source + ": field 'aliases' must be an array");
                for (const rapidjson::Value& alias : value["aliases"].GetArray()) {
                    if (!alias.IsString()) throw std::runtime_error(source + ": aliases must be strings");
                    definition.aliases.emplace_back(alias.GetString());
                    addId(definition.aliases.back());
                }
            }

            terrainBiomeGenerationDefinition& generation = definition.generation;
            generation.waterKind = requireString(value, "water", source);
            const rapidjson::Value& surface = requireObjectMember(value, "surface", source);
            generation.topBlock = registry.getId(requireString(surface, "top", source));
            generation.underBlock = registry.getId(requireString(surface, "under", source));
            generation.soilDepth = requireUint(surface, "soilDepth", source);
            generation.floodedTopBlock = generation.topBlock;
            generation.floodedUnderBlock = generation.underBlock;
            generation.floodedSoilDepth = generation.soilDepth;
            if (surface.HasMember("floodedTop")) {
                generation.floodedTopBlock = registry.getId(requireString(surface, "floodedTop", source));
                generation.floodedSurface = true;
            }
            if (surface.HasMember("floodedUnder"))
                generation.floodedUnderBlock = registry.getId(requireString(surface, "floodedUnder", source));
            if (surface.HasMember("floodedSoilDepth"))
                generation.floodedSoilDepth = requireUint(surface, "floodedSoilDepth", source);
            auto readBool = [&](const rapidjson::Value& object, const char* name, bool& target) {
                if (!object.HasMember(name)) return;
                if (!object[name].IsBool())
                    throw std::runtime_error(source + ": field '" + name + "' must be boolean");
                target = object[name].GetBool();
            };
            readBool(surface, "globalSnow", generation.globalSnow);
            readBool(surface, "alpineSurface", generation.alpineSurface);
            readBool(surface, "highlandRock", generation.highlandRock);
            if (surface.HasMember("alpineRockDepth")) {
                if (!surface["alpineRockDepth"].IsNumber()) throw std::runtime_error(source + ": alpineRockDepth must be numeric");
                generation.alpineRockDepth = surface["alpineRockDepth"].GetFloat();
            }
            if (surface.HasMember("highlandRockDepth")) {
                if (!surface["highlandRockDepth"].IsNumber()) throw std::runtime_error(source + ": highlandRockDepth must be numeric");
                generation.highlandRockDepth = surface["highlandRockDepth"].GetFloat();
            }

            if (value.HasMember("tree")) {
                const rapidjson::Value& tree = requireObjectMember(value, "tree", source);
                generation.treeKind = requireString(tree, "kind", source);
                generation.treeLogBlock = registry.getId(requireString(tree, "log", source));
                generation.treeLeavesBlock = registry.getId(requireString(tree, "leaves", source));
                readChance(tree, "chance", generation.treeChanceNumerator, generation.treeChanceDenominator, source);
                if (tree.HasMember("alternateKind")) {
                    generation.alternateTreeKind = requireString(tree, "alternateKind", source);
                    generation.alternateTreeLogBlock = registry.getId(requireString(tree, "alternateLog", source));
                    generation.alternateTreeLeavesBlock = registry.getId(requireString(tree, "alternateLeaves", source));
                }
                readChance(tree, "alternateChance", generation.alternateChanceNumerator, generation.alternateChanceDenominator, source);
				if (tree.HasMember("variants")) {
					if (!tree["variants"].IsUint() || tree["variants"].GetUint() < 1u ||
						tree["variants"].GetUint() > 4u)
						throw std::runtime_error(source + ": tree field 'variants' must be between 1 and 4");
					generation.treeVariants = tree["variants"].GetUint();
				}
            }

            if (value.HasMember("weather")) {
                const rapidjson::Value& weather = requireObjectMember(value, "weather", source);
                readBool(weather, "dry", generation.dryWeather);
                readBool(weather, "cold", generation.coldWeather);
            }

            if (!value.HasMember("climate") || !value["climate"].IsArray() || value["climate"].Empty())
                throw std::runtime_error(source + ": field 'climate' must be a non-empty selector array");
            for (rapidjson::SizeType selectorIndex = 0; selectorIndex < value["climate"].Size(); ++selectorIndex) {
                const rapidjson::Value& selectorValue = value["climate"][selectorIndex];
                if (!selectorValue.IsObject() || !selectorValue.HasMember("ranges") || !selectorValue["ranges"].IsObject())
                    throw std::runtime_error(source + ": each climate selector requires a 'ranges' object");
                terrainClimateSelector selector;
                if (selectorValue.HasMember("weight")) {
                    if (!selectorValue["weight"].IsNumber() || selectorValue["weight"].GetFloat() < 0.0f)
                        throw std::runtime_error(source + ": climate selector weight must be non-negative");
                    selector.weight = selectorValue["weight"].GetFloat();
                }
                const rapidjson::Value& ranges = selectorValue["ranges"];
                for (auto member = ranges.MemberBegin(); member != ranges.MemberEnd(); ++member) {
                    const std::string metric(member->name.GetString(), member->name.GetStringLength());
                    static const std::unordered_set<std::string> supportedMetrics = {
                        "height", "mountain", "ocean", "lake", "river", "temperature",
                        "moisture", "continentalness", "erosion", "weirdness", "highland",
                        "alpine", "arid"
                    };
                    if (!supportedMetrics.contains(metric))
                        throw std::runtime_error(source + ": unknown climate metric '" + metric + "'");
                    const rapidjson::Value& rangeValue = member->value;
                    if (!rangeValue.IsArray() || (rangeValue.Size() != 2u && rangeValue.Size() != 3u) ||
                        !rangeValue[0].IsNumber() || !rangeValue[1].IsNumber() ||
                        (rangeValue.Size() == 3u && !rangeValue[2].IsNumber()))
                        throw std::runtime_error(source + ": climate range '" + metric + "' must be [min,max,falloff]");
                    terrainClimateRange range;
                    range.minimum = rangeValue[0].GetFloat();
                    range.maximum = rangeValue[1].GetFloat();
                    range.falloff = rangeValue.Size() == 3u ? rangeValue[2].GetFloat() : 0.0f;
                    if (range.minimum > range.maximum || range.falloff < 0.0f)
                        throw std::runtime_error(source + ": invalid climate range '" + metric + "'");
                    selector.ranges.emplace(metric, range);
                }
                generation.climateSelectors.push_back(std::move(selector));
            }
        }
        return result;
    }
}
