#include "simulationServer.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ac {

	void simulationServer::submitInput(const serverInput& input) {
		std::lock_guard<std::mutex> lock(_mutex);
		_latestInput = input;
		_pendingLookX += input.lookX;
		_pendingLookY += input.lookY;
	}

	void simulationServer::schedule(command work, uint8_t subtickOffset) {
		if (!work) return;

		std::lock_guard<std::mutex> lock(_mutex);
		const uint32_t absoluteSubtick = static_cast<uint32_t>(_subtick) + subtickOffset;
		_commands.push_back({
			_tick + absoluteSubtick / SUBTICKS_PER_TICK,
			static_cast<uint8_t>(absoluteSubtick % SUBTICKS_PER_TICK),
			_nextSequence++,
			std::move(work)
		});
	}

	uint32_t simulationServer::advance(float elapsedSeconds, const updateCallback& update) {
		if (!update) return 0u;

		constexpr uint32_t maximumCatchUpSubticks = SUBTICKS_PER_SECOND / 4u;
		_accumulator += std::clamp(elapsedSeconds, 0.0f, 0.25f);
		uint32_t processed = 0u;

		while (_accumulator >= SUBTICK_DELTA_TIME && processed < maximumCatchUpSubticks) {
			serverInput input;
			std::vector<command> dueCommands;
			{
				std::lock_guard<std::mutex> lock(_mutex);
				input = _latestInput;
				input.lookX = _pendingLookX;
				input.lookY = _pendingLookY;
				_pendingLookX = 0.0f;
				_pendingLookY = 0.0f;

				auto due = [this](const scheduledCommand& item) {
					return item.tick < _tick ||
						(item.tick == _tick && item.subtick <= _subtick);
				};
				std::stable_sort(_commands.begin(), _commands.end(),
					[](const scheduledCommand& left, const scheduledCommand& right) {
						return left.sequence < right.sequence;
					});
				for (scheduledCommand& item : _commands) {
					if (due(item)) dueCommands.push_back(std::move(item.work));
				}
				_commands.erase(std::remove_if(_commands.begin(), _commands.end(), due), _commands.end());
			}

			for (command& work : dueCommands) work();
			const serverStep step{ _tick, _subtick, SUBTICK_DELTA_TIME };
			update(step, input);

			_accumulator -= SUBTICK_DELTA_TIME;
			++processed;
			{
				std::lock_guard<std::mutex> lock(_mutex);
				if (++_subtick == SUBTICKS_PER_TICK) {
					_subtick = 0u;
					++_tick;
				}
			}
		}

		// Avoid a permanent spiral of death after a breakpoint or long stall.
		if (processed == maximumCatchUpSubticks && _accumulator >= SUBTICK_DELTA_TIME)
			_accumulator = std::fmod(_accumulator, SUBTICK_DELTA_TIME);
		return processed;
	}

	uint64_t simulationServer::tick() const {
		std::lock_guard<std::mutex> lock(_mutex);
		return _tick;
	}

	uint8_t simulationServer::subtick() const {
		std::lock_guard<std::mutex> lock(_mutex);
		return _subtick;
	}

	float simulationServer::interpolationAlpha() const {
		return std::clamp(_accumulator / SUBTICK_DELTA_TIME, 0.0f, 1.0f);
	}

}
