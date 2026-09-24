#pragma once

#include <core/settingsStore.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace ac {

	struct savedWorldInfo {
		std::string name;
		std::filesystem::path path;
		uint64_t seed = 0;
		int64_t created = 0;
		int64_t lastPlayed = 0;
		// In-game elapsed day-cycle time, measured in the engine's day-length seconds.
		// Defaults keep older worlds backward-compatible when the field is absent.
		float dayTimeSeconds = 600.0f;
	};

	inline std::filesystem::path savesRoot() {
		return std::filesystem::path("saves");
	}

	inline int64_t currentUnixTime() {
		return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch()).count());
	}

	inline std::string sanitizeWorldFolder(std::string name) {
		std::string out;
		out.reserve(name.size());
		for (unsigned char c : name) {
			if (std::isalnum(c) || c == ' ' || c == '-' || c == '_')
				out.push_back(static_cast<char>(c));
		}
		while (!out.empty() && out.front() == ' ')
			out.erase(out.begin());
		while (!out.empty() && out.back() == ' ')
			out.pop_back();
		for (char& c : out) {
			if (c == ' ')
				c = '_';
		}
		if (out.empty())
			out = "world";
		std::string upper = out;
		for (char& c : upper)
			c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL" ||
			upper == "COM1" || upper == "LPT1")
			out = "world_" + out;
		return out;
	}

	inline bool readWorldDatSeed(const std::filesystem::path& worldPath, uint64_t& seed) {
		std::ifstream file(worldPath / "world.dat", std::ios::binary);
		if (!file)
			return false;
		uint32_t magic = 0;
		uint32_t version = 0;
		file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
		file.read(reinterpret_cast<char*>(&version), sizeof(version));
		file.read(reinterpret_cast<char*>(&seed), sizeof(seed));
		return file.good() && magic == 0x41435744u && (version == 1u || version == 2u);
	}

	inline savedWorldInfo loadWorldLevelInfo(const std::filesystem::path& worldPath) {
		savedWorldInfo info;
		info.path = worldPath;
		info.name = worldPath.filename().string();
		if (info.name.empty())
			info.name = "World";
		readWorldDatSeed(worldPath, info.seed);

		rapidjson::Document document;
		if (!loadJsonFile(worldPath / "level.json", document))
			return info;
		if (document.HasMember("name") && document["name"].IsString())
			info.name = document["name"].GetString();
		if (document.HasMember("seed") && document["seed"].IsUint64())
			info.seed = document["seed"].GetUint64();
		else if (document.HasMember("seed") && document["seed"].IsInt64())
			info.seed = static_cast<uint64_t>(document["seed"].GetInt64());
		if (document.HasMember("created") && document["created"].IsInt64())
			info.created = document["created"].GetInt64();
		if (document.HasMember("lastPlayed") && document["lastPlayed"].IsInt64())
			info.lastPlayed = document["lastPlayed"].GetInt64();
		if (document.HasMember("dayTime") && document["dayTime"].IsNumber())
			info.dayTimeSeconds = static_cast<float>(document["dayTime"].GetDouble());
		return info;
	}

	inline bool saveWorldLevelInfo(const savedWorldInfo& info) {
		rapidjson::Document document;
		document.SetObject();
		auto& allocator = document.GetAllocator();
		document.AddMember("name", rapidjson::Value(info.name.c_str(), allocator), allocator);
		document.AddMember("seed", info.seed, allocator);
		document.AddMember("created", info.created, allocator);
		document.AddMember("lastPlayed", info.lastPlayed, allocator);
		document.AddMember("dayTime", static_cast<double>(info.dayTimeSeconds), allocator);
		return writeJsonFile(info.path / "level.json", document);
	}

	inline std::filesystem::path uniqueWorldDirectory(const std::string& displayName) {
		const std::string base = sanitizeWorldFolder(displayName);
		std::error_code error;
		std::filesystem::create_directories(savesRoot(), error);
		std::filesystem::path candidate = savesRoot() / base;
		if (!std::filesystem::exists(candidate, error))
			return candidate;
		for (int index = 2; index < 10000; ++index) {
			candidate = savesRoot() / (base + "_" + std::to_string(index));
			if (!std::filesystem::exists(candidate, error))
				return candidate;
		}
		return savesRoot() / (base + "_new");
	}

	inline std::string uniqueWorldDisplayName(
		const std::string& desired,
		const std::vector<savedWorldInfo>& worlds
	) {
		auto taken = [&](const std::string& name) {
			for (const savedWorldInfo& world : worlds) {
				if (_stricmp(world.name.c_str(), name.c_str()) == 0)
					return true;
			}
			return false;
		};
		if (!taken(desired))
			return desired;
		for (int index = 2; index < 10000; ++index) {
			const std::string name = desired + " " + std::to_string(index);
			if (!taken(name))
				return name;
		}
		return desired + " New";
	}

	inline std::vector<savedWorldInfo> listSavedWorlds() {
		std::vector<savedWorldInfo> worlds;
		std::error_code error;
		auto consider = [&](const std::filesystem::path& directory) {
			if (!std::filesystem::is_directory(directory, error))
				return;
			if (!std::filesystem::exists(directory / "world.dat", error))
				return;
			worlds.push_back(loadWorldLevelInfo(directory));
		};

		consider(std::filesystem::path("world"));
		if (std::filesystem::exists(savesRoot(), error)) {
			for (const auto& entry : std::filesystem::directory_iterator(savesRoot(), error)) {
				if (entry.is_directory(error))
					consider(entry.path());
			}
		}

		std::sort(worlds.begin(), worlds.end(), [](const savedWorldInfo& a, const savedWorldInfo& b) {
			if (a.lastPlayed != b.lastPlayed)
				return a.lastPlayed > b.lastPlayed;
			return _stricmp(a.name.c_str(), b.name.c_str()) < 0;
		});
		return worlds;
	}

	inline std::string formatWorldTime(int64_t unixTime) {
		if (unixTime <= 0)
			return "never";
		const std::time_t value = static_cast<std::time_t>(unixTime);
		std::tm local{};
		if (localtime_s(&local, &value) != 0)
			return "unknown";
		char buffer[32];
		if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &local) == 0)
			return "unknown";
		return buffer;
	}

}
