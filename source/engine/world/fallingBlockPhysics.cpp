#include "fallingBlockPhysics.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ac {

	size_t fallingBlockPhysics::cellHash::operator()(const cell& value) const {
		size_t result = std::hash<int32_t>{}(value.x);
		result ^= std::hash<int32_t>{}(value.y) + 0x9e3779b9u + (result << 6u) + (result >> 2u);
		result ^= std::hash<int32_t>{}(value.z) + 0x9e3779b9u + (result << 6u) + (result >> 2u);
		return result;
	}

	void fallingBlockPhysics::enqueue(int32_t x, int32_t y, int32_t z, bool urgent) {
		if (y < 0 || y >= CHUNK_HEIGHT) return;
		const cell value{ x, y, z };
		if (_queued.find(value) == _queued.end()) {
			if (pending() >= MAX_PENDING_UPDATES) return;
			_queued.insert(value);
			(urgent ? _urgentPending : _pending).push_back(value);
			return;
		}
		if (!urgent) return;
		const auto existing = std::find(_pending.begin(), _pending.end(), value);
		if (existing != _pending.end()) {
			_pending.erase(existing);
			_urgentPending.push_back(value);
		}
	}

	void fallingBlockPhysics::notifyBlockChanged(
		int32_t x, int32_t y, int32_t z,
		const blockReader& read,
		const gravityQuery& isGravity,
		bool urgent
	) {
		const auto tryEnqueue = [&](int32_t cx, int32_t cy, int32_t cz) {
			const auto state = read(cx, cy, cz);
			if (state && *state != 0u && isGravity(*state))
				enqueue(cx, cy, cz, urgent);
		};
		tryEnqueue(x, y, z);
		tryEnqueue(x, y + 1, z);
	}

	size_t fallingBlockPhysics::step(
		size_t updateBudget,
		const blockReader& read,
		const blockWriter& write,
		const gravityQuery& isGravity,
		const supportQuery& canSupport
	) {
		size_t moved = 0;
		struct queuedCell { cell position; };
		std::vector<queuedCell> batch;
		batch.reserve((std::min)(updateBudget, pending()));
		while (batch.size() < updateBudget && !_urgentPending.empty()) {
			batch.push_back({ _urgentPending.front() });
			_urgentPending.pop_front();
		}
		while (batch.size() < updateBudget && !_pending.empty()) {
			batch.push_back({ _pending.front() });
			_pending.pop_front();
		}

		for (const queuedCell& queued : batch) {
			const cell position = queued.position;
			_queued.erase(position);
			const std::optional<blockId> current = read(position.x, position.y, position.z);
			if (!current || *current == 0u) continue;
			if (!isGravity(*current)) continue;

			const int32_t belowY = position.y - 1;
			if (belowY < 0) continue;
			const std::optional<blockId> below = read(position.x, belowY, position.z);
			if (!below) continue;
			if (canSupport(*below)) continue;
			if (*below != 0u) continue; // occupied by non-support (e.g. water) — leave for now

			if (_entities.size() >= MAX_ENTITIES) continue;
			if (!write(position.x, position.y, position.z, 0u)) continue;

			fallingBlockEntity spawned{};
			spawned.id = *current;
			spawned.x = position.x;
			spawned.z = position.z;
			spawned.y = static_cast<float>(position.y);
			spawned.originY = spawned.y;
			spawned.px = static_cast<float>(position.x) + 0.5f;
			spawned.pz = static_cast<float>(position.z) + 0.5f;
			_entities.push_back(spawned);
			++moved;
			const auto above = read(position.x, position.y + 1, position.z);
			if (above && *above != 0u && isGravity(*above))
				enqueue(position.x, position.y + 1, position.z, true);
		}
		return moved;
	}

	bool fallingBlockPhysics::tryLand(
		fallingBlockEntity& entity,
		float newY,
		const blockReader& read,
		const blockWriter& write,
		const gravityQuery& isGravity,
		const supportQuery& canSupport,
		const occupyQuery& occupy,
		std::vector<fallingBlockEntity>* crushed
	) {
		std::optional<int32_t> landY;
		const int32_t startSupport = static_cast<int32_t>(std::floor(entity.y)) - 1;
		for (int32_t supportY = startSupport; supportY >= -1; --supportY) {
			if (supportY < 0) {
				landY = 0;
				break;
			}
			const std::optional<blockId> below = read(entity.x, supportY, entity.z);
			if (!below) {
				entity.y = static_cast<float>(supportY + 1);
				entity.vy = 0.0f;
				return true;
			}
			if (*below == 0u) continue;
			// Solid support or non-air obstacle (e.g. water) — stop on top.
			(void)canSupport;
			landY = supportY + 1;
			break;
		}
		if (!landY || newY > static_cast<float>(*landY)) return false;

		const int32_t placeY = (std::max)(0, (std::min)(*landY, CHUNK_HEIGHT - 1));
		if (occupy && occupy(entity.x, placeY, entity.z)) {
			if (crushed) {
				entity.y = static_cast<float>(placeY);
				entity.vy = 0.0f;
				crushed->push_back(entity);
			}
			return true;
		}
		if (!write(entity.x, placeY, entity.z, entity.id)) {
			entity.y = static_cast<float>(placeY);
			entity.vy = 0.0f;
			return true;
		}
		const auto above = read(entity.x, placeY + 1, entity.z);
		if (above && *above != 0u && isGravity(*above))
			enqueue(entity.x, placeY + 1, entity.z, true);
		return true;
	}

	size_t fallingBlockPhysics::updateEntities(
		float deltaTime,
		const blockReader& read,
		const blockWriter& write,
		const gravityQuery& isGravity,
		const supportQuery& canSupport,
		const occupyQuery& occupy,
		std::vector<fallingBlockEntity>* crushed
	) {
		if (_entities.empty()) return 0;
		const float dt = (std::min)((std::max)(deltaTime, 0.0f), 0.1f);
		size_t landed = 0;
		std::vector<fallingBlockEntity> remaining;
		remaining.reserve(_entities.size());

		for (fallingBlockEntity& entity : _entities) {
			if (entity.px == 0.0f && entity.pz == 0.0f) {
				entity.px = static_cast<float>(entity.x) + 0.5f;
				entity.pz = static_cast<float>(entity.z) + 0.5f;
			}
			auto readId = [&](int32_t x, int32_t y, int32_t z) -> blockId {
				return read(x, y, z).value_or(0u);
			};
			const int32_t cellY = static_cast<int32_t>(std::floor(entity.y));
			const blockId here = readId(entity.x, cellY, entity.z);
			const uint8_t water = fluidLevel(here);
			if (water > 0u) {
				entity.vy -= GRAVITY * dt * 0.22f;
				if (entity.vy < -4.0f) entity.vy = -4.0f;
				const fluidFlow flow = waterFlowAt(entity.x, cellY, entity.z, readId);
				entity.vx += flow.x * 1.05f * dt;
				entity.vy += flow.y * 1.25f * dt;
				entity.vz += flow.z * 1.05f * dt;
				entity.vx *= 0.90f;
				entity.vz *= 0.90f;
			}
			else {
				entity.vy -= GRAVITY * dt;
				if (entity.vy < -TERMINAL_VELOCITY) entity.vy = -TERMINAL_VELOCITY;
			}
			const float newY = entity.y + entity.vy * dt;
			float newPx = entity.px + entity.vx * dt;
			float newPz = entity.pz + entity.vz * dt;
			const int32_t newX = static_cast<int32_t>(std::floor(newPx));
			const int32_t newZ = static_cast<int32_t>(std::floor(newPz));
			const blockId dest = readId(newX, static_cast<int32_t>(std::floor(newY)), newZ);
			if (dest != 0u && blockType(dest) != WATER_BLOCK_TYPE) {
				newPx = entity.px;
				newPz = entity.pz;
				entity.vx = 0.0f;
				entity.vz = 0.0f;
			}
			else {
				entity.x = newX;
				entity.z = newZ;
			}
			if (tryLand(entity, newY, read, write, isGravity, canSupport, occupy, crushed)) {
				++landed;
				continue;
			}
			entity.px = newPx;
			entity.pz = newPz;
			entity.y = newY;
			if (entity.y < -2.0f || entity.y > static_cast<float>(CHUNK_HEIGHT + 2))
				continue;
			remaining.push_back(entity);
		}

		_entities.swap(remaining);
		return landed;
	}

	void fallingBlockPhysics::clear() {
		_urgentPending.clear();
		_pending.clear();
		_queued.clear();
		_entities.clear();
	}

}
