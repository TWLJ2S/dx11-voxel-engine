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
#include <optional>
#include <system_error>
#include <Windows.h>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

namespace ac {

#define WORLD_FILE_MAGIC 0x41435744
#define WORLD_FILE_VERSION 2
#define CHUNK_FILE_VERSION 12
#define LEGACY_CHUNK_FILE_VERSION 10
#define PREVIOUS_CHUNK_FILE_VERSION 11
#define ENTITY_FILE_MAGIC 0x4143454E
#define ENTITY_FILE_VERSION 1

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

	enum class savedEntityKind : uint32_t {
		player = 1,
		mob = 2,
		item = 3
	};

	// Pointer-free, renderer-independent entity state. New runtime entity types can
	// persist without changing the terrain/chunk formats.
	struct savedEntity {
		uint64_t id = 0;
		savedEntityKind kind = savedEntityKind::mob;
		uint32_t flags = 0;
		DirectX::XMFLOAT3 position{};
		DirectX::XMFLOAT3 velocity{};
		DirectX::XMFLOAT4 rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
		DirectX::XMFLOAT3 scale{ 1.0f, 1.0f, 1.0f };
		float yaw = 0.0f;
		float pitch = 0.0f;
		uint32_t assetId = 0;
		uint32_t itemId = 0;
		uint32_t itemCount = 0;
		uint32_t variant = 0;
	};

	class world {
	private:
		std::filesystem::path _path;
		uint64_t _seed = 0;
		terrainBlockPalette _terrainBlocks{};
		terrainBiomeConfig _terrainBiomes{};
		bool _terrainConfigured = false;
		terrainGenerator* _generator = nullptr;
		std::unordered_map<std::string, blockId> _currentBlockIds;
		std::unordered_map<blockId, std::string> _currentBlockNames;
		std::unordered_map<blockId, blockId> _savedBlockIds;
		std::unordered_map<blockId, blockId> _runtimeBlockIds;
		std::unordered_map<blockId, std::string> _savedBlockNames;
		std::unordered_map<blockId, blockId> _legacyBlockIds;
		bool _contentRegistryConfigured = false;
		mutable std::mutex _savedChunksMutex;
		mutable std::unordered_set<savedChunkId, savedChunkIdHash> _savedChunks;
		mutable uint64_t _manifestRevision = 0;
		mutable bool _manifestDirty = false;
		mutable std::mutex _entitiesMutex;
		mutable std::mutex _entityFileMutex;
		mutable std::vector<savedEntity> _entities;
		mutable uint64_t _entitiesRevision = 0;

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

		bool loadContentRegistry() {
			_savedBlockIds.clear();
			_runtimeBlockIds.clear();
			_savedBlockNames.clear();
			std::ifstream file(_path / "content_registry.json", std::ios::binary);
			if (!file) {
				_savedBlockIds = _legacyBlockIds;
				for (const auto& [saved, runtime] : _savedBlockIds) {
					_runtimeBlockIds.emplace(runtime, saved);
					const auto name = _currentBlockNames.find(runtime);
					if (name != _currentBlockNames.end()) _savedBlockNames.emplace(saved, name->second);
				}
			}
			else {
				rapidjson::IStreamWrapper stream(file);
				rapidjson::Document document;
				document.ParseStream(stream);
				if (document.HasParseError() || !document.IsObject() ||
					!document.HasMember("schemaVersion") || !document["schemaVersion"].IsUint() ||
					document["schemaVersion"].GetUint() != 1u ||
					!document.HasMember("blocks") || !document["blocks"].IsArray()) return false;
				for (const auto& entry : document["blocks"].GetArray()) {
					if (!entry.IsObject() || !entry.HasMember("id") || !entry["id"].IsUint() ||
						!entry.HasMember("name") || !entry["name"].IsString()) return false;
					const blockId savedId = entry["id"].GetUint();
					if (savedId == 0u || savedId > BLOCK_TYPE_MASK || _savedBlockNames.contains(savedId)) return false;
					const std::string name(entry["name"].GetString(), entry["name"].GetStringLength());
					_savedBlockNames.emplace(savedId, name);
					const auto current = _currentBlockIds.find(name);
					const blockId runtime = current == _currentBlockIds.end() ? 0u : current->second;
					_savedBlockIds.emplace(savedId, runtime);
					if (runtime != 0u) _runtimeBlockIds.emplace(runtime, savedId);
				}
			}

			blockId nextSavedId = 1u;
			for (const auto& [runtime, name] : _currentBlockNames) {
				if (_runtimeBlockIds.contains(runtime)) continue;
				while (nextSavedId <= BLOCK_TYPE_MASK && _savedBlockNames.contains(nextSavedId)) ++nextSavedId;
				if (nextSavedId > BLOCK_TYPE_MASK) return false;
				_savedBlockNames.emplace(nextSavedId, name);
				_savedBlockIds.emplace(nextSavedId, runtime);
				_runtimeBlockIds.emplace(runtime, nextSavedId);
			}
			return true;
		}

