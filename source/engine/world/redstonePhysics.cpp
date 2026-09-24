#include "redstonePhysics.h"

#include <algorithm>
#include <vector>

namespace ac {

	size_t redstonePhysics::cellHash::operator()(const cell& value) const {
		size_t result = std::hash<int32_t>{}(value.x);
		result ^= std::hash<int32_t>{}(value.y) + 0x9e3779b9u + (result << 6u) + (result >> 2u);
		result ^= std::hash<int32_t>{}(value.z) + 0x9e3779b9u + (result << 6u) + (result >> 2u);
		return result;
	}

	void redstonePhysics::enqueue(int32_t x, int32_t y, int32_t z, bool urgent) {
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

	blockRedstoneBehavior redstonePhysics::behavior(blockId state) const {
		if (_behaviorQuery) {
			const blockRedstoneBehavior configured = _behaviorQuery(state);
			if (configured != blockRedstoneBehavior::none) return configured;
		}
		if (isRedstoneWire(state)) return blockRedstoneBehavior::wire;
		if (isRedstoneTorch(state)) return blockRedstoneBehavior::torch;
		if (isRedstoneLamp(state)) return blockRedstoneBehavior::lamp;
		if (isLever(state)) return blockRedstoneBehavior::switchSource;
		if (blockType(state) == BLOCK_REPEATER) return blockRedstoneBehavior::repeater;
		if (blockType(state) == BLOCK_COMPARATOR) return blockRedstoneBehavior::comparator;
		if (blockType(state) == BLOCK_OBSERVER) return blockRedstoneBehavior::listener;
		if (blockType(state) == BLOCK_REDSTONE_BLOCK) return blockRedstoneBehavior::source;
		return blockRedstoneBehavior::none;
	}

	uint8_t redstonePhysics::outputPower(blockId state) const {
		if (_outputQuery) {
			const uint8_t configured = _outputQuery(state);
			if (configured > 0u) return configured;
		}
		switch (behavior(state)) {
		case blockRedstoneBehavior::repeater: return isBlockPowered(state) ? 15u : 0u;
		case blockRedstoneBehavior::comparator: return redstonePower(state);
		case blockRedstoneBehavior::listener: return isBlockPowered(state) ? 15u : 0u;
		case blockRedstoneBehavior::wire: return redstonePower(state);
		case blockRedstoneBehavior::source: return 15u;
		case blockRedstoneBehavior::torch:
		case blockRedstoneBehavior::switchSource:
			return isBlockPowered(state) ? 15u : 0u;
		default: return 0u;
		}
	}

	uint8_t redstonePhysics::outputToward(blockId state, int32_t sx, int32_t sy, int32_t sz,
		int32_t tx, int32_t ty, int32_t tz) const {
		const auto role = behavior(state);
		if (role == blockRedstoneBehavior::repeater || role == blockRedstoneBehavior::comparator ||
			role == blockRedstoneBehavior::listener) {
			static constexpr int offsets[4][2] = { {0,1}, {1,0}, {0,-1}, {-1,0} };
			const auto& front = offsets[blockFacing(state)];
			if (ty != sy || tx != sx + front[0] || tz != sz + front[1]) return 0;
		}
		return outputPower(state);
	}

	uint8_t redstonePhysics::diodeRear(const blockReader& read, const cell& at, blockId state) const {
		static constexpr int offsets[4][2] = { {0,1}, {1,0}, {0,-1}, {-1,0} };
		const auto& front = offsets[blockFacing(state)];
		const int x = at.x - front[0], z = at.z - front[1];
		const auto rear = read(x, at.y, z);
		if (!rear) return 0;
		uint8_t result = outputToward(*rear, x, at.y, z, at.x, at.y, at.z);
		const bool conductor = _conductorQuery && _conductorQuery(*rear);
		if (conductor)
			result = (std::max)(result, incomingPower(read, x, at.y, z, false,
				at.x, at.y, at.z, false));
		if (behavior(state) == blockRedstoneBehavior::comparator && _analogQuery) {
			if (const auto analog = _analogQuery(x, at.y, z)) result = *analog;
			else if (conductor && result < 15u) {
				if (const auto analog = _analogQuery(x - front[0], at.y, z - front[1]))
					result = *analog;
			}
		}
		return result;
	}

	uint8_t redstonePhysics::diodeSide(const blockReader& read, const cell& at, blockId state,
		bool lockOnly) const {
		static constexpr int offsets[4][2] = { {0,1}, {1,0}, {0,-1}, {-1,0} };
		const auto& front = offsets[blockFacing(state)];
		uint8_t result = 0;
		for (int sign : { -1, 1 }) {
			const int x = at.x + front[1] * sign, z = at.z - front[0] * sign;
			const auto side = read(x, at.y, z);
			if (!side) continue;
			const auto role = behavior(*side);
			const bool diode = role == blockRedstoneBehavior::repeater || role == blockRedstoneBehavior::comparator;
			if (lockOnly ? !diode : (!diode && role != blockRedstoneBehavior::wire &&
				role != blockRedstoneBehavior::source)) continue;
			result = (std::max)(result, outputToward(*side, x, at.y, z, at.x, at.y, at.z));
		}
		return result;
	}

	void redstonePhysics::notifyNeighbors(
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
			for (int32_t distance = 1; distance <= 2; ++distance) {
				const int32_t nx = x + offset[0] * distance;
				const int32_t ny = y + offset[1] * distance;
				const int32_t nz = z + offset[2] * distance;
				// Deliver update messages one and two blocks along every face
				// direction. Each receiver computes its own next state, and only an
				// actual state transition creates another propagation wave.
				const auto neighbor = read(nx, ny, nz);
				if (neighbor)
					enqueue(nx, ny, nz, urgent);
			}
		}
		// Dust can climb onto or descend from neighboring blocks. Queue those
		// diagonal cells as well; non-wire listeners simply re-evaluate to no-op.
		static constexpr int32_t stepped[8][3] = {
			{ -1, -1, 0 }, { -1, 1, 0 }, { 1, -1, 0 }, { 1, 1, 0 },
			{ 0, -1, -1 }, { 0, 1, -1 }, { 0, -1, 1 }, { 0, 1, 1 }
		};
		for (const auto& offset : stepped) {
			const auto neighbor = read(x + offset[0], y + offset[1], z + offset[2]);
			if (neighbor && behavior(*neighbor) == blockRedstoneBehavior::wire)
				enqueue(x + offset[0], y + offset[1], z + offset[2], urgent);
		}
	}

