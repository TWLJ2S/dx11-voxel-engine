#pragma once

#include "chunk.h"

#include <cstddef>
#include <cstdint>
#include <climits>
#include <deque>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace ac {

	inline blockId BLOCK_REDSTONE = 0;
	inline blockId BLOCK_REDSTONE_BLOCK = 0;
	inline blockId BLOCK_REDSTONE_WIRE = 0;
	inline blockId BLOCK_REDSTONE_LAMP = 0;
	inline blockId BLOCK_REDSTONE_LAMP_ON = 0;
	inline blockId BLOCK_REDSTONE_TORCH = 0;
	inline blockId BLOCK_REDSTONE_TORCH_OFF = 0;
	inline blockId BLOCK_LEVER = 0;
	inline blockId BLOCK_LEVER_ON = 0;
	inline blockId BLOCK_REPEATER = 0;
	inline blockId BLOCK_COMPARATOR = 0;
	inline blockId BLOCK_OBSERVER = 0;
	constexpr uint32_t GAME_TICKS_PER_REDSTONE_TICK = 4u;
	constexpr uint32_t repeaterDelay(blockId state) { return ((state >> 25u) & 3u) + 1u; }
	constexpr blockId withRepeaterDelay(blockId state, uint32_t delay) {
		return (state & ~(3u << 25u)) | (((delay - 1u) & 3u) << 25u);
	}
	constexpr bool repeaterLocked(blockId state) { return (state & (1u << 27u)) != 0u; }
	constexpr bool comparatorSubtract(blockId state) { return (state & (1u << 28u)) != 0u; }
	inline bool isDiode(blockId state) {
		return blockType(state) == BLOCK_REPEATER || blockType(state) == BLOCK_COMPARATOR;
	}

	// Power level 0-15 packed above facing bits.
	constexpr uint32_t REDSTONE_POWER_SHIFT = 17u;
	constexpr uint32_t REDSTONE_POWER_MASK = 0xfu;

	constexpr uint8_t redstonePower(blockId state) {
		return static_cast<uint8_t>((state >> REDSTONE_POWER_SHIFT) & REDSTONE_POWER_MASK);
	}

	constexpr blockId withRedstonePower(blockId state, uint8_t power) {
		return (state & ~(REDSTONE_POWER_MASK << REDSTONE_POWER_SHIFT)) |
			((static_cast<blockId>(power) & REDSTONE_POWER_MASK) << REDSTONE_POWER_SHIFT);
	}

	constexpr bool isRedstoneWire(blockId type) {
		return blockType(type) == BLOCK_REDSTONE_WIRE;
	}

	// Dust strength is packed into its detail-mesh vertices for tinting. A
	// power-only state transition therefore still requires a section remesh.
	constexpr bool redstoneWireVisualStateChanged(blockId previousState, blockId newState) {
		return (isRedstoneWire(previousState) || isRedstoneWire(newState)) &&
			redstonePower(previousState) != redstonePower(newState);
	}

	inline bool isRedstoneTorch(blockId type) {
		const blockId t = blockType(type);
		return t == BLOCK_REDSTONE_TORCH || t == BLOCK_REDSTONE_TORCH_OFF;
	}

	inline bool isRedstoneLamp(blockId type) {
		const blockId t = blockType(type);
		return t == BLOCK_REDSTONE_LAMP || t == BLOCK_REDSTONE_LAMP_ON;
	}

	inline bool isLever(blockId type) {
		const blockId t = blockType(type);
		return t == BLOCK_LEVER || t == BLOCK_LEVER_ON;
	}

	inline bool isRedstoneComponent(blockId type) {
		const blockId t = blockType(type);
		return t == BLOCK_REDSTONE_WIRE ||
			t == BLOCK_REDSTONE_BLOCK ||
			isRedstoneTorch(t) ||
			isRedstoneLamp(t) ||
			isLever(t) || isDiode(t) || t == BLOCK_OBSERVER;
	}

	inline bool redstoneConnectsVisually(blockId type) {
		return isRedstoneComponent(type);
	}

	inline blockId leverToggle(blockId state) {
		if (!isLever(state)) return state;
		return withBlockPowered(state, !isBlockPowered(state));
	}

	// Strong power emitted into neighbors (0-15).
	inline uint8_t redstoneOutputPower(blockId state) {
		const blockId type = blockType(state);
		if (type == BLOCK_REDSTONE_BLOCK) return 15u;
		if (isLever(type) && isBlockPowered(state)) return 15u;
		if (isRedstoneTorch(type) && isBlockPowered(state)) return 15u;
		if (type == BLOCK_REDSTONE_WIRE) return redstonePower(state);
		if (type == BLOCK_REPEATER) return isBlockPowered(state) ? 15u : 0u;
		if (type == BLOCK_COMPARATOR) return redstonePower(state);
		if (type == BLOCK_OBSERVER) return isBlockPowered(state) ? 15u : 0u;
		return 0u;
	}

	// Soft / conducted power present in a cell (wire power, sources, or max neighbor).
	inline uint8_t redstoneCellPower(blockId state) {
		const blockId type = blockType(state);
		if (type == BLOCK_REDSTONE_WIRE) return redstonePower(state);
		return redstoneOutputPower(state);
	}

	class redstonePhysics {
	public:
		using blockReader = std::function<std::optional<blockId>(int32_t, int32_t, int32_t)>;
		using blockWriter = std::function<bool(int32_t, int32_t, int32_t, blockId)>;
		// Optional powered devices (doors, etc.): return new state or nullopt to skip.
		using poweredDeviceUpdate = std::function<std::optional<blockId>(
			int32_t x, int32_t y, int32_t z, blockId state, uint8_t power)>;

		void setPoweredDeviceUpdate(poweredDeviceUpdate update) {
			_poweredDeviceUpdate = std::move(update);
		}

		using conductorQuery = std::function<bool(blockId)>;
		void setConductorQuery(conductorQuery query) {
			_conductorQuery = std::move(query);
		}
		using behaviorQuery = std::function<blockRedstoneBehavior(blockId)>;
		void setBehaviorQuery(behaviorQuery query) {
			_behaviorQuery = std::move(query);
		}
		using outputQuery = std::function<uint8_t(blockId)>;
		void setOutputQuery(outputQuery query) {
			_outputQuery = std::move(query);
		}
		// nullopt = ordinary block; zero = empty readable container.
		using analogQuery = std::function<std::optional<uint8_t>(int32_t, int32_t, int32_t)>;
		void setAnalogQuery(analogQuery query) { _analogQuery = std::move(query); }
		using dropDevice = std::function<void(int32_t, int32_t, int32_t, blockId)>;
		void setSupportRules(conductorQuery support, dropDevice drop) {
			_supportQuery = std::move(support); _dropDevice = std::move(drop);
		}

		void enqueue(int32_t x, int32_t y, int32_t z, bool urgent = false);
		void notifyBlockChanged(
			int32_t x, int32_t y, int32_t z,
			blockId previousState,
			blockId newState,
			const blockReader& read,
			bool urgent = false);
		void notifyNeighbors(
			int32_t x, int32_t y, int32_t z,
			const blockReader& read,
			bool urgent = false);
		uint8_t inputPowerAt(
			const blockReader& read,
			int32_t x, int32_t y, int32_t z) const;
		size_t step(
			size_t updateBudget,
			const blockReader& read,
			const blockWriter& write,
			bool advanceTick = true);
		void clear();
		size_t pending() const { return _urgentPending.size() + _pending.size(); }

	private:
		struct cell {
			int32_t x = 0;
			int32_t y = 0;
			int32_t z = 0;
			bool operator==(const cell& other) const {
				return x == other.x && y == other.y && z == other.z;
			}
		};
		struct cellHash { size_t operator()(const cell& value) const; };
		struct torchTransition {
			blockId targetState = 0;
			uint64_t dueTick = 0;
		};
		struct diodeTransition {
			uint64_t dueTick = 0;
			bool turnOn = false;
		};

		static constexpr size_t MAX_PENDING_UPDATES = 65536u;
		static constexpr uint64_t TORCH_DELAY_TICKS = GAME_TICKS_PER_REDSTONE_TICK;
		std::deque<cell> _urgentPending;
		std::deque<cell> _pending;
		std::unordered_set<cell, cellHash> _queued;
		std::unordered_map<cell, torchTransition, cellHash> _torchTransitions;
		std::unordered_map<cell, diodeTransition, cellHash> _diodeTransitions;
		std::unordered_set<cell, cellHash> _comparators;
		uint64_t _tick = 0u;
		poweredDeviceUpdate _poweredDeviceUpdate;
		conductorQuery _conductorQuery;
		behaviorQuery _behaviorQuery;
		outputQuery _outputQuery;
		analogQuery _analogQuery;
		conductorQuery _supportQuery;
		dropDevice _dropDevice;
		uint8_t outputToward(blockId state, int32_t sx, int32_t sy, int32_t sz,
			int32_t tx, int32_t ty, int32_t tz) const;
		uint8_t diodeRear(const blockReader& read, const cell& at, blockId state) const;
		uint8_t diodeSide(const blockReader& read, const cell& at, blockId state, bool lockOnly) const;

		blockRedstoneBehavior behavior(blockId state) const;
		uint8_t outputPower(blockId state) const;
		uint8_t incomingPower(
			const blockReader& read, int32_t x, int32_t y, int32_t z,
			bool attenuateWire,
			int32_t excludeX = INT32_MIN, int32_t excludeY = INT32_MIN,
			int32_t excludeZ = INT32_MIN,
			bool conductThroughSolidNeighbors = true) const;
		bool wirePointsToward(
			const blockReader& read,
			int32_t wireX, int32_t wireY, int32_t wireZ,
			int32_t targetX, int32_t targetY, int32_t targetZ) const;
		uint8_t wireIncomingPower(const blockReader& read, int32_t x, int32_t y, int32_t z) const;
		uint8_t supportPower(
			const blockReader& read, int32_t x, int32_t y, int32_t z, blockId torchState);
	};

}
