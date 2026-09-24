#pragma once

#include <cstdint>

namespace ac {

	enum class gameScreen : uint8_t {
		mainMenu = 0,
		playing,
		paused,
		settings,
		inventory,
		chest,
		crafting,
		furnace,
		console,
		worldSelect,
		createWorld
	};

	inline const char* gameScreenName(gameScreen screen) {
		switch (screen) {
		case gameScreen::mainMenu: return "Main Menu";
		case gameScreen::playing: return "Playing";
		case gameScreen::paused: return "Paused";
		case gameScreen::settings: return "Settings";
		case gameScreen::inventory: return "Inventory";
		case gameScreen::chest: return "Chest";
		case gameScreen::crafting: return "Crafting";
		case gameScreen::furnace: return "Furnace";
		case gameScreen::console: return "Console";
		case gameScreen::worldSelect: return "Select World";
		case gameScreen::createWorld: return "Create World";
		default: return "Unknown";
		}
	}

	inline bool gameScreenCapturesCursor(gameScreen screen) {
		return screen == gameScreen::playing;
	}

	inline bool gameScreenBlocksWorldInput(gameScreen screen) {
		return screen != gameScreen::playing;
	}

	inline bool gameScreenFreezesSimulation(gameScreen screen) {
		return screen == gameScreen::paused ||
			screen == gameScreen::settings ||
			screen == gameScreen::mainMenu ||
			screen == gameScreen::worldSelect ||
			screen == gameScreen::createWorld;
	}

}