	void redstonePhysics::notifyBlockChanged(
		int32_t x, int32_t y, int32_t z,
		blockId previousState,
		blockId newState,
		const blockReader& read,
		bool urgent
	) {
		if (previousState == newState) return;
		// An observer watches its rear face. A changed block schedules an immediate
		// rising edge and a four-game-tick falling edge on observers facing it.
		// This path is used both by world edits and by state transitions produced by
		// this solver, so observers see wire, torch, repeater, and lamp updates too.
		static constexpr int offsets[6][3] = {{1,0,0},{-1,0,0},{0,0,1},{0,0,-1},{0,1,0},{0,-1,0}};
		for (const auto& offset : offsets) {
			const int32_t ox = x + offset[0], oy = y + offset[1], oz = z + offset[2];
			const auto observer = read(ox, oy, oz);
			if (!observer || blockType(*observer) != BLOCK_OBSERVER) continue;
			static constexpr int facingOffsets[4][2] = {{0,1},{1,0},{0,-1},{-1,0}};
			const auto& front = facingOffsets[blockFacing(*observer)];
			if (x != ox - front[0] || z != oz - front[1] || y != oy) continue;
			_diodeTransitions[{ox,oy,oz}] = { _tick + GAME_TICKS_PER_REDSTONE_TICK, false };
			enqueue(ox, oy, oz, true);
		}
		if (blockType(previousState) != blockType(newState) || blockFacing(previousState) != blockFacing(newState)) {
			_diodeTransitions.erase({x, y, z});
			_comparators.erase({x, y, z});
		}
		// The changed cell receives the same update as its surroundings. This
		// lets a freshly placed component initialize from the world immediately.
		if (read(x, y, z))
			enqueue(x, y, z, urgent);
		notifyNeighbors(x, y, z, read, urgent);
	}

