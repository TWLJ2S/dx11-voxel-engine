#pragma once
#include <DirectXMath.h>
#include <core/utility.h>
#include <assets/assetManager.h>
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
#include <string>
#include <cstring>

namespace ac {

#define CHUNK_WIDTH 16
#define CHUNK_LENGTH 16
#define CHUNK_HEIGHT 384
#define CHUNK_VOLUME (CHUNK_WIDTH * CHUNK_LENGTH * CHUNK_HEIGHT)
#define SUBCHUNK_WIDTH 16
#define SUBCHUNK_LENGTH 16
#define SUBCHUNK_HEIGHT 16
#define SUBCHUNK_VOLUME (SUBCHUNK_WIDTH * SUBCHUNK_LENGTH * SUBCHUNK_HEIGHT)
#define CHUNK_SUBCHUNKS ((CHUNK_HEIGHT + SUBCHUNK_HEIGHT - 1) / SUBCHUNK_HEIGHT)

	using blockId = uint32_t;
	using dataTypeId = uint16_t;
	using dataId = uint16_t;

	// Chunk voxels: twelve bits identify the canonical block type (up to 4095).
	// Remaining bits are per-cell state so open/closed, on/off, and door halves
	// are not separate registry entries. Heavy payloads (chests) live in the
	// sparse block-data store, not in extra block IDs.
	constexpr blockId BLOCK_TYPE_MASK = 0xfffu;
	constexpr uint32_t FLUID_LEVEL_SHIFT = 12u;
	constexpr uint32_t BLOCK_FACING_SHIFT = 15u;
	constexpr uint32_t BLOCK_FACING_MASK = 0x3u;
	constexpr uint32_t BLOCK_OPEN_SHIFT = 22u;
	constexpr uint32_t BLOCK_POWERED_SHIFT = 23u;
	constexpr uint32_t BLOCK_UPPER_SHIFT = 24u;
	constexpr uint32_t BLOCK_ATTRIBUTE_SHIFT = 25u;
	constexpr uint32_t BLOCK_ATTRIBUTE_MASK = 0x7fu;
	constexpr uint8_t MAX_FLUID_LEVEL = 8u;
	// Content IDs are resolved by names from the loaded registry at startup.
	inline blockId WATER_BLOCK_TYPE = 0u;
	constexpr blockId blockType(blockId state) { return state & BLOCK_TYPE_MASK; }
	constexpr uint8_t storedFluidLevel(blockId state) {
		return static_cast<uint8_t>((state >> FLUID_LEVEL_SHIFT) & 0x7u);
	}
	constexpr blockId withFluidLevel(blockId type, uint8_t level) {
		if (level == 0u) return 0u;
		const uint8_t clamped = level > MAX_FLUID_LEVEL ? MAX_FLUID_LEVEL : level;
		const uint8_t encoded = clamped == MAX_FLUID_LEVEL ? 0u : clamped;
		return blockType(type) | (static_cast<blockId>(encoded) << FLUID_LEVEL_SHIFT);
	}
	inline uint8_t fluidLevel(blockId state, blockId fluidType = WATER_BLOCK_TYPE) {
		if (blockType(state) != blockType(fluidType)) return 0u;
		const uint8_t stored = storedFluidLevel(state);
		return stored == 0u ? MAX_FLUID_LEVEL : stored;
	}
	inline float fluidHeight(blockId state, blockId fluidType = WATER_BLOCK_TYPE) {
		return static_cast<float>(fluidLevel(state, fluidType)) /
			static_cast<float>(MAX_FLUID_LEVEL);
	}

	struct fluidFlow {
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
	};

	// Unit current from a water cell toward lower/empty neighbors. Solid
	// neighbors are ignored. Falling water also reports a downward component.
	template<typename Read>
	inline fluidFlow waterFlowAt(int32_t x, int32_t y, int32_t z, Read&& read) {
		const blockId here = static_cast<blockId>(read(x, y, z));
		const uint8_t level = fluidLevel(here);
		if (level == 0u) return {};

		auto neighborWeight = [&](int32_t nx, int32_t ny, int32_t nz) -> int {
			const blockId neighbor = static_cast<blockId>(read(nx, ny, nz));
			const uint8_t neighborLevel = fluidLevel(neighbor);
			if (neighborLevel > 0u)
				return neighborLevel < level ? static_cast<int>(level - neighborLevel) : 0;
			return blockType(neighbor) == 0u ? static_cast<int>(level) : 0;
		};

		float fx = static_cast<float>(neighborWeight(x + 1, y, z) - neighborWeight(x - 1, y, z));
		float fz = static_cast<float>(neighborWeight(x, y, z + 1) - neighborWeight(x, y, z - 1));
		float fy = 0.0f;
		const blockId below = static_cast<blockId>(read(x, y - 1, z));
		if (blockType(below) == 0u ||
			(blockType(below) == WATER_BLOCK_TYPE && fluidLevel(below) < MAX_FLUID_LEVEL))
			fy = -1.0f;

		const float horizontal = std::sqrt(fx * fx + fz * fz);
		if (horizontal > 1.0e-4f) {
			fx /= horizontal;
			fz /= horizontal;
		}
		else {
			fx = 0.0f;
			fz = 0.0f;
		}
		return { fx, fy, fz };
	}

	// Horizontal facing stored on placeable detail blocks.
	// 0 = south (+Z), 1 = east (+X), 2 = north (-Z), 3 = west (-X)
	constexpr uint32_t blockFacing(blockId state) {
		return (state >> BLOCK_FACING_SHIFT) & BLOCK_FACING_MASK;
	}
	constexpr blockId withFacing(blockId state, uint32_t facing) {
		return (state & ~(BLOCK_FACING_MASK << BLOCK_FACING_SHIFT)) |
			((facing & BLOCK_FACING_MASK) << BLOCK_FACING_SHIFT);
	}
	// Wall-attached torches use bit 21 (above redstone power) so floor/wall
	// placement can share the same block type.
	constexpr uint32_t BLOCK_WALL_ATTACH_SHIFT = 21u;
	constexpr bool isWallAttached(blockId state) {
		return ((state >> BLOCK_WALL_ATTACH_SHIFT) & 1u) != 0u;
	}
	constexpr blockId withWallAttached(blockId state, bool wall) {
		return wall
			? (state | (static_cast<blockId>(1u) << BLOCK_WALL_ATTACH_SHIFT))
			: (state & ~(static_cast<blockId>(1u) << BLOCK_WALL_ATTACH_SHIFT));
	}
	inline uint32_t facingFromLook(float lookX, float lookZ) {
		if (std::abs(lookX) > std::abs(lookZ))
			return lookX > 0.0f ? 1u : 3u;
		return lookZ > 0.0f ? 0u : 2u;
	}
	// Horizontal face normal from support block toward the placed cell.
	inline uint32_t facingFromHitDelta(int32_t dx, int32_t dy, int32_t dz) {
		(void)dy;
		if (std::abs(dx) >= std::abs(dz))
			return dx > 0 ? 1u : 3u;
		return dz > 0 ? 0u : 2u;
	}
	// Convert stored facing (0=S,1=E,2=N,3=W) into yaw turns for models authored
	// facing south (+Z). Rotation uses (x,z)->(z,-x).
	constexpr uint32_t facingToYawTurns(uint32_t facing) {
		return facing & BLOCK_FACING_MASK;
	}
	inline uint32_t localFaceForFacing(uint32_t worldFace, uint32_t facing) {
		if (worldFace == BLOCK_FACE_DOWN || worldFace == BLOCK_FACE_UP)
			return worldFace;
		int x = 0, z = 0;
		switch (worldFace) {
		case BLOCK_FACE_WEST: x = -1; break;
		case BLOCK_FACE_EAST: x = 1; break;
		case BLOCK_FACE_NORTH: z = -1; break;
		default: z = 1; break;
		}
		for (uint32_t turn = 0; turn < (facing & BLOCK_FACING_MASK); ++turn) {
			const int localX = -z;
			const int localZ = x;
			x = localX;
			z = localZ;
		}
		if (x < 0) return BLOCK_FACE_WEST;
		if (x > 0) return BLOCK_FACE_EAST;
		if (z < 0) return BLOCK_FACE_NORTH;
		return BLOCK_FACE_SOUTH;
	}
	// Wall torch mesh is authored facing east (attached on the west face).
	constexpr uint32_t wallTorchYawTurns(uint32_t facing) {
		return (facing + 3u) & BLOCK_FACING_MASK;
	}

