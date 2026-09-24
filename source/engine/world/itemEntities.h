#pragma once

#include <assets/cpuAsset.h>
#include <world/blockShape.h>
#include <world/chunk.h>
#include <world/world.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

namespace ac {

	inline constexpr uint32_t ITEM_MAX_STACK = 64u;
	inline constexpr size_t ITEM_MAX_ENTITIES = 2048u;
	inline constexpr float ITEM_SIZE = 0.25f;
	inline constexpr float ITEM_GRAVITY = 18.0f;
	inline constexpr float ITEM_TERMINAL = 28.0f;
	inline constexpr float ITEM_FRICTION = 0.82f;
	inline constexpr float ITEM_BOUNCE = 0.28f;
	inline constexpr float ITEM_PICKUP_DELAY = 0.45f;
	inline constexpr float ITEM_PICKUP_RANGE = 1.25f;
	inline constexpr float ITEM_MERGE_RANGE = 0.65f;
	inline constexpr float ITEM_LIFETIME = 300.0f;

	struct itemEntity {
		uint64_t id = 0;
		blockId itemId = 0;
		uint32_t count = 1;
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float vx = 0.0f;
		float vy = 0.0f;
		float vz = 0.0f;
		float yaw = 0.0f;
		float age = 0.0f;
		float pickupDelay = ITEM_PICKUP_DELAY;
		bool onGround = false;
	};

	class itemEntitySystem {
	public:
		using solidBoxesQuery = std::function<size_t(
			int32_t x, int32_t y, int32_t z, blockAABB* out, size_t capacity)>;
		using fluidQuery = std::function<bool(float x, float y, float z)>;
		using flowQuery = std::function<fluidFlow(float x, float y, float z)>;

		void clear() {
			_items.clear();
			_nextId = 2;
		}

		void loadFromWorld(const world& worldData) {
			clear();
			uint64_t maxId = 1;
			for (const savedEntity& entity : worldData.entities()) {
				if (entity.kind != savedEntityKind::item) continue;
				if (entity.itemId == 0 || entity.itemCount == 0) continue;
				itemEntity item{};
				item.id = entity.id;
				item.itemId = static_cast<blockId>(entity.itemId);
				item.count = (std::min)(entity.itemCount, ITEM_MAX_STACK);
				item.x = entity.position.x;
				item.y = entity.position.y;
				item.z = entity.position.z;
				item.vx = entity.velocity.x;
				item.vy = entity.velocity.y;
				item.vz = entity.velocity.z;
				item.yaw = entity.yaw;
				item.age = (std::max)(0.0f, entity.pitch);
				item.pickupDelay = 0.0f;
				item.onGround = (entity.flags & 1u) != 0u;
				_items.push_back(item);
				maxId = (std::max)(maxId, entity.id);
			}
			_nextId = maxId + 1;
		}

		void saveToWorld(world& worldData) const {
			std::vector<uint64_t> removeIds;
			for (const savedEntity& entity : worldData.entities()) {
				if (entity.kind == savedEntityKind::item)
					removeIds.push_back(entity.id);
			}
			for (uint64_t id : removeIds)
				worldData.removeEntity(id);

			for (const itemEntity& item : _items) {
				if (item.itemId == 0 || item.count == 0) continue;
				savedEntity entity{};
				entity.id = item.id;
				entity.kind = savedEntityKind::item;
				entity.flags = item.onGround ? 1u : 0u;
				entity.position = { item.x, item.y, item.z };
				entity.velocity = { item.vx, item.vy, item.vz };
				entity.yaw = item.yaw;
				entity.pitch = item.age;
				entity.scale = { ITEM_SIZE, ITEM_SIZE, ITEM_SIZE };
				entity.itemId = item.itemId;
				entity.itemCount = item.count;
				worldData.upsertEntity(entity);
			}
		}

		itemEntity* spawn(
			blockId itemId,
			uint32_t count,
			float x, float y, float z,
			float vx, float vy, float vz,
			float pickupDelay = ITEM_PICKUP_DELAY
		) {
			if (itemId == 0 || count == 0 || _items.size() >= ITEM_MAX_ENTITIES)
				return nullptr;
			itemEntity item{};
			item.id = _nextId++;
			item.itemId = itemId;
			item.count = (std::min)(count, ITEM_MAX_STACK);
			item.x = x;
			item.y = y;
			item.z = z;
			item.vx = vx;
			item.vy = vy;
			item.vz = vz;
			item.yaw = std::atan2(vx, vz);
			item.pickupDelay = pickupDelay;
			_items.push_back(item);
			return &_items.back();
		}

