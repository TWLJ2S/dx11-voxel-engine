#define NOMINMAX
#include <world/waterPhysics.h>

#include <array>
#include <iostream>
#include <unordered_map>

namespace {
	struct coordinate {
		int32_t x, y, z;
		bool operator==(const coordinate& other) const {
			return x == other.x && y == other.y && z == other.z;
		}
	};

	struct coordinateHash {
		size_t operator()(const coordinate& value) const {
			return static_cast<size_t>((value.x + 16) * 1024 + value.y * 32 + value.z + 16);
		}
	};
}

int main() {
	std::unordered_map<coordinate, ac::blockId, coordinateHash> cells;
	auto read = [&](int32_t x, int32_t y, int32_t z) -> std::optional<ac::blockId> {
		if (x < -10 || x > 10 || y < 0 || y > 8 || z < -1 || z > 1)
			return ac::blockId{ 1u };
		const auto found = cells.find({ x, y, z });
		return found == cells.end() ? ac::blockId{ 1u } : found->second;
	};
	auto write = [&](int32_t x, int32_t y, int32_t z, ac::blockId state) {
		cells[{ x, y, z }] = state;
		return true;
	};

	// An unsupported source falls vertically and does not spread sideways in air.
	for (int32_t y = 1; y <= 5; ++y) cells[{ 0, y, 0 }] = 0u;
	for (int32_t x = 0; x <= 8; ++x) cells[{ x, 5, 0 }] = 0u;
	cells[{ 0, 5, 0 }] = ac::withFluidLevel(ac::WATER_BLOCK_TYPE, 8u);

	ac::waterPhysics physics;
	physics.notifyBlockChanged(0, 5, 0, read);
	for (uint32_t pass = 0; pass < 100u && physics.pending() != 0u; ++pass)
		physics.step(512u, read, write);

	if (ac::fluidLevel(*read(0, 1, 0)) != 7u ||
		ac::fluidLevel(*read(1, 5, 0)) != 0u) {
		std::cerr << "unsupported water did not flow exclusively downward\n";
		return 1;
	}

	cells[{ 0, 5, 0 }] = 0u;
	physics.notifyBlockChanged(0, 5, 0, read);
	for (uint32_t pass = 0; pass < 100u && physics.pending() != 0u; ++pass)
		physics.step(512u, read, write);
	for (const auto& [position, state] : cells) {
		if (ac::fluidLevel(state) != 0u) {
			std::cerr << "unsupported water did not drain\n";
			return 2;
		}
	}

	// Once supported by full blocks, water spreads horizontally by level.
	cells.clear();
	physics.clear();
	for (int32_t x = 0; x <= 8; ++x) cells[{ x, 3, 0 }] = 0u;
	cells[{ 0, 3, 0 }] = ac::withFluidLevel(ac::WATER_BLOCK_TYPE, 8u);
	physics.notifyBlockChanged(0, 3, 0, read);
	for (uint32_t pass = 0; pass < 100u && physics.pending() != 0u; ++pass)
		physics.step(512u, read, write);
	if (ac::fluidLevel(*read(1, 3, 0)) != 7u ||
		ac::fluidLevel(*read(7, 3, 0)) != 1u ||
		ac::fluidLevel(*read(8, 3, 0)) != 0u) {
		std::cerr << "supported water did not spread through all expected levels\n";
		return 7;
	}

	// Two adjacent sources over a supported gap create a permanent source.
	cells[{ -1, 3, 0 }] = ac::withFluidLevel(ac::WATER_BLOCK_TYPE, 8u);
	cells[{ 0, 3, 0 }] = 0u;
	cells[{ 1, 3, 0 }] = ac::withFluidLevel(ac::WATER_BLOCK_TYPE, 8u);
	physics.notifyBlockChanged(0, 3, 0, read);
	for (uint32_t pass = 0; pass < 20u && physics.pending() != 0u; ++pass)
		physics.step(128u, read, write);
	if (ac::fluidLevel(*read(0, 3, 0)) != ac::MAX_FLUID_LEVEL) {
		std::cerr << "two supported source neighbors did not refill their gap\n";
		return 3;
	}

	// A falling column preserves flow strength vertically without turning into
	// a source, and can still spread horizontally from the falling cell.
	cells.clear();
	physics.clear();
	cells[{ 0, 3, 0 }] = ac::withFluidLevel(ac::WATER_BLOCK_TYPE, ac::MAX_FLUID_LEVEL);
	cells[{ 0, 2, 0 }] = 0u;
	cells[{ 1, 2, 0 }] = 0u;
	cells[{ 2, 2, 0 }] = 0u;
	physics.notifyBlockChanged(0, 3, 0, read);
	physics.step(512u, read, write);
	if (ac::fluidLevel(*read(0, 2, 0)) != 7u || ac::fluidLevel(*read(1, 2, 0)) != 0u) {
		std::cerr << "water update cascaded beyond one wave or made falling water a source\n";
		return 4;
	}
	for (uint32_t pass = 0; pass < 20u && physics.pending() != 0u; ++pass)
		physics.step(512u, read, write);
	if (ac::fluidLevel(*read(0, 2, 0)) != 7u ||
		ac::fluidLevel(*read(1, 2, 0)) != 6u ||
		ac::fluidLevel(*read(2, 2, 0)) != 5u) {
		std::cerr << "falling non-source water did not spread horizontally\n";
		return 5;
	}

	// Player changes use the urgent lane and must not wait behind terrain water.
	cells.clear();
	physics.clear();
	for (int32_t x = -10; x <= -4; ++x)
		physics.enqueue(x, 6, 0);
	cells[{ 0, 3, 0 }] = ac::withFluidLevel(ac::WATER_BLOCK_TYPE, ac::MAX_FLUID_LEVEL);
	cells[{ 0, 2, 0 }] = 0u;
	physics.notifyBlockChanged(0, 3, 0, read, true);
	physics.step(7u, read, write);
	if (ac::fluidLevel(*read(0, 2, 0)) != 7u) {
		std::cerr << "urgent player water waited behind background updates\n";
		return 6;
	}

	std::array<ac::blockId, CHUNK_VOLUME> generated{};
	auto volumeIndex = [](int32_t x, int32_t y, int32_t z) {
		return x + CHUNK_WIDTH * (z + CHUNK_LENGTH * y);
	};
	generated[volumeIndex(4, 12, 4)] = ac::withFluidLevel(ac::WATER_BLOCK_TYPE, ac::MAX_FLUID_LEVEL);
	generated[volumeIndex(5, 12, 4)] = 0u;
	generated[volumeIndex(5, 11, 4)] = 0u;
	generated[volumeIndex(5, 13, 4)] = 0u;
	ac::waterPhysics::fillConnectedBasinWater(generated.data());
	if (ac::fluidLevel(generated[volumeIndex(5, 12, 4)]) != ac::MAX_FLUID_LEVEL ||
		ac::fluidLevel(generated[volumeIndex(5, 11, 4)]) != ac::MAX_FLUID_LEVEL ||
		ac::fluidLevel(generated[volumeIndex(5, 13, 4)]) != 0u) {
		std::cerr << "generated water did not fill connected air pockets\n";
		return 8;
	}

	std::cout << "water flow, paced waves, falling spread, drain, and source-regeneration checks passed\n";
	return 0;
}
