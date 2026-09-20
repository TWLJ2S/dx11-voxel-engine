#pragma once
#include "minecraftTerrainGeneration.h"
#include <stdint.h>
#include <string>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <system_error>
#include <Windows.h>

namespace ac {

#define WORLD_FILE_MAGIC 0x41435744
#define WORLD_FILE_VERSION 2
#define CHUNK_FILE_VERSION 8

	struct worldHeader {
		uint32_t _magic = WORLD_FILE_MAGIC;
		uint32_t _version = WORLD_FILE_VERSION;
		uint64_t _seed = 0;
		uint32_t _chunkCount = 0;
	};

	struct savedChunkId {
		int32_t x = 0;
		int32_t y = 0;
		int32_t z = 0;
		bool operator==(const savedChunkId& other) const {
			return x == other.x && y == other.y && z == other.z;
		}
	};

	struct savedChunkIdHash {
		size_t operator()(const savedChunkId& id) const {
			size_t value = std::hash<int32_t>{}(id.x);
			value ^= std::hash<int32_t>{}(id.y) + 0x9e3779b9u + (value << 6) + (value >> 2);
			value ^= std::hash<int32_t>{}(id.z) + 0x9e3779b9u + (value << 6) + (value >> 2);
			return value;
		}
	};

	struct chunkFileHeader {
		uint32_t _magic = WORLD_FILE_MAGIC;
		uint32_t _version = CHUNK_FILE_VERSION;
		int32_t _x = 0;
		int32_t _y = 0;
		int32_t _z = 0;
	};

	class world {
	private:
		std::filesystem::path _path;
		uint64_t _seed = 0;
		terrainGenerator* _generator = nullptr;
		mutable std::mutex _savedChunksMutex;
		mutable std::unordered_set<savedChunkId, savedChunkIdHash> _savedChunks;
		mutable uint64_t _manifestRevision = 0;
		mutable bool _manifestDirty = false;

		static bool writeBytes(std::ofstream& file, const void* data, size_t size) {
			file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
			return file.good();
		}

		static bool readBytes(std::ifstream& file, void* data, size_t size) {
			file.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(size));
			return file.good();
		}

		static std::filesystem::path chunkPath(const std::filesystem::path& path, const DirectX::XMINT3& position) {
			return path / "chunks" / (
				std::to_string(position.x) + "_" +
				std::to_string(position.y) + "_" +
				std::to_string(position.z) + ".chk"
				);
		}

		bool ensureStorageDirectories() const {
			std::error_code error;
			std::filesystem::create_directories(_path / "chunks", error);
			return !error;
		}

	public:
		world() = default;

		explicit world(const std::string& path, uint64_t seed = 0) {
			_path = path;
			if (!load()) create(path, seed);
		}

		~world() {
			delete _generator;
		}

		world(const world&) = delete;
		world& operator=(const world&) = delete;

		bool create(const std::string& path, uint64_t seed = 0) {
			_path = path;
			_seed = seed;

			delete _generator;
			_generator = new terrainGenerator(_seed);

			if (!ensureStorageDirectories())
				return false;

			{
				std::lock_guard<std::mutex> lock(_savedChunksMutex);
				_savedChunks = scanSavedChunks();
				++_manifestRevision;
				_manifestDirty = true;
			}

			return saveWorld();
		}

		bool load() {
			std::ifstream file(_path / "world.dat", std::ios::binary);

			if (!file)
				return false;

			uint32_t magic = 0;
			uint32_t version = 0;
			uint64_t seed = 0;
			if (!readBytes(file, &magic, sizeof(magic)) ||
				!readBytes(file, &version, sizeof(version)) ||
				!readBytes(file, &seed, sizeof(seed)))
				return false;
			if (magic != WORLD_FILE_MAGIC || (version != 1 && version != WORLD_FILE_VERSION))
				return false;

			std::unordered_set<savedChunkId, savedChunkIdHash> manifest;
			if (version == WORLD_FILE_VERSION) {
				uint32_t count = 0;
				if (!readBytes(file, &count, sizeof(count)) || count > 10'000'000u)
					return false;
				manifest.reserve(count);
				for (uint32_t index = 0; index < count; ++index) {
					savedChunkId chunkId;
					if (!readBytes(file, &chunkId.x, sizeof(chunkId.x)) ||
						!readBytes(file, &chunkId.y, sizeof(chunkId.y)) ||
						!readBytes(file, &chunkId.z, sizeof(chunkId.z)))
						return false;
					manifest.insert(chunkId);
				}
			}

			_seed = seed;

			delete _generator;
			_generator = new terrainGenerator(_seed);

			const auto scanned = scanSavedChunks();
			const bool needsMigration = version != WORLD_FILE_VERSION || scanned != manifest;
			{
				std::lock_guard<std::mutex> lock(_savedChunksMutex);
				_savedChunks = scanned;
				++_manifestRevision;
				_manifestDirty = needsMigration;
			}
			if (needsMigration && !saveWorld()) return false;

			return true;
		}

