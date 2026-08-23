#pragma once
#include <DirectXMath.h>
#include <header/utility.h>
#include <assetManager/assetManager.h>
#include <stdint.h>
#include <array>
#include <vector>
#include <cstddef>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <chrono>

namespace ac {

#define CHUNK_WIDTH 16
#define CHUNK_LENGTH 16
#define CHUNK_HEIGHT 255
#define CHUNK_VOLUME (CHUNK_WIDTH * CHUNK_LENGTH * CHUNK_HEIGHT)
#define SUBCHUNK_WIDTH 16
#define SUBCHUNK_LENGTH 16
#define SUBCHUNK_HEIGHT 16
#define SUBCHUNK_VOLUME (SUBCHUNK_WIDTH * SUBCHUNK_LENGTH * SUBCHUNK_HEIGHT)
#define CHUNK_SUBCHUNKS ((CHUNK_HEIGHT + SUBCHUNK_HEIGHT - 1) / SUBCHUNK_HEIGHT)

	using blockId = uint32_t;
	using dataTypeId = uint16_t;
	using dataId = uint16_t;

	struct subchunk {
	private:
		blockId _single = 0;
		std::vector<blockId> _palette;
		std::vector<uint64_t> _blocks;
		uint8_t _bits = 0;

		static constexpr uint32_t index(uint32_t x, uint32_t y, uint32_t z) { return x + SUBCHUNK_WIDTH * (z + SUBCHUNK_LENGTH * y); }

		static uint8_t bitsFor(size_t count) {
			if (count <= 1) return 0;
			uint8_t bits = 0;
			--count;
			while (count) { ++bits; count >>= 1; }
			return bits;
		}

		uint32_t getPacked(uint32_t i) const {
			if (!_bits) return 0;
			const uint64_t bit = static_cast<uint64_t>(i) * _bits;
			const uint32_t word = static_cast<uint32_t>(bit >> 6);
			const uint32_t shift = static_cast<uint32_t>(bit & 63);
			uint64_t value = _blocks[word] >> shift;
			const uint32_t remaining = 64 - shift;
			if (remaining < _bits && word + 1 < _blocks.size()) value |= _blocks[word + 1] << remaining;
			return static_cast<uint32_t>(value & ((1ULL << _bits) - 1ULL));
		}

		void setPacked(uint32_t i, uint32_t value) {
			if (!_bits) return;
			const uint64_t bit = static_cast<uint64_t>(i) * _bits;
			const uint32_t word = static_cast<uint32_t>(bit >> 6);
			const uint32_t shift = static_cast<uint32_t>(bit & 63);
			const uint64_t mask = (1ULL << _bits) - 1ULL;
			value &= static_cast<uint32_t>(mask);
			_blocks[word] = (_blocks[word] & ~(mask << shift)) | (static_cast<uint64_t>(value) << shift);
			const uint32_t remaining = 64 - shift;
			if (remaining < _bits && word + 1 < _blocks.size()) {
				const uint32_t secondBits = _bits - remaining;
				const uint64_t secondMask = (1ULL << secondBits) - 1ULL;
				_blocks[word + 1] = (_blocks[word + 1] & ~secondMask) | ((static_cast<uint64_t>(value >> remaining) & secondMask));
			}
		}

		uint32_t paletteIndex(blockId id) const {
			for (uint32_t i = 0; i < _palette.size(); ++i)
				if (_palette[i] == id) return i;
			return UINT32_MAX;
		}

		void resizePacked(uint8_t newBits) {
			if (newBits == _bits) return;
			const size_t wordCount = (static_cast<size_t>(SUBCHUNK_VOLUME) * newBits + 63) / 64;
			std::vector<uint64_t> old = std::move(_blocks);
			const uint8_t oldBits = _bits;
			_blocks.assign(wordCount, 0);
			_bits = newBits;
			if (!oldBits) return;

			for (uint32_t i = 0; i < SUBCHUNK_VOLUME; ++i) {
				const uint64_t bit = static_cast<uint64_t>(i) * oldBits;
				const uint32_t word = static_cast<uint32_t>(bit >> 6);
				const uint32_t shift = static_cast<uint32_t>(bit & 63);
				uint64_t value = old[word] >> shift;
				const uint32_t remaining = 64 - shift;
				if (remaining < oldBits && word + 1 < old.size()) value |= old[word + 1] << remaining;
				setPacked(i, static_cast<uint32_t>(value & ((1ULL << oldBits) - 1ULL)));
			}
		}

		uint32_t addPalette(blockId id) {
			const uint32_t existing = paletteIndex(id);
			if (existing != UINT32_MAX) return existing;
			const uint8_t oldBits = _bits;
			_palette.push_back(id);
			const uint8_t newBits = bitsFor(_palette.size());
			if (newBits != oldBits) resizePacked(newBits);
			return static_cast<uint32_t>(_palette.size() - 1);
		}

	public:
		blockId getBlock(uint32_t x, uint32_t y, uint32_t z) const {
			if (x >= SUBCHUNK_WIDTH || y >= SUBCHUNK_HEIGHT || z >= SUBCHUNK_LENGTH) return 0;
			if (!_bits) return _single;
			return _palette[getPacked(index(x, y, z))];
		}

