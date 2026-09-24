#define NOMINMAX
#include <world/weatherSystem.h>

#include <algorithm>
#include <cmath>
#include <iostream>

int main() {
	ac::weatherSystem weather;
	weather.reset(123456789ULL, 0.0f);
	weather.setForced(ac::weatherKind::rain);

	float wettest = 0.0f;
	float driest = 1.0f;
	float cloudX = 0.0f;
	float cloudZ = 0.0f;
	float thickestCloud = 0.0f;
	for (int32_t z = -2400; z <= 2400; z += 60) {
		for (int32_t x = -2400; x <= 2400; x += 60) {
			const ac::cloudWeatherSample cloud = weather.cloudAt(
				static_cast<float>(x), static_cast<float>(z));
			wettest = std::max(wettest, cloud.precipitation);
			driest = std::min(driest, cloud.precipitation);
			if (cloud.opacity > thickestCloud) {
				thickestCloud = cloud.opacity;
				cloudX = static_cast<float>(x);
				cloudZ = static_cast<float>(z);
			}
		}
	}
	if (wettest < 0.20f || driest > 0.02f) {
		std::cerr << "rain is not localized beneath cloud cells\n";
		return 1;
	}

	// Follow one advected cloud coordinate through a complete moisture cycle.
	float highestMoisture = 0.0f;
	float lowestMoisture = 1.0f;
	for (int32_t second = 0; second <= 110; ++second) {
		weather.reset(123456789ULL, static_cast<float>(second));
		weather.setForced(ac::weatherKind::rain);
		const ac::cloudWeatherSample cloud = weather.cloudAt(
			cloudX - static_cast<float>(second) * 2.15f,
			cloudZ - static_cast<float>(second) * 0.72f);
		if (cloud.opacity < 0.55f) continue;
		highestMoisture = std::max(highestMoisture, cloud.moisture);
		lowestMoisture = std::min(lowestMoisture, cloud.moisture);
	}
	if (highestMoisture < 0.35f || lowestMoisture > 0.12f) {
		std::cerr << "clouds do not charge and deplete over their lifecycle\n";
		return 2;
	}

	std::cout << "localized cloud weather checks passed\n";
	return 0;
}