		const std::vector<itemEntity>& entities() const { return _items; }

		void update(
			float deltaTime,
			const solidBoxesQuery& solidBoxes,
			const fluidQuery& inFluid,
			const flowQuery& flow = {}
		) {
			if (deltaTime <= 0.0f || _items.empty()) return;

			for (itemEntity& item : _items) {
				item.age += deltaTime;
				if (item.pickupDelay > 0.0f)
					item.pickupDelay = (std::max)(0.0f, item.pickupDelay - deltaTime);
				item.yaw += deltaTime * 2.4f;

				const bool submerged = inFluid(item.x, item.y + ITEM_SIZE * 0.5f, item.z);
				float gravity = submerged ? ITEM_GRAVITY * 0.22f : ITEM_GRAVITY;
				item.vy -= gravity * deltaTime;
				if (item.vy < -ITEM_TERMINAL) item.vy = -ITEM_TERMINAL;
				if (submerged) {
					item.vx *= 0.90f;
					item.vy *= 0.90f;
					item.vz *= 0.90f;
					if (item.vy < -2.5f) item.vy = -2.5f;
					if (flow) {
						const fluidFlow current = flow(item.x, item.y + ITEM_SIZE * 0.5f, item.z);
						item.vx += current.x * 1.25f * deltaTime;
						item.vy += current.y * 1.55f * deltaTime;
						item.vz += current.z * 1.25f * deltaTime;
					}
				}

				item.onGround = false;
				moveAxis(item, 0, item.vx * deltaTime, solidBoxes);
				moveAxis(item, 1, item.vy * deltaTime, solidBoxes);
				moveAxis(item, 2, item.vz * deltaTime, solidBoxes);

				if (item.onGround) {
					item.vx *= ITEM_FRICTION;
					item.vz *= ITEM_FRICTION;
					if (std::abs(item.vx) < 0.01f) item.vx = 0.0f;
					if (std::abs(item.vz) < 0.01f) item.vz = 0.0f;
				}
			}

			mergeNearby();
			despawnOld();
		}

		// Collects into hotbar via callback(itemId, count) -> amount taken.
		using pickupFn = std::function<uint32_t(blockId itemId, uint32_t count)>;

		void tryPickup(float playerX, float playerY, float playerZ, const pickupFn& take) {
			if (!take) return;
			for (size_t i = 0; i < _items.size();) {
				itemEntity& item = _items[i];
				if (item.pickupDelay > 0.0f || item.count == 0) {
					++i;
					continue;
				}
				const float dx = item.x - playerX;
				const float dy = (item.y + ITEM_SIZE * 0.5f) - playerY;
				const float dz = item.z - playerZ;
				const float distSq = dx * dx + dy * dy + dz * dz;
				if (distSq > ITEM_PICKUP_RANGE * ITEM_PICKUP_RANGE) {
					++i;
					continue;
				}
				const uint32_t taken = take(item.itemId, item.count);
				if (taken == 0) {
					++i;
					continue;
				}
				if (taken >= item.count) {
					_items[i] = _items.back();
					_items.pop_back();
					continue;
				}
				item.count -= taken;
				++i;
			}
		}

	private:
		std::vector<itemEntity> _items;
		uint64_t _nextId = 2;

		static blockAABB itemBounds(const itemEntity& item) {
			const float h = ITEM_SIZE * 0.5f;
			return {
				item.x - h, item.y, item.z - h,
				item.x + h, item.y + ITEM_SIZE, item.z + h
			};
		}

