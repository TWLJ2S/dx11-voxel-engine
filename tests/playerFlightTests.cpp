#include <core/player.h>

#include <iostream>

namespace {
	constexpr float STEP = ac::simulationServer::SUBTICK_DELTA_TIME;

	auto dryWorld() {
		return [](float, float, float) { return false; };
	}

	void step(ac::player& player, const ac::serverInput& input, bool creative,
		auto&& solid, uint32_t count = 1u) {
		for (uint32_t i = 0; i < count; ++i)
			player.update(input, { 0.05f, 0.05f, 0.05f }, 19.0f, STEP,
				solid, dryWorld(), false, creative);
	}

	bool doubleTapJump(ac::player& player, bool creative, auto&& solid) {
		ac::serverInput input{};
		input.jump = true;
		step(player, input, creative, solid);
		input.jump = false;
		step(player, input, creative, solid);
		input.jump = true;
		step(player, input, creative, solid);
		return player.isFlying();
	}
}

int main() {
	auto empty = [](int32_t, int32_t, int32_t, float, float, float) { return false; };
	ac::player creativePlayer({ 0.0f, 10.0f, 0.0f }, { 1.0f, 10.0f, 0.0f },
		1.2f, 16.0f / 9.0f, 0.01f);
	if (!doubleTapJump(creativePlayer, true, empty)) {
		std::cerr << "creative double-jump did not enable flight\n";
		return 1;
	}

	const float beforeRise = creativePlayer.getCamera()._gpuData._position.y;
	ac::serverInput vertical{};
	vertical.jump = true;
	step(creativePlayer, vertical, true, empty, 64u);
	if (creativePlayer.getCamera()._gpuData._position.y <= beforeRise + 0.25f) {
		std::cerr << "jump did not ascend during creative flight\n";
		return 1;
	}

	vertical = {};
	vertical.crouch = true;
	const float beforeDescent = creativePlayer.getCamera()._gpuData._position.y;
	step(creativePlayer, vertical, true, empty, 128u);
	if (creativePlayer.getCamera()._gpuData._position.y >= beforeDescent - 0.25f) {
		std::cerr << "crouch did not descend during creative flight\n";
		return 1;
	}
	if (doubleTapJump(creativePlayer, true, empty)) {
		std::cerr << "second creative double-jump did not disable flight\n";
		return 1;
	}

	ac::player survivalPlayer({ 0.0f, 10.0f, 0.0f }, { 1.0f, 10.0f, 0.0f },
		1.2f, 16.0f / 9.0f, 0.01f);
	if (doubleTapJump(survivalPlayer, false, empty)) {
		std::cerr << "survival mode enabled creative flight\n";
		return 1;
	}

	ac::player restored({ 0.0f, 5.0f, 0.0f }, { 1.0f, 5.0f, 0.0f },
		1.2f, 16.0f / 9.0f, 0.01f);
	restored.restoreState({ 0.0f, 5.0f, 0.0f }, {}, 0.0f, 0.0f, false, true);
	if (!restored.isFlying()) {
		std::cerr << "saved creative flight state was not restored\n";
		return 1;
	}

	auto floor = [](int32_t, int32_t y, int32_t, float, float, float) {
		return y <= 0;
	};
	ac::player landing({ 0.0f, 4.0f, 0.0f }, { 1.0f, 4.0f, 0.0f },
		1.2f, 16.0f / 9.0f, 0.01f);
	landing.restoreState({ 0.0f, 4.0f, 0.0f }, {}, 0.0f, 0.0f, false, true);
	vertical = {};
	vertical.crouch = true;
	step(landing, vertical, true, floor, 256u);
	if (landing.isFlying() || !landing.isGrounded()) {
		std::cerr << "landing did not leave creative flight\n";
		return 1;
	}

	std::cout << "creative flight activation, controls, persistence, and landing passed\n";
	return 0;
}
