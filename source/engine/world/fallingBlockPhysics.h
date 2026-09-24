#pragma once

#include "chunk.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <unordered_set>
#include <vector>

namespace ac {

	struct fallingBlockEntity {
		blockId id = 0;
		int32_t x = 0;
		int32_t z = 0;
		float y = 0.0f;
		float vy = 0.0f;
		float originY = 0.0f;
		bool hurtPlayer = false;
		float px = 0.0f;
		float pz = 0.0f;
		float vx = 0.0f;
		float vz = 0.0f;
	};

	class fallingBlockPhysics {
	public:
		using blockReader = std::function<std::optional<blockId>(int32_t, int32_t, int32_t)>;
		using blockWriter = std::function<bool(int32_t, int32_t, int32_t, blockId)>;
		using gravityQuery = std::function<bool(blockId)>;
		using supportQuery = std::function<bool(blockId)>;

		using occupyQuery = std::function<bool(int32_t, int32_t, int32_t)>;

		void enqueue(int32_t x, int32_t y, int32_t z, bool urgent = false);
		void notifyBlockChanged(
			int32_t x, int32_t y, int32_t z,
			const blockReader& read,
			const gravityQuery& isGravity,
			bool urgent = false);
		size_t step(
			size_t updateBudget,
			const blockReader& read,
			const blockWriter& write,
			const gravityQuery& isGravity,
			const supportQuery& canSupport
		);
		size_t updateEntities(
			float deltaTime,
			const blockReader& read,
			const blockWriter& write,
			const gravityQuery& isGravity,
			const supportQuery& canSupport,
			const occupyQuery& occupy = {},
			std::vector<fallingBlockEntity>* crushed = nullptr
		);
		void clear();
		size_t pending() const { return _urgentPending.size() + _pending.size(); }
		const std::vector<fallingBlockEntity>& entities() const { return _entities; }
		std::vector<fallingBlockEntity>& entities() { return _entities; }

	private:
		struct cell {
			int32_t x = 0;
			int32_t y = 0;
			int32_t z = 0;
			bool operator==(const cell& other) const {
				return x == other.x && y == other.y && z == other.z;
			}
		};
		struct cellHash {
			size_t operator()(const cell& value) const;
		};

		static constexpr size_t MAX_PENDING_UPDATES = 65'536u;
		static constexpr size_t MAX_ENTITIES = 512u;
		static constexpr float GRAVITY = 32.0f;
		static constexpr float TERMINAL_VELOCITY = 40.0f;

		bool tryLand(
			fallingBlockEntity& entity,
			float newY,
			const blockReader& read,
			const blockWriter& write,
			const gravityQuery& isGravity,
			const supportQuery& canSupport,
			const occupyQuery& occupy,
			std::vector<fallingBlockEntity>* crushed
		);

		std::deque<cell> _urgentPending;
		std::deque<cell> _pending;
		std::unordered_set<cell, cellHash> _queued;
		std::vector<fallingBlockEntity> _entities;
	};

}
