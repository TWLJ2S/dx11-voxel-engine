#include <server/simulationServer.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>

int main() {
	ac::simulationServer server;
	uint32_t updates = 0u;
	uint32_t tickStarts = 0u;
	float receivedLook = 0.0f;

	ac::serverInput first;
	first.lookX = 2.0f;
	server.submitInput(first);
	ac::serverInput latest;
	latest.lookX = 3.0f;
	latest.forward = true;
	server.submitInput(latest);

	bool commandRan = false;
	uint8_t commandObservedSubtick = 255u;
	server.schedule([&]() { commandRan = true; }, 3u);

	for (uint32_t quarter = 0u; quarter < 4u; ++quarter) {
		server.advance(0.25f, [&](const ac::serverStep& step, const ac::serverInput& input) {
			++updates;
			if (step.subtick == 0u) ++tickStarts;
			if (updates == 1u) {
				receivedLook = input.lookX;
				if (!input.forward) {
					std::cerr << "server did not retain latest button state\n";
					std::exit(1);
				}
			}
			if (commandRan && commandObservedSubtick == 255u)
				commandObservedSubtick = step.subtick;
		});
	}

	if (updates != ac::simulationServer::SUBTICKS_PER_SECOND ||
		tickStarts != ac::simulationServer::TICKS_PER_SECOND || server.tick() != 32u) {
		std::cerr << "32 Hz clock did not produce four subticks per tick\n";
		return 2;
	}
	if (std::abs(receivedLook - 5.0f) > 0.0001f) {
		std::cerr << "mouse input was not accumulated before the next subtick\n";
		return 3;
	}
	if (!commandRan || commandObservedSubtick != 3u) {
		std::cerr << "scheduled command did not execute on its target subtick\n";
		return 4;
	}

	std::cout << "32 Hz server clock, subtick input, and command scheduling checks passed\n";
	return 0;
}
