#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <cmath>
#include <unordered_map>
#include <vector>

#include <DirectXMath.h>

namespace ac {

	struct blockPosKey {
		int32_t x = 0;
		int32_t y = 0;
		int32_t z = 0;

		bool operator==(const blockPosKey& other) const {
			return x == other.x && y == other.y && z == other.z;
		}
	};

	struct blockPosKeyHash {
		size_t operator()(const blockPosKey& value) const noexcept {
			size_t h = static_cast<uint32_t>(value.x) * 73856093u;
			h ^= static_cast<uint32_t>(value.y) * 19349663u;
			h ^= static_cast<uint32_t>(value.z) * 83492791u;
			return h;
		}
	};

	struct chestInventory {
		static constexpr size_t SLOT_COUNT = 27;
		std::array<uint32_t, SLOT_COUNT> itemIds{};
		std::array<uint32_t, SLOT_COUNT> counts{};
	};

	struct furnaceInventory {
		enum slot : size_t { input = 0, fuel = 1, output = 2 };
		std::array<uint32_t, 3> itemIds{};
		std::array<uint32_t, 3> counts{};
		float burnRemaining = 0.0f;
		float burnTotal = 0.0f;
		float cookProgress = 0.0f;
		uint32_t cookingInput = 0;

		bool burning() const { return burnRemaining > 0.0f; }
	};

	// Sparse extra data for cells that need more than the packed voxel bits.
	// Inventories, future sign text, etc. live here; open/powered/half stay on
	// the blockId. Not every cell allocates an entry.
	enum class blockDataKind : uint16_t {
		none = 0,
		chest = 1,
		furnace = 2
	};

	struct blockData {
		blockDataKind kind = blockDataKind::none;
		chestInventory chest{};
		furnaceInventory furnace{};
	};

	class blockEntityStore {
	private:
		static constexpr uint32_t FILE_MAGIC = 0x42454e54; // BENT
		static constexpr uint32_t FILE_VERSION = 3;
		static constexpr uint32_t CHEST_KIND_FILE_VERSION = 2;
		static constexpr uint32_t LEGACY_FILE_VERSION = 1;

		std::filesystem::path _path;
		mutable std::mutex _mutex;
		std::unordered_map<blockPosKey, blockData, blockPosKeyHash> _data;
		bool _dirty = false;
		std::function<uint32_t(uint32_t)> _toRuntimeId;
		std::function<uint32_t(uint32_t)> _toSavedId;

		static bool writeBytes(std::ofstream& file, const void* bytes, size_t size) {
			file.write(reinterpret_cast<const char*>(bytes), static_cast<std::streamsize>(size));
			return file.good();
		}

		static bool readBytes(std::ifstream& file, void* bytes, size_t size) {
			file.read(reinterpret_cast<char*>(bytes), static_cast<std::streamsize>(size));
			return file.good();
		}

	public:
		explicit blockEntityStore(std::filesystem::path path) : _path(std::move(path)) {
			if (!_path.empty())
				load();
		}

		blockData& dataAt(int32_t x, int32_t y, int32_t z, blockDataKind kind) {
			std::lock_guard lock(_mutex);
			blockData& entry = _data[{ x, y, z }];
			if (entry.kind != kind) {
				entry = {};
				entry.kind = kind;
			}
			_dirty = true;
			return entry;
		}

		const blockData* tryData(int32_t x, int32_t y, int32_t z) const {
			std::lock_guard lock(_mutex);
			auto found = _data.find({ x, y, z });
			return found == _data.end() ? nullptr : &found->second;
		}

		chestInventory& chestAt(int32_t x, int32_t y, int32_t z) {
			return dataAt(x, y, z, blockDataKind::chest).chest;
		}

		const chestInventory* tryChest(int32_t x, int32_t y, int32_t z) const {
			const blockData* entry = tryData(x, y, z);
			return entry && entry->kind == blockDataKind::chest ? &entry->chest : nullptr;
		}

		furnaceInventory& furnaceAt(int32_t x, int32_t y, int32_t z) {
			return dataAt(x, y, z, blockDataKind::furnace).furnace;
		}

		const furnaceInventory* tryFurnace(int32_t x, int32_t y, int32_t z) const {
			const blockData* entry = tryData(x, y, z);
			return entry && entry->kind == blockDataKind::furnace ? &entry->furnace : nullptr;
		}

		template<size_t N>
		static uint8_t inventorySignal(const std::array<uint32_t, N>& ids,
			const std::array<uint32_t, N>& counts,
			const std::function<uint32_t(uint32_t)>& stackLimit) {
			double fullness = 0.0;
			bool any = false;
			for (size_t i = 0; i < N; ++i) {
				if (!ids[i] || !counts[i]) continue;
				any = true;
				const uint32_t limit = (std::max)(1u, stackLimit(ids[i]));
				fullness += double((std::min)(counts[i], limit)) / double(limit);
			}
			return any ? static_cast<uint8_t>(1 + std::floor(14.0 * fullness / double(N) + 1e-9)) : 0u;
		}

