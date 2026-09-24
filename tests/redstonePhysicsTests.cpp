#include <world/redstonePhysics.h>
#include <world/blockShape.h>
#include <world/blockEntities.h>

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
	struct circuit {
		blockMap blocks;
		ac::redstonePhysics physics;
		ac::redstonePhysics::blockReader read = [this](int32_t x, int32_t y, int32_t z) -> std::optional<ac::blockId> {
			const auto found = blocks.find({x,y,z});
			return found == blocks.end() ? 0u : found->second;
		};
		ac::redstonePhysics::blockWriter write = [this](int32_t x, int32_t y, int32_t z, ac::blockId state) {
			blocks[{x,y,z}] = state; return true;
		};
		void change(position p, ac::blockId state) {
			const auto before = blocks[p]; blocks[p] = state;
			physics.notifyBlockChanged(p.x,p.y,p.z,before,state,read,true);
			physics.step(65536,read,write,false);
		}
		void ticks(uint32_t count) { while (count--) physics.step(65536,read,write); }
	};

	void check(bool condition, const char* message) {
		if (!condition) throw std::runtime_error(message);
	}

	void diodeChecks() {
		constexpr int directions[4][2] = {{0,1},{1,0},{0,-1},{-1,0}};
		for (uint32_t facing=0; facing<4; ++facing) for (uint32_t delay=1; delay<=4; ++delay) {
			circuit c;
			const int dx=directions[facing][0], dz=directions[facing][1];
			c.physics.setOutputQuery([](ac::blockId state) { return ac::blockType(state)==ac::BLOCK_REDSTONE_BLOCK ? uint8_t{1} : uint8_t{0}; });
			c.change({0,2,0},ac::withRepeaterDelay(ac::withFacing(ac::BLOCK_REPEATER,facing),delay));
			c.change({-dx,2,-dz},ac::BLOCK_REDSTONE_BLOCK);
			c.ticks(delay*4-1);
			check(!ac::isBlockPowered(c.blocks[{0,2,0}]),"repeater activated before configured delay");
			c.ticks(1);
			check(ac::isBlockPowered(c.blocks[{0,2,0}]),"repeater missed activation deadline");
			check(c.physics.inputPowerAt(c.read,dx,2,dz)==15,"repeater did not restore strength 15");
			check(c.physics.inputPowerAt(c.read,dz,2,-dx)==0,"repeater leaked power sideways");
			c.change({-dx,2,-dz},0u);
			c.ticks(delay*4-1);
			check(ac::isBlockPowered(c.blocks[{0,2,0}]),"repeater switched off too early");
			c.ticks(1);
			check(!ac::isBlockPowered(c.blocks[{0,2,0}]),"repeater did not switch off");
		}
		{
			circuit c;
			c.change({0,2,0},ac::withRepeaterDelay(ac::BLOCK_REPEATER,4));
			c.change({0,2,-1},ac::BLOCK_REDSTONE_BLOCK);
			c.ticks(1); c.change({0,2,-1},0); c.ticks(15);
			check(ac::isBlockPowered(c.blocks[{0,2,0}]),"short repeater pulse was discarded");
			c.ticks(15); check(ac::isBlockPowered(c.blocks[{0,2,0}]),"short pulse was not stretched");
			c.ticks(1); check(!ac::isBlockPowered(c.blocks[{0,2,0}]),"stretched pulse did not finish");
		}
		for (bool initiallyOn : {false,true}) {
			circuit c;
			c.blocks[{1,2,0}]=ac::withFacing(ac::withRedstonePower(ac::withBlockPowered(ac::BLOCK_COMPARATOR,true),15),3);
			c.blocks[{2,2,0}]=ac::BLOCK_REDSTONE_BLOCK;
			c.blocks[{0,2,-1}]=initiallyOn ? 0u : ac::BLOCK_REDSTONE_BLOCK;
			c.change({0,2,0},ac::withBlockPowered(ac::BLOCK_REPEATER,initiallyOn));
			c.ticks(20);
			check(ac::repeaterLocked(c.blocks[{0,2,0}]),"side comparator failed to lock repeater");
			check(ac::isBlockPowered(c.blocks[{0,2,0}])==initiallyOn,"locked repeater changed its held output");
			c.change({1,2,0},0); c.ticks(4);
			check(ac::isBlockPowered(c.blocks[{0,2,0}])!=initiallyOn,"unlocked repeater did not follow input");
		}
		for (bool subtract : {false,true}) for (uint8_t rear : {1u,7u,15u}) for (uint8_t side : {1u,7u,15u}) {
			circuit c;
			c.physics.setBehaviorQuery([](ac::blockId state) { return state==900u || state==901u ? ac::blockRedstoneBehavior::source : ac::blockRedstoneBehavior::none; });
			c.physics.setOutputQuery([&](ac::blockId state) { return state==900u ? rear : state==901u ? side : uint8_t{0}; });
			c.blocks[{0,2,-1}]=900u; c.blocks[{1,2,0}]=901u;
			c.change({0,2,0},ac::BLOCK_COMPARATOR | (subtract ? 1u<<28u : 0u));
			c.ticks(3); check(ac::redstonePower(c.blocks[{0,2,0}])==0,"comparator changed before one redstone tick");
			c.ticks(1);
			const uint8_t expected=subtract ? (rear>side ? rear-side : 0u) : (rear>=side ? rear : 0u);
			check(ac::redstonePower(c.blocks[{0,2,0}])==expected,"comparator compare/subtract result incorrect");
			check(c.physics.inputPowerAt(c.read,0,2,1)==expected,"comparator front output incorrect");
		}
		{
			circuit c;
			uint8_t fullness=0;
			c.physics.setConductorQuery([](ac::blockId s){return s==1u;});
			c.physics.setAnalogQuery([&](int32_t x,int32_t y,int32_t z)->std::optional<uint8_t>{
				if(x==0 && y==2 && z==-2) return fullness; return std::nullopt;
			});
			c.blocks[{0,2,-1}]=1u;
			c.change({0,2,0},ac::BLOCK_COMPARATOR);
			fullness=11; c.ticks(5);
			check(ac::redstonePower(c.blocks[{0,2,0}])==11,"container update through a block was missed");
			fullness=0; c.ticks(5);
			check(ac::redstonePower(c.blocks[{0,2,0}])==0,"empty container kept comparator powered");
		}
		{
			ac::chestInventory chest;
			auto limit=[](uint32_t id){return id==2u ? 1u : 64u;};
			check(ac::blockEntityStore::inventorySignal(chest.itemIds,chest.counts,limit)==0,"empty inventory signal incorrect");
			chest.itemIds[0]=1; chest.counts[0]=1;
			check(ac::blockEntityStore::inventorySignal(chest.itemIds,chest.counts,limit)==1,"single item signal incorrect");
			chest.itemIds.fill(2); chest.counts.fill(1);
			check(ac::blockEntityStore::inventorySignal(chest.itemIds,chest.counts,limit)==15,"tools must fill one inventory slot each");
		}
		for (uint32_t facing = 0; facing < 4; ++facing) {
			circuit c;
			c.change({0, 2, 0}, ac::withFacing(ac::BLOCK_OBSERVER, facing));
			const int dx = directions[facing][0], dz = directions[facing][1];
			c.change({-dx, 2, -dz}, 1u);
			check(ac::isBlockPowered(c.blocks[{0,2,0}]), "observer did not emit its pulse");
			check(c.physics.inputPowerAt(c.read, dx, 2, dz) == 15u, "observer output was not directional");
			c.ticks(ac::GAME_TICKS_PER_REDSTONE_TICK);
			check(!ac::isBlockPowered(c.blocks[{0,2,0}]), "observer pulse did not end after four game ticks");
		}
		{
			// Changes calculated by redstonePhysics do not pass through the normal
			// world-edit callback. Observers must still see the wire state update.
			circuit c;
			c.blocks[{-2, 2, 0}] = ac::BLOCK_REDSTONE_BLOCK;
			c.blocks[{-1, 2, 0}] = ac::BLOCK_REDSTONE_WIRE;
			c.blocks[{0, 2, 0}] = ac::withFacing(ac::BLOCK_OBSERVER, 1u);
			c.physics.enqueue(-1, 2, 0, true);
			c.physics.step(65536u, c.read, c.write, false);
			check(ac::redstonePower(c.blocks[{-1,2,0}]) == 15u,
				"wire did not update beside observer");
			check(ac::isBlockPowered(c.blocks[{0,2,0}]),
				"observer missed a redstone-authored block-state change");
			check(c.physics.inputPowerAt(c.read, 1, 2, 0) == 15u,
				"observer did not output after detecting a wire update");
			c.ticks(ac::GAME_TICKS_PER_REDSTONE_TICK);
			check(!ac::isBlockPowered(c.blocks[{0,2,0}]),
				"observer wire-update pulse did not end");
		}
	}

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
		for (uint32_t tick = 0; tick <= ac::GAME_TICKS_PER_REDSTONE_TICK; ++tick)
			physics.step(16u, read, write);
		return ac::isBlockPowered(blocks[{ x, y, z }]);
	}

	bool updateTorch(blockMap& blocks) {
		return updateTorchAt(blocks, 0, 1, 0);
	}

	void loadRedstoneBlockIds() {
		ac::staticAssetManager blocks;
		blocks.load("assets/block");
		ac::BLOCK_REDSTONE = blocks.getId("core:redstone");
		ac::BLOCK_REDSTONE_BLOCK = blocks.getId("core:redstone_block");
		ac::BLOCK_REDSTONE_WIRE = blocks.getId("core:redstone_wire");
		ac::BLOCK_REDSTONE_LAMP = blocks.getId("core:redstone_lamp");
		ac::BLOCK_REDSTONE_LAMP_ON = blocks.getId("core:redstone_lamp_on");
		ac::BLOCK_REDSTONE_TORCH = blocks.getId("core:redstone_torch");
		ac::BLOCK_REDSTONE_TORCH_OFF = blocks.getId("core:redstone_torch_off");
		ac::BLOCK_LEVER = blocks.getId("core:lever");
		ac::BLOCK_LEVER_ON = blocks.getId("core:lever_on");
		ac::BLOCK_REPEATER = blocks.getId("core:repeater");
		ac::BLOCK_COMPARATOR = blocks.getId("core:comparator");
		ac::BLOCK_OBSERVER = blocks.getId("core:observer");
	}
}

