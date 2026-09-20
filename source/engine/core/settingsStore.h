#pragma once

#include <cstdint>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

namespace ac {

	struct userSettingsData {
		bool vsync = true;
		int fpsLimit = 300; // 0 = unlimited
		uint32_t lightingQuality = 1;
		bool gpuTerrain = true;
		bool gpuEntityPathfinding = true;
		int viewDistance = 8;
		bool fxaa = true;
		bool motionBlur = true;
		bool shadows = true;
		int fov = 85;
		int antialiasing = 2; // 0 off, 1 FXAA, 2 SMAA
		bool bloom = true;
		bool gtao = true;
		bool fog = true;
		int renderScale = 100;
		std::string lastWorld;
	};

	struct worldSettingsData {
		uint32_t dayCycleIndex = 2; // 0 paused, 1 slow, 2 normal, 3 fast
		float weatherClock = 0.0f;
	};

	inline constexpr size_t PLAYER_HOTBAR_SLOTS = 9;
	inline constexpr size_t PLAYER_INVENTORY_SLOTS = 27;

	struct playerInventoryData {
		std::array<uint32_t, PLAYER_HOTBAR_SLOTS> hotbar{};
		std::array<uint32_t, PLAYER_HOTBAR_SLOTS> counts{};
		std::array<uint32_t, PLAYER_INVENTORY_SLOTS> inventory{};
		std::array<uint32_t, PLAYER_INVENTORY_SLOTS> inventoryCounts{};
		uint32_t selectedSlot = 0;
		float health = 20.0f;
		float hunger = 20.0f;
		float saturation = 20.0f;
		float air = 10.0f;
	};

	inline bool loadJsonFile(const std::filesystem::path& path, rapidjson::Document& document) {
		std::ifstream file(path, std::ios::binary);
		if (!file)
			return false;
		file.seekg(0, std::ios::end);
		const std::streamoff size = file.tellg();
		if (size <= 0)
			return false;
		file.seekg(0, std::ios::beg);
		std::string content(static_cast<size_t>(size), '\0');
		if (!file.read(content.data(), size))
			return false;
		if (content.size() >= 3 &&
			static_cast<unsigned char>(content[0]) == 0xEF &&
			static_cast<unsigned char>(content[1]) == 0xBB &&
			static_cast<unsigned char>(content[2]) == 0xBF) {
			content.erase(0, 3);
		}
		document.Parse(content.data(), content.size());
		return !document.HasParseError() && document.IsObject();
	}

	inline bool writeJsonFile(const std::filesystem::path& path, const rapidjson::Document& document) {
		std::error_code error;
		if (path.has_parent_path())
			std::filesystem::create_directories(path.parent_path(), error);

		rapidjson::StringBuffer buffer;
		rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
		writer.SetIndent(' ', 2);
		document.Accept(writer);

		const auto temporary = path.string() + ".tmp";
		{
			std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
			if (!file)
				return false;
			file.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
			if (!file)
				return false;
		}
		std::filesystem::rename(temporary, path, error);
		if (error) {
			std::filesystem::remove(path, error);
			std::filesystem::rename(temporary, path, error);
		}
		return !error;
	}

