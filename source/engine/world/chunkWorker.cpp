#include "chunkWorker.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

#include <cstdio>

namespace ac {

	uint32_t chunkWorker::choosePoolSize() {
		uint32_t hardwareThreads = std::thread::hardware_concurrency();
		if (hardwareThreads == 0u)
			hardwareThreads = 4u;
		// Leave two cores for the render/audio threads; use the rest for world work.
		const uint32_t reserved = hardwareThreads > 2u ? 2u : 1u;
		return std::clamp(hardwareThreads - reserved, 2u, 12u);
	}

	void chunkWorker::nameCurrentThread(uint32_t index, bool preferRemesh) {
#ifdef _WIN32
		wchar_t name[32];
		swprintf_s(name, 32, preferRemesh ? L"chunk-mesh-%u" : L"chunk-gen-%u", index);
		SetThreadDescription(GetCurrentThread(), name);
#else
		(void)index;
		(void)preferRemesh;
#endif
	}

}