		void setBlock(uint32_t x, uint32_t y, uint32_t z, blockId id) {
			if (x >= SUBCHUNK_WIDTH || y >= SUBCHUNK_HEIGHT || z >= SUBCHUNK_LENGTH) return;
			const uint32_t i = index(x, y, z);

			if (!_bits) {
				if (_single == id) return;
				const blockId old = _single;
				_palette.clear();
				_palette.push_back(old);
				const uint32_t pi = addPalette(id);
				for (uint32_t n = 0; n < SUBCHUNK_VOLUME; ++n) setPacked(n, 0);
				setPacked(i, pi);
				return;
			}

			setPacked(i, addPalette(id));
		}

		void fill(blockId id) {
			_single = id;
			_palette.clear();
			_blocks.clear();
			_bits = 0;
		}

		bool isEmpty() const { return !_bits && _single == 0; }
		bool isSingle() const { return !_bits; }
		blockId singleBlock() const { return _single; }
		const std::vector<blockId>& palette() const { return _palette; }
		size_t paletteSize() const { return _bits ? _palette.size() : 1; }
		uint8_t bits() const { return _bits; }
		void clear() { fill(0); }

		void buildFromDense(const blockId* source) {
			const blockId first = source[0];
			bool single = true;
			for (uint32_t i = 1; i < SUBCHUNK_VOLUME; ++i) {
				if (source[i] != first) { single = false; break; }
			}

			if (single) {
				_single = first;
				_palette.clear();
				_blocks.clear();
				_bits = 0;
				return;
			}

			_single = 0;
			_palette.clear();
			_blocks.clear();
			_palette.reserve(8);
			std::unordered_map<blockId, uint32_t> lookup;
			bool useLookup = false;

			for (uint32_t i = 0; i < SUBCHUNK_VOLUME; ++i) {
				const blockId id = source[i];

				if (useLookup) {
					if (lookup.find(id) == lookup.end()) {
						const uint32_t paletteEntry = static_cast<uint32_t>(_palette.size());
						_palette.push_back(id);
						lookup.emplace(id, paletteEntry);
					}
					continue;
				}

				if (std::find(_palette.begin(), _palette.end(), id) != _palette.end())
					continue;

				_palette.push_back(id);

				if (_palette.size() == 16) {
					lookup.reserve(32);
					for (uint32_t entry = 0; entry < _palette.size(); ++entry)
						lookup.emplace(_palette[entry], entry);
					useLookup = true;
				}
			}

			_bits = bitsFor(_palette.size());
			const size_t wordCount = (static_cast<size_t>(SUBCHUNK_VOLUME) * _bits + 63) / 64;
			_blocks.assign(wordCount, 0);

			for (uint32_t i = 0; i < SUBCHUNK_VOLUME; ++i) {
				const uint32_t entry = useLookup
					? lookup.find(source[i])->second
					: paletteIndex(source[i]);
				setPacked(i, entry);
			}
		}

		bool save(std::ofstream& file) const {
			file.write(reinterpret_cast<const char*>(&_single), sizeof(_single));
			file.write(reinterpret_cast<const char*>(&_bits), sizeof(_bits));
			const uint32_t paletteCount = static_cast<uint32_t>(_palette.size());
			const uint32_t blockCount = static_cast<uint32_t>(_blocks.size());
			file.write(reinterpret_cast<const char*>(&paletteCount), sizeof(paletteCount));
			file.write(reinterpret_cast<const char*>(&blockCount), sizeof(blockCount));
			if (paletteCount) file.write(reinterpret_cast<const char*>(_palette.data()), sizeof(blockId) * paletteCount);
			if (blockCount) file.write(reinterpret_cast<const char*>(_blocks.data()), sizeof(uint64_t) * blockCount);
			return file.good();
		}

		bool load(std::ifstream& file) {
			uint32_t paletteCount = 0, blockCount = 0;
			if (!file.read(reinterpret_cast<char*>(&_single), sizeof(_single))) return false;
			if (!file.read(reinterpret_cast<char*>(&_bits), sizeof(_bits))) return false;
			if (!file.read(reinterpret_cast<char*>(&paletteCount), sizeof(paletteCount))) return false;
			if (!file.read(reinterpret_cast<char*>(&blockCount), sizeof(blockCount))) return false;
			if (paletteCount > SUBCHUNK_VOLUME || blockCount > SUBCHUNK_VOLUME) return false;
			_palette.resize(paletteCount);
			_blocks.resize(blockCount);
			if (paletteCount && !file.read(reinterpret_cast<char*>(_palette.data()), sizeof(blockId) * paletteCount)) return false;
			if (blockCount && !file.read(reinterpret_cast<char*>(_blocks.data()), sizeof(uint64_t) * blockCount)) return false;
			return true;
		}
	};

	class chunkData {
	private:
		std::vector<std::byte> _storage;
	public:
		void clear() { _storage.clear(); }
		void resize(size_t size) { _storage.resize(size); }
		size_t size() const { return _storage.size(); }
		std::byte* data() { return _storage.data(); }
		const std::byte* data() const { return _storage.data(); }
	};