	constexpr bool isBlockOpen(blockId state) {
		return ((state >> BLOCK_OPEN_SHIFT) & 1u) != 0u;
	}
	constexpr blockId withBlockOpen(blockId state, bool open) {
		return open
			? (state | (static_cast<blockId>(1u) << BLOCK_OPEN_SHIFT))
			: (state & ~(static_cast<blockId>(1u) << BLOCK_OPEN_SHIFT));
	}
	constexpr bool isBlockPowered(blockId state) {
		return ((state >> BLOCK_POWERED_SHIFT) & 1u) != 0u;
	}
	constexpr blockId withBlockPowered(blockId state, bool powered) {
		return powered
			? (state | (static_cast<blockId>(1u) << BLOCK_POWERED_SHIFT))
			: (state & ~(static_cast<blockId>(1u) << BLOCK_POWERED_SHIFT));
	}
	constexpr bool isUpperHalf(blockId state) {
		return ((state >> BLOCK_UPPER_SHIFT) & 1u) != 0u;
	}
	constexpr blockId withUpperHalf(blockId state, bool upper) {
		return upper
			? (state | (static_cast<blockId>(1u) << BLOCK_UPPER_SHIFT))
			: (state & ~(static_cast<blockId>(1u) << BLOCK_UPPER_SHIFT));
	}
	constexpr uint32_t blockAttribute(blockId state) {
		return (state >> BLOCK_ATTRIBUTE_SHIFT) & BLOCK_ATTRIBUTE_MASK;
	}
	constexpr blockId withBlockAttribute(blockId state, uint32_t attribute) {
		return (state & ~(BLOCK_ATTRIBUTE_MASK << BLOCK_ATTRIBUTE_SHIFT)) |
			((attribute & BLOCK_ATTRIBUTE_MASK) << BLOCK_ATTRIBUTE_SHIFT);
	}

	// Maps legacy variant type IDs (oak_door_open, lamp_on, …) onto one
	// canonical type plus the packed state bits above.
	struct blockVariantTable {
		static constexpr uint32_t CAPACITY = BLOCK_TYPE_MASK + 1u;
		static constexpr blockId VISUAL_STATE_MASK =
			(BLOCK_ATTRIBUTE_MASK << BLOCK_ATTRIBUTE_SHIFT) |
			(1u << BLOCK_OPEN_SHIFT) |
			(1u << BLOCK_POWERED_SHIFT) |
			(1u << BLOCK_UPPER_SHIFT);
		std::array<blockId, CAPACITY> canonical{};
		std::array<uint32_t, CAPACITY> extraBits{};
		std::array<bool, CAPACITY> directional{};
		std::unordered_map<blockId, blockId> visualTypes;

		blockVariantTable() { reset(); }

		void reset() {
			for (uint32_t i = 0; i < CAPACITY; ++i) {
				canonical[i] = i;
				extraBits[i] = 0u;
				directional[i] = false;
			}
			visualTypes.clear();
		}

		void alias(blockId from, blockId to, uint32_t bits, bool visual = true) {
			if (from == 0u || from >= CAPACITY || to >= CAPACITY) return;
			canonical[from] = to;
			extraBits[from] = bits;
			if (visual)
				visualTypes[to | (bits & VISUAL_STATE_MASK)] = from;
		}

		void markDirectional(blockId type) {
			type = blockType(type);
			if (type < CAPACITY) directional[type] = true;
		}

		bool usesFacing(blockId type) const {
			type = blockType(type);
			if (type >= CAPACITY) return false;
			return directional[canonical[type]];
		}

		bool isAlias(blockId type) const {
			type = blockType(type);
			return type < CAPACITY && canonical[type] != type;
		}

		blockId normalize(blockId state) const {
			const blockId type = blockType(state);
			if (type >= CAPACITY) return state;
			const blockId canon = canonical[type];
			if (canon == type) return state;
			return (state & ~BLOCK_TYPE_MASK) | canon | extraBits[type];
		}

		blockId visualize(blockId state) const {
			state = normalize(state);
			const blockId key = blockType(state) | (state & VISUAL_STATE_MASK);
			const auto found = visualTypes.find(key);
			return found == visualTypes.end()
				? state
				: ((state & ~BLOCK_TYPE_MASK) | found->second);
		}
	};

	inline blockVariantTable& blockVariants() {
		static blockVariantTable table;
		return table;
	}

	inline blockId normalizeBlockState(blockId state) {
		return blockVariants().normalize(state);
	}

	inline bool isBlockStateAlias(blockId type) {
		return blockVariants().isAlias(type);
	}

	inline bool blockUsesFacingState(blockId type) {
		return blockVariants().usesFacing(type);
	}

	// Appearance-only type for meshes that still key textures off registry IDs
	// (lamp on, torch off). World storage stays canonical.
	inline blockId visualBlockState(blockId state) {
		return blockVariants().visualize(state);
	}

