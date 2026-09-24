#include "waterPhysics.h"

#include <algorithm>
#include <deque>
#include <vector>

namespace ac {
	namespace {
		bool isFullSupport(const std::optional<blockId>& state) {
			if (!state) return false;
			const blockId type = blockType(*state);
			return type != 0u &&
				(type != WATER_BLOCK_TYPE || fluidLevel(*state) == MAX_FLUID_LEVEL);
		}

		bool canHoldWater(const std::optional<blockId>& state) {
			if (!state) return false;
			const blockId type = blockType(*state);
			return type == 0u || type == WATER_BLOCK_TYPE;
		}
	}

	size_t waterPhysics::cellHash::operator()(const cell& value) const {
		size_t result = std::hash<int32_t>{}(value.x);
		result ^= std::hash<int32_t>{}(value.y) + 0x9e3779b9u + (result << 6u) + (result >> 2u);
		result ^= std::hash<int32_t>{}(value.z) + 0x9e3779b9u + (result << 6u) + (result >> 2u);
		return result;
	}

	void waterPhysics::enqueue(int32_t x, int32_t y, int32_t z, bool urgent) {
		if (y < 0 || y >= CHUNK_HEIGHT) return;
		const cell value{ x, y, z };
		if (_queued.find(value) == _queued.end()) {
			if (pending() >= MAX_PENDING_UPDATES) return;
			_queued.insert(value);
			(urgent ? _urgentPending : _pending).push_back(value);
			return;
		}
		if (!urgent) return;

		// Promote an already queued terrain update so a player edit never waits
		// behind the background water backlog.
		const auto existing = std::find(_pending.begin(), _pending.end(), value);
		if (existing != _pending.end()) {
			_pending.erase(existing);
			_urgentPending.push_back(value);
		}
	}

	void waterPhysics::notifyFlowNeighbors(
		int32_t x, int32_t y, int32_t z,
		const blockReader& read,
		bool urgent
	) {
		static constexpr int32_t neighbors[6][3] = {
			{ -1, 0, 0 }, { 1, 0, 0 },
			{ 0, -1, 0 }, { 0, 1, 0 },
			{ 0, 0, -1 }, { 0, 0, 1 }
		};
		for (const auto& offset : neighbors) {
			const int32_t nx = x + offset[0];
			const int32_t ny = y + offset[1];
			const int32_t nz = z + offset[2];
			if (canHoldWater(read(nx, ny, nz)))
				enqueue(nx, ny, nz, urgent);
		}
	}

	void waterPhysics::notifyBlockChanged(
		int32_t x, int32_t y, int32_t z,
		const blockReader& read,
		bool urgent
	) {
		if (canHoldWater(read(x, y, z)))
			enqueue(x, y, z, urgent);
		notifyFlowNeighbors(x, y, z, read, urgent);
	}