	inline userSettingsData loadUserSettings(const std::filesystem::path& path) {
		userSettingsData settings{};
		rapidjson::Document document;
		if (!loadJsonFile(path, document))
			return settings;

		if (document.HasMember("vsync") && document["vsync"].IsBool())
			settings.vsync = document["vsync"].GetBool();
		if (document.HasMember("fpsLimit") && document["fpsLimit"].IsInt())
			settings.fpsLimit = document["fpsLimit"].GetInt();
		if (document.HasMember("lightingQuality") && document["lightingQuality"].IsUint())
			settings.lightingQuality = (std::min)(document["lightingQuality"].GetUint(), 2u);
		if (document.HasMember("gpuTerrain") && document["gpuTerrain"].IsBool())
			settings.gpuTerrain = document["gpuTerrain"].GetBool();
		if (document.HasMember("gpuEntityPathfinding") && document["gpuEntityPathfinding"].IsBool())
			settings.gpuEntityPathfinding = document["gpuEntityPathfinding"].GetBool();
		if (document.HasMember("viewDistance") && document["viewDistance"].IsInt())
			settings.viewDistance = document["viewDistance"].GetInt();
		if (document.HasMember("fxaa") && document["fxaa"].IsBool())
			settings.fxaa = document["fxaa"].GetBool();
		if (document.HasMember("motionBlur") && document["motionBlur"].IsBool())
			settings.motionBlur = document["motionBlur"].GetBool();
		if (document.HasMember("shadows") && document["shadows"].IsBool())
			settings.shadows = document["shadows"].GetBool();
		if (document.HasMember("fov") && document["fov"].IsInt())
			settings.fov = std::clamp(document["fov"].GetInt(), 60, 120);
		if (document.HasMember("antialiasing") && document["antialiasing"].IsInt())
			settings.antialiasing = std::clamp(document["antialiasing"].GetInt(), 0, 2);
		else if (document.HasMember("fxaa") && document["fxaa"].IsBool())
			settings.antialiasing = document["fxaa"].GetBool() ? 1 : 0;
		if (document.HasMember("bloom") && document["bloom"].IsBool())
			settings.bloom = document["bloom"].GetBool();
		if (document.HasMember("gtao") && document["gtao"].IsBool())
			settings.gtao = document["gtao"].GetBool();
		if (document.HasMember("fog") && document["fog"].IsBool())
			settings.fog = document["fog"].GetBool();
		if (document.HasMember("renderScale") && document["renderScale"].IsInt())
			settings.renderScale = std::clamp(document["renderScale"].GetInt(), 50, 150);
		if (document.HasMember("lastWorld") && document["lastWorld"].IsString())
			settings.lastWorld = document["lastWorld"].GetString();
		return settings;
	}

	inline bool saveUserSettings(const std::filesystem::path& path, const userSettingsData& settings) {
		rapidjson::Document document;
		document.SetObject();
		auto& allocator = document.GetAllocator();
		document.AddMember("vsync", settings.vsync, allocator);
		document.AddMember("fpsLimit", settings.fpsLimit, allocator);
		document.AddMember("lightingQuality", settings.lightingQuality, allocator);
		document.AddMember("gpuTerrain", settings.gpuTerrain, allocator);
		document.AddMember("gpuEntityPathfinding", settings.gpuEntityPathfinding, allocator);
		document.AddMember("viewDistance", settings.viewDistance, allocator);
		document.AddMember("fxaa", settings.fxaa, allocator);
		document.AddMember("motionBlur", settings.motionBlur, allocator);
		document.AddMember("shadows", settings.shadows, allocator);
		document.AddMember("fov", settings.fov, allocator);
		document.AddMember("antialiasing", settings.antialiasing, allocator);
		document.AddMember("bloom", settings.bloom, allocator);
		document.AddMember("gtao", settings.gtao, allocator);
		document.AddMember("fog", settings.fog, allocator);
		document.AddMember("renderScale", settings.renderScale, allocator);
		document.AddMember("lastWorld", rapidjson::Value(settings.lastWorld.c_str(), allocator), allocator);
		return writeJsonFile(path, document);
	}

	inline worldSettingsData loadWorldSettings(const std::filesystem::path& path) {
		worldSettingsData settings{};
		rapidjson::Document document;
		if (!loadJsonFile(path, document))
			return settings;
		if (document.HasMember("dayCycleIndex") && document["dayCycleIndex"].IsUint())
			settings.dayCycleIndex = (std::min)(document["dayCycleIndex"].GetUint(), 3u);
		if (document.HasMember("weatherClock") && document["weatherClock"].IsNumber())
			settings.weatherClock = std::max(document["weatherClock"].GetFloat(), 0.0f);
		return settings;
	}

	inline bool saveWorldSettings(const std::filesystem::path& path, const worldSettingsData& settings) {
		rapidjson::Document document;
		document.SetObject();
		auto& allocator = document.GetAllocator();
		document.AddMember("dayCycleIndex", settings.dayCycleIndex, allocator);
		document.AddMember("weatherClock", settings.weatherClock, allocator);
		return writeJsonFile(path, document);
	}

