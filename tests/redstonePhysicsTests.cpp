#include <world/redstonePhysics.h>
#include <world/blockShape.h>

#include <cstdint>
#include <iostream>
#include <unordered_map>

namespace {
	struct position {
		int32_t x;
		int32_t y;
		int32_t z;
		bool operator==(const position&) const = default;
	};

	struct positionHash {
		size_t operator()(const position& value) const {
			size_t result = std::hash<int32_t>{}(value.x);
			result ^= std::hash<int32_t>{}(value.y) + 0x9e3779b9u + (result << 6u) + (result >> 2u);
			result ^= std::hash<int32_t>{}(value.z) + 0x9e3779b9u + (result << 6u) + (result >> 2u);
			return result;
		}
	};

	using blockMap = std::unordered_map<position, ac::blockId, positionHash>;

	ac::blockId floorTorch(bool lit = true) {
		return ac::withBlockPowered(
			ac::withWallAttached(ac::withFacing(ac::BLOCK_REDSTONE_TORCH, 0u), false),
			lit);
	}

	bool updateTorchAt(blockMap& blocks, int32_t x, int32_t y, int32_t z) {
		ac::redstonePhysics physics;
		physics.setConductorQuery([](ac::blockId state) {
			return ac::blockType(state) == 1u;
		});
		auto read = [&](int32_t x, int32_t y, int32_t z) -> std::optional<ac::blockId> {
			const auto found = blocks.find({ x, y, z });
			return found == blocks.end() ? std::optional<ac::blockId>(0u) : found->second;
		};
		auto write = [&](int32_t x, int32_t y, int32_t z, ac::blockId state) {
			blocks[{ x, y, z }] = state;
			return true;
		};
		physics.enqueue(x, y, z, true);
		for (int tick = 0; tick < 9; ++tick)
			physics.step(16u, read, write);
		return ac::isBlockPowered(blocks[{ x, y, z }]);
	}

	bool updateTorch(blockMap& blocks) {
		return updateTorchAt(blocks, 0, 1, 0);
	}
}