	inline void registerBlockStateVariants(const staticAssetManager& blocks) {
		auto& table = blockVariants();
		table.reset();

		auto canonicalNameOf = [](std::string name, uint32_t& extra) {
			extra = 0u;
			if (name == "redstone_lamp_on") {
				extra = 1u << BLOCK_POWERED_SHIFT;
				return std::string("redstone_lamp");
			}
			if (name == "redstone_torch_off")
				return std::string("redstone_torch");
			if (name == "lever_on") {
				extra = 1u << BLOCK_POWERED_SHIFT;
				return std::string("lever");
			}
			if (name.find("trapdoor") != std::string::npos)
				return name;
			if (name.find("door") == std::string::npos)
				return name;
			if (name.size() >= 5 && name.compare(name.size() - 5, 5, "_open") == 0) {
				extra |= 1u << BLOCK_OPEN_SHIFT;
				name.resize(name.size() - 5);
			}
			const size_t top = name.find("_door_top");
			if (top != std::string::npos) {
				extra |= 1u << BLOCK_UPPER_SHIFT;
				name = name.substr(0, top) + "_door";
			}
			return name;
		};

		for (uint32_t id : blocks.ids()) {
			uint32_t extra = 0u;
			const std::string canonical = canonicalNameOf(blocks.getName(id), extra);
			if (canonical.empty() || canonical == blocks.getName(id))
				continue;
			try {
				const blockId to = static_cast<blockId>(blocks.getId(canonical));
				if (to != id)
					table.alias(static_cast<blockId>(id), to, extra);
			}
			catch (...) {
			}
		}

		auto findType = [&](const std::string& name) -> blockId {
			try { return static_cast<blockId>(blocks.getId(name)); }
			catch (...) { return 0u; }
		};
		auto hydrationBase = [&](std::string name, uint32_t& attribute) {
			const size_t marker = name.find("_hydration_");
			if (marker == std::string::npos || marker + 11u >= name.size())
				return name;
			const char value = name[marker + 11u];
			if (value < '0' || value > '9') return name;
			attribute = static_cast<uint32_t>(value - '0');
			name[marker + 11u] = '0';
			return name;
		};

		for (uint32_t id : blocks.ids()) {
			const std::string name = blocks.getName(id);
			if (table.isAlias(id)) continue;

			if (name.size() > 4u && name.compare(name.size() - 4u, 4u, "_inv") == 0) {
				const blockId target = findType(name.substr(0, name.size() - 4u));
				if (target) table.alias(id, target, 0u, false);
				continue;
			}

			uint32_t hydration = 0u;
			const std::string hydratedBase = hydrationBase(name, hydration);
			if (hydratedBase != name && hydration > 0u) {
				const blockId target = findType(hydratedBase);
				if (target)
					table.alias(id, target, hydration << BLOCK_ATTRIBUTE_SHIFT, true);
			}

			struct directionToken { const char* text; uint32_t facing; };
			static constexpr directionToken directions[] = {
				{ "_south", 0u }, { "_east", 1u },
				{ "_north", 2u }, { "_west", 3u }
			};
			for (const directionToken& direction : directions) {
				const size_t marker = name.find(direction.text);
				if (marker == std::string::npos) continue;
				std::string base = name.substr(0, marker);
				uint32_t attribute = 0u;
				base = hydrationBase(base, attribute);
				const std::string tail = name.substr(marker + std::strlen(direction.text));
				if (tail == "_crafting") attribute = 1u;
				else if (tail == "_triggered") attribute = 2u;
				const blockId target = findType(base);
				if (target) {
					const uint32_t bits =
						(direction.facing << BLOCK_FACING_SHIFT) |
						(attribute << BLOCK_ATTRIBUTE_SHIFT);
					table.alias(id, target, bits, false);
					table.markDirectional(target);
				}
				break;
			}

			for (const char* suffix : { "_active", "_inactive" }) {
				const size_t length = std::strlen(suffix);
				if (name.size() <= length || name.compare(name.size() - length, length, suffix) != 0)
					continue;
				const blockId target = findType(name.substr(0, name.size() - length));
				if (target) {
					const bool active = std::string(suffix) == "_active";
					table.alias(id, target, active ? (1u << BLOCK_POWERED_SHIFT) : 0u, true);
				}
			}
		}
	}

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

		template<class Fn>
		void mapStates(Fn&& fn) {
			if (!_bits) {
				_single = fn(_single);
				return;
			}
			for (blockId& entry : _palette)
				entry = fn(entry);
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
			return normalizeBlockState(_subchunks[y >> 4].getBlock(x, y & 15u, z));
		}

