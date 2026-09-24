#pragma once

#include <assets/cpuAsset.h>
#include <world/chunk.h>
#include <core/playerController.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ac {

	struct blockAABB {
		float minX = 0.0f;
		float minY = 0.0f;
		float minZ = 0.0f;
		float maxX = 1.0f;
		float maxY = 1.0f;
		float maxZ = 1.0f;
	};

	inline bool aabbOverlap(const blockAABB& a, const blockAABB& b) {
		return a.minX < b.maxX && a.maxX > b.minX &&
			a.minY < b.maxY && a.maxY > b.minY &&
			a.minZ < b.maxZ && a.maxZ > b.minZ;
	}

	inline blockAABB rotateAabbY(const blockAABB& box, uint32_t turns) {
		blockAABB result = box;
		for (uint32_t t = 0; t < (turns & 3u); ++t) {
			const float minX = result.minZ;
			const float maxX = result.maxZ;
			const float minZ = 1.0f - result.maxX;
			const float maxZ = 1.0f - result.minX;
			result.minX = std::min(minX, maxX);
			result.maxX = std::max(minX, maxX);
			result.minZ = std::min(minZ, maxZ);
			result.maxZ = std::max(minZ, maxZ);
		}
		return result;
	}

	inline blockAABB leverSelectionBox(blockId state) {
		blockAABB box{ 0.3125f, 0.0f, 0.0138f, 0.6875f, 0.5487f, 0.75f };
		if (isBlockPowered(state)) { box.minZ = 0.25f; box.maxZ = 0.9862f; }
		if (isWallAttached(state))
			box = { box.minX, 1.0f - box.maxZ, box.minY,
				box.maxX, 1.0f - box.minZ, box.maxY };
		return rotateAabbY(box, blockFacing(state));
	}

	inline bool rayAabb(
		float ox, float oy, float oz,
		float dx, float dy, float dz,
		const blockAABB& box,
		float& outT
	) {
		float tMin = 0.0f;
		float tMax = 1.0e30f;
		const float origin[3] = { ox, oy, oz };
		const float direction[3] = { dx, dy, dz };
		const float bMin[3] = { box.minX, box.minY, box.minZ };
		const float bMax[3] = { box.maxX, box.maxY, box.maxZ };
		for (int axis = 0; axis < 3; ++axis) {
			if (std::abs(direction[axis]) < 1.0e-8f) {
				if (origin[axis] < bMin[axis] || origin[axis] > bMax[axis])
					return false;
				continue;
			}
			float inv = 1.0f / direction[axis];
			float t0 = (bMin[axis] - origin[axis]) * inv;
			float t1 = (bMax[axis] - origin[axis]) * inv;
			if (t0 > t1) std::swap(t0, t1);
			tMin = std::max(tMin, t0);
			tMax = std::min(tMax, t1);
			if (tMin > tMax) return false;
		}
		outT = tMin;
		return tMax >= 0.0f;
	}

	inline size_t collisionBoxesForStairs(
		uint32_t facing,
		stairShape shape,
		blockAABB* out,
		size_t capacity
	) {
		if (!out || capacity == 0) return 0;
		size_t count = 0;
		auto push = [&](blockAABB box) {
			if (count >= capacity) return;
			out[count++] = box;
		};

		const uint32_t turns = facingToYawTurns(facing);
		push(rotateAabbY({ 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 1.0f }, turns));

		const blockAABB southUpper{ 0.0f, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f };
		const blockAABB southEast{ 0.5f, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f };
		const blockAABB northEast{ 0.5f, 0.5f, 0.0f, 1.0f, 1.0f, 0.5f };
		const blockAABB northWest{ 0.0f, 0.5f, 0.0f, 0.5f, 1.0f, 0.5f };

		switch (shape) {
		case stairShape::straight:
			push(rotateAabbY(southUpper, turns));
			break;
		case stairShape::outerLeft:
			push(rotateAabbY(southEast, turns));
			break;
		case stairShape::outerRight:
			push(rotateAabbY(southEast, turns + 3u));
			break;
		case stairShape::innerLeft:
			push(rotateAabbY(southUpper, turns));
			push(rotateAabbY(northEast, turns));
			break;
		case stairShape::innerRight:
			push(rotateAabbY(southUpper, turns));
			push(rotateAabbY(northWest, turns));
			break;
		}
		return count;
	}

	// Matches connected fence visuals: center post plus a rail AABB per link.
	// Height is 1.5 so players cannot jump over (Minecraft fence collision).
	inline size_t collisionBoxesForFence(
		const fenceConnections& links,
		blockAABB* out,
		size_t capacity
	) {
		if (!out || capacity == 0) return 0;
		size_t count = 0;
		auto push = [&](blockAABB box) {
			if (count >= capacity) return;
			out[count++] = box;
		};

		constexpr float p0 = 6.0f / 16.0f;
		constexpr float p1 = 10.0f / 16.0f;
		constexpr float height = 1.5f;
		push({ p0, 0.0f, p0, p1, height, p1 });
		if (links.north)
			push({ p0, 0.0f, 0.0f, p1, height, p1 });
		if (links.south)
			push({ p0, 0.0f, p0, p1, height, 1.0f });
		if (links.west)
			push({ 0.0f, 0.0f, p0, p1, height, p1 });
		if (links.east)
			push({ p0, 0.0f, p0, 1.0f, height, p1 });
		return count;
	}

	inline size_t collisionBoxesForPane(
		const fenceConnections& links,
		blockAABB* out,
		size_t capacity
	) {
		if (!out || capacity == 0) return 0;
		size_t count = 0;
		auto push = [&](blockAABB box) {
			if (count >= capacity) return;
			out[count++] = box;
		};

		constexpr float p0 = 7.0f / 16.0f;
		constexpr float p1 = 9.0f / 16.0f;
		push({ p0, 0.0f, p0, p1, 1.0f, p1 });
		if (links.north)
			push({ p0, 0.0f, 0.0f, p1, 1.0f, p1 });
		if (links.south)
			push({ p0, 0.0f, p0, p1, 1.0f, 1.0f });
		if (links.west)
			push({ 0.0f, 0.0f, p0, p1, 1.0f, p1 });
		if (links.east)
			push({ p0, 0.0f, p0, 1.0f, 1.0f, p1 });
		return count;
	}

	inline size_t collisionBoxesForModel(
		uint32_t modelId,
		uint32_t facing,
		blockAABB* out,
		size_t capacity,
		stairShape stairs = stairShape::straight,
		fenceConnections fence = {}
	) {
		if (!out || capacity == 0) return 0;
		size_t count = 0;
		auto push = [&](blockAABB box) {
			if (count >= capacity) return;
			out[count++] = box;
		};

		switch (modelId) {
		case MODEL_SLAB:
			push({ 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 1.0f });
			break;
		case MODEL_STAIRS:
			return collisionBoxesForStairs(facing, stairs, out, capacity);
		case MODEL_FENCE:
			return collisionBoxesForFence(fence, out, capacity);
		case MODEL_PANE:
			return collisionBoxesForPane(fence, out, capacity);
		case MODEL_DOOR: {
			// Closed mesh is west slab (x=0..thickness); mesher uses facing+1 yaw.
			const uint32_t turns = (facingToYawTurns(facing) + 1u) & 3u;
			push(rotateAabbY({ 0.0f, 0.0f, 0.0f, DOOR_THICKNESS, 1.0f, 1.0f }, turns));
			break;
		}
		case MODEL_DOOR_OPEN: {
			// Open mesh is north slab (z=0..thickness) = closed west swung -90° with
			// the same placement yaw as the closed door (hinge at the shared edge).
			const uint32_t turns = (facingToYawTurns(facing) + 1u) & 3u;
			push(rotateAabbY({ 0.0f, 0.0f, 0.0f, DOOR_THICKNESS, 1.0f, 1.0f }, (turns + 3u) & 3u));
			break;
		}
		case MODEL_TORCH:
			// Torches have no player collision (Minecraft behavior).
			return 0;
		case MODEL_CROSS:
			return 0;
		case MODEL_THIN:
			push({ 0.0f, 0.0f, 0.0f, 1.0f, THIN_HEIGHT, 1.0f });
			break;
		case MODEL_CHEST:
			push({ 0.03125f, 0.0f, 0.03125f, 0.96875f, 0.875f, 0.96875f });
			break;
		case MODEL_CAKE:
			push({ 0.0625f, 0.0f, 0.0625f, 0.9375f, 0.5f, 0.9375f });
			break;
		case MODEL_ENCHANTING_TABLE:
			push({ 0.0f, 0.0f, 0.0f, 1.0f, 0.875f, 1.0f });
			break;
		case MODEL_CAULDRON:
			push({ 0.0f, 0.0f, 0.0f, 1.0f, 0.3125f, 1.0f });
			push({ 0.0f, 0.3125f, 0.0f, 0.1875f, 1.0f, 1.0f });
			push({ 0.8125f, 0.3125f, 0.0f, 1.0f, 1.0f, 1.0f });
			push({ 0.1875f, 0.3125f, 0.0f, 0.8125f, 1.0f, 0.1875f });
			push({ 0.1875f, 0.3125f, 0.8125f, 0.8125f, 1.0f, 1.0f });
			break;
		case MODEL_ANVIL:
			push({ 0.125f, 0.0f, 0.0625f, 0.875f, 0.25f, 0.9375f });
			push({ 0.3125f, 0.25f, 0.25f, 0.6875f, 0.6875f, 0.75f });
			push({ 0.0f, 0.6875f, 0.125f, 1.0f, 1.0f, 0.875f });
			break;
		case MODEL_TRAPDOOR:
			push({ 0.0f, 0.0f, 0.0f, 1.0f, 0.1875f, 1.0f });
			break;
		case MODEL_TRAPDOOR_OPEN:
			push(rotateAabbY(
				{ 0.0f, 0.0f, 0.8125f, 1.0f, 1.0f, 1.0f },
				facingToYawTurns(facing)));
			break;
		case MODEL_ROD:
			push({ 0.375f, 0.0f, 0.375f, 0.625f, 1.0f, 0.625f });
			break;
		case MODEL_EGG:
			push({ 0.25f, 0.0f, 0.25f, 0.75f, 0.75f, 0.75f });
			break;
		case MODEL_LADDER:
		case MODEL_LEVER:
		case MODEL_BELL:
			return 0;
		case MODEL_CAMPFIRE:
			push({ 0.0625f, 0.0f, 0.0625f, 0.9375f, 0.4375f, 0.9375f });
			break;
		case MODEL_FENCE_GATE:
			push(rotateAabbY(
				{ 0.0f, 0.0f, 0.375f, 1.0f, 1.5f, 0.625f },
				facingToYawTurns(facing)));
			break;
		case MODEL_FENCE_GATE_OPEN:
			push(rotateAabbY(
				{ 0.0f, 0.0f, 0.0f, 0.1875f, 1.5f, 1.0f },
				facingToYawTurns(facing)));
			push(rotateAabbY(
				{ 0.8125f, 0.0f, 0.0f, 1.0f, 1.5f, 1.0f },
				facingToYawTurns(facing)));
			break;
		case MODEL_LECTERN:
			push({ 0.0625f, 0.0f, 0.0625f, 0.9375f, 0.875f, 0.9375f });
			break;
		case MODEL_GRINDSTONE:
			push({ 0.125f, 0.0f, 0.125f, 0.875f, 1.0f, 0.875f });
			break;
		case MODEL_STONECUTTER:
			push({ 0.0f, 0.0f, 0.0f, 1.0f, 0.5625f, 1.0f });
			break;
		case MODEL_SENSOR:
			push({ 0.0625f, 0.0f, 0.0625f, 0.9375f, 0.5f, 0.9375f });
			break;
		case MODEL_SCAFFOLDING:
			push({ 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f });
			break;
		case MODEL_SHELF:
			push({ 0.0f, 0.0f, 0.125f, 1.0f, 1.0f, 0.875f });
			break;
		case MODEL_SHULKER_BOX:
			push({ 0.0625f, 0.0f, 0.0625f, 0.9375f, 0.9375f, 0.9375f });
			break;
		case MODEL_FLOWER_POT:
			push({ 0.25f, 0.0f, 0.25f, 0.75f, 0.625f, 0.75f });
			break;
		default:
			push({ 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f });
			break;
		}
		return count;
	}

	// Targeting outline for a torch. Wall torches use the exact yaw convention
	// used by the mesher, so the selectable volume follows the visible slanted
	// stem instead of appearing on the opposite side of the cell.
	inline blockAABB torchSelectionBox(blockId state) {
		if (!isWallAttached(state))
			return { 0.35f, 0.0f, 0.35f, 0.65f, 0.75f, 0.65f };
		return rotateAabbY(
			{ 0.0f, 0.20f, 0.35f, 0.40f, 0.90f, 0.65f },
			wallTorchYawTurns(blockFacing(state)));
	}

	inline size_t collisionBoxesFor(
		const blockDefinition* definition,
		blockId state,
		blockAABB* out,
		size_t capacity,
		stairShape stairs = stairShape::straight,
		fenceConnections fence = {}
	) {
		const bool door = definition && definition->_interaction == blockInteraction::door;
		if (!definition || (!definition->_solid && !door) || !out || capacity == 0)
			return 0;
		if (definition->_model == MODEL_CUBE || !isDetailModel(definition->_model)) {
			out[0] = { 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f };
			return 1;
		}
		uint32_t model = definition->_model;
		if (model == MODEL_DOOR || model == MODEL_DOOR_OPEN)
			model = isBlockOpen(state) ? MODEL_DOOR_OPEN : MODEL_DOOR;
		else if (model == MODEL_TRAPDOOR)
			model = isBlockOpen(state) ? MODEL_TRAPDOOR_OPEN : MODEL_TRAPDOOR;
		else if (model == MODEL_FENCE_GATE)
			return isBlockOpen(state)
				? 0
				: collisionBoxesForModel(MODEL_FENCE_GATE, blockFacing(state), out, capacity, stairs, fence);
		return collisionBoxesForModel(
			model, blockFacing(state), out, capacity, stairs, fence);
	}

	inline size_t collisionBoxesFor(
		const blockDefinition* definition,
		blockAABB* out,
		size_t capacity
	) {
		return collisionBoxesFor(
			definition, definition ? definition->_id : 0u, out, capacity);
	}

	inline float maxCollisionHeight(
		const blockDefinition* definition,
		blockId state = 0
	) {
		blockAABB boxes[8];
		const size_t count = collisionBoxesFor(definition, state, boxes, 8);
		float height = 0.0f;
		for (size_t i = 0; i < count; ++i)
			height = std::max(height, boxes[i].maxY);
		return height;
	}

	struct playerCollisionBox {
		float radius = 0.0f;
		float feetBelowEye = 0.0f;
		float headAboveEye = 0.0f;
	};

	inline playerCollisionBox standingPlayerBox() {
		const auto& value = playerController().collision.standing;
		return { value.radius, value.feetBelowEye, value.headAboveEye };
	}

	inline playerCollisionBox crouchingPlayerBox() {
		const auto& value = playerController().collision.crouching;
		return { value.radius, value.feetBelowEye, value.headAboveEye };
	}

	inline playerCollisionBox swimmingPlayerBox() {
		const auto& value = playerController().collision.swimming;
		return { value.radius, value.feetBelowEye, value.headAboveEye };
	}

	inline playerCollisionBox posePlayerBox(bool swimming, bool crouching) {
		if (swimming) return swimmingPlayerBox();
		if (crouching) return crouchingPlayerBox();
		return standingPlayerBox();
	}

	inline playerCollisionBox blendedPlayerBox(float swimBlend, bool crouching) {
		const float t = std::clamp(swimBlend, 0.0f, 1.0f);
		const playerCollisionBox stand = crouching ? crouchingPlayerBox() : standingPlayerBox();
		if (t <= 0.0f) return stand;
		const playerCollisionBox swim = swimmingPlayerBox();
		return {
			stand.radius + (swim.radius - stand.radius) * t,
			stand.feetBelowEye + (swim.feetBelowEye - stand.feetBelowEye) * t,
			stand.headAboveEye + (swim.headAboveEye - stand.headAboveEye) * t
		};
	}

	inline bool playerOverlapsBlock(
		float eyeX, float eyeY, float eyeZ,
		int32_t blockX, int32_t blockY, int32_t blockZ,
		const blockDefinition* definition,
		blockId state = 0,
		float epsilon = 0.002f,
		stairShape stairs = stairShape::straight,
		fenceConnections fence = {},
		playerCollisionBox pose = standingPlayerBox()
	) {
		if (!definition || !definition->_solid) return false;
		blockAABB player{
			eyeX - pose.radius + epsilon,
			eyeY - pose.feetBelowEye + epsilon,
			eyeZ - pose.radius + epsilon,
			eyeX + pose.radius - epsilon,
			eyeY + pose.headAboveEye - epsilon,
			eyeZ + pose.radius - epsilon
		};
		blockAABB local[8];
		const size_t count = collisionBoxesFor(
			definition, state ? state : definition->_id, local, 8, stairs, fence);
		const float ox = static_cast<float>(blockX);
		const float oy = static_cast<float>(blockY);
		const float oz = static_cast<float>(blockZ);
		for (size_t i = 0; i < count; ++i) {
			blockAABB world = local[i];
			world.minX += ox; world.maxX += ox;
			world.minY += oy; world.maxY += oy;
			world.minZ += oz; world.maxZ += oz;
			if (aabbOverlap(player, world)) return true;
		}
		return false;
	}

	inline bool eyeCollidesWithCell(
		float eyeX, float eyeY, float eyeZ,
		int32_t blockX, int32_t blockY, int32_t blockZ,
		const blockDefinition* definition,
		blockId state = 0,
		stairShape stairs = stairShape::straight,
		fenceConnections fence = {},
		playerCollisionBox pose = standingPlayerBox()
	) {
		return playerOverlapsBlock(
			eyeX, eyeY, eyeZ, blockX, blockY, blockZ, definition, state, 0.001f, stairs, fence, pose);
	}

}