	struct chunkMesh {
		model _opaque;
		model _translucent;
	};
	using chunkSectionMeshes = std::array<chunkMesh, CHUNK_SUBCHUNKS>;

	struct chunkMeshTimings {
		uint64_t cacheNanoseconds = 0;
		uint64_t faceNanoseconds = 0;
	};

	struct chunk {
		DirectX::XMINT3 _position = {};
		std::array<subchunk, CHUNK_SUBCHUNKS> _subchunks{};
		chunkData _data;
		bool _dirty = true;

		static constexpr uint32_t subchunkIndex(uint32_t y) { return y / SUBCHUNK_HEIGHT; }

		blockId getBlock(uint32_t x, uint32_t y, uint32_t z) const {
			if (x >= CHUNK_WIDTH || y >= CHUNK_HEIGHT || z >= CHUNK_LENGTH) return 0;
			return _subchunks[subchunkIndex(y)].getBlock(x, y % SUBCHUNK_HEIGHT, z);
		}

		void setBlock(uint32_t x, uint32_t y, uint32_t z, blockId id) {
			if (x >= CHUNK_WIDTH || y >= CHUNK_HEIGHT || z >= CHUNK_LENGTH) return;
			_subchunks[subchunkIndex(y)].setBlock(x, y % SUBCHUNK_HEIGHT, z, id);
			_dirty = true;
		}

		void fill(blockId id) {
			for (subchunk& s : _subchunks) s.fill(id);
			_dirty = true;
		}

		bool isSubchunkEmpty(uint32_t y) const { return y >= CHUNK_SUBCHUNKS || _subchunks[y].isEmpty(); }

		void buildFromDense(const std::array<blockId, CHUNK_VOLUME>& dense) {
			std::array<blockId, SUBCHUNK_VOLUME> local{};

			for (uint32_t sy = 0; sy < CHUNK_SUBCHUNKS; ++sy) {
				const uint32_t yStart = sy * SUBCHUNK_HEIGHT;
				const uint32_t yEnd = std::min(yStart + SUBCHUNK_HEIGHT, static_cast<uint32_t>(CHUNK_HEIGHT));

				local.fill(0);

				for (uint32_t y = yStart; y < yEnd; ++y)
					for (uint32_t z = 0; z < CHUNK_LENGTH; ++z)
						std::copy_n(
							&dense[CHUNK_WIDTH * (z + CHUNK_LENGTH * y)],
							CHUNK_WIDTH,
							&local[SUBCHUNK_WIDTH * (z + SUBCHUNK_LENGTH * (y - yStart))]
						);

				_subchunks[sy].buildFromDense(local.data());
			}

			_dirty = true;
		}
	};

	constexpr uint32_t MODEL_CUBE = 0;

	class chunkMesher {
	private:
		struct meshBlock {
			const blockDefinition* _definition = nullptr;
		};

		struct face {
			bool _visible = false;
			uint32_t _material = 0;
			float _opacity = 1.0f;
			RENDER_MODE _renderMode = RENDER_MODE_OPAQUE;
			std::array<uint8_t, 4> _ao = { 3u, 3u, 3u, 3u };
		};

		static constexpr int32_t CACHE_WIDTH = CHUNK_WIDTH + 2;
		static constexpr int32_t CACHE_LENGTH = CHUNK_LENGTH + 2;
		static constexpr int32_t CACHE_HEIGHT = CHUNK_HEIGHT + 2;
		static constexpr size_t CACHE_VOLUME =
			static_cast<size_t>(CACHE_WIDTH) * CACHE_LENGTH * CACHE_HEIGHT;

		// Include a one-block border around the chunk. AO and face visibility can
		// then use flat memory lookups instead of repeatedly decompressing neighbor
		// chunks and searching the asset registry. Keep this on the heap because a
		// mesher lives on each worker thread and the padded cache is fairly large.
		std::vector<meshBlock> _cache = std::vector<meshBlock>(CACHE_VOLUME);
		std::vector<face> _mask;
		const chunk* _neighbors[8] = {};
		int32_t _maxY = -1;

		static constexpr size_t cacheIndex(int32_t x, int32_t y, int32_t z) {
			return static_cast<size_t>(x + 1) +
				static_cast<size_t>(CACHE_WIDTH) *
				(static_cast<size_t>(z + 1) + static_cast<size_t>(CACHE_LENGTH) * static_cast<size_t>(y + 1));
		}