	size_t waterPhysics::step(size_t updateBudget, const blockReader& read, const blockWriter& write) {
		size_t changed = 0;
		// Process only the wave that was pending at the beginning of this update.
		// Neighbors enqueued by a change wait for the next water tick instead of
		// cascading through an entire river or waterfall in one server update.
		struct queuedCell { cell position; bool urgent; };
		std::vector<queuedCell> batch;
		batch.reserve((std::min)(updateBudget, pending()));
		while (batch.size() < updateBudget && !_urgentPending.empty()) {
			batch.push_back({ _urgentPending.front(), true });
			_urgentPending.pop_front();
		}
		while (batch.size() < updateBudget && !_pending.empty()) {
			batch.push_back({ _pending.front(), false });
			_pending.pop_front();
		}
		for (const queuedCell& queued : batch) {
			const cell position = queued.position;
			_queued.erase(position);
			const std::optional<blockId> currentValue = read(position.x, position.y, position.z);
			if (!currentValue) continue;
			const blockId currentType = blockType(*currentValue);
			if (currentType != 0u && currentType != WATER_BLOCK_TYPE) continue;

			const uint8_t currentLevel = fluidLevel(*currentValue);
			uint8_t desiredLevel = currentLevel == MAX_FLUID_LEVEL ? MAX_FLUID_LEVEL : 0u;
			if (desiredLevel != MAX_FLUID_LEVEL) {
				uint32_t adjacentSources = 0u;
				if (const std::optional<blockId> above = read(position.x, position.y + 1, position.z)) {
					const uint8_t aboveLevel = fluidLevel(*above);
					if (aboveLevel > 0u)
						desiredLevel = (std::max)(desiredLevel, static_cast<uint8_t>((std::min)(aboveLevel, uint8_t{ 7u })));
				}
				constexpr int32_t horizontal[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
				for (const auto& offset : horizontal) {
					if (const std::optional<blockId> neighbor = read(
						position.x + offset[0], position.y, position.z + offset[1])) {
						const uint8_t neighborLevel = fluidLevel(*neighbor);
						adjacentSources += neighborLevel == MAX_FLUID_LEVEL ? 1u : 0u;
						const std::optional<blockId> neighborSupport = read(
							position.x + offset[0], position.y - 1, position.z + offset[1]);
						if (neighborLevel > 1u && isFullSupport(neighborSupport))
							desiredLevel = (std::max)(desiredLevel, static_cast<uint8_t>(neighborLevel - 1u));
					}
				}
				if (adjacentSources >= 2u) {
					const std::optional<blockId> below = read(position.x, position.y - 1, position.z);
					if (isFullSupport(below)) desiredLevel = MAX_FLUID_LEVEL;
				}
			}

			const blockId desiredState = withFluidLevel(WATER_BLOCK_TYPE, desiredLevel);
			if (desiredState != *currentValue && write(position.x, position.y, position.z, desiredState)) {
				++changed;
				notifyFlowNeighbors(position.x, position.y, position.z, read, queued.urgent);
			}
		}
		return changed;
	}

	void waterPhysics::clear() {
		_urgentPending.clear();
		_pending.clear();
		_queued.clear();
	}

	void waterPhysics::fillConnectedBasinWater(blockId* blocks, blockId waterType) {
		if (!blocks) return;
		std::deque<int32_t> flood;
		const auto index = [](int32_t x, int32_t y, int32_t z) {
			return x + CHUNK_WIDTH * (z + CHUNK_LENGTH * y);
		};
		for (int32_t y = 1; y < CHUNK_HEIGHT; ++y) {
			for (int32_t z = 0; z < CHUNK_LENGTH; ++z) {
				for (int32_t x = 0; x < CHUNK_WIDTH; ++x) {
					if (fluidLevel(blocks[index(x, y, z)], waterType) == MAX_FLUID_LEVEL)
						flood.push_back(index(x, y, z));
				}
			}
		}

		constexpr int32_t directions[5][3] = {
			{ 1, 0, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { 0, -1, 0 }
		};
		const blockId source = withFluidLevel(waterType, MAX_FLUID_LEVEL);
		while (!flood.empty()) {
			const int32_t packed = flood.front();
			flood.pop_front();
			const int32_t x = packed % CHUNK_WIDTH;
			const int32_t yz = packed / CHUNK_WIDTH;
			const int32_t z = yz % CHUNK_LENGTH;
			const int32_t y = yz / CHUNK_LENGTH;
			for (const auto& offset : directions) {
				const int32_t nx = x + offset[0];
				const int32_t ny = y + offset[1];
				const int32_t nz = z + offset[2];
				if (nx < 0 || nx >= CHUNK_WIDTH || nz < 0 || nz >= CHUNK_LENGTH ||
					ny < 1 || ny >= CHUNK_HEIGHT)
					continue;
				const int32_t neighbor = index(nx, ny, nz);
				if (blocks[neighbor] != 0u) continue;
				blocks[neighbor] = source;
				flood.push_back(neighbor);
			}
		}
	}

}