	uint8_t redstonePhysics::inputPowerAt(
		const blockReader& read,
		int32_t x,
		int32_t y,
		int32_t z
	) const {
		return incomingPower(read, x, y, z, false);
	}

	bool redstonePhysics::wirePointsToward(
		const blockReader& read,
		int32_t wireX, int32_t wireY, int32_t wireZ,
		int32_t targetX, int32_t targetY, int32_t targetZ
	) const {
		static constexpr int32_t directions[4][2] = {
			{ 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 }
		};
		bool directed[4]{};
		bool hasDirection = false;
		for (uint32_t direction = 0; direction < 4u; ++direction) {
			const int32_t nx = wireX + directions[direction][0];
			const int32_t nz = wireZ + directions[direction][1];
			const auto beside = read(nx, wireY, nz);
			// Only redstone components beside the dust on the same Y level may
			// redirect its horizontal output. Stepped wires are connectivity,
			// not direction selectors.
			if (beside && behavior(*beside) != blockRedstoneBehavior::none) {
				const auto role = behavior(*beside);
				const bool diode = role == blockRedstoneBehavior::repeater || role == blockRedstoneBehavior::comparator;
				directed[direction] = !diode || ((blockFacing(*beside) & 1u) == (direction & 1u));
			}
			hasDirection = hasDirection || directed[direction];
		}

		int targetDirection = -1;
		const int32_t dx = targetX - wireX;
		const int32_t dz = targetZ - wireZ;
		if (std::abs(targetY - wireY) <= 1) {
			for (int direction = 0; direction < 4; ++direction) {
				if (dx == directions[direction][0] && dz == directions[direction][1]) {
					targetDirection = direction;
					break;
				}
			}
		}
		if (targetDirection < 0) return false;

		// A one-block height transition remains connected without allowing the
		// upper/lower component to redirect the dust's horizontal output.
		if (targetY == wireY + 1) {
			const auto beside = read(targetX, wireY, targetZ);
			const auto overhead = read(wireX, wireY + 1, wireZ);
			return beside && _conductorQuery && _conductorQuery(*beside) &&
				!(overhead && _conductorQuery(*overhead));
		}
		if (targetY == wireY - 1) {
			const auto beside = read(targetX, wireY, targetZ);
			return !(beside && _conductorQuery && _conductorQuery(*beside));
		}
		if (hasDirection) return directed[targetDirection];

		const auto wire = read(wireX, wireY, wireZ);
		return wire && targetY == wireY &&
			targetDirection == static_cast<int>(blockFacing(*wire) & 3u);
	}