		void buildCache(const chunk& c, staticAssetManager& blocks) {
			std::fill(_cache.begin(), _cache.end(), meshBlock{});
			_maxY = -1;

			blockId cachedId = UINT32_MAX;
			meshBlock cachedBlock{};

			auto resolve = [&](blockId id) {
				if (!id) return meshBlock{};
				if (id == cachedId)
					return cachedBlock;

				cachedId = id;
				cachedBlock = {};

				const blockDefinition* definition = blocks.get(id);
				cachedBlock._definition = definition;

				return cachedBlock;
				};

			for (uint32_t sy = 0; sy < CHUNK_SUBCHUNKS; ++sy) {
				const subchunk& sub = c._subchunks[sy];
				if (sub.isEmpty()) continue;

				const uint32_t yStart = sy * SUBCHUNK_HEIGHT;
				const uint32_t yEnd = std::min(yStart + SUBCHUNK_HEIGHT, static_cast<uint32_t>(CHUNK_HEIGHT));

				if (sub.isSingle()) {
					const meshBlock block = resolve(sub.singleBlock());
					for (uint32_t y = yStart; y < yEnd; ++y)
						for (uint32_t z = 0; z < CHUNK_LENGTH; ++z) {
							const size_t begin = cacheIndex(0, static_cast<int32_t>(y), static_cast<int32_t>(z));
							std::fill_n(_cache.begin() + begin, CHUNK_WIDTH, block);
						}

					if (block._definition &&
						(block._definition->_model == MODEL_CUBE || block._definition->_occludes))
						_maxY = static_cast<int32_t>(yEnd) - 1;

					continue;
				}

				for (uint32_t y = yStart; y < yEnd; ++y) {
					for (uint32_t z = 0; z < CHUNK_LENGTH; ++z) {
						for (uint32_t x = 0; x < CHUNK_WIDTH; ++x) {
							const blockId id = sub.getBlock(x, y - yStart, z);
							if (!id) continue;

							const meshBlock block = resolve(id);
							_cache[cacheIndex(
								static_cast<int32_t>(x),
								static_cast<int32_t>(y),
								static_cast<int32_t>(z)
							)] = block;

							if (block._definition &&
								(block._definition->_model == MODEL_CUBE || block._definition->_occludes))
								_maxY = static_cast<int32_t>(y);
						}
					}
				}
			}

			// Cache the four side borders and four corner columns once. These are
			// precisely the cells needed by visibility tests and vertex AO.
			if (_maxY < 0) return;
			const int32_t borderMaxY = std::min<int32_t>(CHUNK_HEIGHT - 1, _maxY + 1);
			for (int32_t y = 0; y <= borderMaxY; ++y) {
				for (int32_t z = 0; z < CHUNK_LENGTH; ++z) {
					if (_neighbors[0]) _cache[cacheIndex(-1, y, z)] = resolve(_neighbors[0]->getBlock(CHUNK_WIDTH - 1, y, z));
					if (_neighbors[1]) _cache[cacheIndex(CHUNK_WIDTH, y, z)] = resolve(_neighbors[1]->getBlock(0, y, z));
				}

				for (int32_t x = 0; x < CHUNK_WIDTH; ++x) {
					if (_neighbors[2]) _cache[cacheIndex(x, y, -1)] = resolve(_neighbors[2]->getBlock(x, y, CHUNK_LENGTH - 1));
					if (_neighbors[3]) _cache[cacheIndex(x, y, CHUNK_LENGTH)] = resolve(_neighbors[3]->getBlock(x, y, 0));
				}

				if (_neighbors[4]) _cache[cacheIndex(-1, y, -1)] = resolve(_neighbors[4]->getBlock(CHUNK_WIDTH - 1, y, CHUNK_LENGTH - 1));
				if (_neighbors[5]) _cache[cacheIndex(-1, y, CHUNK_LENGTH)] = resolve(_neighbors[5]->getBlock(CHUNK_WIDTH - 1, y, 0));
				if (_neighbors[6]) _cache[cacheIndex(CHUNK_WIDTH, y, -1)] = resolve(_neighbors[6]->getBlock(0, y, CHUNK_LENGTH - 1));
				if (_neighbors[7]) _cache[cacheIndex(CHUNK_WIDTH, y, CHUNK_LENGTH)] = resolve(_neighbors[7]->getBlock(0, y, 0));
			}
		}