int main() {
	{
		const ac::blockAABB floor = ac::torchSelectionBox(ac::BLOCK_REDSTONE_TORCH);
		if (floor.minY != 0.0f || floor.maxY < 0.7f || floor.minX >= floor.maxX) {
			std::cerr << "floor redstone torch has no usable selection hitbox\n";
			return 1;
		}
		auto wallBox = [](uint32_t facing) {
			return ac::torchSelectionBox(ac::withWallAttached(
				ac::withFacing(ac::BLOCK_REDSTONE_TORCH, facing), true));
		};
		const ac::blockAABB north = wallBox(0u);
		const ac::blockAABB west = wallBox(1u);
		const ac::blockAABB south = wallBox(2u);
		const ac::blockAABB east = wallBox(3u);
		if (north.maxZ > 0.41f || west.maxX > 0.41f ||
			south.minZ < 0.59f || east.minX < 0.59f ||
			north.minX >= north.maxX || west.minZ >= west.maxZ) {
			std::cerr << "wall redstone torch hitbox does not follow its rendered model\n";
			return 1;
		}
	}

	{
		ac::staticAssetManager blocks;
		blocks.load("assets/block");
		const ac::blockDefinition* stone = blocks.get(1u);
		const ac::blockDefinition* redstone = blocks.get(ac::BLOCK_REDSTONE);
		const ac::blockDefinition* wire = blocks.get(ac::BLOCK_REDSTONE_WIRE);
		const ac::blockDefinition* source = blocks.get(ac::BLOCK_REDSTONE_BLOCK);
		const ac::blockDefinition* lamp = blocks.get(ac::BLOCK_REDSTONE_LAMP);
		if (!stone || !stone->_behavior.dropSpecified || stone->_behavior.drop != 14u ||
			!redstone || redstone->_behavior.placeAs != ac::BLOCK_REDSTONE_WIRE ||
			!wire || wire->_behavior.redstone != ac::blockRedstoneBehavior::wire ||
			wire->_behavior.drop != ac::BLOCK_REDSTONE || !source || !lamp) {
			std::cerr << "block behavior JSON did not load or resolve name references\n";
			return 1;
		}
		if (ac::redstoneDustConnectsTo(stone) ||
			!ac::redstoneDustConnectsTo(wire) ||
			!ac::redstoneDustConnectsTo(source) ||
			!ac::redstoneDustConnectsTo(lamp)) {
			std::cerr << "redstone dust was directed by a non-redstone block\n";
			return 1;
		}
	}

	{
		ac::redstonePhysics physics;
		auto read = [](int32_t x, int32_t y, int32_t z) -> std::optional<ac::blockId> {
			return (x == 0 && y == 4 && z == 0)
				? std::optional<ac::blockId>(1u)
				: std::optional<ac::blockId>(0u);
		};
		physics.notifyBlockChanged(0, 4, 0, 1u, 1u, read, true);
		if (physics.pending() != 0u) {
			std::cerr << "unchanged block sent neighbor update messages\n";
			return 1;
		}
		physics.notifyBlockChanged(0, 4, 0, 0u, 1u, read, true);
		if (physics.pending() != 13u) {
			std::cerr << "placement did not update itself and two blocks along all six directions\n";
			return 1;
		}
		physics.clear();
		physics.notifyBlockChanged(0, 4, 0, 1u, 0u, read, true);
		if (physics.pending() != 13u) {
			std::cerr << "breaking a block did not update itself and its two-block neighborhood\n";
			return 1;
		}
	}

	{
		const ac::blockId unpowered = ac::BLOCK_REDSTONE_WIRE;
		const ac::blockId powered = ac::withRedstonePower(unpowered, 15u);
		if (!ac::redstoneWireVisualStateChanged(unpowered, powered) ||
			ac::redstoneWireVisualStateChanged(powered, powered) ||
			ac::redstoneWireVisualStateChanged(ac::BLOCK_REDSTONE_LAMP,
				ac::withRedstonePower(ac::BLOCK_REDSTONE_LAMP, 15u))) {
			std::cerr << "redstone dust power changes were not classified as visual updates\n";
			return 1;
		}
	}

	{
		blockMap blocks;
		blocks[{ 0, 1, 0 }] = ac::BLOCK_REDSTONE_BLOCK;
		blocks[{ 1, 1, 0 }] = ac::BLOCK_REDSTONE_WIRE;
		ac::redstonePhysics physics;
		auto read = [&](int32_t x, int32_t y, int32_t z) -> std::optional<ac::blockId> {
			const auto found = blocks.find({ x, y, z });
			return found == blocks.end() ? std::optional<ac::blockId>(0u) : found->second;
		};
		auto write = [&](int32_t x, int32_t y, int32_t z, ac::blockId state) {
			blocks[{ x, y, z }] = state;
			return true;
		};
		physics.notifyBlockChanged(
			1, 1, 0, 0u, ac::BLOCK_REDSTONE_WIRE, read, true);
		physics.step(32u, read, write);
		if (ac::redstonePower(blocks[{ 1, 1, 0 }]) != 15u) {
			std::cerr << "freshly placed redstone dust did not initialize from its neighbor\n";
			return 1;
		}
	}

	{
		blockMap blocks;
		blocks[{ 0, 1, 0 }] = ac::BLOCK_REDSTONE_LAMP;
		blocks[{ 1, 1, 0 }] = ac::withRedstonePower(ac::BLOCK_REDSTONE_WIRE, 1u);
		ac::redstonePhysics physics;
		auto read = [&](int32_t x, int32_t y, int32_t z) -> std::optional<ac::blockId> {
			const auto found = blocks.find({ x, y, z });
			return found == blocks.end() ? std::optional<ac::blockId>(0u) : found->second;
		};
		auto write = [&](int32_t x, int32_t y, int32_t z, ac::blockId state) {
			blocks[{ x, y, z }] = state;
			return true;
		};
		if (physics.inputPowerAt(read, 0, 1, 0) != 1u) {
			std::cerr << "device did not detect adjacent strength-one dust\n";
			return 1;
		}
		physics.enqueue(0, 1, 0, true);
		physics.step(1u, read, write);
		if (!ac::isBlockPowered(blocks[{ 0, 1, 0 }])) {
			std::cerr << "strength-one dust did not power an adjacent lamp\n";
			return 1;
		}

		blocks[{ 0, 1, 0 }] = ac::BLOCK_REDSTONE_WIRE;
		physics.clear();
		physics.enqueue(0, 1, 0, true);
		physics.step(1u, read, write);
		if (ac::redstonePower(blocks[{ 0, 1, 0 }]) != 0u) {
			std::cerr << "strength-one dust did not attenuate between wire segments\n";
			return 1;
		}
	}

	{
		blockMap blocks;
		blocks[{ 0, 0, 0 }] = ac::BLOCK_REDSTONE_BLOCK;
		for (int32_t x = 1; x <= 15; ++x)
			blocks[{ x, 0, 0 }] = ac::BLOCK_REDSTONE_WIRE;
		ac::redstonePhysics physics;
		auto read = [&](int32_t x, int32_t y, int32_t z) -> std::optional<ac::blockId> {
			const auto found = blocks.find({ x, y, z });
			return found == blocks.end() ? std::optional<ac::blockId>(0u) : found->second;
		};
		auto write = [&](int32_t x, int32_t y, int32_t z, ac::blockId state) {
			blocks[{ x, y, z }] = state;
			return true;
		};
		physics.notifyBlockChanged(
			0, 0, 0, 0u, ac::BLOCK_REDSTONE_BLOCK, read, true);
		physics.step(65536u, read, write);
		for (int32_t x = 1; x <= 15; ++x) {
			const uint8_t expected = static_cast<uint8_t>(16 - x);
			if (ac::redstonePower(blocks[{ x, 0, 0 }]) != expected) {
				std::cerr << "a redstone line did not settle consistently in one game tick\n";
				return 1;
			}
		}
	}

	{
		blockMap blocks;
		blocks[{ 0, 0, 0 }] = ac::withFacing(
			ac::withRedstonePower(ac::BLOCK_REDSTONE_WIRE, 15u), 0u);
		ac::redstonePhysics physics;
		auto read = [&](int32_t x, int32_t y, int32_t z) -> std::optional<ac::blockId> {
			const auto found = blocks.find({ x, y, z });
			return found == blocks.end() ? std::optional<ac::blockId>(0u) : found->second;
		};
		if (physics.inputPowerAt(read, 0, 0, 1) != 15u ||
			physics.inputPowerAt(read, 0, 0, -1) != 0u ||
			physics.inputPowerAt(read, 1, 0, 0) != 0u) {
			std::cerr << "undirected dust did not power only its stored forward direction\n";
			return 1;
		}
		blocks[{ 0, 0, -1 }] = ac::BLOCK_REDSTONE_LAMP;
		if (physics.inputPowerAt(read, 0, 0, -1) != 15u ||
			physics.inputPowerAt(read, 0, 0, 1) != 0u) {
			std::cerr << "redstone component did not override the dust default direction\n";
			return 1;
		}
		blocks[{ 1, 0, 0 }] = ac::BLOCK_REDSTONE_LAMP;
		if (physics.inputPowerAt(read, 0, 0, -1) != 15u ||
			physics.inputPowerAt(read, 1, 0, 0) != 15u ||
			physics.inputPowerAt(read, 0, 0, 1) != 0u) {
			std::cerr << "dust did not restrict power to its component-directed arms\n";
			return 1;
		}
	}

	{
		for (uint32_t facing = 0; facing < 4u; ++facing) {
			blockMap blocks;
			blocks[{ -1, 0, 0 }] = ac::BLOCK_REDSTONE_BLOCK;
			blocks[{ 0, 0, 0 }] = ac::withFacing(ac::BLOCK_REDSTONE_WIRE, facing);
			ac::redstonePhysics physics;
			auto read = [&](int32_t x, int32_t y, int32_t z) -> std::optional<ac::blockId> {
				const auto found = blocks.find({ x, y, z });
				return found == blocks.end() ? std::optional<ac::blockId>(0u) : found->second;
			};
			auto write = [&](int32_t x, int32_t y, int32_t z, ac::blockId state) {
				blocks[{ x, y, z }] = state;
				return true;
			};
			physics.enqueue(0, 0, 0, true);
			physics.step(8u, read, write);
			const ac::blockId updated = blocks[{ 0, 0, 0 }];
			if (ac::redstonePower(updated) != 15u || ac::blockFacing(updated) != facing) {
				std::cerr << "redstone power update erased the dust placement direction\n";
				return 1;
			}
		}
	}

	{
		auto conductor = [](ac::blockId state) {
			return ac::blockType(state) == 1u;
		};
		blockMap blocks;
		blocks[{ 0, 0, 0 }] = ac::withRedstonePower(ac::BLOCK_REDSTONE_WIRE, 15u);
		blocks[{ 1, 0, 0 }] = 1u;
		blocks[{ 1, 1, 0 }] = ac::BLOCK_REDSTONE_WIRE;
		ac::redstonePhysics physics;
		physics.setConductorQuery(conductor);
		auto read = [&](int32_t x, int32_t y, int32_t z) -> std::optional<ac::blockId> {
			const auto found = blocks.find({ x, y, z });
			return found == blocks.end() ? std::optional<ac::blockId>(0u) : found->second;
		};
		auto write = [&](int32_t x, int32_t y, int32_t z, ac::blockId state) {
			blocks[{ x, y, z }] = state;
			return true;
		};
		physics.enqueue(1, 1, 0, true);
		physics.step(16u, read, write);
		if (ac::redstonePower(blocks[{ 1, 1, 0 }]) != 14u) {
			std::cerr << "redstone dust did not climb a full block\n";
			return 1;
		}

		// A stepped component is a valid electrical connection, but it must not
		// redirect the lower dust because it is not on the same Y level.
		blocks.clear();
		blocks[{ 0, 0, 0 }] = ac::withFacing(
			ac::withRedstonePower(ac::BLOCK_REDSTONE_WIRE, 15u), 2u);
		blocks[{ 1, 0, 0 }] = 1u;
		blocks[{ 1, 1, 0 }] = ac::BLOCK_REDSTONE_WIRE;
		physics.clear();
		if (physics.inputPowerAt(read, 1, 0, 0) != 0u) {
			std::cerr << "component above the dust redirected its horizontal output\n";
			return 1;
		}
		blocks[{ 1, 0, 0 }] = ac::BLOCK_REDSTONE_LAMP;
		if (physics.inputPowerAt(read, 1, 0, 0) != 15u) {
			std::cerr << "same-level component did not redirect redstone dust\n";
			return 1;
		}

		// Dust strongly powers its supporting block, which may then power a
		// device attached to another face of that block.
		blocks.clear();
		blocks[{ 0, 1, 0 }] = ac::withRedstonePower(ac::BLOCK_REDSTONE_WIRE, 15u);
		blocks[{ 0, 0, 0 }] = 1u;
		blocks[{ 1, 0, 0 }] = ac::BLOCK_REDSTONE_LAMP;
		physics.clear();
		if (physics.inputPowerAt(read, 0, 0, 0) != 15u ||
			physics.inputPowerAt(read, 1, 0, 0) != 15u) {
			std::cerr << "redstone dust did not power and conduct through its supporting block\n";
			return 1;
		}

		blocks.clear();
		blocks[{ 0, 0, 0 }] = ac::withRedstonePower(ac::BLOCK_REDSTONE_WIRE, 15u);
		blocks[{ 1, 0, 0 }] = 1u;
		blocks[{ 0, 1, 0 }] = 1u;
		blocks[{ 1, 1, 0 }] = ac::BLOCK_REDSTONE_WIRE;
		physics.clear();
		physics.enqueue(1, 1, 0, true);
		physics.step(16u, read, write);
		if (ac::redstonePower(blocks[{ 1, 1, 0 }]) != 0u) {
			std::cerr << "full block above lower dust did not block its upward path\n";
			return 1;
		}

		blocks.clear();
		blocks[{ -1, 0, 0 }] = ac::withFacing(
			ac::withRedstonePower(ac::BLOCK_REDSTONE_WIRE, 15u), 1u);
		blocks[{ 0, 0, 0 }] = 1u;
		blocks[{ 1, 0, 0 }] = ac::BLOCK_REDSTONE_LAMP;
		physics.clear();
		if (physics.inputPowerAt(read, 1, 0, 0) != 0u) {
			std::cerr << "full block relayed dust power through to its opposite face\n";
			return 1;
		}
	}

	{
		blockMap blocks;
		blocks[{ 0, 1, 0 }] = floorTorch();
		blocks[{ 0, 0, 0 }] = ac::BLOCK_REDSTONE_BLOCK;
		ac::redstonePhysics physics;
		auto read = [&](int32_t x, int32_t y, int32_t z) -> std::optional<ac::blockId> {
			const auto found = blocks.find({ x, y, z });
			return found == blocks.end() ? std::optional<ac::blockId>(0u) : found->second;
		};
		auto write = [&](int32_t x, int32_t y, int32_t z, ac::blockId state) {
			blocks[{ x, y, z }] = state;
			return true;
		};
		physics.enqueue(0, 1, 0, true);
		physics.step(16u, read, write, false);
		if (!ac::isBlockPowered(blocks[{ 0, 1, 0 }])) {
			std::cerr << "redstone torch switched off without its eight-tick delay\n";
			return 1;
		}
		physics.step(16u, read, write, false);
		if (!ac::isBlockPowered(blocks[{ 0, 1, 0 }])) {
			std::cerr << "non-tick redstone work advanced the torch delay\n";
			return 1;
		}
		for (int tick = 1; tick < 8; ++tick) {
			physics.step(16u, read, write);
			if (!ac::isBlockPowered(blocks[{ 0, 1, 0 }])) {
				std::cerr << "redstone torch switched off before eight ticks elapsed\n";
				return 1;
			}
		}
		physics.step(16u, read, write);
		if (ac::isBlockPowered(blocks[{ 0, 1, 0 }])) {
			std::cerr << "redstone torch did not switch off after eight ticks\n";
			return 1;
		}

		blocks[{ 0, 0, 0 }] = 1u;
		physics.notifyBlockChanged(
			0, 0, 0, ac::BLOCK_REDSTONE_BLOCK, 1u, read, true);
		physics.step(64u, read, write, false);
		for (int tick = 1; tick < 8; ++tick) {
			physics.step(64u, read, write);
			if (ac::isBlockPowered(blocks[{ 0, 1, 0 }])) {
				std::cerr << "redstone torch switched on before eight ticks elapsed\n";
				return 1;
			}
		}
		physics.step(64u, read, write);
		if (!ac::isBlockPowered(blocks[{ 0, 1, 0 }])) {
			std::cerr << "redstone torch did not switch on after eight ticks\n";
			return 1;
		}
	}

	{
		// A source behind a second solid block must not propagate through that
		// block and make the torch's own support appear powered.
		blockMap blocks;
		blocks[{ 0, 1, 0 }] = floorTorch();
		blocks[{ 0, 0, 0 }] = 1u;
		blocks[{ 1, 0, 0 }] = 1u;
		blocks[{ 2, 0, 0 }] = ac::BLOCK_REDSTONE_BLOCK;
		if (!updateTorch(blocks)) {
			std::cerr << "redstone torch read power through a block beside its support\n";
			return 1;
		}
	}

	{
		// Stored facing points away from the supporting wall. Verify all four
		// directions use that block, rather than a generic neighbor of the torch.
		static constexpr int32_t supportOffsets[4][2] = {
			{ 0, -1 }, { -1, 0 }, { 0, 1 }, { 1, 0 }
		};
		for (uint32_t facing = 0; facing < 4u; ++facing) {
			const int32_t sx = supportOffsets[facing][0];
			const int32_t sz = supportOffsets[facing][1];
			blockMap blocks;
			blocks[{ 0, 1, 0 }] = ac::withBlockPowered(
				ac::withWallAttached(
					ac::withFacing(ac::BLOCK_REDSTONE_TORCH, facing), true), true);
			blocks[{ sx, 1, sz }] = 1u;
			blocks[{ sx * 2, 1, sz * 2 }] = ac::BLOCK_REDSTONE_BLOCK;
			if (updateTorchAt(blocks, 0, 1, 0)) {
				std::cerr << "wall redstone torch ignored its charged supporting block\n";
				return 1;
			}

			blocks.erase({ sx * 2, 1, sz * 2 });
			blocks[{ -sz, 1, sx }] = ac::BLOCK_REDSTONE_BLOCK;
			if (!updateTorchAt(blocks, 0, 1, 0)) {
				std::cerr << "power beside wall torch bypassed its supporting block\n";
				return 1;
			}
		}
	}

	{
		blockMap blocks;
		blocks[{ 0, 1, 0 }] = floorTorch();
		blocks[{ 0, 0, 0 }] = 1u;
		blocks[{ 1, 1, 0 }] = ac::BLOCK_REDSTONE_BLOCK;
		if (!updateTorch(blocks)) {
			std::cerr << "power beside torch incorrectly bypassed its support block\n";
			return 1;
		}
	}

	{
		blockMap blocks;
		blocks[{ 0, 1, 0 }] = floorTorch();
		blocks[{ 0, 0, 0 }] = 1u;
		blocks[{ 1, 0, 0 }] = ac::withBlockPowered(ac::BLOCK_LEVER, true);
		if (updateTorch(blocks)) {
			std::cerr << "powered support block did not turn torch off\n";
			return 1;
		}
	}

	{
		blockMap blocks;
		blocks[{ 0, 1, 0 }] = floorTorch(false);
		blocks[{ 0, 0, 0 }] = 1u;
		if (!updateTorch(blocks)) {
			std::cerr << "unpowered torch did not turn back on\n";
			return 1;
		}
	}

	{
		blockMap blocks;
		blocks[{ 0, 1, 0 }] = floorTorch();
		blocks[{ 0, 0, 0 }] = 1u;
		blocks[{ 1, 2, 0 }] = ac::BLOCK_REDSTONE_BLOCK;
		if (!updateTorch(blocks)) {
			std::cerr << "diagonal power incorrectly affected torch\n";
			return 1;
		}
	}

	{
		constexpr ac::blockId customSource = 900u;
		constexpr ac::blockId customWire = 901u;
		blockMap blocks;
		blocks[{ 0, 0, 0 }] = customSource;
		blocks[{ 1, 0, 0 }] = customWire;

		ac::redstonePhysics physics;
		physics.setBehaviorQuery([](ac::blockId state) {
			if (ac::blockType(state) == customSource) return ac::blockRedstoneBehavior::source;
			if (ac::blockType(state) == customWire) return ac::blockRedstoneBehavior::wire;
			return ac::blockRedstoneBehavior::none;
		});
		physics.setOutputQuery([](ac::blockId state) {
			return ac::blockType(state) == customSource ? uint8_t{ 15 } : uint8_t{ 0 };
		});
		auto read = [&](int32_t x, int32_t y, int32_t z) -> std::optional<ac::blockId> {
			const auto found = blocks.find({ x, y, z });
			return found == blocks.end() ? std::optional<ac::blockId>(0u) : found->second;
		};
		auto write = [&](int32_t x, int32_t y, int32_t z, ac::blockId state) {
			blocks[{ x, y, z }] = state;
			return true;
		};
		physics.enqueue(1, 0, 0, true);
		physics.step(8u, read, write);
		if (ac::redstonePower(blocks[{ 1, 0, 0 }]) != 15u) {
			std::cerr << "data-driven source and wire roles did not propagate power\n";
			return 1;
		}
	}

	std::cout << "state-change propagation, block behavior, and redstone checks passed\n";
	return 0;
}
