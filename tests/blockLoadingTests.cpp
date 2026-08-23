#define NOMINMAX
#include <assetManager/assetManager.h>

#include <cmath>
#include <iostream>
#include <memory>
#include <string>

int main() {
    ac::modelManager models;
    models.load(std::make_unique<ac::model>(), 0);

    ac::staticAssetManager blocks;
    blocks.loadValidated("asset/block", models);

    if (blocks.size() != 12 || blocks.getId("water") != 5 || blocks.getId("glass") != 12) {
        std::cerr << "valid block registry did not load as expected\n";
        return 1;
    }

    const ac::blockDefinition* water = blocks.get(5);
    if (!water || water->_renderMode != ac::RENDER_MODE_TRANSPARENT ||
        std::abs(water->_opacity - 0.62f) > 0.0001f || water->_occludes) {
        std::cerr << "water render properties were parsed incorrectly\n";
        return 2;
    }
	if (blocks.texturePaths().size() != 14 ||
		!blocks.get(3) || blocks.get(3)->materialForFace(ac::BLOCK_FACE_UP) ==
		blocks.get(3)->materialForFace(ac::BLOCK_FACE_WEST)) {
		std::cerr << "block texture paths were not assigned per face\n";
		return 7;
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