		void buildCacheRange(
			const chunk& c,
			staticAssetManager& blocks,
			int32_t yStart,
			int32_t yEnd
		) {
			yStart = std::clamp(yStart, 0, CHUNK_HEIGHT);
			yEnd = std::clamp(yEnd, yStart, CHUNK_HEIGHT);
			const int32_t sampleStart = std::max(-1, yStart - 1);
			const int32_t sampleEnd = std::min(CHUNK_HEIGHT, yEnd);
			for (int32_t y = sampleStart; y <= sampleEnd; ++y) {
				const size_t first = cacheIndex(-1, y, -1);
				std::fill_n(_cache.begin() + first, CACHE_WIDTH * CACHE_LENGTH, meshBlock{});
			}
			_maxY = -1;

			blockId cachedId = UINT32_MAX;
			meshBlock cachedBlock{};
			auto resolve = [&](blockId id) {
				if (!id) return meshBlock{};
				if (id == cachedId) return cachedBlock;
				cachedId = id;
				cachedBlock = { blocks.get(id) };
				return cachedBlock;
			};

			for (int32_t y = std::max(0, sampleStart); y < std::min(CHUNK_HEIGHT, sampleEnd + 1); ++y) {
				for (int32_t z = 0; z < CHUNK_LENGTH; ++z) {
					for (int32_t x = 0; x < CHUNK_WIDTH; ++x) {
						const meshBlock block = resolve(c.getBlock(x, y, z));
						_cache[cacheIndex(x, y, z)] = block;
						if (y >= yStart && y < yEnd && block._definition &&
							(block._definition->_model == MODEL_CUBE || block._definition->_occludes))
							_maxY = std::max(_maxY, y);
					}
				}

				for (int32_t z = 0; z < CHUNK_LENGTH; ++z) {
					if (_neighbors[0]) _cache[cacheIndex(-1, y, z)] = resolve(_neighbors[0]->getBlock(CHUNK_WIDTH - 1, y, z));
					if (_neighbors[1]) _cache[cacheIndex(CHUNK_WIDTH, y, z)] = resolve(_neighbors[1]->getBlock(0, y, z));
				}
				for (int32_t x = 0; x < CHUNK_WIDTH; ++x) {
					if (_neighbors[2]) _cache[cacheIndex(x, y, -1)] = resolve(_neighbors[2]->getBlock(x, y, CHUNK_LENGTH - 1));
					if (_neighbors[3]) _cache[cacheIndex(x, y, CHUNK_LENGTH)] = resolve(_neighbors[3]->getBlock(x, y, 0));
				}
				if (_neighbors[4]) _cache[cacheIndex(-1, y, -1)] = resolve(_neighbors[4]->getBlock(CHUNK_WIDTH - 1, y, CHUNK_LENGTH - 1));
				if (_neighbors[5]) _cache[cacheIndex(-1, y, CHUNK_LENGTH)] = resolve(_neighbors[5]->getBlock(CHUNK_WIDTH - 1, y, 0));
				if (_neighbors[6]) _cache[cacheIndex(CHUNK_WIDTH, y, -1)] = resolve(_neighbors[6]->getBlock(0, y, CHUNK_LENGTH - 1));
				if (_neighbors[7]) _cache[cacheIndex(CHUNK_WIDTH, y, CHUNK_LENGTH)] = resolve(_neighbors[7]->getBlock(0, y, 0));
			}
		}

		const blockDefinition* getDefinition(int32_t x, int32_t y, int32_t z) const {
			if (x < -1 || x > CHUNK_WIDTH || y < -1 || y > CHUNK_HEIGHT ||
				z < -1 || z > CHUNK_LENGTH)
				return nullptr;
			return _cache[cacheIndex(x, y, z)]._definition;
		}

		bool cube(int32_t x, int32_t y, int32_t z) const {
			const blockDefinition* definition = getDefinition(x, y, z);
			return definition && definition->_occludes &&
				definition->_renderMode != RENDER_MODE_TRANSPARENT;
		}

		bool faceVisible(
			blockId id,
			const blockDefinition& definition,
			int32_t neighborX,
			int32_t neighborY,
			int32_t neighborZ
		) const {
			const blockDefinition* neighbor = getDefinition(neighborX, neighborY, neighborZ);
			if (!neighbor) return true;

			if (definition._renderMode == RENDER_MODE_TRANSPARENT) {
				// Adjacent blocks of the same translucent type share no visible face.
				return neighbor->_renderMode != RENDER_MODE_TRANSPARENT || neighbor->_id != id;
			}

			// Opaque and cutout geometry must remain visible behind glass or water.
			return neighbor->_renderMode == RENDER_MODE_TRANSPARENT || !neighbor->_occludes;
		}

		static void addQuad(model& mesh, const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, const DirectX::XMFLOAT3& c, const DirectX::XMFLOAT3& d, const DirectX::XMFLOAT3& normal, uint32_t material, float opacity, float uvWidth, float uvHeight, bool flipV, uint32_t ao0, uint32_t ao1, uint32_t ao2, uint32_t ao3) {
			const uint32_t base = static_cast<uint32_t>(mesh._vertex.size());
			vertex v0{}, v1{}, v2{}, v3{};
			v0._position = a; v1._position = b; v2._position = c; v3._position = d;
			v0._normal = normal; v1._normal = normal; v2._normal = normal; v3._normal = normal;
			if (flipV) {
				v0._uv = { 0, uvHeight }; v1._uv = { uvWidth, uvHeight };
				v2._uv = { uvWidth, 0 }; v3._uv = { 0, 0 };
			}
			else {
				v0._uv = { 0, 0 }; v1._uv = { uvWidth, 0 };
				v2._uv = { uvWidth, uvHeight }; v3._uv = { 0, uvHeight };
			}
			v0._material = material; v1._material = material; v2._material = material; v3._material = material;
			// Store all four AO corners on every vertex. The pixel shader uses
			// bilinear interpolation, avoiding a visible lighting diagonal between
			// the two triangles that form this quad.
			const uint32_t uvAO0 = flipV ? ao3 : ao0;
			const uint32_t uvAO1 = flipV ? ao2 : ao1;
			const uint32_t uvAO2 = flipV ? ao1 : ao2;
			const uint32_t uvAO3 = flipV ? ao0 : ao3;
			const uint32_t packedAO = uvAO0 | (uvAO1 << 2) | (uvAO2 << 4) | (uvAO3 << 6);
			v0._ao = packedAO; v1._ao = packedAO; v2._ao = packedAO; v3._ao = packedAO;
			v0._opacity = opacity; v1._opacity = opacity; v2._opacity = opacity; v3._opacity = opacity;
			mesh._vertex.resize(static_cast<size_t>(base) + 4);
			mesh._vertex[base] = v0;
			mesh._vertex[base + 1] = v1;
			mesh._vertex[base + 2] = v2;
			mesh._vertex[base + 3] = v3;

			// Bilinear AO is independent of the mesh diagonal, so keep one stable
			// winding for every quad. The old AO-selected alternate split produced
			// half-quad triangles on some side faces under back-face culling.
			const size_t indexBase = mesh._index.size();
			mesh._index.resize(indexBase + 6);
			mesh._index[indexBase] = base;
			mesh._index[indexBase + 1] = base + 1;
			mesh._index[indexBase + 2] = base + 2;
			mesh._index[indexBase + 3] = base;
			mesh._index[indexBase + 4] = base + 2;
			mesh._index[indexBase + 5] = base + 3;
		}

