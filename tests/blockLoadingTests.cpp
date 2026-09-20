#define NOMINMAX
#include <assets/assetManager.h>

#include <cmath>
#include <iostream>
#include <memory>
#include <string>

int runChecks() {
    ac::modelManager models;
	models.load("assets/model");

    ac::staticAssetManager blocks;
    blocks.loadValidated("assets/block", models);

    if (blocks.size() < 50 || blocks.getId("water") != 5 || blocks.getId("glass") != 12) {
        std::cerr << "valid block registry did not load as expected\n";
        return 1;
    }

    const ac::blockDefinition* water = blocks.get(5);
    if (!water || water->_renderMode != ac::RENDER_MODE_TRANSPARENT ||
        std::abs(water->_opacity - 0.62f) > 0.0001f || water->_occludes) {
        std::cerr << "water render properties were parsed incorrectly\n";
        return 2;
    }
	const ac::blockDefinition* grass = blocks.get(blocks.getId("grass"));
	if (blocks.texturePaths().size() < 14 || !grass ||
		grass->overlayMaterialForFace(ac::BLOCK_FACE_WEST) == ac::NO_OVERLAY_MATERIAL) {
		std::cerr << "block texture paths were not assigned per face\n";
		return 7;
	}
	const ac::blockDefinition* torch = blocks.get(blocks.getId("torch"));
	if (!torch || std::abs(torch->_emission.position.y - 0.82f) > 0.0001f ||
		!torch->_emission.wallPositionSpecified ||
		std::abs(torch->_emission.wallPosition.x - 0.261f) > 0.0001f ||
		std::abs(torch->_emission.halfExtent.x - 0.075f) > 0.0001f ||
		std::abs(torch->_emission.uvBounds.y - 0.35f) > 0.0001f ||
		std::abs(torch->_emission.uvBounds.w - 0.52f) > 0.0001f) {
		std::cerr << "partial-block emission volume was parsed incorrectly\n";
		return 11;
	}

	ac::staticAssetManager dataDriven;
	dataDriven.loadValidated("tests/fixtures/data_driven_blocks", models, 12);
	const ac::blockDefinition* configured = dataDriven.get(
		dataDriven.getId("diamond_pickaxe_that_is_actually_cloth"));
	if (!configured || configured->_id == 40u) {
		std::cerr << "serialized block id was not replaced with a runtime id\n";
		return 9;
	}
	if (std::abs(configured->_hardness - 7.0f) > 0.0001f ||
		configured->_preferredTool != ac::blockToolClass::none ||
		configured->_heldStyle != ac::heldItemStyle::sprite ||
		configured->_soundMaterial != ac::blockSoundMaterial::cloth ||
		std::abs(configured->_armorReduction - 0.25f) > 0.0001f ||
		!configured->_connectsToPanes || !configured->_flatIcon ||
		std::abs(configured->_lightTransmission.y - 0.4f) > 0.0001f) {
		std::cerr << "block metadata was inferred from its name instead of JSON\n";
		return 8;
	}

	ac::staticAssetManager modBlocks;
	modBlocks.load("tests/fixtures/data_driven_blocks", "testmod");
	const uint32_t modId = modBlocks.getId("testmod:diamond_pickaxe_that_is_actually_cloth");
	if (modId != 1u || modBlocks.getName(modId) !=
		"testmod:diamond_pickaxe_that_is_actually_cloth") {
		std::cerr << "mod block was not namespaced and assigned a compact runtime id\n";
		return 10;
	}

    ac::staticAssetManager invalid;
    std::string message;
    try {
        invalid.load("tests/fixtures/invalid_blocks");
    }
    catch (const std::exception& error) {
        message = error.what();
    }

    if (message.find("blocks[1]") == std::string::npos ||
        message.find("opacity") == std::string::npos) {
        std::cerr << "invalid definition did not report a precise source location\n";
        return 3;
    }
    if (invalid.size() != 0) {
        std::cerr << "failed registry load committed partial block data\n";
        return 4;
    }

	struct failureCase {
		const char* path;
		const char* expected;
	};
	const failureCase failures[] = {
		{ "tests/fixtures/unknown_block_field", "renderMod" },
		{ "tests/fixtures/empty_block_pack", "must not be empty" },
		{ "tests/fixtures/missing_reference", "missing material 99" }
	};

	for (const failureCase& failure : failures) {
		ac::staticAssetManager registry;
		message.clear();
		try {
			registry.loadValidated(failure.path, models, 12);
		}
		catch (const std::exception& error) {
			message = error.what();
		}
		if (message.find(failure.expected) == std::string::npos) {
			std::cerr << "unexpected error for " << failure.path << ": " << message << '\n';
			return 5;
		}
		if (registry.size() != 0) {
			std::cerr << "failed validated load committed block data\n";
			return 6;
		}
	}

    std::cout << "block loading checks passed\n";
    return 0;
}

int main() {
	try {
		return runChecks();
	}
	catch (const std::exception& error) {
		std::cerr << "unexpected block loading exception: " << error.what() << '\n';
		return 100;
	}
}