	uint8_t redstonePhysics::incomingPower(
		const blockReader& read, int32_t x, int32_t y, int32_t z,
		bool attenuateWire,
		int32_t excludeX, int32_t excludeY, int32_t excludeZ,
		bool conductThroughSolidNeighbors
	) const {
		uint8_t best = 0u;
		const int32_t neighbors[6][3] = {
			{ -1, 0, 0 }, { 1, 0, 0 },
			{ 0, -1, 0 }, { 0, 1, 0 },
			{ 0, 0, -1 }, { 0, 0, 1 }
		};
		for (const auto& offset : neighbors) {
			const int32_t nx = x + offset[0];
			const int32_t ny = y + offset[1];
			const int32_t nz = z + offset[2];
			if (nx == excludeX && ny == excludeY && nz == excludeZ) continue;
			const auto neighbor = read(nx, ny, nz);
			if (!neighbor) continue;
			uint8_t power = outputToward(*neighbor, nx, ny, nz, x, y, z);
			if (behavior(*neighbor) == blockRedstoneBehavior::wire) {
				// Dust always powers the solid block directly beneath it. Other
				// outputs continue to respect its horizontal direction.
				const bool directlyAbove = nx == x && ny == y + 1 && nz == z;
				if (!directlyAbove &&
					!wirePointsToward(read, nx, ny, nz, x, y, z))
					power = 0u;
			}
			// Dust attenuates only while propagating into another dust segment.
			// Devices consume the wire's actual level, including level one.
			if (attenuateWire &&
				behavior(*neighbor) == blockRedstoneBehavior::wire && power > 0u)
				--power;
			best = (std::max)(best, power);

			// Opaque solid cubes conduct power from a source on another face.
			if (conductThroughSolidNeighbors && power == 0u &&
				_conductorQuery && _conductorQuery(*neighbor)) {
				for (const auto& sourceOffset : neighbors) {
					const int32_t sx = nx + sourceOffset[0];
					const int32_t sy = ny + sourceOffset[1];
					const int32_t sz = nz + sourceOffset[2];
					if ((sx == x && sy == y && sz == z) ||
						(sx == excludeX && sy == excludeY && sz == excludeZ))
						continue;
					const auto source = read(sx, sy, sz);
					if (!source) continue;
					// A full cube blocks dust instead of relaying its signal through
					// to the opposite face. Non-wire strong sources may still conduct.
					if (behavior(*source) == blockRedstoneBehavior::wire) {
						// A dust line strongly powers its supporting block, but a full
						// cube still may not relay wire power from any other face.
						const bool onTop = sx == nx && sy == ny + 1 && sz == nz;
						if (!onTop) continue;
					}
					uint8_t conducted = outputToward(*source, sx, sy, sz, nx, ny, nz);
					best = (std::max)(best, conducted);
				}
			}
		}
		return best;
	}

	uint8_t redstonePhysics::wireIncomingPower(
		const blockReader& read, int32_t x, int32_t y, int32_t z
	) const {
		uint8_t best = incomingPower(read, x, y, z, true);
		static constexpr int32_t horizontal[4][2] = {
			{ -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }
		};
		for (const auto& offset : horizontal) {
			for (const int32_t dy : { -1, 1 }) {
				const auto stepped = read(x + offset[0], y + dy, z + offset[1]);
				if (!stepped || behavior(*stepped) != blockRedstoneBehavior::wire) continue;
				if (!wirePointsToward(
						read, x + offset[0], y + dy, z + offset[1], x, y, z))
					continue;
				const uint8_t power = redstonePower(*stepped);
				best = (std::max)(best, static_cast<uint8_t>(power > 0u ? power - 1u : 0u));
			}
		}
		return best;
	}

	uint8_t redstonePhysics::supportPower(
		const blockReader& read,
		int32_t x,
		int32_t y,
		int32_t z,
		blockId torchState
	) {
		int32_t sx = x;
		int32_t sy = y - 1;
		int32_t sz = z;
		if (isWallAttached(torchState)) {
			const uint32_t facing = blockFacing(torchState);
			sx = x;
			sy = y;
			sz = z;
			switch (facing) {
			case 0: --sz; break; // facing south → attached to north
			case 1: --sx; break; // facing east → attached to west
			case 2: ++sz; break; // facing north → attached to south
			case 3: ++sx; break; // facing west → attached to east
			default: break;
			}
		}

		uint8_t supportPowerValue = 0u;
		const auto support = read(sx, sy, sz);
		if (support) {
			supportPowerValue = outputPower(*support);
			// Only an actual supporting conductor can receive external power.
			// The torch's cell is excluded below, so powering the torch itself
			// never becomes an input to its inverter.
			if (supportPowerValue == 0u && _conductorQuery && !_conductorQuery(*support))
				return 0u;
			if (supportPowerValue == 0u)
				// A torch samples power delivered directly into its attached block.
				// Do not let incomingPower look through an adjacent solid and make a
				// source two blocks away appear to charge the support.
				supportPowerValue = incomingPower(
					read, sx, sy, sz, false, x, y, z, false);
		}
		return supportPowerValue;
	}