		bool saveContentRegistry() const {
			if (!_contentRegistryConfigured || _path.empty()) return false;
			rapidjson::Document document;
			document.SetObject();
			auto& allocator = document.GetAllocator();
			document.AddMember("schemaVersion", 1u, allocator);
			rapidjson::Value blocks(rapidjson::kArrayType);
			std::vector<std::pair<blockId, std::string>> ordered(
				_savedBlockNames.begin(), _savedBlockNames.end());
			std::sort(ordered.begin(), ordered.end());
			for (const auto& [id, name] : ordered) {
				rapidjson::Value entry(rapidjson::kObjectType);
				entry.AddMember("id", id, allocator);
				rapidjson::Value value;
				value.SetString(name.c_str(), static_cast<rapidjson::SizeType>(name.size()), allocator);
				entry.AddMember("name", value, allocator);
				blocks.PushBack(entry, allocator);
			}
			document.AddMember("blocks", blocks, allocator);

			rapidjson::StringBuffer buffer;
			rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
			writer.SetIndent(' ', 2);
			document.Accept(writer);
			const auto target = _path / "content_registry.json";
			auto temporary = target;
			temporary += ".tmp";
			std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
			if (!output) return false;
			output.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
			output.flush();
			const bool good = output.good();
			output.close();
			return good && replaceFile(temporary, target);
		}