		void meshFace(chunkMesh& output, int direction, int32_t yStart, int32_t yEnd) {
			yStart = std::clamp(yStart, 0, CHUNK_HEIGHT);
			yEnd = std::clamp(yEnd, yStart, CHUNK_HEIGHT);
			if (_maxY < yStart || yStart == yEnd) return;
			yEnd = std::min(yEnd, _maxY + 1);

			int width, height, depth;
			const int activeHeight = yEnd - yStart;
			if (direction < 2) { width = CHUNK_LENGTH; height = activeHeight; depth = CHUNK_WIDTH; }
			else if (direction < 4) { width = CHUNK_WIDTH; height = CHUNK_LENGTH; depth = activeHeight; }
			else { width = CHUNK_WIDTH; height = activeHeight; depth = CHUNK_LENGTH; }

			const size_t maskSize = static_cast<size_t>(width) * height;
			if (_mask.size() != maskSize) _mask.resize(maskSize);

			auto solid = [&](int x, int y, int z) -> bool {
				return cube(x, y, z);
				};

			auto vertexAO = [&](int x, int y, int z, int sx, int sy, int sz, int tx, int ty, int tz) -> uint8_t {
				const bool side1 = solid(x + sx, y + sy, z + sz);
				const bool side2 = solid(x + tx, y + ty, z + tz);
				if (side1 && side2) return 0;
				const bool corner = solid(x + sx + tx, y + sy + ty, z + sz + tz);
				return 3u - static_cast<uint32_t>(side1) - static_cast<uint32_t>(side2) - static_cast<uint32_t>(corner);
				};

			auto faceAO = [&](int x, int y, int z) -> std::array<uint8_t, 4> {
				auto sample = [&](int bx, int by, int bz, int sx, int sy, int sz, int tx, int ty, int tz) {
					return vertexAO(bx, by, bz, sx, sy, sz, tx, ty, tz);
					};

				switch (direction) {
				case 0: // -X
					return {
						sample(x - 1, y, z, 0, -1, 0, 0, 0, -1),
						sample(x - 1, y, z, 0, -1, 0, 0, 0, 1),
						sample(x - 1, y, z, 0, 1, 0, 0, 0, 1),
						sample(x - 1, y, z, 0, 1, 0, 0, 0, -1)
					};
				case 1: // +X
					return {
						sample(x + 1, y, z, 0, -1, 0, 0, 0, 1),
						sample(x + 1, y, z, 0, -1, 0, 0, 0, -1),
						sample(x + 1, y, z, 0, 1, 0, 0, 0, -1),
						sample(x + 1, y, z, 0, 1, 0, 0, 0, 1)
					};
				case 2: // -Y
					return {
						sample(x, y - 1, z, -1, 0, 0, 0, 0, -1),
						sample(x, y - 1, z, 1, 0, 0, 0, 0, -1),
						sample(x, y - 1, z, 1, 0, 0, 0, 0, 1),
						sample(x, y - 1, z, -1, 0, 0, 0, 0, 1)
					};
				case 3: // +Y
					return {
						sample(x, y + 1, z, 1, 0, 0, 0, 0, -1),
						sample(x, y + 1, z, -1, 0, 0, 0, 0, -1),
						sample(x, y + 1, z, -1, 0, 0, 0, 0, 1),
						sample(x, y + 1, z, 1, 0, 0, 0, 0, 1)
					};
				case 4: // -Z
					return {
						sample(x, y, z - 1, 1, 0, 0, 0, -1, 0),
						sample(x, y, z - 1, -1, 0, 0, 0, -1, 0),
						sample(x, y, z - 1, -1, 0, 0, 0, 1, 0),
						sample(x, y, z - 1, 1, 0, 0, 0, 1, 0)
					};
				default: // +Z
					return {
						sample(x, y, z + 1, -1, 0, 0, 0, -1, 0),
						sample(x, y, z + 1, 1, 0, 0, 0, -1, 0),
						sample(x, y, z + 1, 1, 0, 0, 0, 1, 0),
						sample(x, y, z + 1, -1, 0, 0, 0, 1, 0)
					};
				}
				};

			for (int slice = 0; slice < depth; ++slice) {
				for (int i = 0; i < width; ++i) {
					const size_t row = static_cast<size_t>(i) * height;
					for (int j = 0; j < height; ++j) {
						face& f = _mask[row + j];
						f._visible = false;

						int x, y, z;
						if (direction < 2) { x = slice; y = yStart + j; z = i; }
						else if (direction < 4) { x = i; y = yStart + slice; z = j; }
						else { x = i; y = yStart + j; z = slice; }

						const meshBlock& block = _cache[cacheIndex(x, y, z)];
						if (!block._definition ||
							block._definition->_model != MODEL_CUBE)
							continue;

						int nx = x, ny = y, nz = z;
						switch (direction) {
						case 0: --nx; break;
						case 1: ++nx; break;
						case 2: --ny; break;
						case 3: ++ny; break;
						case 4: --nz; break;
						case 5: ++nz; break;
						}

						if (faceVisible(block._definition->_id, *block._definition, nx, ny, nz)) {
							f._visible = true;
							f._material = block._definition->materialForFace(static_cast<uint32_t>(direction));
							f._opacity = block._definition->_opacity;
							f._renderMode = block._definition->_renderMode;
							f._ao = faceAO(x, y, z);
						}
					}
				}

				for (int i = 0; i < width; ++i) {
					const size_t row = static_cast<size_t>(i) * height;

					for (int j = 0; j < height;) {
						face& f = _mask[row + j];
						if (!f._visible) { ++j; continue; }

						int w = 1;
						int h = 1;

						const bool uniformAO =
							f._ao[0] == f._ao[1] &&
							f._ao[0] == f._ao[2] &&
							f._ao[0] == f._ao[3];

							auto matches = [&](const face& n) {
							return n._visible &&
								n._material == f._material &&
								n._opacity == f._opacity &&
								n._renderMode == f._renderMode &&
								n._ao == f._ao;
							};

						// A quad with varying AO must stay block-sized; otherwise the
						// four corner values get stretched across the whole greedy quad.
						if (uniformAO) {
							while (i + w < width) {
								const face& n = _mask[static_cast<size_t>(i + w) * height + j];
								if (!matches(n)) break;
								++w;
							}

							while (j + h < height) {
								bool valid = true;
								for (int k = 0; k < w; ++k) {
									const face& n = _mask[static_cast<size_t>(i + k) * height + j + h];
									if (!matches(n)) { valid = false; break; }
								}
								if (!valid) break;
								++h;
							}
						}

						float x0, x1, y0, y1, z0, z1;

						if (direction < 2) {
							x0 = x1 = static_cast<float>(slice);
							y0 = static_cast<float>(yStart + j); y1 = y0 + h;
							z0 = static_cast<float>(i); z1 = z0 + w;
							if (direction == 1) x0 = x1 += 1.0f;
						}
						else if (direction < 4) {
							y0 = y1 = static_cast<float>(yStart + slice);
							x0 = static_cast<float>(i); x1 = x0 + w;
							z0 = static_cast<float>(j); z1 = z0 + h;
							if (direction == 3) y0 = y1 += 1.0f;
						}
						else {
							z0 = z1 = static_cast<float>(slice);
							x0 = static_cast<float>(i); x1 = x0 + w;
							y0 = static_cast<float>(yStart + j); y1 = y0 + h;
							if (direction == 5) z0 = z1 += 1.0f;
						}

						DirectX::XMFLOAT3 a, b, cc, d, normal;

						switch (direction) {
						case 0:
							a = { x0,y0,z0 }; b = { x0,y0,z1 }; cc = { x0,y1,z1 }; d = { x0,y1,z0 }; normal = { -1,0,0 }; break;
						case 1:
							a = { x0,y0,z1 }; b = { x0,y0,z0 }; cc = { x0,y1,z0 }; d = { x0,y1,z1 }; normal = { 1,0,0 }; break;
						case 2:
							a = { x0,y0,z0 }; b = { x1,y0,z0 }; cc = { x1,y0,z1 }; d = { x0,y0,z1 }; normal = { 0,-1,0 }; break;
						case 3:
							a = { x1,y0,z0 }; b = { x0,y0,z0 }; cc = { x0,y0,z1 }; d = { x1,y0,z1 }; normal = { 0,1,0 }; break;
						case 4:
							a = { x1,y0,z0 }; b = { x0,y0,z0 }; cc = { x0,y1,z0 }; d = { x1,y1,z0 }; normal = { 0,0,-1 }; break;
						default:
							a = { x0,y0,z0 }; b = { x1,y0,z0 }; cc = { x1,y1,z0 }; d = { x0,y1,z0 }; normal = { 0,0,1 }; break;
						}

						model& target = f._renderMode == RENDER_MODE_TRANSPARENT
							? output._translucent
							: output._opaque;
						addQuad(
							target,
							a, b, cc, d,
							normal,
							f._material,
							f._opacity,
							static_cast<float>(w),
							static_cast<float>(h),
							direction < 2 || direction >= 4,
							f._ao[0], f._ao[1], f._ao[2], f._ao[3]
						);

						for (int xx = 0; xx < w; ++xx)
							for (int yy = 0; yy < h; ++yy)
								_mask[static_cast<size_t>(i + xx) * height + j + yy]._visible = false;

						j += h;
					}
				}
			}
		}

