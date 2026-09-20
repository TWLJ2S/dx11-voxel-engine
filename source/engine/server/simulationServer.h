#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace ac {

	struct serverInput {
		float lookX = 0.0f;
		float lookY = 0.0f;
		bool forward = false;
		bool backward = false;
		bool left = false;
		bool right = false;
		bool jump = false;
		bool crouch = false;
		bool sprint = false;
	};

	struct serverStep {
		uint64_t tick = 0;
		uint8_t subtick = 0;
		float deltaTime = 0.0f;
	};

	// Fixed-step authoritative simulation clock. A 32 Hz game tick is divided
	// into four 128 Hz subticks so movement and command timing remain responsive.
	class simulationServer {
	public:
		using updateCallback = std::function<void(const serverStep&, const serverInput&)>;
		using command = std::function<void()>;

		static constexpr uint32_t TICKS_PER_SECOND = 32u;
		static constexpr uint8_t SUBTICKS_PER_TICK = 4u;
		static constexpr uint32_t SUBTICKS_PER_SECOND =
			TICKS_PER_SECOND * SUBTICKS_PER_TICK;
		static constexpr float SUBTICK_DELTA_TIME =
			1.0f / static_cast<float>(SUBTICKS_PER_SECOND);

		void submitInput(const serverInput& input);
		void schedule(command work, uint8_t subtickOffset = 0u);
		uint32_t advance(float elapsedSeconds, const updateCallback& update);

		uint64_t tick() const;
		uint8_t subtick() const;
		float interpolationAlpha() const;

	private:
		struct scheduledCommand {
			uint64_t tick = 0;
			uint8_t subtick = 0;
			uint64_t sequence = 0;
			command work;
		};

		mutable std::mutex _mutex;
		serverInput _latestInput{};
		float _pendingLookX = 0.0f;
		float _pendingLookY = 0.0f;
		std::vector<scheduledCommand> _commands;
		uint64_t _nextSequence = 0;
		uint64_t _tick = 0;
		uint8_t _subtick = 0;
		float _accumulator = 0.0f;
	};

}