		bool loadEntityData() {
			std::ifstream file(_path / "entities.dat", std::ios::binary);
			if (!file) {
				std::lock_guard<std::mutex> lock(_entitiesMutex);
				_entities.clear();
				++_entitiesRevision;
				return true;
			}

			uint32_t magic = 0;
			uint32_t version = 0;
			uint32_t count = 0;
			if (!readBytes(file, &magic, sizeof(magic)) ||
				!readBytes(file, &version, sizeof(version)) ||
				!readBytes(file, &count, sizeof(count)) ||
				magic != ENTITY_FILE_MAGIC || version != ENTITY_FILE_VERSION || count > 1'000'000u)
				return false;

			std::vector<savedEntity> loaded;
			std::unordered_set<uint64_t> loadedIds;
			loaded.reserve(count);
			loadedIds.reserve(count);
			for (uint32_t index = 0; index < count; ++index) {
				savedEntity entity;
				uint32_t kind = 0;
				const bool good = readBytes(file, &entity.id, sizeof(entity.id)) &&
					readBytes(file, &kind, sizeof(kind)) &&
					readBytes(file, &entity.flags, sizeof(entity.flags)) &&
					readBytes(file, &entity.position, sizeof(entity.position)) &&
					readBytes(file, &entity.velocity, sizeof(entity.velocity)) &&
					readBytes(file, &entity.rotation, sizeof(entity.rotation)) &&
					readBytes(file, &entity.scale, sizeof(entity.scale)) &&
					readBytes(file, &entity.yaw, sizeof(entity.yaw)) &&
					readBytes(file, &entity.pitch, sizeof(entity.pitch)) &&
					readBytes(file, &entity.assetId, sizeof(entity.assetId)) &&
					readBytes(file, &entity.itemId, sizeof(entity.itemId)) &&
					readBytes(file, &entity.itemCount, sizeof(entity.itemCount)) &&
					readBytes(file, &entity.variant, sizeof(entity.variant));
				if (!good || entity.id == 0 || kind < static_cast<uint32_t>(savedEntityKind::player) ||
					kind > static_cast<uint32_t>(savedEntityKind::item)) return false;
				entity.kind = static_cast<savedEntityKind>(kind);
				entity.itemId = remapSavedBlockState(entity.itemId);
				if (!loadedIds.insert(entity.id).second) return false;
				loaded.push_back(entity);
			}

			std::lock_guard<std::mutex> lock(_entitiesMutex);
			_entities = std::move(loaded);
			++_entitiesRevision;
			return true;
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
			if (!_terrainConfigured || !_contentRegistryConfigured) return false;
			_path = path;
			_seed = seed;

			delete _generator;
			_generator = new terrainGenerator(_seed, _terrainBlocks, _terrainBiomes);

			if (!ensureStorageDirectories())
				return false;
			_savedBlockIds.clear();
			_runtimeBlockIds.clear();
			_savedBlockNames.clear();
			for (const auto& [runtime, name] : _currentBlockNames) {
				_savedBlockIds.emplace(runtime, runtime);
				_runtimeBlockIds.emplace(runtime, runtime);
				_savedBlockNames.emplace(runtime, name);
			}

			{
				std::lock_guard<std::mutex> lock(_savedChunksMutex);
				_savedChunks = scanSavedChunks();
				++_manifestRevision;
				_manifestDirty = true;
			}
			{
				std::lock_guard<std::mutex> lock(_entitiesMutex);
				_entities.clear();
				++_entitiesRevision;
			}

			return saveWorld();
		}

		bool open(const std::string& path, uint64_t createSeed = 0, bool createIfMissing = false) {
			_path = path;
			if (load())
				return true;
			if (!createIfMissing)
				return false;
			return create(path, createSeed);
		}

		bool isOpen() const {
			return !_path.empty() && _generator != nullptr;
		}

		void closeSession() {
			{
				std::lock_guard<std::mutex> lock(_savedChunksMutex);
				_savedChunks.clear();
				++_manifestRevision;
				_manifestDirty = false;
			}
			{
				std::lock_guard<std::mutex> lock(_entitiesMutex);
				_entities.clear();
				++_entitiesRevision;
			}
			delete _generator;
			_generator = nullptr;
			_path.clear();
			_seed = 0;
		}

		bool recreate(uint64_t seed) {
			std::error_code error;
			if (!_path.empty())
				std::filesystem::remove_all(_path, error);
			return create(_path.string(), seed);
		}

		bool load() {
			if (!_terrainConfigured || !_contentRegistryConfigured) return false;
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
			if (!loadContentRegistry()) return false;

			delete _generator;
			_generator = new terrainGenerator(_seed, _terrainBlocks, _terrainBiomes);

			const bool entityDataValid = loadEntityData();
			if (!entityDataValid) {
				std::lock_guard<std::mutex> lock(_entitiesMutex);
				_entities.clear();
				++_entitiesRevision;
			}
			// Version 2 writes an authoritative manifest atomically. Trust it on the
			// normal path instead of opening every .chk file during every launch.
			// Legacy worlds still receive the full directory scan during migration.
			if (version != WORLD_FILE_VERSION)
				manifest = scanSavedChunks();
			const bool needsMigration = version != WORLD_FILE_VERSION || !entityDataValid;
			{
				std::lock_guard<std::mutex> lock(_savedChunksMutex);
				_savedChunks = std::move(manifest);
				++_manifestRevision;
				_manifestDirty = needsMigration;
			}
			if (needsMigration) return saveWorld();

			// A successful load is read-only. Entity data is already current and
			// does not need to be sorted and rewritten before the first frame.
			return true;
		}