	public:
		chunkMesh generate(
			const chunk& c,
			staticAssetManager& blocks,
			modelManager& models,
			const chunk* negX = nullptr,
			const chunk* posX = nullptr,
			const chunk* negZ = nullptr,
			const chunk* posZ = nullptr,
			const chunk* negXNegZ = nullptr,
			const chunk* negXPosZ = nullptr,
			const chunk* posXNegZ = nullptr,
			const chunk* posXPosZ = nullptr,
			chunkMeshTimings* timings = nullptr
		) {
			_neighbors[0] = negX;
			_neighbors[1] = posX;
			_neighbors[2] = negZ;
			_neighbors[3] = posZ;
			_neighbors[4] = negXNegZ;
			_neighbors[5] = negXPosZ;
			_neighbors[6] = posXNegZ;
			_neighbors[7] = posXPosZ;

			chunkMesh output;
			output._opaque._vertex.reserve(8192);
			output._opaque._index.reserve(12288);
			output._translucent._vertex.reserve(2048);
			output._translucent._index.reserve(3072);

			const auto cacheStart = std::chrono::steady_clock::now();
			buildCache(c, blocks);
			const auto faceStart = std::chrono::steady_clock::now();

			for (int direction = 0; direction < 6; ++direction)
				meshFace(output, direction, 0, _maxY + 1);

			if (timings) {
				const auto end = std::chrono::steady_clock::now();
				timings->cacheNanoseconds = static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(faceStart - cacheStart).count()
				);
				timings->faceNanoseconds = static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(end - faceStart).count()
				);
			}