	inline playerInventoryData loadPlayerInventory(const std::filesystem::path& path) {
		playerInventoryData inventory{};
		rapidjson::Document document;
		if (!loadJsonFile(path, document))
			return inventory;

		if (document.HasMember("selectedSlot") && document["selectedSlot"].IsUint())
			inventory.selectedSlot = (std::min)(
				document["selectedSlot"].GetUint(),
				static_cast<uint32_t>(PLAYER_HOTBAR_SLOTS - 1));

		if (document.HasMember("hotbar") && document["hotbar"].IsArray()) {
			const auto& hotbar = document["hotbar"];
			const size_t count = (std::min)(hotbar.Size(), static_cast<rapidjson::SizeType>(PLAYER_HOTBAR_SLOTS));
			for (rapidjson::SizeType i = 0; i < count; ++i) {
				if (hotbar[i].IsUint())
					inventory.hotbar[i] = hotbar[i].GetUint();
			}
		}
		if (document.HasMember("counts") && document["counts"].IsArray()) {
			const auto& counts = document["counts"];
			const size_t count = (std::min)(counts.Size(), static_cast<rapidjson::SizeType>(PLAYER_HOTBAR_SLOTS));
			for (rapidjson::SizeType i = 0; i < count; ++i) {
				if (counts[i].IsUint())
					inventory.counts[i] = counts[i].GetUint();
			}
		}
		else {
			for (size_t i = 0; i < PLAYER_HOTBAR_SLOTS; ++i)
				inventory.counts[i] = inventory.hotbar[i] != 0 ? 64u : 0;
		}
		if (document.HasMember("inventory") && document["inventory"].IsArray()) {
			const auto& slots = document["inventory"];
			const size_t count = (std::min)(slots.Size(), static_cast<rapidjson::SizeType>(PLAYER_INVENTORY_SLOTS));
			for (rapidjson::SizeType i = 0; i < count; ++i) {
				if (slots[i].IsUint())
					inventory.inventory[i] = slots[i].GetUint();
			}
		}
		if (document.HasMember("inventoryCounts") && document["inventoryCounts"].IsArray()) {
			const auto& counts = document["inventoryCounts"];
			const size_t count = (std::min)(counts.Size(), static_cast<rapidjson::SizeType>(PLAYER_INVENTORY_SLOTS));
			for (rapidjson::SizeType i = 0; i < count; ++i) {
				if (counts[i].IsUint())
					inventory.inventoryCounts[i] = counts[i].GetUint();
			}
		}
		else {
			for (size_t i = 0; i < PLAYER_INVENTORY_SLOTS; ++i)
				inventory.inventoryCounts[i] = inventory.inventory[i] != 0 ? 64u : 0;
		}
		if (document.HasMember("health") && document["health"].IsNumber())
			inventory.health = document["health"].GetFloat();
		if (document.HasMember("hunger") && document["hunger"].IsNumber())
			inventory.hunger = document["hunger"].GetFloat();
		if (document.HasMember("saturation") && document["saturation"].IsNumber())
			inventory.saturation = document["saturation"].GetFloat();
		else
			inventory.saturation = inventory.hunger;
		if (document.HasMember("air") && document["air"].IsNumber())
			inventory.air = document["air"].GetFloat();
		return inventory;
	}

	inline bool savePlayerInventory(const std::filesystem::path& path, const playerInventoryData& inventory) {
		rapidjson::Document document;
		document.SetObject();
		auto& allocator = document.GetAllocator();
		document.AddMember("selectedSlot", inventory.selectedSlot, allocator);
		rapidjson::Value hotbar(rapidjson::kArrayType);
		rapidjson::Value counts(rapidjson::kArrayType);
		for (size_t i = 0; i < PLAYER_HOTBAR_SLOTS; ++i) {
			hotbar.PushBack(inventory.hotbar[i], allocator);
			counts.PushBack(inventory.counts[i], allocator);
		}
		document.AddMember("hotbar", hotbar, allocator);
		document.AddMember("counts", counts, allocator);
		rapidjson::Value inv(rapidjson::kArrayType);
		rapidjson::Value invCounts(rapidjson::kArrayType);
		for (size_t i = 0; i < PLAYER_INVENTORY_SLOTS; ++i) {
			inv.PushBack(inventory.inventory[i], allocator);
			invCounts.PushBack(inventory.inventoryCounts[i], allocator);
		}
		document.AddMember("inventory", inv, allocator);
		document.AddMember("inventoryCounts", invCounts, allocator);
		document.AddMember("health", inventory.health, allocator);
		document.AddMember("hunger", inventory.hunger, allocator);
		document.AddMember("saturation", inventory.saturation, allocator);
		document.AddMember("air", inventory.air, allocator);
		return writeJsonFile(path, document);
	}

}