		std::optional<uint8_t> analogSignalAt(int32_t x, int32_t y, int32_t z,
			const std::function<uint32_t(uint32_t)>& stackLimit) const {
			std::lock_guard lock(_mutex);
			const auto it = _data.find({ x, y, z });
			if (it == _data.end()) return std::nullopt;
			if (it->second.kind == blockDataKind::chest)
				return inventorySignal(it->second.chest.itemIds, it->second.chest.counts, stackLimit);
			if (it->second.kind == blockDataKind::furnace)
				return inventorySignal(it->second.furnace.itemIds, it->second.furnace.counts, stackLimit);
			return std::nullopt;
		}

		void updateFurnaces(
			float deltaTime,
			const std::function<bool(uint32_t, uint32_t&, uint32_t&, float&)>& resolveSmelting,
			const std::function<float(uint32_t)>& resolveFuel,
			const std::function<void(const blockPosKey&, bool)>& updateBurning = {}
		) {
			if (deltaTime <= 0.0f || !resolveSmelting || !resolveFuel) return;
			std::lock_guard lock(_mutex);
			for (auto& [key, data] : _data) {
				if (data.kind != blockDataKind::furnace) continue;
				furnaceInventory& furnace = data.furnace;
				uint32_t result = 0, resultCount = 0;
				float cookSeconds = 0.0f;
				const uint32_t input = furnace.itemIds[furnaceInventory::input];
				const bool hasRecipe = input != 0 && furnace.counts[furnaceInventory::input] > 0 &&
					resolveSmelting(input, result, resultCount, cookSeconds);
				const bool outputFits = hasRecipe && resultCount > 0 &&
					(furnace.itemIds[furnaceInventory::output] == 0 ||
					 furnace.itemIds[furnaceInventory::output] == result) &&
					furnace.counts[furnaceInventory::output] + resultCount <= 64u;

				if (!outputFits || furnace.cookingInput != input) {
					furnace.cookProgress = 0.0f;
					furnace.cookingInput = outputFits ? input : 0;
				}
				if (furnace.burnRemaining <= 0.0f && outputFits) {
					const size_t fuelSlot = furnaceInventory::fuel;
					const float fuelSeconds = furnace.itemIds[fuelSlot] != 0
						? resolveFuel(furnace.itemIds[fuelSlot]) : 0.0f;
					if (fuelSeconds > 0.0f && furnace.counts[fuelSlot] > 0) {
						furnace.burnRemaining = fuelSeconds;
						furnace.burnTotal = fuelSeconds;
						if (--furnace.counts[fuelSlot] == 0) furnace.itemIds[fuelSlot] = 0;
						_dirty = true;
					}
				}
				if (furnace.burnRemaining > 0.0f) {
					furnace.burnRemaining = (std::max)(0.0f, furnace.burnRemaining - deltaTime);
					_dirty = true;
					if (outputFits) {
						furnace.cookProgress += deltaTime;
						if (furnace.cookProgress >= cookSeconds) {
							furnace.cookProgress -= cookSeconds;
							if (--furnace.counts[furnaceInventory::input] == 0)
								furnace.itemIds[furnaceInventory::input] = 0;
							furnace.itemIds[furnaceInventory::output] = result;
							furnace.counts[furnaceInventory::output] += resultCount;
						}
					}
				}
				if (updateBurning) updateBurning(key, furnace.burning());
			}
		}

		void removeAt(int32_t x, int32_t y, int32_t z) {
			std::lock_guard lock(_mutex);
			if (_data.erase({ x, y, z }) > 0)
				_dirty = true;
		}

