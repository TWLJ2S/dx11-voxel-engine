#pragma once

#include "chunk.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <unordered_set>

namespace ac {

	class waterPhysics {
	public:
		using blockReader = std::function<std::optional<blockId>(int32_t, int32_t, int32_t)>;
		using blockWriter = std::function<bool(int32_t, int32_t, int32_t, blockId)>;

		void enqueue(int32_t x, int32_t y, int32_t z, bool urgent = false);
		void notifyBlockChanged(
			int32_t x, int32_t y, int32_t z,
			const blockReader& read,
			bool urgent = false);
		void notifyFlowNeighbors(
			int32_t x, int32_t y, int32_t z,
			const blockReader& read,
			bool urgent = false);
		size_t step(size_t updateBudget, const blockReader& read, const blockWriter& write);
		void clear();
		size_t pending() const { return _urgentPending.size() + _pending.size(); }

		// After terrain generation, expand source water into connected air in the
		// same chunk so ocean/river/lake voids are not left dry until physics runs.
		static void fillConnectedBasinWater(blockId* blocks, blockId waterType = WATER_BLOCK_TYPE);

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
		static constexpr size_t MAX_PENDING_UPDATES = 131'072u;
		std::deque<cell> _urgentPending;
		std::deque<cell> _pending;
		std::unordered_set<cell, cellHash> _queued;
	};

}