		bool saveEntityData() const {
			std::lock_guard<std::mutex> fileLock(_entityFileMutex);
			if (!ensureStorageDirectories()) return false;
			std::vector<savedEntity> entities;
			{
				std::lock_guard<std::mutex> lock(_entitiesMutex);
				entities = _entities;
			}
			std::sort(entities.begin(), entities.end(), [](const savedEntity& a, const savedEntity& b) {
				return a.id < b.id;
			});

			const auto target = _path / "entities.dat";
			const auto temporary = _path / "entities.dat.tmp";
			std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
			if (!file) return false;
			const uint32_t magic = ENTITY_FILE_MAGIC;
			const uint32_t version = ENTITY_FILE_VERSION;
			const uint32_t count = static_cast<uint32_t>(entities.size());
			bool good = writeBytes(file, &magic, sizeof(magic)) &&
				writeBytes(file, &version, sizeof(version)) &&
				writeBytes(file, &count, sizeof(count));
			for (savedEntity entity : entities) {
				entity.itemId = persistentBlockState(entity.itemId);
				const uint32_t kind = static_cast<uint32_t>(entity.kind);
				good = good && writeBytes(file, &entity.id, sizeof(entity.id)) &&
					writeBytes(file, &kind, sizeof(kind)) &&
					writeBytes(file, &entity.flags, sizeof(entity.flags)) &&
					writeBytes(file, &entity.position, sizeof(entity.position)) &&
					writeBytes(file, &entity.velocity, sizeof(entity.velocity)) &&
					writeBytes(file, &entity.rotation, sizeof(entity.rotation)) &&
					writeBytes(file, &entity.scale, sizeof(entity.scale)) &&
					writeBytes(file, &entity.yaw, sizeof(entity.yaw)) &&
					writeBytes(file, &entity.pitch, sizeof(entity.pitch)) &&
					writeBytes(file, &entity.assetId, sizeof(entity.assetId)) &&
					writeBytes(file, &entity.itemId, sizeof(entity.itemId)) &&
					writeBytes(file, &entity.itemCount, sizeof(entity.itemCount)) &&
					writeBytes(file, &entity.variant, sizeof(entity.variant));
			}
			file.flush();
			good = good && file.good();
			file.close();
			return good && replaceFile(temporary, target);
		}

		std::vector<savedEntity> entities() const {
			std::lock_guard<std::mutex> lock(_entitiesMutex);
			return _entities;
		}

		std::optional<savedEntity> entity(uint64_t entityId) const {
			std::lock_guard<std::mutex> lock(_entitiesMutex);
			const auto found = std::find_if(_entities.begin(), _entities.end(),
				[entityId](const savedEntity& value) { return value.id == entityId; });
			if (found == _entities.end()) return std::nullopt;
			return *found;
		}

		void upsertEntity(const savedEntity& value) {
			if (value.id == 0) return;
			std::lock_guard<std::mutex> lock(_entitiesMutex);
			const auto found = std::find_if(_entities.begin(), _entities.end(),
				[&](const savedEntity& existing) { return existing.id == value.id; });
			if (found == _entities.end()) _entities.push_back(value);
			else *found = value;
			++_entitiesRevision;
		}

		bool removeEntity(uint64_t entityId) {
			std::lock_guard<std::mutex> lock(_entitiesMutex);
			const auto found = std::remove_if(_entities.begin(), _entities.end(),
				[entityId](const savedEntity& value) { return value.id == entityId; });
			if (found == _entities.end()) return false;
			_entities.erase(found, _entities.end());
			++_entitiesRevision;
			return true;
		}

