#pragma once

#include <assets/cpuAsset.h>
#include <world/itemEntities.h>
#include <core/applicationConfig.h>

#include <rapidjson/document.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ac {

	enum class gameMode : uint8_t {
		survival = 0,
		creative = 1,
		spectator = 2
	};

	inline const float PLAYER_MAX_HEALTH = appConfig().playerVitals.maximumHealth;
	inline const float PLAYER_MAX_HUNGER = appConfig().playerVitals.maximumHunger;
	inline const float PLAYER_MAX_SATURATION = appConfig().playerVitals.maximumSaturation;
	inline const float PLAYER_MAX_AIR = appConfig().playerVitals.maximumAir;
	inline const float PLAYER_FALL_SAFE = appConfig().playerVitals.safeFallDistance;
	inline const float PLAYER_HURT_COOLDOWN = appConfig().playerVitals.hurtCooldown;
	inline const float PLAYER_REGEN_SATURATED = appConfig().playerVitals.saturatedRegenSeconds;
	inline const float PLAYER_REGEN_HUNGRY = appConfig().playerVitals.hungryRegenSeconds;
	inline const float PLAYER_EAT_SECONDS = appConfig().playerVitals.eatSeconds;
	inline const float PLAYER_EXHAUSTION_UNIT = appConfig().playerVitals.exhaustionUnit;

	inline float fallDamageFromDistance(float fallenBlocks) {
		return (std::max)(0.0f, std::floor(fallenBlocks - PLAYER_FALL_SAFE));
	}

	inline float fallingBlockDamage(float fallenBlocks, bool heavy) {
		if (fallenBlocks < 2.0f) return 0.0f;
		const float damage = heavy
			? std::floor(fallenBlocks * 2.0f)
			: std::floor(fallenBlocks - 1.0f);
		return std::clamp(damage, 0.0f, 40.0f);
	}

	inline blockId BLOCK_CRAFTING_TABLE = 0;

	inline bool isNonPlaceableItem(const blockDefinition* definition) {
		return definition && definition->_item;
	}

	inline int toolHarvestLevel(const blockDefinition* held) {
		return held ? held->_toolHarvestLevel : -1;
	}

	inline blockToolClass toolClassOf(const blockDefinition* held) {
		if (!held) return blockToolClass::none;
		return held->_preferredTool;
	}

	inline float toolMiningSpeed(const blockDefinition* held) {
		if (!held || !held->isMiningTool()) return 1.0f;
		return (std::max)(1.0f, held->_miningSpeed);
	}

	inline bool isCorrectTool(const blockDefinition* block, const blockDefinition* held) {
		if (!block || block->_preferredTool == blockToolClass::none) return false;
		return held && held->isMiningTool() && held->_preferredTool == block->_preferredTool;
	}

	inline bool canHarvestDrop(
		const blockDefinition* block,
		const blockDefinition* held,
		bool creative
	) {
		// Creative breaking removes blocks directly; it never creates a world
		// item entity, regardless of tool or harvest level.
		if (creative) return false;
		if (!block) return false;
		if (block->_hardness < 0.0f) return false;
		if (!block->_requiresTool) return true;
		return isCorrectTool(block, held) && toolHarvestLevel(held) >= block->_harvestLevel;
	}

	inline float blockHardness(const blockDefinition* block) {
		if (!block) return 1.0f;
		return block->_hardness;
	}

	inline float miningSeconds(
		const blockDefinition* block,
		const blockDefinition* held,
		bool creative
	) {
		if (creative) return 0.0f;
		if (!block) return 1.0e9f;
		const float hardness = block->_hardness;
		if (hardness < 0.0f) return 1.0e9f;
		if (hardness <= 0.0f) return 0.05f;
		float speed = 1.0f;
		if (isCorrectTool(block, held))
			speed = toolMiningSpeed(held);
		if (!canHarvestDrop(block, held, false))
			speed /= 3.33f;
		return (std::max)(0.05f, hardness * 1.5f / (std::max)(0.05f, speed));
	}

	inline float armorDamageReduction(const blockDefinition* piece) {
		return piece && piece->_item ? piece->_armorReduction : 0.0f;
	}

	struct recipeIngredient {
		enum class kind : uint8_t { empty, item, type };
		kind match = kind::empty;
		blockId item = 0;
		std::string type;
	};

	struct craftRecipe {
		bool unordered = false;
		bool rotate = false;
		bool mirror = true;
		int width = 0;
		int height = 0;
		std::vector<recipeIngredient> pattern;
		std::vector<recipeIngredient> ingredients;
		recipeIngredient result;
		uint32_t resultCount = 1;
	};

	struct craftMatch {
		const craftRecipe* recipe = nullptr;
		blockId result = 0;
		uint32_t resultCount = 0;
		explicit operator bool() const { return recipe != nullptr && result != 0; }
	};

	struct smeltingRecipe {
		blockId input = 0;
		blockId result = 0;
		uint32_t resultCount = 1;
		float seconds = 10.0f;
	};

	struct fuelDefinition {
		recipeIngredient ingredient;
		float seconds = 0.0f;
	};

	class recipeBook {
	public:
		bool load(
			const std::filesystem::path& path,
			const std::function<std::optional<blockId>(const std::string&)>& resolve,
			const std::function<void(const std::function<void(blockId, const std::string&)>&)>& forEachBlock,
			bool append = false
		) {
			if (!append) {
				_recipes.clear();
				_smelting.clear();
				_fuels.clear();
				_names.clear();
				_ids.clear();
				_types.clear();
			}
			if (forEachBlock) {
				forEachBlock([&](blockId id, const std::string& name) {
					_names[id] = name;
					_ids[name] = id;
					registerItemTypes(id, name);
				});
			}

			std::ifstream file(path, std::ios::binary);
			if (!file) return false;
			file.seekg(0, std::ios::end);
			const auto size = file.tellg();
			if (size <= 0) return false;
			file.seekg(0, std::ios::beg);
			std::string content(static_cast<size_t>(size), '\0');
			if (!file.read(content.data(), size)) return false;
			rapidjson::Document doc;
			doc.Parse(content.data(), content.size());
			if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("recipes") ||
				!doc["recipes"].IsArray())
				return false;

			if (doc.HasMember("types") && doc["types"].IsObject()) {
				for (auto it = doc["types"].MemberBegin(); it != doc["types"].MemberEnd(); ++it) {
					if (!it->value.IsArray()) continue;
					auto& members = _types[it->name.GetString()];
					for (const auto& entry : it->value.GetArray()) {
						if (!entry.IsString()) continue;
						const auto id = resolveItem(entry.GetString(), resolve);
						if (id) members.insert(*id);
					}
				}
			}

			for (const auto& entry : doc["recipes"].GetArray()) {
				if (!entry.IsObject()) continue;
				craftRecipe recipe{};
				const bool hasPattern = entry.HasMember("pattern") && entry["pattern"].IsArray();
				const std::string typeName = entry.HasMember("type") && entry["type"].IsString()
					? entry["type"].GetString() : "";
				recipe.unordered = entry.HasMember("unordered") && entry["unordered"].IsBool()
					? entry["unordered"].GetBool()
					: (!hasPattern || typeName == "shapeless");
				recipe.rotate = optionalBool(entry, "rotate", false);
				recipe.mirror = optionalBool(entry, "mirror", !recipe.unordered);
				if (!entry.HasMember("result") || !entry["result"].IsString()) continue;
				recipe.result = parseIngredient(entry["result"].GetString(), resolve);
				if (recipe.result.match == recipeIngredient::kind::empty) continue;
				recipe.resultCount = entry.HasMember("count") && entry["count"].IsUint()
					? (std::max)(1u, entry["count"].GetUint()) : 1u;

				if (!recipe.unordered) {
					if (!hasPattern) continue;
					std::unordered_map<char, recipeIngredient> key;
					if (entry.HasMember("key") && entry["key"].IsObject()) {
						for (auto it = entry["key"].MemberBegin(); it != entry["key"].MemberEnd(); ++it) {
							if (it->name.GetStringLength() != 1 || !it->value.IsString()) continue;
							key[it->name.GetString()[0]] = parseIngredient(it->value.GetString(), resolve);
						}
					}
					const auto& rows = entry["pattern"].GetArray();
					recipe.height = static_cast<int>(rows.Size());
					recipe.width = 0;
					for (const auto& row : rows) {
						if (!row.IsString()) continue;
						recipe.width = (std::max)(recipe.width, static_cast<int>(row.GetStringLength()));
					}
					if (recipe.width <= 0 || recipe.height <= 0) continue;
					recipe.pattern.assign(static_cast<size_t>(recipe.width * recipe.height), {});
					int y = 0;
					for (const auto& row : rows) {
						if (!row.IsString()) { ++y; continue; }
						const std::string line = row.GetString();
						for (int x = 0; x < recipe.width; ++x) {
							const char c = x < static_cast<int>(line.size()) ? line[x] : ' ';
							if (c != ' ' && key.count(c))
								recipe.pattern[static_cast<size_t>(y * recipe.width + x)] = key[c];
						}
						++y;
					}
				}
				else {
					if (!entry.HasMember("ingredients") || !entry["ingredients"].IsArray()) continue;
					for (const auto& ingredient : entry["ingredients"].GetArray()) {
						if (!ingredient.IsString()) continue;
						auto spec = parseIngredient(ingredient.GetString(), resolve);
						if (spec.match != recipeIngredient::kind::empty)
							recipe.ingredients.push_back(std::move(spec));
					}
					if (recipe.ingredients.empty()) continue;
				}
				_recipes.push_back(std::move(recipe));
			}

			if (doc.HasMember("smelting") && doc["smelting"].IsArray()) {
				for (const auto& entry : doc["smelting"].GetArray()) {
					if (!entry.IsObject() || !entry.HasMember("input") ||
						!entry["input"].IsString() || !entry.HasMember("result") ||
						!entry["result"].IsString()) continue;
					const auto input = resolveItem(entry["input"].GetString(), resolve);
					const auto result = resolveItem(entry["result"].GetString(), resolve);
					if (!input || !result || *input == 0 || *result == 0) continue;
					smeltingRecipe recipe;
					recipe.input = *input;
					recipe.result = *result;
					recipe.resultCount = entry.HasMember("count") && entry["count"].IsUint()
						? (std::max)(1u, entry["count"].GetUint()) : 1u;
					recipe.seconds = entry.HasMember("seconds") && entry["seconds"].IsNumber()
						? (std::max)(0.05f, entry["seconds"].GetFloat()) : 10.0f;
					_smelting[recipe.input] = recipe;
				}
			}

			if (doc.HasMember("fuels") && doc["fuels"].IsArray()) {
				for (const auto& entry : doc["fuels"].GetArray()) {
					if (!entry.IsObject() || !entry.HasMember("item") ||
						!entry["item"].IsString() || !entry.HasMember("seconds") ||
						!entry["seconds"].IsNumber()) continue;
					fuelDefinition fuel;
					fuel.ingredient = parseIngredient(entry["item"].GetString(), resolve);
					fuel.seconds = (std::max)(0.0f, entry["seconds"].GetFloat());
					if (fuel.ingredient.match != recipeIngredient::kind::empty && fuel.seconds > 0.0f)
						_fuels.push_back(std::move(fuel));
				}
			}
			return !_recipes.empty() || !_smelting.empty();
		}

		craftMatch match(const blockId* grid, int width, int height) const {
			for (const craftRecipe& recipe : _recipes) {
				std::string variant;
				if (recipe.unordered) {
					if (matchUnordered(recipe, grid, width, height, variant))
						return finishMatch(recipe, variant);
				}
				else if (matchShaped(recipe, grid, width, height, variant)) {
					return finishMatch(recipe, variant);
				}
			}
			return {};
		}

		const std::vector<craftRecipe>& recipes() const { return _recipes; }

		const smeltingRecipe* smelting(blockId input) const {
			const auto found = _smelting.find(input);
			return found == _smelting.end() ? nullptr : &found->second;
		}

		float fuelSeconds(blockId item) const {
			for (const fuelDefinition& fuel : _fuels)
				if (matches(fuel.ingredient, item)) return fuel.seconds;
			return 0.0f;
		}

	private:
		static bool optionalBool(const rapidjson::Value& entry, const char* name, bool fallback) {
			if (!entry.HasMember(name) || !entry[name].IsBool()) return fallback;
			return entry[name].GetBool();
		}

		static bool endsWith(const std::string& value, const char* suffix) {
			const size_t length = std::char_traits<char>::length(suffix);
			return value.size() >= length &&
				value.compare(value.size() - length, length, suffix) == 0;
		}

		static bool startsWith(const std::string& value, const char* prefix) {
			const size_t length = std::char_traits<char>::length(prefix);
			return value.size() >= length && value.compare(0, length, prefix) == 0;
		}

		static std::string itemVariant(std::string name) {
			if (startsWith(name, "stripped_"))
				name.erase(0, 9);
			static constexpr const char* suffixes[] = {
				"_pressure_plate", "_trapdoor", "_fence_gate", "_planks",
				"_stairs", "_button", "_sign", "_door", "_slab", "_fence",
				"_hyphae", "_stem", "_wood", "_log", "_wool"
			};
			for (const char* suffix : suffixes) {
				if (endsWith(name, suffix)) {
					name.erase(name.size() - std::char_traits<char>::length(suffix));
					break;
				}
			}
			return name;
		}

		static const char* resultSuffix(const std::string& type) {
			if (type == "plank") return "_planks";
			if (type == "log") return "_log";
			if (type == "wood") return "_wood";
			if (type == "slab") return "_slab";
			if (type == "stairs") return "_stairs";
			if (type == "door") return "_door";
			if (type == "fence") return "_fence";
			if (type == "wool") return "_wool";
			return nullptr;
		}

		void registerItemTypes(blockId id, const std::string& name) {
			auto add = [&](const char* type) { _types[type].insert(id); };
			if (name == "plank" || name == "planks" || endsWith(name, "_planks")) add("plank");
			if (name == "log" || endsWith(name, "_log") || endsWith(name, "_stem")) add("log");
			if (endsWith(name, "_wood")) add("wood");
			if (endsWith(name, "_slab")) add("slab");
			if (endsWith(name, "_stairs")) add("stairs");
			if (endsWith(name, "_door") && !endsWith(name, "_trapdoor")) add("door");
			if (endsWith(name, "_fence") && !endsWith(name, "_fence_gate")) add("fence");
			if (name == "wool" || endsWith(name, "_wool")) add("wool");
		}

		std::optional<blockId> resolveItem(
			const std::string& name,
			const std::function<std::optional<blockId>(const std::string&)>& resolve
		) const {
			const auto found = _ids.find(name);
			if (found != _ids.end()) return found->second;
			return resolve ? resolve(name) : std::nullopt;
		}

		bool isTypeName(const std::string& name) const {
			return _types.find(name) != _types.end() || resultSuffix(name) != nullptr;
		}

		recipeIngredient parseIngredient(
			const std::string& name,
			const std::function<std::optional<blockId>(const std::string&)>& resolve
		) {
			recipeIngredient spec{};
			if (name.empty() || name == "empty") return spec;
			if (isTypeName(name)) {
				spec.match = recipeIngredient::kind::type;
				spec.type = name;
				return spec;
			}
			const auto id = resolveItem(name, resolve);
			if (!id || *id == 0) return spec;
			spec.match = recipeIngredient::kind::item;
			spec.item = *id;
			return spec;
		}

		bool matches(const recipeIngredient& spec, blockId id) const {
			if (spec.match == recipeIngredient::kind::empty) return id == 0;
			if (id == 0) return false;
			if (spec.match == recipeIngredient::kind::item) return spec.item == id;
			const auto found = _types.find(spec.type);
			return found != _types.end() && found->second.count(id) != 0;
		}

		bool accept(
			const recipeIngredient& spec,
			blockId id,
			bool lockVariant,
			std::string& variant
		) const {
			if (!matches(spec, id)) return false;
			if (!lockVariant || spec.match != recipeIngredient::kind::type || id == 0)
				return true;
			const auto name = _names.find(id);
			if (name == _names.end()) return false;
			const std::string next = itemVariant(name->second);
			if (variant.empty()) {
				variant = next;
				return true;
			}
			return variant == next;
		}

		blockId resolveResult(const recipeIngredient& spec, const std::string& variant) const {
			if (spec.match == recipeIngredient::kind::item) return spec.item;
			if (spec.match != recipeIngredient::kind::type) return 0;
			const char* suffix = resultSuffix(spec.type);
			if (!suffix || variant.empty()) return 0;
			const auto found = _ids.find(variant + suffix);
			return found == _ids.end() ? 0 : found->second;
		}

		craftMatch finishMatch(const craftRecipe& recipe, const std::string& variant) const {
			const blockId result = resolveResult(recipe.result, variant);
			if (result == 0) return {};
			return { &recipe, result, recipe.resultCount };
		}

		static std::vector<recipeIngredient> transformPattern(
			const std::vector<recipeIngredient>& source,
			int width,
			int height,
			int mode,
			int& outWidth,
			int& outHeight
		) {
			const bool swap = mode == 1 || mode == 3;
			outWidth = swap ? height : width;
			outHeight = swap ? width : height;
			std::vector<recipeIngredient> dest(static_cast<size_t>(outWidth * outHeight));
			for (int y = 0; y < height; ++y) {
				for (int x = 0; x < width; ++x) {
					const recipeIngredient& cell = source[static_cast<size_t>(y * width + x)];
					int dx = x;
					int dy = y;
					switch (mode) {
					case 1: dx = height - 1 - y; dy = x; break;
					case 2: dx = width - 1 - x; dy = height - 1 - y; break;
					case 3: dx = y; dy = width - 1 - x; break;
					default: break;
					}
					dest[static_cast<size_t>(dy * outWidth + dx)] = cell;
				}
			}
			return dest;
		}

		static std::vector<recipeIngredient> mirrorPattern(
			const std::vector<recipeIngredient>& source,
			int width,
			int height
		) {
			std::vector<recipeIngredient> dest(source.size());
			for (int y = 0; y < height; ++y) {
				for (int x = 0; x < width; ++x)
					dest[static_cast<size_t>(y * width + (width - 1 - x))] =
						source[static_cast<size_t>(y * width + x)];
			}
			return dest;
		}

		bool matchPattern(
			const std::vector<recipeIngredient>& pattern,
			int patternWidth,
			int patternHeight,
			const blockId* grid,
			int width,
			int height,
			bool lockVariant,
			std::string& variant
		) const {
			if (patternWidth > width || patternHeight > height) return false;
			for (int offY = 0; offY <= height - patternHeight; ++offY) {
				for (int offX = 0; offX <= width - patternWidth; ++offX) {
					std::string local = variant;
					bool ok = true;
					for (int y = 0; y < height && ok; ++y) {
						for (int x = 0; x < width; ++x) {
							recipeIngredient expect{};
							const int ly = y - offY;
							const int lx = x - offX;
							if (ly >= 0 && ly < patternHeight && lx >= 0 && lx < patternWidth)
								expect = pattern[static_cast<size_t>(ly * patternWidth + lx)];
							if (!accept(expect, grid[y * width + x], lockVariant, local)) {
								ok = false;
								break;
							}
						}
					}
					if (ok) {
						variant = std::move(local);
						return true;
					}
				}
			}
			return false;
		}

		bool matchShaped(
			const craftRecipe& recipe,
			const blockId* grid,
			int width,
			int height,
			std::string& variant
		) const {
			const bool lockVariant = recipe.result.match == recipeIngredient::kind::type;
			const int rotations = recipe.rotate ? 4 : 1;
			for (int mode = 0; mode < rotations; ++mode) {
				int pw = 0;
				int ph = 0;
				auto pattern = transformPattern(recipe.pattern, recipe.width, recipe.height, mode, pw, ph);
				if (matchPattern(pattern, pw, ph, grid, width, height, lockVariant, variant))
					return true;
				if (recipe.mirror) {
					auto mirrored = mirrorPattern(pattern, pw, ph);
					if (matchPattern(mirrored, pw, ph, grid, width, height, lockVariant, variant))
						return true;
				}
			}
			return false;
		}

		bool matchUnordered(
			const craftRecipe& recipe,
			const blockId* grid,
			int width,
			int height,
			std::string& variant
		) const {
			std::vector<blockId> have;
			have.reserve(static_cast<size_t>(width * height));
			for (int i = 0; i < width * height; ++i)
				if (grid[i] != 0) have.push_back(grid[i]);
			if (have.size() != recipe.ingredients.size()) return false;
			const bool lockVariant = recipe.result.match == recipeIngredient::kind::type;
			std::vector<bool> used(have.size(), false);
			for (const recipeIngredient& spec : recipe.ingredients) {
				bool found = false;
				for (size_t i = 0; i < have.size(); ++i) {
					if (used[i]) continue;
					std::string local = variant;
					if (!accept(spec, have[i], lockVariant, local)) continue;
					used[i] = true;
					variant = std::move(local);
					found = true;
					break;
				}
				if (!found) return false;
			}
			return true;
		}

		std::vector<craftRecipe> _recipes;
		std::unordered_map<blockId, smeltingRecipe> _smelting;
		std::vector<fuelDefinition> _fuels;
		std::unordered_map<blockId, std::string> _names;
		std::unordered_map<std::string, blockId> _ids;
		std::unordered_map<std::string, std::unordered_set<blockId>> _types;
	};

}
