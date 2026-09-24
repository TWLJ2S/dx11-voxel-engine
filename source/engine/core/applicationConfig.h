#pragma once

#include <core/settingsStore.h>

#include <rapidjson/document.h>

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <type_traits>
#include <vector>

namespace ac {
	struct applicationConfig {
		std::vector<int> fpsLimits;
		std::vector<int> viewDistances;
		std::vector<int> fovDegrees;
		std::vector<int> renderScales;
		std::vector<float> dayCycleScales;
		float dayLengthSeconds = 0.0f;
		float initialDayTimeSeconds = 0.0f;
		float footstepSpeedThreshold = 0.0f;
		float inventoryDoubleClickSeconds = 0.0f;
		struct {
			float maximumHealth = 0.0f;
			float maximumHunger = 0.0f;
			float maximumSaturation = 0.0f;
			float maximumAir = 0.0f;
			float safeFallDistance = 0.0f;
			float hurtCooldown = 0.0f;
			float saturatedRegenSeconds = 0.0f;
			float hungryRegenSeconds = 0.0f;
			float eatSeconds = 0.0f;
			float exhaustionUnit = 0.0f;
		} playerVitals;
		std::string userSettingsPath;
		std::unordered_map<std::string, std::string> blockAliases;
		std::vector<std::string> defaultHotbar;
	};

	template<typename T>
	inline std::vector<T> requiredConfigArray(const rapidjson::Value& object,
		const char* key, const std::string& source) {
		if (!object.IsObject() || !object.HasMember(key) || !object[key].IsArray() || object[key].Empty())
			throw std::runtime_error(source + ": missing non-empty array '" + key + "'");
		std::vector<T> result;
		for (const auto& entry : object[key].GetArray()) {
			if constexpr (std::is_same_v<T, int>) {
				if (!entry.IsInt()) throw std::runtime_error(source + ": '" + key + "' must contain integers");
				result.push_back(entry.GetInt());
			}
			else {
				if (!entry.IsNumber()) throw std::runtime_error(source + ": '" + key + "' must contain numbers");
				result.push_back(entry.GetFloat());
			}
		}
		return result;
	}

	inline applicationConfig loadApplicationConfig(const std::filesystem::path& path) {
		rapidjson::Document document;
		if (!loadJsonFile(path, document))
			throw std::runtime_error(path.string() + ": missing or invalid application config");
		const std::string source = path.string();
		applicationConfig result;
		if (!document.HasMember("options") || !document["options"].IsObject())
			throw std::runtime_error(source + ": missing options object");
		const auto& options = document["options"];
		result.fpsLimits = requiredConfigArray<int>(options, "fpsLimits", source);
		result.viewDistances = requiredConfigArray<int>(options, "viewDistances", source);
		result.fovDegrees = requiredConfigArray<int>(options, "fovDegrees", source);
		result.renderScales = requiredConfigArray<int>(options, "renderScales", source);
		result.dayCycleScales = requiredConfigArray<float>(options, "dayCycleScales", source);
		if (!document.HasMember("world") || !document["world"].IsObject())
			throw std::runtime_error(source + ": missing world object");
		const auto& world = document["world"];
		if (!world.HasMember("dayLengthSeconds") || !world["dayLengthSeconds"].IsNumber() ||
			!world.HasMember("initialDayTimeSeconds") || !world["initialDayTimeSeconds"].IsNumber())
			throw std::runtime_error(source + ": world day timing is required");
		result.dayLengthSeconds = world["dayLengthSeconds"].GetFloat();
		result.initialDayTimeSeconds = world["initialDayTimeSeconds"].GetFloat();
		if (!document.HasMember("gameplay") || !document["gameplay"].IsObject())
			throw std::runtime_error(source + ": missing gameplay object");
		const auto& gameplay = document["gameplay"];
		if (!gameplay.HasMember("footstepSpeedThreshold") || !gameplay["footstepSpeedThreshold"].IsNumber() ||
			!gameplay.HasMember("inventoryDoubleClickSeconds") || !gameplay["inventoryDoubleClickSeconds"].IsNumber())
			throw std::runtime_error(source + ": gameplay timing values are required");
		result.footstepSpeedThreshold = gameplay["footstepSpeedThreshold"].GetFloat();
		result.inventoryDoubleClickSeconds = gameplay["inventoryDoubleClickSeconds"].GetFloat();
		if (!gameplay.HasMember("playerVitals") || !gameplay["playerVitals"].IsObject())
			throw std::runtime_error(source + ": gameplay.playerVitals is required");
		const auto& vitals = gameplay["playerVitals"];
		auto vital = [&](const char* key) {
			if (!vitals.HasMember(key) || !vitals[key].IsNumber())
				throw std::runtime_error(source + ": missing playerVitals value '" + key + "'");
			return vitals[key].GetFloat();
		};
		result.playerVitals.maximumHealth = vital("maximumHealth");
		result.playerVitals.maximumHunger = vital("maximumHunger");
		result.playerVitals.maximumSaturation = vital("maximumSaturation");
		result.playerVitals.maximumAir = vital("maximumAir");
		result.playerVitals.safeFallDistance = vital("safeFallDistance");
		result.playerVitals.hurtCooldown = vital("hurtCooldown");
		result.playerVitals.saturatedRegenSeconds = vital("saturatedRegenSeconds");
		result.playerVitals.hungryRegenSeconds = vital("hungryRegenSeconds");
		result.playerVitals.eatSeconds = vital("eatSeconds");
		result.playerVitals.exhaustionUnit = vital("exhaustionUnit");
		if (!document.HasMember("userSettingsPath") || !document["userSettingsPath"].IsString())
			throw std::runtime_error(source + ": userSettingsPath is required");
		result.userSettingsPath = document["userSettingsPath"].GetString();
		if (!document.HasMember("blocks") || !document["blocks"].IsObject())
			throw std::runtime_error(source + ": blocks alias object is required");
		for (auto it = document["blocks"].MemberBegin(); it != document["blocks"].MemberEnd(); ++it) {
			if (!it->value.IsString()) throw std::runtime_error(source + ": block aliases must be strings");
			result.blockAliases.emplace(it->name.GetString(), it->value.GetString());
		}
		if (!document.HasMember("defaultHotbar") || !document["defaultHotbar"].IsArray())
			throw std::runtime_error(source + ": defaultHotbar array is required");
		for (const auto& entry : document["defaultHotbar"].GetArray()) {
			if (!entry.IsString()) throw std::runtime_error(source + ": defaultHotbar must contain block names");
			result.defaultHotbar.emplace_back(entry.GetString());
		}
		return result;
	}

	inline const applicationConfig& appConfig() {
		static const applicationConfig config = loadApplicationConfig("assets/application.json");
		return config;
	}
}