		void setBlock(uint32_t x, uint32_t y, uint32_t z, blockId id) {
			if (x >= CHUNK_WIDTH || y >= CHUNK_HEIGHT || z >= CHUNK_LENGTH) return;
			_subchunks[subchunkIndex(y)].setBlock(
				x, y % SUBCHUNK_HEIGHT, z, normalizeBlockState(id));
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
	constexpr uint32_t MODEL_DOOR = 1;
	constexpr uint32_t MODEL_DOOR_OPEN = 2;
	constexpr uint32_t MODEL_SLAB = 3;
	constexpr uint32_t MODEL_STAIRS = 4;
	constexpr uint32_t MODEL_FENCE = 5;
	constexpr uint32_t MODEL_FENCE_POST = 6;
	constexpr uint32_t MODEL_FENCE_SIDE = 7;
	constexpr uint32_t MODEL_PANE = 8;
	constexpr uint32_t MODEL_PANE_POST = 9;
	constexpr uint32_t MODEL_PANE_SIDE = 10;
	constexpr uint32_t MODEL_STAIRS_OUTER = 11;
	constexpr uint32_t MODEL_STAIRS_INNER = 12;
	constexpr uint32_t MODEL_STAIRS_INNER_RIGHT = 13;
	constexpr uint32_t MODEL_TORCH = 14;
	constexpr uint32_t MODEL_CROSS = 15;
	constexpr uint32_t MODEL_THIN = 16;
	constexpr uint32_t MODEL_TORCH_WALL = 17;
	constexpr uint32_t MODEL_REDSTONE_DUST_SIDE = 18;
	constexpr uint32_t MODEL_REDSTONE_DUST_UP = 42;
	constexpr uint32_t MODEL_CHEST = 19;
	constexpr uint32_t MODEL_CAKE = 20;
	constexpr uint32_t MODEL_ENCHANTING_TABLE = 21;
	constexpr uint32_t MODEL_CAULDRON = 22;
	constexpr uint32_t MODEL_ANVIL = 23;
	constexpr uint32_t MODEL_TRAPDOOR = 24;
	constexpr uint32_t MODEL_TRAPDOOR_OPEN = 25;
	constexpr uint32_t MODEL_ROD = 26;
	constexpr uint32_t MODEL_EGG = 27;
	constexpr uint32_t MODEL_LADDER = 28;
	constexpr uint32_t MODEL_CAMPFIRE = 29;
	constexpr uint32_t MODEL_BELL = 30;
	constexpr uint32_t MODEL_LEVER = 31;
	constexpr uint32_t MODEL_FENCE_GATE = 32;
	constexpr uint32_t MODEL_FENCE_GATE_OPEN = 33;
	constexpr uint32_t MODEL_LECTERN = 34;
	constexpr uint32_t MODEL_GRINDSTONE = 35;
	constexpr uint32_t MODEL_STONECUTTER = 36;
	constexpr uint32_t MODEL_SENSOR = 37;
	constexpr uint32_t MODEL_SCAFFOLDING = 38;
	constexpr uint32_t MODEL_SHELF = 39;
	constexpr uint32_t MODEL_SHULKER_BOX = 40;
	constexpr uint32_t MODEL_FLOWER_POT = 41;
	constexpr uint32_t MODEL_REPEATER = 43;
	constexpr uint32_t MODEL_COMPARATOR = 59;
	inline bool isDiodeModel(uint32_t modelId) {
		return modelId == MODEL_REPEATER || modelId == MODEL_COMPARATOR;
	}
	inline uint32_t diodeModelForState(uint32_t modelId, blockId state) {
		if (modelId == MODEL_REPEATER)
			return modelId + ((state >> 25u) & 3u) * 4u + (isBlockPowered(state) ? 1u : 0u) +
				((state & (1u << 27u)) ? 2u : 0u);
		return modelId + (isBlockPowered(state) ? 1u : 0u) + ((state & (1u << 28u)) ? 2u : 0u);
	}
	constexpr float DOOR_THICKNESS = 3.0f / 16.0f;
	constexpr float THIN_HEIGHT = 1.0f / 16.0f;

	inline bool isDetailModel(uint32_t modelId) {
		return modelId == MODEL_DOOR || modelId == MODEL_DOOR_OPEN ||
			modelId == MODEL_SLAB || modelId == MODEL_STAIRS ||
			modelId == MODEL_FENCE || modelId == MODEL_PANE ||
			modelId == MODEL_TORCH || modelId == MODEL_CROSS ||
			modelId == MODEL_THIN || modelId == MODEL_CHEST ||
			modelId == MODEL_CAKE || modelId == MODEL_ENCHANTING_TABLE ||
			modelId == MODEL_CAULDRON || modelId == MODEL_ANVIL ||
			(modelId >= MODEL_TRAPDOOR && modelId <= MODEL_COMPARATOR + 3u);
	}

	inline bool usesPlacementFacing(uint32_t modelId) {
		return modelId == MODEL_STAIRS || modelId == MODEL_DOOR ||
			modelId == MODEL_DOOR_OPEN || modelId == MODEL_TORCH ||
			modelId == MODEL_TRAPDOOR || modelId == MODEL_FENCE_GATE ||
			modelId == MODEL_LADDER || modelId == MODEL_LEVER || isDiodeModel(modelId);
	}

	enum class stairShape : uint8_t {
		straight = 0,
		outerLeft = 1,
		outerRight = 2,
		innerLeft = 3,
		innerRight = 4
	};

	struct stairNeighborInfo {
		bool isStairs = false;
		uint32_t facing = 0;
	};

	struct fenceConnections {
		bool north = false; // -Z
		bool west = false;  // -X
		bool south = false; // +Z
		bool east = false;  // +X
	};

	inline bool fenceConnectsTo(const blockDefinition* definition) {
		if (!definition) return false;
		if (definition->_model == MODEL_FENCE ||
			definition->_model == MODEL_FENCE_GATE) return true;
		return definition->_solid && definition->_occludes;
	}

	// Panes connect like fences, and also to glass blocks (Minecraft behavior).
	inline bool paneConnectsTo(const blockDefinition* definition) {
		if (!definition) return false;
		if (definition->_model == MODEL_PANE) return true;
		if (definition->_connectsToPanes) return true;
		return definition->_solid && definition->_occludes;
	}

	// Dust arms are controlled only by explicitly configured redstone
	// components. Ordinary full cubes must not redirect the dust shape.
	inline bool redstoneDustConnectsTo(const blockDefinition* definition) {
		return definition &&
			definition->_behavior.redstone != blockRedstoneBehavior::none;
	}

	// A height transition is a dust-to-dust path. Components one block above or
	// below do not create a diagonal arm or become an electrical connection.
	inline bool redstoneDustStepsTo(const blockDefinition* definition) {
		return definition &&
			definition->_behavior.redstone == blockRedstoneBehavior::wire;
	}

	inline bool redstoneDustBlockedBy(const blockDefinition* definition) {
		return definition && definition->_solid && definition->_occludes &&
			definition->_model == MODEL_CUBE &&
			definition->_behavior.redstone == blockRedstoneBehavior::none;
	}

	// Dye used when light rays pass through stained glass / panes.
	inline DirectX::XMFLOAT3 glassLightDye(const blockDefinition* definition) {
		return definition ? definition->_lightTransmission : DirectX::XMFLOAT3{ 1.0f, 1.0f, 1.0f };
	}

	// neighbors[0]=south(+Z), [1]=east(+X), [2]=north(-Z), [3]=west(-X)
	inline stairShape resolveStairShape(
		uint32_t facing,
		const stairNeighborInfo neighbors[4]
	) {
		facing &= 3u;
		const uint32_t forward = facing;
		const uint32_t back = (facing + 2u) & 3u;
		const uint32_t left = (facing + 1u) & 3u;
		const auto canTakeShape = [&](uint32_t dir) {
			const stairNeighborInfo& neighbor = neighbors[dir & 3u];
			return !neighbor.isStairs || (neighbor.facing & 3u) != facing;
		};

		const stairNeighborInfo& front = neighbors[forward];
		if (front.isStairs) {
			const uint32_t frontFacing = front.facing & 3u;
			if (((frontFacing ^ facing) & 1u) != 0u &&
				canTakeShape((frontFacing + 2u) & 3u)) {
				return frontFacing == left ? stairShape::outerLeft : stairShape::outerRight;
			}
		}

		const stairNeighborInfo& rear = neighbors[back];
		if (rear.isStairs) {
			const uint32_t rearFacing = rear.facing & 3u;
			if (((rearFacing ^ facing) & 1u) != 0u && canTakeShape(rearFacing)) {
				return rearFacing == left ? stairShape::innerLeft : stairShape::innerRight;
			}
		}

		return stairShape::straight;
	}

	class chunkMesher {
	private:
		struct meshBlock {
			const blockDefinition* _definition = nullptr;
			blockId _state = 0u;
		};

		struct face {
			bool _visible = false;
			uint32_t _vertexFlags = 0u;
			uint32_t _material = 0;
			uint16_t _textureRotation = 0;
			float _opacity = 1.0f;
			RENDER_MODE _renderMode = RENDER_MODE_OPAQUE;
			std::array<uint8_t, 4> _ao = { 3u, 3u, 3u, 3u };
			uint8_t _fluidLevel = 0u;
			uint8_t _neighborFluidLevel = 0u;
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

				const blockDefinition* definition = blocks.get(blockType(visualBlockState(id)));
				cachedBlock._definition = definition;
				cachedBlock._state = id;

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
						(block._definition->_model != MODEL_CUBE ||
							block._definition->_occludes ||
							block._definition->_solid))
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
								(block._definition->_model != MODEL_CUBE ||
									block._definition->_occludes ||
									block._definition->_solid))
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
				cachedBlock = { blocks.get(blockType(id)), id };
				return cachedBlock;
			};