		bool saveWorld() const {
			// The world directory can be removed between startup and a later save.
			// Make every manifest write capable of recreating the complete layout.
			if (!ensureStorageDirectories()) return false;

			std::vector<savedChunkId> chunks;
			uint64_t revision = 0;
			{
				std::lock_guard<std::mutex> lock(_savedChunksMutex);
				chunks.assign(_savedChunks.begin(), _savedChunks.end());
				revision = _manifestRevision;
			}
			std::sort(chunks.begin(), chunks.end(), [](const savedChunkId& a, const savedChunkId& b) {
				if (a.x != b.x) return a.x < b.x;
				if (a.y != b.y) return a.y < b.y;
				return a.z < b.z;
			});

			const auto target = _path / "world.dat";
			const auto temporary = _path / "world.dat.tmp";
			std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
			if (!file) return false;

			const uint32_t magic = WORLD_FILE_MAGIC;
			const uint32_t version = WORLD_FILE_VERSION;
			const uint32_t count = static_cast<uint32_t>(chunks.size());
			bool good = writeBytes(file, &magic, sizeof(magic)) &&
				writeBytes(file, &version, sizeof(version)) &&
				writeBytes(file, &_seed, sizeof(_seed)) &&
				writeBytes(file, &count, sizeof(count));
			for (const savedChunkId& chunkId : chunks) {
				good = good && writeBytes(file, &chunkId.x, sizeof(chunkId.x)) &&
					writeBytes(file, &chunkId.y, sizeof(chunkId.y)) &&
					writeBytes(file, &chunkId.z, sizeof(chunkId.z));
			}
			file.flush();
			good = good && file.good();
			file.close();
			if (!good || !replaceFile(temporary, target)) return false;
			{
				std::lock_guard<std::mutex> lock(_savedChunksMutex);
				if (_manifestRevision == revision) _manifestDirty = false;
			}
			return true;
		}

		void generateChunk(chunk& c) const {
			if (!_generator)
				return;

			_generator->generate(c);
			c._dirty = true;
		}

		bool chunkExists(const DirectX::XMINT3& position) const {
			std::lock_guard<std::mutex> lock(_savedChunksMutex);
			return _savedChunks.contains(id(position));
		}

		bool loadChunk(chunk& c, const DirectX::XMINT3& position) const {
			const auto path = chunkPath(_path, position);
			std::ifstream file(path, std::ios::binary);
			if (!file) return false;

			chunkFileHeader header;

			if (!readBytes(file, &header, sizeof(header)))
				return false;

			if (header._magic != WORLD_FILE_MAGIC)
				return false;

			if (header._version != CHUNK_FILE_VERSION)
				return false;

			if (header._x != position.x ||
				header._y != position.y ||
				header._z != position.z)
				return false;

			for (subchunk& s : c._subchunks)
				if (!s.load(file))
					return false;

			c._position = position;
			c._dirty = false;

			return true;
		}

		bool saveChunk(const chunk& c) const {
			if (!ensureStorageDirectories()) return false;

			const auto path = chunkPath(_path, c._position);
			auto temporary = path;
			temporary += ".tmp";
			std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
			if (!file) return false;

			chunkFileHeader header;
			header._x = c._position.x;
			header._y = c._position.y;
			header._z = c._position.z;

			if (!writeBytes(file, &header, sizeof(header)))
				return false;

			bool good = true;
			for (const subchunk& s : c._subchunks)
				good = good && s.save(file);
			file.flush();
			good = good && file.good();
			file.close();
			if (!good || !replaceFile(temporary, path)) return false;
			bool manifestChanged = false;
			{
				std::lock_guard<std::mutex> lock(_savedChunksMutex);
				if (_savedChunks.insert(id(c._position)).second) {
					++_manifestRevision;
					_manifestDirty = true;
					manifestChanged = true;
				}
			}
			return !manifestChanged || saveWorld();
		}

		chunk loadOrGenerate(const DirectX::XMINT3& position, bool* generated = nullptr) const {
			chunk c;
			c._position = position;

			if (chunkExists(position) && loadChunk(c, position)) {
				if (generated) *generated = false;
				return c;
			}

			generateChunk(c);
			if (generated) *generated = true;
			return c;
		}

		static savedChunkId id(const DirectX::XMINT3& position) {
			return { position.x, position.y, position.z };
		}

		static bool replaceFile(const std::filesystem::path& temporary, const std::filesystem::path& target) {
			if (MoveFileExW(temporary.c_str(), target.c_str(),
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
			std::error_code ignored;
			std::filesystem::remove(temporary, ignored);
			return false;
		}

		std::unordered_set<savedChunkId, savedChunkIdHash> scanSavedChunks() const {
			std::unordered_set<savedChunkId, savedChunkIdHash> result;
			std::error_code error;
			const auto directory = _path / "chunks";
			if (!std::filesystem::exists(directory, error) || error) return result;
			for (std::filesystem::directory_iterator it(directory, error), end; it != end && !error; it.increment(error)) {
				if (!it->is_regular_file(error) || error || it->path().extension() != ".chk") continue;
				std::ifstream file(it->path(), std::ios::binary);
				chunkFileHeader header;
				if (!file.read(reinterpret_cast<char*>(&header), sizeof(header))) continue;
				if (header._magic == WORLD_FILE_MAGIC && header._version == CHUNK_FILE_VERSION)
					result.insert({ header._x, header._y, header._z });
			}
			return result;
		}

		bool deleteChunk(const DirectX::XMINT3& position) const {
			std::error_code error;
			const bool removed = std::filesystem::remove(
				chunkPath(_path, position),
				error
			) && !error;
			bool manifestChanged = false;
			if (removed) {
				std::lock_guard<std::mutex> lock(_savedChunksMutex);
				if (_savedChunks.erase(id(position))) {
					++_manifestRevision;
					_manifestDirty = true;
					manifestChanged = true;
				}
			}
			return removed && (!manifestChanged || saveWorld());
		}

		const std::filesystem::path& path() const {
			return _path;
		}

		uint64_t seed() const {
			return _seed;
		}

		void setSeed(uint64_t seed) {
			_seed = seed;

			delete _generator;
			_generator = new terrainGenerator(_seed);
		}

		terrainGenerator* generator() const {
			return _generator;
		}
	};

}