		bool load() {
			std::ifstream file(_path / "block_entities.dat", std::ios::binary);
			std::lock_guard lock(_mutex);
			_data.clear();
			if (!file) return true;

			uint32_t magic = 0, version = 0, count = 0;
			if (!readBytes(file, &magic, sizeof(magic)) ||
				!readBytes(file, &version, sizeof(version)) ||
				!readBytes(file, &count, sizeof(count)) ||
				magic != FILE_MAGIC ||
				(version != FILE_VERSION && version != CHEST_KIND_FILE_VERSION &&
				 version != LEGACY_FILE_VERSION) ||
				count > 100000u)
				return false;

			for (uint32_t i = 0; i < count; ++i) {
				blockPosKey key{};
				blockData entry{};
				entry.kind = blockDataKind::chest;
				if (!readBytes(file, &key, sizeof(key)))
					return false;
				if (version >= CHEST_KIND_FILE_VERSION) {
					uint16_t kind = 0;
					if (!readBytes(file, &kind, sizeof(kind)))
						return false;
					entry.kind = static_cast<blockDataKind>(kind);
				}
				if (entry.kind == blockDataKind::furnace && version >= FILE_VERSION) {
					if (!readBytes(file, entry.furnace.itemIds.data(), sizeof(entry.furnace.itemIds)) ||
						!readBytes(file, entry.furnace.counts.data(), sizeof(entry.furnace.counts)) ||
						!readBytes(file, &entry.furnace.burnRemaining, sizeof(float)) ||
						!readBytes(file, &entry.furnace.burnTotal, sizeof(float)) ||
						!readBytes(file, &entry.furnace.cookProgress, sizeof(float)) ||
						!readBytes(file, &entry.furnace.cookingInput, sizeof(uint32_t))) return false;
					if (_toRuntimeId) {
						for (uint32_t& itemId : entry.furnace.itemIds) itemId = _toRuntimeId(itemId);
						entry.furnace.cookingInput = _toRuntimeId(entry.furnace.cookingInput);
					}
				}
				else {
					entry.kind = blockDataKind::chest;
					if (!readBytes(file, entry.chest.itemIds.data(), sizeof(entry.chest.itemIds)) ||
						!readBytes(file, entry.chest.counts.data(), sizeof(entry.chest.counts))) return false;
					if (_toRuntimeId)
						for (uint32_t& itemId : entry.chest.itemIds) itemId = _toRuntimeId(itemId);
				}
				_data.emplace(key, entry);
			}
			_dirty = false;
			return true;
		}

		bool save() {
			std::lock_guard lock(_mutex);
			if (!_dirty) return true;
			std::error_code error;
			std::filesystem::create_directories(_path, error);
			const auto temporary = _path / "block_entities.dat.tmp";
			const auto target = _path / "block_entities.dat";
			std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
			if (!file) return false;
			const uint32_t magic = FILE_MAGIC;
			const uint32_t version = FILE_VERSION;
			const uint32_t count = static_cast<uint32_t>(_data.size());
			if (!writeBytes(file, &magic, sizeof(magic)) ||
				!writeBytes(file, &version, sizeof(version)) ||
				!writeBytes(file, &count, sizeof(count)))
				return false;
			for (const auto& [key, entry] : _data) {
				const uint16_t kind = static_cast<uint16_t>(entry.kind);
				if (!writeBytes(file, &key, sizeof(key)) || !writeBytes(file, &kind, sizeof(kind)))
					return false;
				if (entry.kind == blockDataKind::furnace) {
					furnaceInventory persisted = entry.furnace;
					if (_toSavedId) {
						for (uint32_t& itemId : persisted.itemIds) itemId = _toSavedId(itemId);
						persisted.cookingInput = _toSavedId(persisted.cookingInput);
					}
					if (!writeBytes(file, persisted.itemIds.data(), sizeof(persisted.itemIds)) ||
						!writeBytes(file, persisted.counts.data(), sizeof(persisted.counts)) ||
						!writeBytes(file, &persisted.burnRemaining, sizeof(float)) ||
						!writeBytes(file, &persisted.burnTotal, sizeof(float)) ||
						!writeBytes(file, &persisted.cookProgress, sizeof(float)) ||
						!writeBytes(file, &persisted.cookingInput, sizeof(uint32_t))) return false;
				}
				else {
					chestInventory persisted = entry.chest;
					if (_toSavedId)
						for (uint32_t& itemId : persisted.itemIds) itemId = _toSavedId(itemId);
					if (!writeBytes(file, persisted.itemIds.data(), sizeof(persisted.itemIds)) ||
						!writeBytes(file, persisted.counts.data(), sizeof(persisted.counts))) return false;
				}
			}
			file.close();
			std::filesystem::rename(temporary, target, error);
			if (error) return false;
			_dirty = false;
			return true;
		}

		void reset() {
			{
				std::lock_guard lock(_mutex);
				_data.clear();
				_dirty = false;
			}
			load();
		}

		void setPath(std::filesystem::path path) {
			save();
			_path = std::move(path);
			load();
		}

		void configureIdTranslation(
			std::function<uint32_t(uint32_t)> toRuntime,
			std::function<uint32_t(uint32_t)> toSaved
		) {
			std::lock_guard lock(_mutex);
			_toRuntimeId = std::move(toRuntime);
			_toSavedId = std::move(toSaved);
		}

		void clearMemory() {
			std::lock_guard lock(_mutex);
			_data.clear();
			_dirty = false;
		}
	};

}
