#define NOMINMAX
#include <assets/assetManager.h>
#include <assets/contentPack.h>
#include <world/gameplay.h>
#include <world/blockEntities.h>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

int runChecks() {
	const ac::contentPackSet packs = ac::contentPackSet::discover("assets/pack.json", "mods");
	const std::vector<std::string> expectedAreas{
		"biomes", "blocks", "entities", "models", "recipes", "terrainBlocks"
	};
	if (packs.kinds() != expectedAreas || packs.paths("blocks").empty() ||
		packs.singleton("biomes").filename() != "biomes.json") {
		std::cerr << "pack.json content areas were not discovered for resource reload\n";
		return 17;
	}

    ac::modelManager models;
	models.load("assets/model");

    ac::staticAssetManager blocks;
    blocks.loadValidated("assets/block", models);

    size_t definitionFiles = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator("assets/block")) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        const auto document = ac::jsonLoader::load(entry.path().string());
        if (!document.IsObject() || !document.HasMember("name") || document.HasMember("blocks")) {
            std::cerr << "shipped block files must contain exactly one definition\n";
            return 101;
        }
        ++definitionFiles;
		if (entry.path().filename().string() != std::string("core_") + document["name"].GetString() + ".json") {
			std::cerr << "shipped block filename must use its core identifier\n";
			return 103;
		}
    }
    if (definitionFiles != blocks.size()) {
        std::cerr << "block file count differs from the loaded registry\n";
        return 102;
    }

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
	const ac::blockDefinition* woodenAxe = blocks.get(blocks.getId("wooden_axe"));
	const ac::blockDefinition* woodenSword = blocks.get(blocks.getId("wooden_sword"));
	const ac::blockDefinition* woodenPickaxe = blocks.get(blocks.getId("wooden_pickaxe"));
	const ac::blockDefinition* shears = blocks.get(blocks.getId("shears"));
	if (!woodenAxe || !woodenSword || !woodenPickaxe || !shears ||
		woodenAxe->heldStyle() != ac::heldItemStyle::axe ||
		woodenSword->heldStyle() != ac::heldItemStyle::sword ||
		woodenPickaxe->heldStyle() != ac::heldItemStyle::tool ||
		shears->heldStyle() != ac::heldItemStyle::tool || !shears->_item) {
		std::cerr << "weapon held styles were not derived from tool metadata\n";
		return 12;
	}
	ac::recipeBook recipes;
	const bool recipesLoaded = recipes.load(
		"assets/recipes.json",
		[&](const std::string& name) -> std::optional<ac::blockId> {
			try { return static_cast<ac::blockId>(blocks.getId(name)); }
			catch (...) { return std::nullopt; }
		},
		[&](const std::function<void(ac::blockId, const std::string&)>& visit) {
			for (const uint32_t id : blocks.ids()) {
				const ac::blockDefinition* definition = blocks.get(id);
				if (definition) visit(static_cast<ac::blockId>(id), definition->_name);
			}
		});
	const ac::blockId iron = static_cast<ac::blockId>(blocks.getId("iron_ingot"));
	const ac::blockId shearId = static_cast<ac::blockId>(blocks.getId("shears"));
	const ac::blockId shearGrid[4] = { 0, iron, iron, 0 };
	const ac::craftMatch shearRecipe = recipes.match(shearGrid, 2, 2);
	if (!recipesLoaded || !shearRecipe || shearRecipe.result != shearId) {
		std::cerr << "shears item or crafting recipe was not loaded\n";
		return 13;
	}
	const ac::blockId cobblestone = static_cast<ac::blockId>(blocks.getId("cobblestone"));
	const ac::blockId furnaceId = static_cast<ac::blockId>(blocks.getId("furnace"));
	const ac::blockId furnaceGrid[9] = {
		cobblestone, cobblestone, cobblestone,
		cobblestone, 0, cobblestone,
		cobblestone, cobblestone, cobblestone
	};
	const ac::craftMatch furnaceRecipe = recipes.match(furnaceGrid, 3, 3);
	if (!furnaceRecipe || furnaceRecipe.result != furnaceId) {
		std::cerr << "furnace crafting recipe was not loaded\n";
		return 16;
	}
	const ac::blockId ironOre = static_cast<ac::blockId>(blocks.getId("iron_ore"));
	const ac::blockId coal = static_cast<ac::blockId>(blocks.getId("coal"));
	const ac::smeltingRecipe* ironSmelting = recipes.smelting(ironOre);
	if (!ironSmelting || ironSmelting->result != iron ||
		std::abs(ironSmelting->seconds - 10.0f) > 0.0001f ||
		std::abs(recipes.fuelSeconds(coal) - 80.0f) > 0.0001f) {
		std::cerr << "smelting recipes or furnace fuels were not loaded\n";
		return 14;
	}
	ac::blockEntityStore machines(std::filesystem::path{});
	ac::furnaceInventory& furnace = machines.furnaceAt(3, 4, 5);
	furnace.itemIds[ac::furnaceInventory::input] = ironOre;
	furnace.counts[ac::furnaceInventory::input] = 1;
	furnace.itemIds[ac::furnaceInventory::fuel] = coal;
	furnace.counts[ac::furnaceInventory::fuel] = 1;
	machines.updateFurnaces(10.01f,
		[&](uint32_t input, uint32_t& result, uint32_t& count, float& seconds) {
			const ac::smeltingRecipe* recipe = recipes.smelting(input);
			if (!recipe) return false;
			result = recipe->result;
			count = recipe->resultCount;
			seconds = recipe->seconds;
			return true;
		},
		[&](uint32_t fuel) { return recipes.fuelSeconds(fuel); });
	if (furnace.itemIds[ac::furnaceInventory::input] != 0 ||
		furnace.itemIds[ac::furnaceInventory::fuel] != 0 ||
		furnace.itemIds[ac::furnaceInventory::output] != iron ||
		furnace.counts[ac::furnaceInventory::output] != 1 || !furnace.burning()) {
		std::cerr << "furnace did not consume fuel and complete a smelting cycle\n";
		return 15;
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