	size_t redstonePhysics::step(
		size_t updateBudget,
		const blockReader& read,
		const blockWriter& write,
		bool advanceTick
	) {
		if (advanceTick)
			++_tick;
		// Inventory changes do not change the container's voxel state.
		// Revisit loaded comparators so changes in fullness still propagate.
		if (advanceTick) {
			for (auto it = _comparators.begin(); it != _comparators.end();) {
				const auto state = read(it->x, it->y, it->z);
				if (!state || behavior(*state) != blockRedstoneBehavior::comparator)
					it = _comparators.erase(it);
				else { enqueue(it->x, it->y, it->z); ++it; }
			}
		}
		std::vector<cell> dueDiodes;
		for (const auto& [position, transition] : _diodeTransitions)
			if (transition.dueTick <= _tick) dueDiodes.push_back(position);
		std::sort(dueDiodes.begin(), dueDiodes.end(), [&](const cell& a, const cell& b) {
			const auto& ta = _diodeTransitions.at(a);
			const auto& tb = _diodeTransitions.at(b);
			if (ta.dueTick != tb.dueTick) return ta.dueTick < tb.dueTick;
			if (ta.turnOn != tb.turnOn) return !ta.turnOn;
			if (a.y != b.y) return a.y < b.y;
			if (a.z != b.z) return a.z < b.z;
			return a.x < b.x;
		});
		for (const auto& position : dueDiodes) enqueue(position.x, position.y, position.z, true);
		std::vector<cell> dueTorches;
		dueTorches.reserve(_torchTransitions.size());
		for (const auto& [position, transition] : _torchTransitions) {
			if (transition.dueTick <= _tick)
				dueTorches.push_back(position);
		}
		std::sort(dueTorches.begin(), dueTorches.end(), [](const cell& a, const cell& b) {
			if (a.y != b.y) return a.y < b.y;
			if (a.z != b.z) return a.z < b.z;
			return a.x < b.x;
		});
		for (const cell& position : dueTorches)
			enqueue(position.x, position.y, position.z, true);
		size_t processed = 0u;
		size_t changed = 0u;
		while (processed < updateBudget && pending() > 0u) {
			++processed;
			cell value{};
			if (!_urgentPending.empty()) {
				value = _urgentPending.front();
				_urgentPending.pop_front();
			}
			else {
				value = _pending.front();
				_pending.pop_front();
			}
			_queued.erase(value);

			const auto current = read(value.x, value.y, value.z);
			if (!current) {
				_torchTransitions.erase(value);
				_diodeTransitions.erase(value);
				_comparators.erase(value);
				continue;
			}
			const blockId state = *current;
			const blockId type = blockType(state);
			const blockRedstoneBehavior role = behavior(state);
			if (_supportQuery && (role == blockRedstoneBehavior::repeater || role == blockRedstoneBehavior::comparator)) {
				const auto support = read(value.x, value.y - 1, value.z);
				if (support && !_supportQuery(*support)) {
					if (write(value.x, value.y, value.z, 0u)) {
						_diodeTransitions.erase(value);
						_comparators.erase(value);
						notifyNeighbors(value.x, value.y, value.z, read, true);
						if (_dropDevice) _dropDevice(value.x, value.y, value.z, blockType(state));
						++changed;
					}
					continue;
				}
			}
			blockId next = state;
			if (role != blockRedstoneBehavior::torch)
				_torchTransitions.erase(value);
			if (role != blockRedstoneBehavior::repeater && role != blockRedstoneBehavior::comparator &&
				role != blockRedstoneBehavior::listener)
				_diodeTransitions.erase(value);

			if (role == blockRedstoneBehavior::repeater) {
				const bool locked = diodeSide(read, value, state, true) > 0u;
				next = locked ? state | (1u << 27u) : state & ~(1u << 27u);
				if (locked) _diodeTransitions.erase(value);
				else {
					const bool input = diodeRear(read, value, state) > 0u;
					const auto pending = _diodeTransitions.find(value);
					if (pending != _diodeTransitions.end()) {
						if (pending->second.dueTick <= _tick) {
							// Preserve short ON pulses for a full configured delay;
							// cancel an OFF transition if the input recovered in time.
							next = withBlockPowered(next, pending->second.turnOn || input);
							_diodeTransitions.erase(pending);
						}
					}
					else if (input != isBlockPowered(state))
						_diodeTransitions[value] = { _tick + repeaterDelay(state) * GAME_TICKS_PER_REDSTONE_TICK, input };
				}
			}
			else if (role == blockRedstoneBehavior::comparator) {
				_comparators.insert(value);
				const uint8_t rear = diodeRear(read, value, state);
				const uint8_t side = diodeSide(read, value, state, false);
				const uint8_t output = comparatorSubtract(state)
					? (rear > side ? rear - side : 0u) : (rear >= side ? rear : 0u);
				const auto pending = _diodeTransitions.find(value);
				if (pending != _diodeTransitions.end()) {
					if (pending->second.dueTick <= _tick) {
						next = withRedstonePower(withBlockPowered(state, output > 0u), output);
						_diodeTransitions.erase(pending);
					}
				}
				else if (output != redstonePower(state) || isBlockPowered(state) != (output > 0u))
					_diodeTransitions[value] = { _tick + GAME_TICKS_PER_REDSTONE_TICK, output > 0u };
			}
			else if (role == blockRedstoneBehavior::listener) {
				const auto pending = _diodeTransitions.find(value);
				if (pending != _diodeTransitions.end() && pending->second.dueTick <= _tick) {
					next = withBlockPowered(state, pending->second.turnOn);
					_diodeTransitions.erase(pending);
				}
				else if (pending != _diodeTransitions.end())
					next = withBlockPowered(state, true);
			}
			else if (role == blockRedstoneBehavior::wire) {
				const uint8_t power = wireIncomingPower(read, value.x, value.y, value.z);
				// Signal recalculation must not erase placement-facing or any future
				// wire attributes stored outside the power bits.
				next = withRedstonePower(state, power);
			}
			else if (role == blockRedstoneBehavior::lamp) {
				const uint8_t power = incomingPower(read, value.x, value.y, value.z, false);
				const blockId stateType = isRedstoneLamp(type) ? BLOCK_REDSTONE_LAMP : type;
				next = withRedstonePower(withBlockPowered(
					withFacing(stateType, blockFacing(state)),
					power > 0u), power);
			}
			else if (role == blockRedstoneBehavior::torch) {
				const uint8_t powered = supportPower(read, value.x, value.y, value.z, state);
				const blockId stateType = isRedstoneTorch(type) ? BLOCK_REDSTONE_TORCH : type;
				next = withBlockPowered(
					withWallAttached(
						withFacing(stateType, blockFacing(state)),
						isWallAttached(state)),
					powered == 0u);
				const auto pending = _torchTransitions.find(value);
				if (next == state) {
					if (pending != _torchTransitions.end())
						_torchTransitions.erase(pending);
				}
				else if (pending == _torchTransitions.end() ||
					pending->second.targetState != next) {
					_torchTransitions[value] = { next, _tick + TORCH_DELAY_TICKS };
					next = state;
				}
				else if (pending->second.dueTick > _tick) {
					next = state;
				}
				else {
					_torchTransitions.erase(pending);
				}
			}
			else if (_poweredDeviceUpdate) {
				const uint8_t power = incomingPower(read, value.x, value.y, value.z, false);
				if (const auto resolved = _poweredDeviceUpdate(
						value.x, value.y, value.z, state, power))
					next = *resolved;
			}

			if (next != state) {
				if (write(value.x, value.y, value.z, next)) {
					// Redstone-authored block-state changes are real neighbor changes.
					// Route them through the full notification path so an observer
					// watching this cell produces a pulse.
					notifyBlockChanged(
						value.x, value.y, value.z, state, next, read, true);
					if (role == blockRedstoneBehavior::repeater)
						enqueue(value.x, value.y, value.z, true);
					++changed;
				}
			}
		}
		return changed;
	}

	void redstonePhysics::clear() {
		_urgentPending.clear();
		_pending.clear();
		_queued.clear();
		_torchTransitions.clear();
		_diodeTransitions.clear();
		_comparators.clear();
		_tick = 0u;
	}

}