		bool saveWorld() const {
			if (_path.empty())
				return false;
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
			return saveEntityData() && saveContentRegistry();
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

			if (header._version != CHUNK_FILE_VERSION &&
				header._version != PREVIOUS_CHUNK_FILE_VERSION &&
				header._version != LEGACY_CHUNK_FILE_VERSION)
				return false;

			if (header._x != position.x ||
				header._y != position.y ||
				header._z != position.z)
				return false;

			const uint32_t sectionCount = header._version == LEGACY_CHUNK_FILE_VERSION ? 16u : CHUNK_SUBCHUNKS;
			for (uint32_t section = 0; section < sectionCount; ++section)
				if (!c._subchunks[section].load(file))
					return false;

			c._position = position;
			if (header._version != CHUNK_FILE_VERSION || !_savedBlockIds.empty()) {
				for (subchunk& section : c._subchunks) {
					section.mapStates([&](blockId state) {
						// Pre-v12 files used the old core registry id for a lit torch.
						constexpr blockId LEGACY_REDSTONE_TORCH_ID = 698u;
						if (header._version != CHUNK_FILE_VERSION &&
							blockType(state) == LEGACY_REDSTONE_TORCH_ID)
							state = withBlockPowered(state, true);
						return normalizeBlockState(remapSavedBlockState(state));
					});
				}
				c._dirty = header._version != CHUNK_FILE_VERSION;
			}
			else {
				c._dirty = false;
			}

			return true;
		}

		bool saveChunk(const chunk& c) const {
			if (_path.empty()) return false;
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
			for (const subchunk& s : c._subchunks) {
				subchunk persisted = s;
				persisted.mapStates([&](blockId state) { return persistentBlockState(state); });
				good = good && persisted.save(file);
			}
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
				if (header._magic == WORLD_FILE_MAGIC &&
					(header._version == CHUNK_FILE_VERSION ||
						header._version == PREVIOUS_CHUNK_FILE_VERSION ||
						header._version == LEGACY_CHUNK_FILE_VERSION))
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
			_generator = _terrainConfigured
				? new terrainGenerator(_seed, _terrainBlocks, _terrainBiomes)
				: nullptr;
		}

		void configureTerrain(terrainBlockPalette blocks, terrainBiomeConfig biomes) {
			_terrainBlocks = std::move(blocks);
			_terrainBiomes = std::move(biomes);
			_terrainConfigured = true;
			if (_generator) {
				delete _generator;
				_generator = new terrainGenerator(_seed, _terrainBlocks, _terrainBiomes);
			}
		}

		void configureContentRegistry(const staticAssetManager& blocks) {
			_currentBlockIds.clear();
			_currentBlockNames.clear();
			_legacyBlockIds.clear();
			for (const uint32_t id : blocks.ids()) {
				const std::string name = blocks.persistentName(id);
				_currentBlockIds.emplace(name, id);
				_currentBlockNames.emplace(id, name);
			}
			for (uint32_t id = 1u; id <= BLOCK_TYPE_MASK; ++id) {
				const uint32_t runtimeId = blocks.legacyIdToRuntime(id);
				if (runtimeId != 0u) _legacyBlockIds.emplace(id, runtimeId);
			}
			_contentRegistryConfigured = true;
		}

		blockId remapSavedBlockState(blockId state) const {
			const blockId type = blockType(state);
			if (type == 0u || _savedBlockIds.empty()) return state;
			const auto found = _savedBlockIds.find(type);
			const blockId runtimeType = found == _savedBlockIds.end() ? 0u : found->second;
			return (state & ~BLOCK_TYPE_MASK) | runtimeType;
		}

		blockId persistentBlockState(blockId state) const {
			const blockId type = blockType(state);
			if (type == 0u) return state;
			const auto found = _runtimeBlockIds.find(type);
			const blockId savedType = found == _runtimeBlockIds.end() ? 0u : found->second;
			return (state & ~BLOCK_TYPE_MASK) | savedType;
		}

		const terrainBlockPalette& terrainBlocks() const { return _terrainBlocks; }
		const terrainBiomeConfig& terrainBiomes() const { return _terrainBiomes; }

		terrainGenerator* generator() const {
			return _generator;
		}
	};

}