			for (int32_t y = std::max(0, sampleStart); y < std::min(CHUNK_HEIGHT, sampleEnd + 1); ++y) {
				for (int32_t z = 0; z < CHUNK_LENGTH; ++z) {
					for (int32_t x = 0; x < CHUNK_WIDTH; ++x) {
						const meshBlock block = resolve(c.getBlock(x, y, z));
						_cache[cacheIndex(x, y, z)] = block;
						if (y >= yStart && y < yEnd && block._definition &&
							(block._definition->_model != MODEL_CUBE ||
								block._definition->_occludes ||
								block._definition->_solid))
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

		blockId getState(int32_t x, int32_t y, int32_t z) const {
			if (x < -1 || x > CHUNK_WIDTH || y < -1 || y > CHUNK_HEIGHT ||
				z < -1 || z > CHUNK_LENGTH)
				return 0u;
			return _cache[cacheIndex(x, y, z)]._state;
		}

		float fluidCornerHeight(int32_t x, int32_t y, int32_t z, int32_t cornerX, int32_t cornerZ) const {
			const int32_t startX = x + cornerX - 1;
			const int32_t startZ = z + cornerZ - 1;
			uint32_t total = 0u;
			uint32_t samples = 0u;
			for (int32_t dz = 0; dz <= 1; ++dz) {
				for (int32_t dx = 0; dx <= 1; ++dx) {
					if (fluidLevel(getState(startX + dx, y + 1, startZ + dz)) > 0u)
						return 1.0f;
					const uint8_t level = fluidLevel(getState(startX + dx, y, startZ + dz));
					if (level == 0u) continue;
					// Exposed sources render two texture pixels (1/8 block) below
					// the cube ceiling. Stacked water still returns 1.0 above, so
					// deep columns remain sealed with no internal air slice.
					total += level == MAX_FLUID_LEVEL ? MAX_FLUID_LEVEL - 1u : level;
					++samples;
				}
			}
			return samples == 0u ? 0.0f :
				static_cast<float>(total) / (static_cast<float>(samples) * MAX_FLUID_LEVEL);
		}

		bool cube(int32_t x, int32_t y, int32_t z) const {
			const blockDefinition* definition = getDefinition(x, y, z);
			return definition && definition->_occludes &&
				definition->_renderMode != RENDER_MODE_TRANSPARENT;
		}

		bool faceVisible(
			blockId state,
			const blockDefinition& definition,
			int direction,
			int32_t neighborX,
			int32_t neighborY,
			int32_t neighborZ
		) const {
			const blockDefinition* neighbor = getDefinition(neighborX, neighborY, neighborZ);
			if (!neighbor) return true;

			if (definition._renderMode == RENDER_MODE_TRANSPARENT) {
				if (neighbor->_renderMode == RENDER_MODE_TRANSPARENT &&
					neighbor->_id == blockType(state)) {
					// Adjacent water top edges use the same averaged corner heights, so
					// they join as one continuous sloped surface without internal walls.
					return false;
				}
				// Never emit water/glass against occluding solids. Those quads sit on
				// the same plane as the opaque face and z-fight once alpha is opaque.
				if (neighbor->_renderMode != RENDER_MODE_TRANSPARENT && neighbor->_occludes)
					return false;
				return true;
			}

			// Fancy leaves / cutout foliage: punch holes show real geometry behind.
			// Cull cutout-against-cutout so coplanar leaf faces do not z-fight.
			if (definition._renderMode == RENDER_MODE_CUTOUT) {
				if (neighbor->_renderMode == RENDER_MODE_CUTOUT)
					return false;
				return neighbor->_renderMode == RENDER_MODE_TRANSPARENT || !neighbor->_occludes;
			}

			// Opaque geometry must remain visible behind glass, water, or cutout leaves.
			return neighbor->_renderMode == RENDER_MODE_TRANSPARENT ||
				neighbor->_renderMode == RENDER_MODE_CUTOUT ||
				!neighbor->_occludes;
		}

		static void addQuad(model& mesh, const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, const DirectX::XMFLOAT3& c, const DirectX::XMFLOAT3& d, const DirectX::XMFLOAT3& normal, uint32_t material, float opacity, float uvWidth, float uvHeight, bool flipV, uint16_t rotation, uint32_t ao0, uint32_t ao1, uint32_t ao2, uint32_t ao3) {
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
			for (uint16_t turn = 0; turn < (rotation % 360u) / 90u; ++turn)
				for (auto* vertex : { &v0, &v1, &v2, &v3 }) {
					const float u = vertex->_uv.x;
					vertex->_uv.x = vertex->_uv.y;
					vertex->_uv.y = 1.0f - u;
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

		void emitModelInstance(
			chunkMesh& output,
			const model& prototype,
			float ox, float oy, float oz,
			uint32_t material,
			float opacity,
			RENDER_MODE renderMode,
			uint32_t yawTurns = 0,
			const blockDefinition* materials = nullptr,
			int32_t panelCutoutAxis = -1,
			uint32_t vertexFlags = 0u,
			blockId instanceState = 0u
		) {
			if (prototype._vertex.empty() || prototype._index.empty()) return;
			model& target = renderMode == RENDER_MODE_TRANSPARENT
				? output._translucent
				: output._opaque;
			const uint32_t base = static_cast<uint32_t>(target._vertex.size());
			target._vertex.reserve(target._vertex.size() + prototype._vertex.size());
			const uint32_t turns = yawTurns & 3u;
			for (const vertex& source : prototype._vertex) {
				vertex v = source;
				const bool lever = materials && materials->_model == MODEL_LEVER;
				if (lever) {
					// The handle is authored at -45 degrees; rotate it 90 degrees
					// around its hinge when switched on.
					if (source._material == 0u && isBlockPowered(instanceState)) {
						const float py = v._position.y - 0.0625f;
						const float pz = v._position.z - 0.5f;
						v._position.y = 0.0625f - pz;
						v._position.z = 0.5f + py;
						const float ny = v._normal.y;
						v._normal.y = -v._normal.z;
						v._normal.z = ny;
					}
					if (isWallAttached(instanceState)) {
						const float py = v._position.y;
						v._position.y = 1.0f - v._position.z;
						v._position.z = py;
						const float ny = v._normal.y;
						v._normal.y = -v._normal.z;
						v._normal.z = ny;
					}
				}
				float px = v._position.x - 0.5f;
				float pz = v._position.z - 0.5f;
				float nx = v._normal.x;
				float nz = v._normal.z;
				for (uint32_t t = 0; t < turns; ++t) {
					const float rx = pz;
					const float rz = -px;
					px = rx; pz = rz;
					const float rnx = nz;
					const float rnz = -nx;
					nx = rnx; nz = rnz;
				}
				v._position.x = px + 0.5f + ox;
				v._position.y += oy;
				v._position.z = pz + 0.5f + oz;
				v._normal.x = nx;
				v._normal.z = nz;
				if (lever)
					v._material = materials->materialForFace(source._material == 1u ? BLOCK_FACE_UP : BLOCK_FACE_WEST);
				else if (materials && isDiodeModel(materials->_model))
					v._material = materials->materialForFace(source._material);
				else if (materials)
					v._material = materials->materialForNormal(source._normal);
				else {
					v._material = material;
				}
				v._opacity = opacity;
				if (v._ao == 0) v._ao = 0xFFu;
				// Doors and trapdoors may use alpha for windows on their two broad
				// panel faces. Their thin edge faces are physical material, not more
				// copies of that window mask, so mark those texels as forced solid.
				if (panelCutoutAxis >= 0) {
					const float panelNormal = panelCutoutAxis == 0
						? std::abs(source._normal.x)
						: (panelCutoutAxis == 1
							? std::abs(source._normal.y)
							: std::abs(source._normal.z));
					if (panelNormal < 0.5f)
						v._ao |= 0x100u;
				}
				v._ao |= vertexFlags;
				target._vertex.push_back(v);
			}
			target._index.reserve(target._index.size() + prototype._index.size());
			for (uint32_t index : prototype._index)
				target._index.push_back(base + index);
		}

		static bool fenceConnects(const meshBlock& neighbor) {
			return fenceConnectsTo(neighbor._definition);
		}

		static bool paneConnects(const meshBlock& neighbor) {
			return paneConnectsTo(neighbor._definition);
		}

		static bool redstoneDustConnects(const meshBlock& neighbor) {
			return redstoneDustConnectsTo(neighbor._definition);
		}

		static bool redstoneDustIsWire(const meshBlock& neighbor) {
			return redstoneDustStepsTo(neighbor._definition);
		}

		static stairNeighborInfo stairNeighborFrom(const meshBlock& neighbor) {
			stairNeighborInfo info{};
			if (!neighbor._definition || neighbor._definition->_model != MODEL_STAIRS)
				return info;
			info.isStairs = true;
			info.facing = blockFacing(neighbor._state);
			return info;
		}

		void meshDetailModels(
			chunkMesh& output,
			modelManager& models,
			int32_t yStart,
			int32_t yEnd
		) {
			yStart = std::clamp(yStart, 0, CHUNK_HEIGHT);
			yEnd = std::clamp(yEnd, yStart, CHUNK_HEIGHT);
			const model* closedDoor = models.get(MODEL_DOOR);
			const model* openDoor = models.get(MODEL_DOOR_OPEN);
			const model* slab = models.get(MODEL_SLAB);
			const model* stairs = models.get(MODEL_STAIRS);
			const model* stairsOuter = models.get(MODEL_STAIRS_OUTER);
			const model* stairsInner = models.get(MODEL_STAIRS_INNER);
			const model* stairsInnerRight = models.get(MODEL_STAIRS_INNER_RIGHT);
			const model* fence = models.get(MODEL_FENCE);
			const model* fencePost = models.get(MODEL_FENCE_POST);
			const model* fenceSide = models.get(MODEL_FENCE_SIDE);
			const model* pane = models.get(MODEL_PANE);
			const model* panePost = models.get(MODEL_PANE_POST);
			const model* paneSide = models.get(MODEL_PANE_SIDE);
			const model* torch = models.get(MODEL_TORCH);
			const model* torchWall = models.get(MODEL_TORCH_WALL);
			const model* cross = models.get(MODEL_CROSS);
			const model* thin = models.get(MODEL_THIN);
			const model* dustSide = models.get(MODEL_REDSTONE_DUST_SIDE);
			const model* dustUp = models.get(MODEL_REDSTONE_DUST_UP);
			for (int32_t y = yStart; y < yEnd; ++y) {
				for (int32_t z = 0; z < CHUNK_LENGTH; ++z) {
					for (int32_t x = 0; x < CHUNK_WIDTH; ++x) {
						const meshBlock& block = _cache[cacheIndex(x, y, z)];
						if (!block._definition) continue;
						const uint32_t modelId = block._definition->_model;
						if (!isDetailModel(modelId)) continue;
						const uint32_t material = block._definition->materialForFace(BLOCK_FACE_WEST);
						const float opacity = block._definition->_opacity;
						const RENDER_MODE renderMode = block._definition->_renderMode;
						const float fx = static_cast<float>(x);
						const float fy = static_cast<float>(y);
						const float fz = static_cast<float>(z);
						const uint32_t facing = blockFacing(block._state);
						const uint32_t facingTurns = facingToYawTurns(facing);

						if (modelId == MODEL_FENCE) {
							const model* post = fencePost ? fencePost : fence;
							if (post)
								emitModelInstance(output, *post, fx, fy, fz, material, opacity, renderMode, 0, block._definition);
							if (fenceSide) {
								// Side model points toward -Z (north).
								// Rotation (x,z)->(z,-x): 0=N, 1=W, 2=S, 3=E
								if (fenceConnects(_cache[cacheIndex(x, y, z - 1)]))
									emitModelInstance(output, *fenceSide, fx, fy, fz, material, opacity, renderMode, 0, block._definition);
								if (fenceConnects(_cache[cacheIndex(x - 1, y, z)]))
									emitModelInstance(output, *fenceSide, fx, fy, fz, material, opacity, renderMode, 1, block._definition);
								if (fenceConnects(_cache[cacheIndex(x, y, z + 1)]))
									emitModelInstance(output, *fenceSide, fx, fy, fz, material, opacity, renderMode, 2, block._definition);
								if (fenceConnects(_cache[cacheIndex(x + 1, y, z)]))
									emitModelInstance(output, *fenceSide, fx, fy, fz, material, opacity, renderMode, 3, block._definition);
							}
							else if (fence && !fencePost) {
								emitModelInstance(output, *fence, fx, fy, fz, material, opacity, renderMode, 0, block._definition);
							}
							continue;
						}

						if (modelId == MODEL_PANE) {
							const model* post = panePost ? panePost : pane;
							if (post)
								emitModelInstance(output, *post, fx, fy, fz, material, opacity, renderMode, 0, block._definition);
							if (paneSide) {
								if (paneConnects(_cache[cacheIndex(x, y, z - 1)]))
									emitModelInstance(output, *paneSide, fx, fy, fz, material, opacity, renderMode, 0, block._definition);
								if (paneConnects(_cache[cacheIndex(x - 1, y, z)]))
									emitModelInstance(output, *paneSide, fx, fy, fz, material, opacity, renderMode, 1, block._definition);
								if (paneConnects(_cache[cacheIndex(x, y, z + 1)]))
									emitModelInstance(output, *paneSide, fx, fy, fz, material, opacity, renderMode, 2, block._definition);
								if (paneConnects(_cache[cacheIndex(x + 1, y, z)]))
									emitModelInstance(output, *paneSide, fx, fy, fz, material, opacity, renderMode, 3, block._definition);
							}
							else if (pane && !panePost) {
								emitModelInstance(output, *pane, fx, fy, fz, material, opacity, renderMode, 0, block._definition);
							}
							continue;
						}

						if (modelId == MODEL_STAIRS) {
							stairNeighborInfo neighbors[4] = {
								stairNeighborFrom(_cache[cacheIndex(x, y, z + 1)]),
								stairNeighborFrom(_cache[cacheIndex(x + 1, y, z)]),
								stairNeighborFrom(_cache[cacheIndex(x, y, z - 1)]),
								stairNeighborFrom(_cache[cacheIndex(x - 1, y, z)])
							};
							const stairShape shape = resolveStairShape(facing, neighbors);
							const model* prototype = stairs;
							uint32_t yaw = facingTurns;
							switch (shape) {
							case stairShape::outerLeft:
								prototype = stairsOuter ? stairsOuter : stairs;
								break;
							case stairShape::outerRight:
								prototype = stairsOuter ? stairsOuter : stairs;
								yaw = (facingTurns + 3u) & 3u;
								break;
							case stairShape::innerLeft:
								prototype = stairsInner ? stairsInner : stairs;
								break;
							case stairShape::innerRight:
								prototype = stairsInnerRight
									? stairsInnerRight
									: (stairsInner ? stairsInner : stairs);
								break;
							case stairShape::straight:
							default:
								prototype = stairs;
								break;
							}
							if (!prototype) continue;
							emitModelInstance(
								output, *prototype, fx, fy, fz,
								material, opacity, renderMode, yaw, block._definition);
							continue;
						}

						if (modelId == MODEL_THIN &&
							block._definition->_behavior.redstone == blockRedstoneBehavior::wire) {
							// Bit 13 identifies redstone dust to the pixel shader; bits 9-12
							// carry its per-block signal strength without consuming a material.
							const uint32_t dustPower = (block._state >> 17u) & 0xfu;
							const uint32_t dustVertexFlags = 0x2000u | (dustPower << 9u);
							// Redstone dust uses the dot on UP and the line texture on SIDE.
							const uint32_t dotMaterial =
								block._definition->materialForFace(BLOCK_FACE_UP);
							const uint32_t lineMaterial =
								block._definition->materialForFace(BLOCK_FACE_NORTH);
							auto connectionInDirection = [&](int32_t dx, int32_t dz) {
								const meshBlock& beside = _cache[cacheIndex(x + dx, y, z + dz)];
								if (redstoneDustConnects(beside)) return 1;
								if (redstoneDustBlockedBy(beside._definition)) {
									if (y + 1 >= CHUNK_HEIGHT) return 0;
									const meshBlock& overhead = _cache[cacheIndex(x, y + 1, z)];
									if (redstoneDustBlockedBy(overhead._definition)) return 0;
									return redstoneDustIsWire(
										_cache[cacheIndex(x + dx, y + 1, z + dz)]) ? 2 : 0;
								}
								if (y == 0) return 0;
								return redstoneDustIsWire(
									_cache[cacheIndex(x + dx, y - 1, z + dz)]) ? 1 : 0;
							};
							const int connectNKind = connectionInDirection(0, -1);
							const int connectWKind = connectionInDirection(-1, 0);
							const int connectSKind = connectionInDirection(0, 1);
							const int connectEKind = connectionInDirection(1, 0);
							const bool connectN = connectNKind != 0;
							const bool connectW = connectWKind != 0;
							const bool connectS = connectSKind != 0;
							const bool connectE = connectEKind != 0;
							const int connectCount =
								(connectN ? 1 : 0) + (connectW ? 1 : 0) +
								(connectS ? 1 : 0) + (connectE ? 1 : 0);
							const bool straightNS = connectN && connectS && !connectW && !connectE;
							const bool straightEW = connectE && connectW && !connectN && !connectS;
							const bool corner = connectCount == 2 && !straightNS && !straightEW;
							const bool junction = connectCount >= 3 || corner;
							// Isolated dust points along its placement facing. Nearby
							// redstone components override that default with connection arms.
							if (connectCount == 0) {
								if (dustSide)
									emitModelInstance(
										output, *dustSide, fx, fy, fz,
										lineMaterial, opacity, renderMode,
										(facing + 2u) & 3u, nullptr, -1, dustVertexFlags);
								else if (thin)
									emitModelInstance(
										output, *thin, fx, fy, fz,
										dotMaterial, opacity, renderMode,
										0u, nullptr, -1, dustVertexFlags);
							}
							else {
								if (dustSide) {
									if (connectN)
										emitModelInstance(output, *dustSide, fx, fy, fz, lineMaterial, opacity, renderMode, 0, nullptr, -1, dustVertexFlags);
									if (connectW)
										emitModelInstance(output, *dustSide, fx, fy, fz, lineMaterial, opacity, renderMode, 1, nullptr, -1, dustVertexFlags);
									if (connectS)
										emitModelInstance(output, *dustSide, fx, fy, fz, lineMaterial, opacity, renderMode, 2, nullptr, -1, dustVertexFlags);
									if (connectE)
										emitModelInstance(output, *dustSide, fx, fy, fz, lineMaterial, opacity, renderMode, 3, nullptr, -1, dustVertexFlags);
								}
								if (dustUp) {
									if (connectNKind == 2)
										emitModelInstance(output, *dustUp, fx, fy, fz, lineMaterial, opacity, renderMode, 0, nullptr, -1, dustVertexFlags);
									if (connectWKind == 2)
										emitModelInstance(output, *dustUp, fx, fy, fz, lineMaterial, opacity, renderMode, 1, nullptr, -1, dustVertexFlags);
									if (connectSKind == 2)
										emitModelInstance(output, *dustUp, fx, fy, fz, lineMaterial, opacity, renderMode, 2, nullptr, -1, dustVertexFlags);
									if (connectEKind == 2)
										emitModelInstance(output, *dustUp, fx, fy, fz, lineMaterial, opacity, renderMode, 3, nullptr, -1, dustVertexFlags);
								}
								if (junction && thin)
									emitModelInstance(
										output, *thin, fx, fy, fz,
										dotMaterial, opacity, renderMode,
										0u, nullptr, -1, dustVertexFlags);
							}
							continue;
						}

						if (isDiodeModel(modelId)) {
							if (const model* prototype = models.get(diodeModelForState(modelId, block._state)))
								emitModelInstance(output, *prototype, fx, fy, fz, material, opacity,
									renderMode, (facing + 2u) & 3u, block._definition);
							continue;
						}

						if (modelId == MODEL_LEVER) {
							if (const model* prototype = models.get(MODEL_LEVER))
								emitModelInstance(output, *prototype, fx, fy, fz,
									material, opacity, renderMode, facing & 3u,
									block._definition, -1, 0u, block._state);
							continue;
						}

						if (modelId == MODEL_TORCH) {
							const bool wall = isWallAttached(block._state);
							const model* prototype = (wall && torchWall) ? torchWall : torch;
							if (!prototype) continue;
							const uint32_t yaw = wall ? wallTorchYawTurns(facing) : 0u;
							emitModelInstance(
								output, *prototype, fx, fy, fz,
								material, opacity, renderMode, yaw, block._definition);
							continue;
						}

						if (modelId == MODEL_DOOR || modelId == MODEL_DOOR_OPEN) {
							const bool open = isBlockOpen(block._state);
							const model* prototype = open
								? (openDoor ? openDoor : closedDoor)
								: (closedDoor ? closedDoor : openDoor);
							if (!prototype) continue;
							emitModelInstance(
								output, *prototype, fx, fy, fz,
								material, opacity, renderMode,
								(facingTurns + 1u) & 3u, block._definition,
								open ? 2 : 0);
							continue;
						}

						if (modelId == MODEL_TRAPDOOR || modelId == MODEL_FENCE_GATE) {
							const bool open = isBlockOpen(block._state);
							const uint32_t visualModel = open
								? (modelId == MODEL_TRAPDOOR
									? MODEL_TRAPDOOR_OPEN : MODEL_FENCE_GATE_OPEN)
								: modelId;
							const model* prototype = models.get(visualModel);
							if (!prototype) continue;
							emitModelInstance(
								output, *prototype, fx, fy, fz,
								material, opacity, renderMode,
								facingTurns, block._definition,
								modelId == MODEL_TRAPDOOR ? (open ? 2 : 1) : -1);
							continue;
						}

						const model* prototype = nullptr;
						switch (modelId) {
						case MODEL_SLAB: prototype = slab; break;
						case MODEL_CROSS: prototype = cross; break;
						case MODEL_THIN: prototype = thin; break;
						default: prototype = models.get(modelId); break;
						}
						if (prototype) {
							emitModelInstance(
								output, *prototype, fx, fy, fz,
								material, opacity, renderMode,
								(usesPlacementFacing(modelId) || blockUsesFacingState(block._state))
									? facingTurns : 0u,
								block._definition);
						}
					}
				}
			}
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
						f._vertexFlags = 0u;

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

						if (faceVisible(block._state, *block._definition, direction, nx, ny, nz)) {
							f._visible = true;
							uint32_t materialFace = static_cast<uint32_t>(direction);
							if (blockUsesFacingState(block._state))
								materialFace = localFaceForFacing(materialFace, blockFacing(block._state));
			f._material = block._definition->materialForFace(materialFace);
			f._textureRotation = block._definition->_faceRotations[materialFace];
							f._opacity = block._definition->_opacity;
							f._renderMode = block._definition->_renderMode;
							f._ao = faceAO(x, y, z);
							if (block._definition->_behavior.redstone == blockRedstoneBehavior::lamp)
								f._vertexFlags = 0x4000u | (((block._state >> 17u) & 0xfu) << 9u);
							f._fluidLevel = fluidLevel(block._state);
							f._neighborFluidLevel = (direction < 2 || direction >= 4)
								? fluidLevel(getState(nx, ny, nz)) : 0u;
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
								n._textureRotation == f._textureRotation &&
								n._vertexFlags == f._vertexFlags &&
								n._opacity == f._opacity &&
								n._renderMode == f._renderMode &&
								n._ao == f._ao &&
								n._fluidLevel == f._fluidLevel &&
								n._neighborFluidLevel == f._neighborFluidLevel;
							};

						// A quad with varying AO must stay block-sized; otherwise the
						// four corner values get stretched across the whole greedy quad.
						if (uniformAO && f._fluidLevel == 0u) {
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

						if (f._fluidLevel != 0u) {
							const int32_t blockX = direction < 2 ? slice : i;
							const int32_t blockY = yStart + (direction < 2 || direction >= 4 ? j : slice);
							const int32_t blockZ = direction >= 4 ? slice : (direction < 2 ? i : j);
							const float baseY = static_cast<float>(blockY);
							const float h00 = baseY + fluidCornerHeight(blockX, blockY, blockZ, 0, 0);
							const float h10 = baseY + fluidCornerHeight(blockX, blockY, blockZ, 1, 0);
							const float h01 = baseY + fluidCornerHeight(blockX, blockY, blockZ, 0, 1);
							const float h11 = baseY + fluidCornerHeight(blockX, blockY, blockZ, 1, 1);
							if (direction == 0) { cc.y = h01; d.y = h00; }
							else if (direction == 1) { cc.y = h10; d.y = h11; }
							else if (direction == 3) {
								a.y = h10; b.y = h00; cc.y = h01; d.y = h11;
								const DirectX::XMVECTOR edgeA = DirectX::XMVectorSubtract(
									DirectX::XMLoadFloat3(&b), DirectX::XMLoadFloat3(&a));
								const DirectX::XMVECTOR edgeB = DirectX::XMVectorSubtract(
									DirectX::XMLoadFloat3(&d), DirectX::XMLoadFloat3(&a));
								DirectX::XMStoreFloat3(&normal,
									DirectX::XMVector3Normalize(DirectX::XMVector3Cross(edgeA, edgeB)));
							}
							else if (direction == 4) { cc.y = h00; d.y = h10; }
							else if (direction == 5) { cc.y = h11; d.y = h01; }
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
							f._textureRotation, f._ao[0], f._ao[1], f._ao[2], f._ao[3]
						);
						for (size_t v = target._vertex.size() - 4u; v < target._vertex.size(); ++v)
							target._vertex[v]._ao |= f._vertexFlags;

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
			meshDetailModels(output, models, 0, CHUNK_HEIGHT);

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
				meshDetailModels(mesh, models, yStart, yEnd);
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
			meshDetailModels(output, models, yStart, yEnd);
			if (timings) {
				const auto end = std::chrono::steady_clock::now();
				timings->cacheNanoseconds = static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(faceStart - cacheStart).count());
				timings->faceNanoseconds = static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(end - faceStart).count());
			}
			return output;
		}

		// Detail meshes (doors/slabs/stairs/fences) for GPU-meshed chunks that only emit cubes.
		chunkSectionMeshes generateDoorSections(
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
			uint32_t sectionMask = ~0u
		) {
			_neighbors[0] = negX; _neighbors[1] = posX;
			_neighbors[2] = negZ; _neighbors[3] = posZ;
			_neighbors[4] = negXNegZ; _neighbors[5] = negXPosZ;
			_neighbors[6] = posXNegZ; _neighbors[7] = posXPosZ;
			buildCache(c, blocks);
			chunkSectionMeshes output{};
			for (uint32_t section = 0; section < CHUNK_SUBCHUNKS; ++section) {
				if (((sectionMask >> section) & 1u) == 0u) continue;
				const int32_t yStart = static_cast<int32_t>(section * SUBCHUNK_HEIGHT);
				const int32_t yEnd = std::min(yStart + SUBCHUNK_HEIGHT, CHUNK_HEIGHT);
				meshDetailModels(output[section], models, yStart, yEnd);
			}
			return output;
		}
	};

}