			return output;
		}

		chunkSectionMeshes generateSections(
			const chunk& c,
			staticAssetManager& blocks,
			modelManager& models,
			const chunk* negX = nullptr,
			const chunk* posX = nullptr,
			const chunk* negZ = nullptr,
			const chunk* posZ = nullptr,
			const chunk* negXNegZ = nullptr,
			const chunk* negXPosZ = nullptr,
			const chunk* posXNegZ = nullptr,
			const chunk* posXPosZ = nullptr,
			chunkMeshTimings* timings = nullptr
		) {
			(void)models;
			_neighbors[0] = negX; _neighbors[1] = posX;
			_neighbors[2] = negZ; _neighbors[3] = posZ;
			_neighbors[4] = negXNegZ; _neighbors[5] = negXPosZ;
			_neighbors[6] = posXNegZ; _neighbors[7] = posXPosZ;
			const auto cacheStart = std::chrono::steady_clock::now();
			buildCache(c, blocks);
			const auto faceStart = std::chrono::steady_clock::now();
			chunkSectionMeshes output;
			for (uint32_t section = 0; section < CHUNK_SUBCHUNKS; ++section) {
				chunkMesh& mesh = output[section];
				mesh._opaque._vertex.reserve(1024);
				mesh._opaque._index.reserve(1536);
				mesh._translucent._vertex.reserve(256);
				mesh._translucent._index.reserve(384);
				const int32_t yStart = static_cast<int32_t>(section * SUBCHUNK_HEIGHT);
				const int32_t yEnd = std::min(yStart + SUBCHUNK_HEIGHT, CHUNK_HEIGHT);
				for (int direction = 0; direction < 6; ++direction)
					meshFace(mesh, direction, yStart, yEnd);
			}
			if (timings) {
				const auto end = std::chrono::steady_clock::now();
				timings->cacheNanoseconds = static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(faceStart - cacheStart).count());
				timings->faceNanoseconds = static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(end - faceStart).count());
			}
			return output;
		}

		chunkMesh generateSection(
			const chunk& c,
			staticAssetManager& blocks,
			modelManager& models,
			uint32_t section,
			const chunk* negX = nullptr,
			const chunk* posX = nullptr,
			const chunk* negZ = nullptr,
			const chunk* posZ = nullptr,
			const chunk* negXNegZ = nullptr,
			const chunk* negXPosZ = nullptr,
			const chunk* posXNegZ = nullptr,
			const chunk* posXPosZ = nullptr,
			chunkMeshTimings* timings = nullptr
		) {
			(void)models;
			_neighbors[0] = negX; _neighbors[1] = posX;
			_neighbors[2] = negZ; _neighbors[3] = posZ;
			_neighbors[4] = negXNegZ; _neighbors[5] = negXPosZ;
			_neighbors[6] = posXNegZ; _neighbors[7] = posXPosZ;
			const int32_t yStart = static_cast<int32_t>(section * SUBCHUNK_HEIGHT);
			const int32_t yEnd = std::min(yStart + SUBCHUNK_HEIGHT, CHUNK_HEIGHT);
			const auto cacheStart = std::chrono::steady_clock::now();
			buildCacheRange(c, blocks, yStart, yEnd);
			const auto faceStart = std::chrono::steady_clock::now();
			chunkMesh output;
			output._opaque._vertex.reserve(1024);
			output._opaque._index.reserve(1536);
			output._translucent._vertex.reserve(256);
			output._translucent._index.reserve(384);
			for (int direction = 0; direction < 6; ++direction)
				meshFace(output, direction, yStart, yEnd);
			if (timings) {
				const auto end = std::chrono::steady_clock::now();
				timings->cacheNanoseconds = static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(faceStart - cacheStart).count());
				timings->faceNanoseconds = static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(end - faceStart).count());
			}
			return output;
		}
	};

}