int main() {
	loadRedstoneBlockIds();
	try { diodeChecks(); }
	catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
	{
		// A lamp must retain changes between two nonzero strengths as well as
		// transitions on/off, so its mesh and emitted light can follow the input.
		ac::redstonePhysics physics;
		blockMap blocks;
		blocks[{ 1, 1, 0 }] = ac::BLOCK_REDSTONE_LAMP;
		uint8_t strength = 0;
		physics.setOutputQuery([&](ac::blockId state) {
			return ac::blockType(state) == ac::BLOCK_REDSTONE_BLOCK ? strength : uint8_t{ 0 };
		});
		auto read = [&](int32_t x, int32_t y, int32_t z) -> std::optional<ac::blockId> {
			const auto found = blocks.find({ x, y, z });
			return found == blocks.end() ? 0u : found->second;
		};
		auto write = [&](int32_t x, int32_t y, int32_t z, ac::blockId state) {
			blocks[{ x, y, z }] = state;
			return true;
		};
		for (uint8_t input : { 1u, 7u, 15u, 3u, 0u }) {
			strength = input;
			blocks[{ 0, 1, 0 }] = input ? ac::BLOCK_REDSTONE_BLOCK : 0u;
			physics.enqueue(1, 1, 0, true);
			physics.step(64u, read, write);
			const auto lamp = blocks[{ 1, 1, 0 }];
			if (ac::redstonePower(lamp) != input || ac::isBlockPowered(lamp) != (input != 0)) {
				std::cerr << "lamp did not preserve analog input strength\n";
				return 1;
			}
		}
	}
	{
		// Mounting orientation must agree with torch/lever placement facing.
		for (uint32_t facing = 0; facing < 4; ++facing) {
			const auto box = ac::leverSelectionBox(ac::withWallAttached(
				ac::withFacing(ac::BLOCK_LEVER, facing), true));
			const float attachedFace[] = { box.minZ, box.minX, 1.0f - box.maxZ, 1.0f - box.maxX };
			if (std::abs(attachedFace[facing]) > 0.001f) {
				std::cerr << "lever selection box is attached to the wrong wall\n";
				return 1;
			}
		}
	}
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
		if (!ac::redstoneDustStepsTo(wire) ||
			ac::redstoneDustStepsTo(source) ||
			ac::redstoneDustStepsTo(lamp)) {
			std::cerr << "stepped dust connected to a component on another Y level\n";
			return 1;
		}
		ac::modelManager models;
		models.load("assets/model");
		ac::chunkMesher mesher;
		ac::BLOCK_REPEATER = blocks.getId("core:repeater");
		ac::BLOCK_COMPARATOR = blocks.getId("core:comparator");
		for (const auto type : { ac::BLOCK_REPEATER, ac::BLOCK_COMPARATOR }) {
			const auto* definition = blocks.get(type);
			if (!definition || definition->_behavior.use != ac::blockUseBehavior::configureDiode) {
				std::cerr << "diode definition or interaction missing\n";
				return 1;
			}
			for (uint32_t facing = 0; facing < 4; ++facing)
				for (uint32_t variant = 0; variant < (type == ac::BLOCK_REPEATER ? 16u : 4u); ++variant) {
					auto state = ac::withBlockPowered(ac::withFacing(type, facing), (variant & 1u) != 0);
					if (type == ac::BLOCK_REPEATER)
						state = ac::withRepeaterDelay(state, variant / 4u + 1u) | ((variant & 2u) ? (1u << 27u) : 0u);
					else state |= (variant & 2u) ? (1u << 28u) : 0u;
					ac::chunk diodeChunk;
					diodeChunk.setBlock(4, 4, 4, state);
					const auto mesh = mesher.generate(diodeChunk, blocks, models);
					if (mesh._opaque._vertex.empty()) {
						std::cerr << "diode variant geometry missing\n";
						return 1;
					}
					for (const auto& vertex : mesh._opaque._vertex) {
						bool mapped = false;
						for (uint32_t face = 0; face < 6; ++face)
							mapped |= vertex._material == definition->materialForFace(face);
						if (!mapped) {
							std::cerr << "diode texture palette mapping missing\n";
							return 1;
						}
					}
				}
		}
		const auto* lever = blocks.get(ac::BLOCK_LEVER);
		for (uint32_t facing = 0; facing < 4; ++facing) {
			for (bool wall : { false, true }) for (bool powered : { false, true }) {
				const auto state = ac::withBlockPowered(ac::withWallAttached(
					ac::withFacing(ac::BLOCK_LEVER, facing), wall), powered);
				ac::chunk chunk;
				chunk.setBlock(4, 4, 4, state);
				const auto mesh = mesher.generate(chunk, blocks, models);
				const auto bounds = ac::leverSelectionBox(state);
				bool base = false, handle = false;
				for (const auto& vertex : mesh._opaque._vertex) {
					base |= vertex._material == lever->materialForFace(ac::BLOCK_FACE_UP);
					handle |= vertex._material == lever->materialForFace(ac::BLOCK_FACE_WEST);
					const auto& p = vertex._position;
					if (p.x < 4 + bounds.minX - 0.001f || p.x > 4 + bounds.maxX + 0.001f ||
						p.y < 4 + bounds.minY - 0.001f || p.y > 4 + bounds.maxY + 0.001f ||
						p.z < 4 + bounds.minZ - 0.001f || p.z > 4 + bounds.maxZ + 0.001f) {
						std::cerr << "lever mesh extends outside its selection box\n";
						return 1;
					}
				}
				if (!base || !handle || mesh._opaque._index.size() != 72u) {
					std::cerr << "lever base or handle geometry/material missing\n";
					return 1;
				}
			}
		}
		const auto properties = blocks.materialTable();
		const auto* litLamp = blocks.get(ac::BLOCK_REDSTONE_LAMP_ON);
		if (properties[litLamp->materialForFace(ac::BLOCK_FACE_UP)].emissionWarmMask != 2u) {
			std::cerr << "lamp dark-pixel emission mask missing\n";
			return 1;
		}
		ac::chunk lampChunk;
		lampChunk.setBlock(4, 4, 4, ac::withRedstonePower(ac::BLOCK_REDSTONE_LAMP_ON, 7u));
		const auto lampMesh = mesher.generate(lampChunk, blocks, models);
		for (const auto& vertex : lampMesh._opaque._vertex) {
			if ((vertex._ao & 0x4000u) == 0u || ((vertex._ao >> 9u) & 15u) != 7u) {
				std::cerr << "lamp mesh lost its signal brightness\n";
				return 1;
			}
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
			std::cerr << "redstone torch switched off without its four-game-tick delay\n";
			return 1;
		}
		physics.step(16u, read, write, false);
		if (!ac::isBlockPowered(blocks[{ 0, 1, 0 }])) {
			std::cerr << "non-tick redstone work advanced the torch delay\n";
			return 1;
		}
		for (uint32_t tick = 1; tick < ac::GAME_TICKS_PER_REDSTONE_TICK; ++tick) {
			physics.step(16u, read, write);
			if (!ac::isBlockPowered(blocks[{ 0, 1, 0 }])) {
				std::cerr << "redstone torch switched off before one redstone tick elapsed\n";
				return 1;
			}
		}
		physics.step(16u, read, write);
		if (ac::isBlockPowered(blocks[{ 0, 1, 0 }])) {
			std::cerr << "redstone torch did not switch off after one redstone tick\n";
			return 1;
		}

		blocks[{ 0, 0, 0 }] = 1u;
		physics.notifyBlockChanged(
			0, 0, 0, ac::BLOCK_REDSTONE_BLOCK, 1u, read, true);
		physics.step(64u, read, write, false);
		for (uint32_t tick = 1; tick < ac::GAME_TICKS_PER_REDSTONE_TICK; ++tick) {
			physics.step(64u, read, write);
			if (ac::isBlockPowered(blocks[{ 0, 1, 0 }])) {
				std::cerr << "redstone torch switched on before one redstone tick elapsed\n";
				return 1;
			}
		}
		physics.step(64u, read, write);
		if (!ac::isBlockPowered(blocks[{ 0, 1, 0 }])) {
			std::cerr << "redstone torch did not switch on after one redstone tick\n";
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