		void moveAxis(
			itemEntity& item,
			int axis,
			float delta,
			const solidBoxesQuery& solidBoxes
		) {
			if (std::abs(delta) < 1.0e-8f) return;
			const blockAABB before = itemBounds(item);
			blockAABB box = before;
			if (axis == 0) {
				box.minX += delta;
				box.maxX += delta;
			}
			else if (axis == 1) {
				box.minY += delta;
				box.maxY += delta;
			}
			else {
				box.minZ += delta;
				box.maxZ += delta;
			}

			const int32_t minBX = static_cast<int32_t>(std::floor(box.minX));
			const int32_t maxBX = static_cast<int32_t>(std::floor(box.maxX));
			const int32_t minBY = static_cast<int32_t>(std::floor(box.minY));
			const int32_t maxBY = static_cast<int32_t>(std::floor(box.maxY));
			const int32_t minBZ = static_cast<int32_t>(std::floor(box.minZ));
			const int32_t maxBZ = static_cast<int32_t>(std::floor(box.maxZ));

			bool hit = false;
			float resolved = delta;
			blockAABB solid[8];
			for (int32_t y = minBY; y <= maxBY; ++y) {
				for (int32_t z = minBZ; z <= maxBZ; ++z) {
					for (int32_t x = minBX; x <= maxBX; ++x) {
						const size_t count = solidBoxes(x, y, z, solid, 8);
						for (size_t i = 0; i < count; ++i) {
							blockAABB world = solid[i];
							world.minX += static_cast<float>(x);
							world.maxX += static_cast<float>(x);
							world.minY += static_cast<float>(y);
							world.maxY += static_cast<float>(y);
							world.minZ += static_cast<float>(z);
							world.maxZ += static_cast<float>(z);
							if (!aabbOverlap(box, world)) continue;
							hit = true;
							if (axis == 0) {
								if (delta > 0.0f)
									resolved = (std::min)(resolved, world.minX - before.maxX - 1.0e-4f);
								else
									resolved = (std::max)(resolved, world.maxX - before.minX + 1.0e-4f);
							}
							else if (axis == 1) {
								if (delta > 0.0f)
									resolved = (std::min)(resolved, world.minY - before.maxY - 1.0e-4f);
								else
									resolved = (std::max)(resolved, world.maxY - before.minY + 1.0e-4f);
							}
							else {
								if (delta > 0.0f)
									resolved = (std::min)(resolved, world.minZ - before.maxZ - 1.0e-4f);
								else
									resolved = (std::max)(resolved, world.maxZ - before.minZ + 1.0e-4f);
							}
						}
					}
				}
			}

			if (axis == 0) {
				item.x += resolved;
				if (hit) item.vx = -item.vx * ITEM_BOUNCE;
			}
			else if (axis == 1) {
				item.y += resolved;
				if (hit) {
					if (delta < 0.0f) {
						item.onGround = true;
						item.vy = std::abs(item.vy) > 1.5f ? -item.vy * ITEM_BOUNCE : 0.0f;
					}
					else {
						item.vy = 0.0f;
					}
				}
			}
			else {
				item.z += resolved;
				if (hit) item.vz = -item.vz * ITEM_BOUNCE;
			}
		}

		void mergeNearby() {
			for (size_t i = 0; i < _items.size(); ++i) {
				if (_items[i].count == 0 || _items[i].count >= ITEM_MAX_STACK) continue;
				for (size_t j = i + 1; j < _items.size();) {
					if (_items[j].itemId != _items[i].itemId ||
						_items[j].count == 0 ||
						_items[i].count >= ITEM_MAX_STACK) {
						++j;
						continue;
					}
					const float dx = _items[i].x - _items[j].x;
					const float dy = _items[i].y - _items[j].y;
					const float dz = _items[i].z - _items[j].z;
					if (dx * dx + dy * dy + dz * dz > ITEM_MERGE_RANGE * ITEM_MERGE_RANGE) {
						++j;
						continue;
					}
					const uint32_t space = ITEM_MAX_STACK - _items[i].count;
					const uint32_t move = (std::min)(space, _items[j].count);
					_items[i].count += move;
					_items[j].count -= move;
					_items[i].pickupDelay = (std::max)(_items[i].pickupDelay, _items[j].pickupDelay);
					_items[i].age = (std::max)(_items[i].age, _items[j].age);
					if (_items[j].count == 0) {
						_items[j] = _items.back();
						_items.pop_back();
						continue;
					}
					++j;
				}
			}
		}

		void despawnOld() {
			for (size_t i = 0; i < _items.size();) {
				if (_items[i].age >= ITEM_LIFETIME || _items[i].y < -64.0f || _items[i].count == 0) {
					_items[i] = _items.back();
					_items.pop_back();
				}
				else {
					++i;
				}
			}
		}
	};

}
