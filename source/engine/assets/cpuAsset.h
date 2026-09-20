#pragma once
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include <array>
#include <limits>
#include <DirectXMath.h>
#include <fstream>
#include <filesystem>
#include <unordered_set>
#include <initializer_list>
#include <cctype>
#include <cmath>

#include "gpuAsset.h"

namespace ac {

    struct cpuAsset {
        std::vector<uint32_t> _model;
        std::vector<uint32_t> _material;
        std::vector<uint32_t> _hitbox;
    };

    struct hitbox {
        std::vector<DirectX::XMINT3> _vertex;
    };

    enum RENDER_MODE : uint8_t {
        RENDER_MODE_OPAQUE = 0,
        RENDER_MODE_TRANSPARENT = 1,
        RENDER_MODE_CUTOUT = 2
    };

	enum class blockToolClass : uint8_t {
		none = 0,
		pickaxe,
		shovel,
		axe,
		hoe,
		sword
	};

	enum class heldItemStyle : uint8_t {
		none,
		block,
		slab,
		sprite,
		tool,
		rod,
		cross
	};

	enum class blockSoundMaterial : uint8_t {
		stone = 0,
		wood,
		glass,
		grass,
		gravel,
		sand,
		snow,
		cloth
	};

    // Block pack is a single Texture2DArray at t0. Pack size is no longer tied
    // to the SM5.0 SRV register limit (t0..t127); only the array axis is.
    constexpr uint32_t SRV_BLOCK_TEXTURE_ARRAY = 0;
    constexpr uint32_t MAX_BLOCK_MATERIALS = 2048; // D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION
    // Historical name: max unique block textures / material indices.
    constexpr uint32_t BLOCK_TEXTURE_SLOTS = MAX_BLOCK_MATERIALS;
    constexpr uint32_t MAX_RUNTIME_BLOCK_ID = 0xfffu;

    // Fixed terrain PS resources after the block array. Keep in step with
    // staticPixel.hlsl register(t1)..register(t13).
    constexpr uint32_t SRV_LIGHTS = 1;
    constexpr uint32_t SRV_POINT_SHADOWS = 2;
    constexpr uint32_t SRV_MATERIAL_EMISSIONS = 3;
    constexpr uint32_t SRV_LIGHT_OCCLUDERS = 4;
    constexpr uint32_t SRV_VOXEL_LIGHTS = 5;
    constexpr uint32_t SRV_CELESTIAL_SHADOW = 6;
    constexpr uint32_t SRV_OPAQUE_SCENE_COLOR = 7;
    constexpr uint32_t SRV_OPAQUE_SCENE_DEPTH = 8;
    constexpr uint32_t SRV_REFLECTION_VOXELS = 9;
    constexpr uint32_t SRV_PLANAR_REFLECTION = 10;
    constexpr uint32_t SRV_GRASS_COLORMAP = 11;
    constexpr uint32_t SRV_FOLIAGE_COLORMAP = 12;
    constexpr uint32_t SRV_MATERIAL_PROPERTIES = 13;

    // How a material takes its colour from the biome climate. Grass and
    // foliage index the resource pack colormaps; constant is for species that
    // ignore climate, such as spruce and birch leaves.
    enum TINT_MODE : uint32_t {
        TINT_MODE_NONE = 0,
        TINT_MODE_GRASS = 1,
        TINT_MODE_FOLIAGE = 2,
        TINT_MODE_CONSTANT = 3
    };

    enum class blockInteraction : uint8_t {
        none = 0,
        door = 1,
        chest = 2,
        crafting = 3,
        lever = 4,
        trapdoor = 5,
        fenceGate = 6
    };

	enum class blockPlacementBehavior : uint8_t {
		automatic = 0,
		standard,
		horizontalFacing,
		wallOnly,
		wallOrFloor,
		trapdoor,
		door
	};

	enum class blockUseBehavior : uint8_t {
		automatic = 0,
		none,
		toggleOpen,
		togglePowered,
		container,
		crafting
	};

	enum class blockEntityBehavior : uint8_t {
		automatic = 0,
		none,
		chest
	};

	enum class blockRedstoneBehavior : uint8_t {
		none = 0,
		wire,
		source,
		torch,
		lamp,
		switchSource,
		listener
	};

	// Declarative gameplay hooks. Name references are resolved only after the
	// complete block pack has loaded, so definitions may reference later files.
	struct blockBehaviorDefinition {
		blockPlacementBehavior placement = blockPlacementBehavior::automatic;
		blockUseBehavior use = blockUseBehavior::automatic;
		blockEntityBehavior entity = blockEntityBehavior::automatic;
		blockRedstoneBehavior redstone = blockRedstoneBehavior::none;
		std::string placeAsName;
		std::string dropName;
		uint32_t placeAs = 0;
		uint32_t drop = 0;
		uint32_t dropCount = 1;
		uint8_t redstoneOutput = 0;
		bool poweredOnPlace = false;
		bool dropSpecified = false;
	};

    constexpr uint32_t NO_OVERLAY_MATERIAL = UINT32_MAX;

    // One entry per block texture slot, consumed by staticPixel.hlsl.
    // Layout must stay 16-byte aligned to match the HLSL StructuredBuffer.
    struct materialProperties {
        DirectX::XMFLOAT4 tintColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        uint32_t tintMode = TINT_MODE_NONE;
        uint32_t overlayMaterial = NO_OVERLAY_MATERIAL;
        uint32_t overlayTintMode = TINT_MODE_NONE;
        uint32_t frameCount = 1;
        float frameTime = 0.05f;
        uint32_t interpolate = 0;
        float roughness = 0.8f;
        float metallic = 0.0f;
		DirectX::XMFLOAT4 emissionUvBounds{ 0.0f, 0.0f, 1.0f, 1.0f };
    };

    struct shader {
        std::string _vertexPath;
        std::string _pixelPath;

        gpuShader _gpu;
    };

    struct material {
        uint32_t _id;

        uint32_t _shader;

        uint32_t _albedoTexture;
        uint32_t _normalTexture;
        uint32_t _roughnessTexture;
        uint32_t _metallicTexture;

        float _roughness = 1.0f;
        float _metallic = 0.0f;

        float _opacity = 1.0f;
        RENDER_MODE _renderMode = RENDER_MODE_OPAQUE;
        bool _doubleSided = false;

        uint32_t _renderLayer = 0;

        gpuMaterial _gpu;
    };

    struct keyframe {
        float _time = 0.0f;
        DirectX::XMFLOAT3 _position = {};
        DirectX::XMFLOAT4 _rotation = {};
        DirectX::XMFLOAT3 _scale = { 1.0f, 1.0f, 1.0f };
    };

    struct animationChannel {
        uint32_t _boneId = 0;
        std::vector<keyframe> _keyframes;
    };

    struct animation {
        float _duration = 0.0f;
        float _ticksPerSecond = 24.0f;
        std::vector<animationChannel> _channels;
        bool _loop = true;
    };

    struct bone {
        uint32_t _id = 0;
        std::string _name;
        int32_t _parent = -1;

        DirectX::XMMATRIX _offset;
        DirectX::XMMATRIX _localTransform;
        DirectX::XMMATRIX _globalTransform;
    };

    struct skeleton {
        std::vector<bone> _bone;
        uint32_t _rootBone = 0;
    };

    enum DIRECTION : uint8_t {
        DIR_NULL = 1 << 0,
        DIR_NORTH = 1 << 1,
        DIR_EAST = 1 << 2,
        DIR_SOUTH = 1 << 3,
        DIR_WEST = 1 << 4,
        DIR_UP = 1 << 5,
        DIR_DOWN = 1 << 6,
    };

    inline DIRECTION operator|(DIRECTION a, DIRECTION b) {
        return static_cast<DIRECTION>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    struct staticAsset : public cpuAsset {
        DIRECTION _direction;
    };

    struct dynamicAsset : public cpuAsset {
        DirectX::XMFLOAT4 _rotationQuaternion;
        uint32_t _animation;
    };

    struct blockLight {
        DirectX::XMFLOAT3 color = { 0,0,0 };
        float intensity = 0.0f;
        float radius = 0.0f;
		// Local source volume. Full luminous cubes retain the legacy nearly-full
		// block default; detail blocks can put a small light at their emitting part.
		DirectX::XMFLOAT3 position = { 0.5f, 0.5f, 0.5f };
		DirectX::XMFLOAT3 wallPosition = { 0.5f, 0.5f, 0.5f };
		DirectX::XMFLOAT3 halfExtent = { 0.49f, 0.49f, 0.49f };
		// Normalized texture region that visibly glows. This is independent of
		// the physical source volume so a flame can glow without the wooden stem.
		DirectX::XMFLOAT4 uvBounds = { 0.0f, 0.0f, 1.0f, 1.0f };
		bool wallPositionSpecified = false;
    };

    // These values intentionally match chunkMesher's six face directions.
    enum BLOCK_FACE : uint8_t {
        BLOCK_FACE_WEST = 0,   // -X
        BLOCK_FACE_EAST = 1,   // +X
        BLOCK_FACE_DOWN = 2,   // -Y
        BLOCK_FACE_UP = 3,     // +Y
        BLOCK_FACE_NORTH = 4,  // -Z
        BLOCK_FACE_SOUTH = 5,  // +Z
        BLOCK_FACE_COUNT = 6
    };

    struct blockDefinition {
        static constexpr uint32_t INHERIT_MATERIAL =
            std::numeric_limits<uint32_t>::max();

        uint32_t _id = 0;
		// Optional pre-registry id from older data files. It is used only to
		// migrate saves made before names became the persistent identity.
		uint32_t _legacyId = 0;
        bool _occludes = false;
        bool _solid = false;
        bool _gravity = false;
        bool _item = false;
        RENDER_MODE _renderMode = RENDER_MODE_OPAQUE;
        float _opacity = 1.0f;
        float _roughness = 1.0f;
        float _metallic = 0.0f;
        blockInteraction _interaction = blockInteraction::none;
		blockBehaviorDefinition _behavior;
        std::string _name;
		std::string _texture;
		std::array<std::string, BLOCK_FACE_COUNT> _faceTextures;
		std::array<std::string, BLOCK_FACE_COUNT> _faceOverlays;
		std::array<uint32_t, BLOCK_FACE_COUNT> _faceOverlayMaterials = {
			NO_OVERLAY_MATERIAL,
			NO_OVERLAY_MATERIAL,
			NO_OVERLAY_MATERIAL,
			NO_OVERLAY_MATERIAL,
			NO_OVERLAY_MATERIAL,
			NO_OVERLAY_MATERIAL
		};
		std::array<TINT_MODE, BLOCK_FACE_COUNT> _faceTints = {
			TINT_MODE_NONE,
			TINT_MODE_NONE,
			TINT_MODE_NONE,
			TINT_MODE_NONE,
			TINT_MODE_NONE,
			TINT_MODE_NONE
		};
		DirectX::XMFLOAT4 _tintColor{ 1.0f, 1.0f, 1.0f, 1.0f };

        uint32_t _material = 0;
        std::array<uint32_t, BLOCK_FACE_COUNT> _faceMaterials = {
            INHERIT_MATERIAL,
            INHERIT_MATERIAL,
            INHERIT_MATERIAL,
            INHERIT_MATERIAL,
            INHERIT_MATERIAL,
            INHERIT_MATERIAL
        };
        uint32_t _model = 0;
        uint32_t _hitbox = 0;

        blockLight _emission;

		float _hardness = 1.0f;
		int _harvestLevel = 0;
		blockToolClass _preferredTool = blockToolClass::none;
		bool _requiresTool = false;
		int _toolHarvestLevel = -1;
		float _miningSpeed = 1.0f;
		float _foodHunger = 0.0f;
		float _foodSaturation = 0.0f;
		float _armorReduction = 0.0f;
		heldItemStyle _heldStyle = heldItemStyle::block;
		blockSoundMaterial _soundMaterial = blockSoundMaterial::stone;
		DirectX::XMFLOAT3 _lightTransmission{ 1.0f, 1.0f, 1.0f };
		bool _connectsToPanes = false;
		bool _flatIcon = false;

		bool isFood() const { return _foodHunger > 0.0f || _foodSaturation > 0.0f; }
		bool isMiningTool() const { return _item && _preferredTool != blockToolClass::none && _toolHarvestLevel >= 0; }

		heldItemStyle heldStyle() const { return _heldStyle; }

        uint32_t materialForFace(uint32_t face) const {
            if (face >= _faceMaterials.size() ||
                _faceMaterials[face] == INHERIT_MATERIAL)
                return _material;

            return _faceMaterials[face];
        }

		uint32_t materialForNormal(const DirectX::XMFLOAT3& normal) const {
			const float ax = std::abs(normal.x);
			const float ay = std::abs(normal.y);
			const float az = std::abs(normal.z);
			if (ay >= ax && ay >= az)
				return materialForFace(normal.y >= 0.0f ? BLOCK_FACE_UP : BLOCK_FACE_DOWN);
			if (ax >= az)
				return materialForFace(normal.x >= 0.0f ? BLOCK_FACE_EAST : BLOCK_FACE_WEST);
			return materialForFace(normal.z >= 0.0f ? BLOCK_FACE_SOUTH : BLOCK_FACE_NORTH);
		}

        void setFaceMaterial(BLOCK_FACE face, uint32_t material) {
            _faceMaterials[static_cast<size_t>(face)] = material;
        }

		const std::string& textureForFace(uint32_t face) const {
			if (face < _faceTextures.size() && !_faceTextures[face].empty())
				return _faceTextures[face];
			return _texture;
		}

		bool hasTextures() const {
			if (!_texture.empty()) return true;
			return std::any_of(_faceTextures.begin(), _faceTextures.end(), [](const std::string& path) {
				return !path.empty();
			});
		}

		const std::string& overlayForFace(uint32_t face) const {
			static const std::string empty;
			return face < _faceOverlays.size() ? _faceOverlays[face] : empty;
		}

		uint32_t overlayMaterialForFace(uint32_t face) const {
			return face < _faceOverlayMaterials.size()
				? _faceOverlayMaterials[face]
				: NO_OVERLAY_MATERIAL;
		}

		void setFaceOverlayMaterial(uint32_t face, uint32_t material) {
			if (face < _faceOverlayMaterials.size())
				_faceOverlayMaterials[face] = material;
		}

		TINT_MODE tintForFace(uint32_t face) const {
			return face < _faceTints.size() ? _faceTints[face] : TINT_MODE_NONE;
		}

        void load(const rapidjson::Value& doc) {
            if (!doc.IsObject())
                throw std::runtime_error("Block definition must be a JSON object");

			auto validateMembers = [](
				const rapidjson::Value& object,
				std::initializer_list<const char*> allowed,
				const char* section
			) {
				std::unordered_set<std::string> seen;
				for (auto member = object.MemberBegin(); member != object.MemberEnd(); ++member) {
					const std::string name(member->name.GetString(), member->name.GetStringLength());
					if (!seen.insert(name).second)
						throw std::runtime_error(std::string("Duplicate ") + section + " field '" + name + "'");
					const bool known = std::find_if(allowed.begin(), allowed.end(), [&](const char* item) {
						return name == item;
						}) != allowed.end();
					if (!known)
						throw std::runtime_error(std::string("Unknown ") + section + " field '" + name + "'");
				}
			};

			validateMembers(doc, {
				"id", "name", "material", "model", "hitbox", "solid", "occludes",
				"gravity", "faceMaterials", "texture", "textures", "overlay", "tint",
				"roughness", "metallic", "renderMode", "opacity", "emission",
				"interaction", "item", "hardness", "harvestLevel", "toolHarvestLevel",
				"tool", "requiresTool", "miningSpeed", "food", "armorReduction",
				"heldStyle", "soundMaterial", "lightTransmission", "connectsToPanes",
				"flatIcon", "behavior"
				}, "block");

            auto requiredUint = [&](const char* key) -> uint32_t {
                if (!doc.HasMember(key) || !doc[key].IsUint())
                    throw std::runtime_error(std::string("Block field '") + key + "' must be an unsigned integer");
                return doc[key].GetUint();
            };
            auto optionalUint = [&](const char* key, uint32_t fallback) -> uint32_t {
                if (!doc.HasMember(key)) return fallback;
                if (!doc[key].IsUint())
                    throw std::runtime_error(std::string("Block field '") + key + "' must be an unsigned integer");
                return doc[key].GetUint();
            };
            auto optionalBool = [&](const char* key, bool fallback) -> bool {
                if (!doc.HasMember(key)) return fallback;
                if (!doc[key].IsBool())
                    throw std::runtime_error(std::string("Block field '") + key + "' must be true or false");
                return doc[key].GetBool();
            };

			_legacyId = optionalUint("id", 0u);
			_id = 0u; // Registries assign process-local ids in load order.
            if (!doc.HasMember("name") || !doc["name"].IsString() || doc["name"].GetStringLength() == 0)
                throw std::runtime_error("Block field 'name' must be a non-empty string");
            _name = doc["name"].GetString();
			if (_name.size() > 64)
				throw std::runtime_error("Block name must not exceed 64 characters");
			for (const unsigned char character : _name) {
				const bool asciiLower = character >= 'a' && character <= 'z';
				const bool asciiDigit = character >= '0' && character <= '9';
				if (!(asciiLower || asciiDigit ||
					character == '_' || character == '-' || character == '.' || character == ':'))
					throw std::runtime_error("Block name must use lowercase letters, digits, '_', '-', '.', or ':'");
			}

            _material = optionalUint("material", 0);
            _model = optionalUint("model", 0);
            _hitbox = optionalUint("hitbox", 0);
            _solid = optionalBool("solid", true);
            _gravity = optionalBool("gravity", false);
            _item = optionalBool("item", false);

            _faceMaterials.fill(INHERIT_MATERIAL);
			_faceTextures.fill({});

			auto texturePath = [](const rapidjson::Value& value, const std::string& field) {
				if (!value.IsString() || value.GetStringLength() == 0)
					throw std::runtime_error("Block texture field '" + field + "' must be a non-empty string");
				return std::string(value.GetString(), value.GetStringLength());
			};
			if (doc.HasMember("texture"))
				_texture = texturePath(doc["texture"], "texture");
			if (doc.HasMember("textures")) {
				const auto& faces = doc["textures"];
				if (!faces.IsObject())
					throw std::runtime_error("Block field 'textures' must be an object");
				validateMembers(faces, {
					"all", "side", "west", "east", "bottom", "top", "north", "south"
					}, "textures");
				if (faces.HasMember("all")) _texture = texturePath(faces["all"], "textures.all");
				auto setTexture = [&](const char* name, BLOCK_FACE face) {
					if (faces.HasMember(name))
						_faceTextures[static_cast<size_t>(face)] = texturePath(faces[name], std::string("textures.") + name);
				};
				if (faces.HasMember("side")) {
					const std::string side = texturePath(faces["side"], "textures.side");
					_faceTextures[BLOCK_FACE_WEST] = side;
					_faceTextures[BLOCK_FACE_EAST] = side;
					_faceTextures[BLOCK_FACE_NORTH] = side;
					_faceTextures[BLOCK_FACE_SOUTH] = side;
				}
				setTexture("west", BLOCK_FACE_WEST);
				setTexture("east", BLOCK_FACE_EAST);
				setTexture("bottom", BLOCK_FACE_DOWN);
				setTexture("top", BLOCK_FACE_UP);
				setTexture("north", BLOCK_FACE_NORTH);
				setTexture("south", BLOCK_FACE_SOUTH);
			}
			if (hasTextures()) {
				for (uint32_t face = 0; face < BLOCK_FACE_COUNT; ++face) {
					if (textureForFace(face).empty())
						throw std::runtime_error("Block textures do not define every face (add 'texture', 'all', or the missing face)");
				}
			}

			_faceOverlays.fill({});
			if (doc.HasMember("overlay")) {
				const auto& faces = doc["overlay"];
				if (!faces.IsObject())
					throw std::runtime_error("Block field 'overlay' must be an object");
				validateMembers(faces, {
					"all", "side", "west", "east", "bottom", "top", "north", "south"
					}, "overlay");
				if (faces.HasMember("all"))
					_faceOverlays.fill(texturePath(faces["all"], "overlay.all"));
				if (faces.HasMember("side")) {
					const std::string side = texturePath(faces["side"], "overlay.side");
					_faceOverlays[BLOCK_FACE_WEST] = side;
					_faceOverlays[BLOCK_FACE_EAST] = side;
					_faceOverlays[BLOCK_FACE_NORTH] = side;
					_faceOverlays[BLOCK_FACE_SOUTH] = side;
				}
				auto setOverlay = [&](const char* name, BLOCK_FACE face) {
					if (faces.HasMember(name))
						_faceOverlays[static_cast<size_t>(face)] =
							texturePath(faces[name], std::string("overlay.") + name);
				};
				setOverlay("west", BLOCK_FACE_WEST);
				setOverlay("east", BLOCK_FACE_EAST);
				setOverlay("bottom", BLOCK_FACE_DOWN);
				setOverlay("top", BLOCK_FACE_UP);
				setOverlay("north", BLOCK_FACE_NORTH);
				setOverlay("south", BLOCK_FACE_SOUTH);
				if (!hasTextures())
					throw std::runtime_error("Block field 'overlay' requires textures to overlay");
			}

			// A face tint colours the topmost layer of that face: the overlay
			// when one is present, otherwise the base texture. That keeps a
			// shared texture such as dirt untinted under a grass block.
			_faceTints.fill(TINT_MODE_NONE);
			if (doc.HasMember("tint")) {
				auto parseTint = [&](const rapidjson::Value& value, const std::string& field) {
					if (value.IsString()) {
						const std::string name(value.GetString(), value.GetStringLength());
						if (name == "none") return TINT_MODE_NONE;
						if (name == "grass") return TINT_MODE_GRASS;
						if (name == "foliage") return TINT_MODE_FOLIAGE;
						throw std::runtime_error("Unknown block tint '" + name + "' in '" + field + "'");
					}
					if (!value.IsObject())
						throw std::runtime_error(
							"Block tint '" + field + "' must be a tint name or an rgb object");
					validateMembers(value, { "r", "g", "b", "strength" }, "tint");
					auto channel = [&](const char* key) {
						if (!value.HasMember(key) || !value[key].IsNumber())
							throw std::runtime_error(
								"Block tint '" + field + "' is missing numeric '" + key + "'");
						const float channelValue = value[key].GetFloat();
						if (!std::isfinite(channelValue) || channelValue < 0.0f || channelValue > 4.0f)
							throw std::runtime_error(
								"Block tint '" + field + "' channel must be between 0 and 4");
						return channelValue;
					};
					_tintColor = { channel("r"), channel("g"), channel("b"), 1.0f };
					if (value.HasMember("strength")) {
						if (!value["strength"].IsNumber())
							throw std::runtime_error("Block tint 'strength' must be a number");
						const float strength = value["strength"].GetFloat();
						if (!std::isfinite(strength) || strength < 0.0f || strength > 1.0f)
							throw std::runtime_error("Block tint 'strength' must be between 0 and 1");
						_tintColor.w = strength;
					}
					return TINT_MODE_CONSTANT;
				};

				const auto& tint = doc["tint"];
				if (tint.IsString() || (tint.IsObject() && tint.HasMember("r"))) {
					_faceTints.fill(parseTint(tint, "tint"));
				}
				else if (tint.IsObject()) {
					validateMembers(tint, {
						"all", "side", "west", "east", "bottom", "top", "north", "south"
						}, "tint");
					if (tint.HasMember("all"))
						_faceTints.fill(parseTint(tint["all"], "tint.all"));
					if (tint.HasMember("side")) {
						const TINT_MODE side = parseTint(tint["side"], "tint.side");
						_faceTints[BLOCK_FACE_WEST] = side;
						_faceTints[BLOCK_FACE_EAST] = side;
						_faceTints[BLOCK_FACE_NORTH] = side;
						_faceTints[BLOCK_FACE_SOUTH] = side;
					}
					auto setTint = [&](const char* name, BLOCK_FACE face) {
						if (tint.HasMember(name))
							_faceTints[static_cast<size_t>(face)] =
								parseTint(tint[name], std::string("tint.") + name);
					};
					setTint("west", BLOCK_FACE_WEST);
					setTint("east", BLOCK_FACE_EAST);
					setTint("bottom", BLOCK_FACE_DOWN);
					setTint("top", BLOCK_FACE_UP);
					setTint("north", BLOCK_FACE_NORTH);
					setTint("south", BLOCK_FACE_SOUTH);
				}
				else {
					throw std::runtime_error("Block field 'tint' must be a string or an object");
				}
			}

			auto unitFloat = [&](const char* key, float fallback) {
				if (!doc.HasMember(key)) return fallback;
				if (!doc[key].IsNumber())
					throw std::runtime_error(std::string("Block field '") + key + "' must be a number");
				const float value = doc[key].GetFloat();
				if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
					throw std::runtime_error(std::string("Block field '") + key + "' must be between 0 and 1");
				return value;
			};
			_roughness = unitFloat("roughness", 1.0f);
			_metallic = unitFloat("metallic", 0.0f);
			if (doc.HasMember("interaction")) {
				if (!doc["interaction"].IsString())
					throw std::runtime_error("Block field 'interaction' must be a string");
				const std::string interaction = doc["interaction"].GetString();
				if (interaction == "none") _interaction = blockInteraction::none;
				else if (interaction == "door") _interaction = blockInteraction::door;
				else if (interaction == "chest") _interaction = blockInteraction::chest;
				else if (interaction == "crafting") _interaction = blockInteraction::crafting;
				else if (interaction == "lever") _interaction = blockInteraction::lever;
				else if (interaction == "trapdoor") _interaction = blockInteraction::trapdoor;
				else if (interaction == "fence_gate") _interaction = blockInteraction::fenceGate;
				else
					throw std::runtime_error(
						"Block field 'interaction' must be none, door, chest, crafting, lever, trapdoor, or fence_gate");
			}
			if (doc.HasMember("behavior")) {
				const auto& behavior = doc["behavior"];
				if (!behavior.IsObject())
					throw std::runtime_error("Block field 'behavior' must be an object");
				validateMembers(behavior, {
					"placement", "use", "entity", "placeAs", "drop", "dropCount",
					"redstone", "redstoneOutput", "poweredOnPlace"
					}, "behavior");

				auto behaviorString = [&](const char* key) {
					if (!behavior[key].IsString() || behavior[key].GetStringLength() == 0)
						throw std::runtime_error(std::string("Block behavior '") + key + "' must be a non-empty string");
					return std::string(behavior[key].GetString(), behavior[key].GetStringLength());
				};
				if (behavior.HasMember("placement")) {
					const std::string value = behaviorString("placement");
					if (value == "standard") _behavior.placement = blockPlacementBehavior::standard;
					else if (value == "horizontal_facing") _behavior.placement = blockPlacementBehavior::horizontalFacing;
					else if (value == "wall_only") _behavior.placement = blockPlacementBehavior::wallOnly;
					else if (value == "wall_or_floor") _behavior.placement = blockPlacementBehavior::wallOrFloor;
					else if (value == "trapdoor") _behavior.placement = blockPlacementBehavior::trapdoor;
					else if (value == "door") _behavior.placement = blockPlacementBehavior::door;
					else throw std::runtime_error("Unknown block behavior placement '" + value + "'");
				}
				if (behavior.HasMember("use")) {
					const std::string value = behaviorString("use");
					if (value == "none") _behavior.use = blockUseBehavior::none;
					else if (value == "toggle_open") _behavior.use = blockUseBehavior::toggleOpen;
					else if (value == "toggle_powered") _behavior.use = blockUseBehavior::togglePowered;
					else if (value == "container") _behavior.use = blockUseBehavior::container;
					else if (value == "crafting") _behavior.use = blockUseBehavior::crafting;
					else throw std::runtime_error("Unknown block behavior use '" + value + "'");
				}
				if (behavior.HasMember("entity")) {
					const std::string value = behaviorString("entity");
					if (value == "none") _behavior.entity = blockEntityBehavior::none;
					else if (value == "chest") _behavior.entity = blockEntityBehavior::chest;
					else throw std::runtime_error("Unknown block behavior entity '" + value + "'");
				}
				if (behavior.HasMember("redstone")) {
					const std::string value = behaviorString("redstone");
					if (value == "none") _behavior.redstone = blockRedstoneBehavior::none;
					else if (value == "wire") _behavior.redstone = blockRedstoneBehavior::wire;
					else if (value == "source") _behavior.redstone = blockRedstoneBehavior::source;
					else if (value == "torch") _behavior.redstone = blockRedstoneBehavior::torch;
					else if (value == "lamp") _behavior.redstone = blockRedstoneBehavior::lamp;
					else if (value == "switch") _behavior.redstone = blockRedstoneBehavior::switchSource;
					else if (value == "listener") _behavior.redstone = blockRedstoneBehavior::listener;
					else throw std::runtime_error("Unknown block behavior redstone role '" + value + "'");
				}
				if (behavior.HasMember("placeAs"))
					_behavior.placeAsName = behaviorString("placeAs");
				if (behavior.HasMember("drop")) {
					_behavior.dropName = behaviorString("drop");
					_behavior.dropSpecified = true;
				}
				if (behavior.HasMember("dropCount")) {
					if (!behavior["dropCount"].IsUint() || behavior["dropCount"].GetUint() > 64u)
						throw std::runtime_error("Block behavior 'dropCount' must be an unsigned integer from 0 to 64");
					_behavior.dropCount = behavior["dropCount"].GetUint();
				}
				if (behavior.HasMember("redstoneOutput")) {
					if (!behavior["redstoneOutput"].IsUint() || behavior["redstoneOutput"].GetUint() > 15u)
						throw std::runtime_error("Block behavior 'redstoneOutput' must be an unsigned integer from 0 to 15");
					_behavior.redstoneOutput = static_cast<uint8_t>(behavior["redstoneOutput"].GetUint());
				}
				if (behavior.HasMember("poweredOnPlace")) {
					if (!behavior["poweredOnPlace"].IsBool())
						throw std::runtime_error("Block behavior 'poweredOnPlace' must be true or false");
					_behavior.poweredOnPlace = behavior["poweredOnPlace"].GetBool();
				}
				if (!_behavior.dropSpecified && behavior.HasMember("dropCount"))
					throw std::runtime_error("Block behavior 'dropCount' requires 'drop'");
			}
			// Hinged panels remain solid blocks while their collision geometry
			// rotates between the closed and open orientations.
			if (_interaction == blockInteraction::door ||
				_interaction == blockInteraction::trapdoor)
				_solid = true;

            if (doc.HasMember("faceMaterials") && !doc["faceMaterials"].IsObject())
                throw std::runtime_error("Block field 'faceMaterials' must be an object");

            if (doc.HasMember("faceMaterials")) {
                const auto& faces = doc["faceMaterials"];
				validateMembers(faces, {
					"all", "side", "west", "east", "bottom", "top", "north", "south"
					}, "faceMaterials");

                if (faces.HasMember("all")) {
					if (!faces["all"].IsUint())
						throw std::runtime_error("Block face material 'all' must be an unsigned integer");
					_material = faces["all"].GetUint();
				}

                if (faces.HasMember("side")) {
                    if (!faces["side"].IsUint())
                        throw std::runtime_error("Block face material 'side' must be an unsigned integer");
                    const uint32_t side = faces["side"].GetUint();
                    setFaceMaterial(BLOCK_FACE_WEST, side);
                    setFaceMaterial(BLOCK_FACE_EAST, side);
                    setFaceMaterial(BLOCK_FACE_NORTH, side);
                    setFaceMaterial(BLOCK_FACE_SOUTH, side);
                }

                auto loadFace = [&](const char* name, BLOCK_FACE face) {
                    if (faces.HasMember(name)) {
                        if (!faces[name].IsUint())
                            throw std::runtime_error(std::string("Block face material '") + name + "' must be an unsigned integer");
                        setFaceMaterial(face, faces[name].GetUint());
                    }
                    };

                loadFace("west", BLOCK_FACE_WEST);
                loadFace("east", BLOCK_FACE_EAST);
                loadFace("bottom", BLOCK_FACE_DOWN);
                loadFace("top", BLOCK_FACE_UP);
                loadFace("north", BLOCK_FACE_NORTH);
                loadFace("south", BLOCK_FACE_SOUTH);
            }

            if (doc.HasMember("renderMode")) {
                const auto& mode = doc["renderMode"];
                if (mode.IsString()) {
                    const std::string value = mode.GetString();
                    if (value == "opaque") _renderMode = RENDER_MODE_OPAQUE;
                    else if (value == "transparent" || value == "translucent") _renderMode = RENDER_MODE_TRANSPARENT;
                    else if (value == "cutout") _renderMode = RENDER_MODE_CUTOUT;
                    else throw std::runtime_error("Unknown block renderMode: " + value);
                }
                else if (mode.IsUint() && mode.GetUint() <= RENDER_MODE_CUTOUT) {
                    _renderMode = static_cast<RENDER_MODE>(mode.GetUint());
                }
                else {
                    throw std::runtime_error("Block field 'renderMode' must be opaque, translucent, transparent, cutout, or 0-2");
                }
            }

			if (doc.HasMember("opacity")) {
				if (!doc["opacity"].IsNumber())
					throw std::runtime_error("Block field 'opacity' must be a number");
				_opacity = doc["opacity"].GetFloat();
				if (!std::isfinite(_opacity) || _opacity < 0.0f || _opacity > 1.0f)
					throw std::runtime_error("Block opacity must be between 0 and 1");
			}
			if (_renderMode != RENDER_MODE_TRANSPARENT && _opacity != 1.0f)
				throw std::runtime_error("Only translucent blocks may use opacity below 1");

			_occludes = optionalBool("occludes", _renderMode != RENDER_MODE_TRANSPARENT);
			if (_renderMode == RENDER_MODE_TRANSPARENT && _occludes)
				throw std::runtime_error("Translucent blocks cannot occlude neighboring faces");

            if (doc.HasMember("emission")) {
                const auto& emission = doc["emission"];
				if (!emission.IsObject())
					throw std::runtime_error("Block field 'emission' must be an object");
				validateMembers(emission, {
					"r", "g", "b", "intensity", "radius",
					"position", "wallPosition", "halfExtent", "uvBounds"
				}, "emission");
				auto emissionValue = [&](const char* key) {
					if (!emission.HasMember(key)) return 0.0f;
					if (!emission[key].IsNumber())
						throw std::runtime_error(std::string("Block emission field '") + key + "' must be a number");
					const float value = emission[key].GetFloat();
					if (!std::isfinite(value) || value < 0.0f)
						throw std::runtime_error(std::string("Block emission field '") + key + "' must be finite and non-negative");
					return value;
				};

                _emission.color.x = emissionValue("r");
                _emission.color.y = emissionValue("g");
                _emission.color.z = emissionValue("b");
				_emission.intensity = emissionValue("intensity");
				_emission.radius = emissionValue("radius");
				auto emissionVector = [&](const char* key, DirectX::XMFLOAT3 fallback,
					float minimum, float maximum) {
					if (!emission.HasMember(key)) return fallback;
					const auto& vector = emission[key];
					if (!vector.IsArray() || vector.Size() != 3)
						throw std::runtime_error(std::string("Block emission field '") + key + "' must be a 3-number array");
					DirectX::XMFLOAT3 result{};
					float* channels[] = { &result.x, &result.y, &result.z };
					for (rapidjson::SizeType index = 0; index < 3; ++index) {
						if (!vector[index].IsNumber())
							throw std::runtime_error(std::string("Block emission field '") + key + "' must contain numbers");
						const float channel = vector[index].GetFloat();
						if (!std::isfinite(channel) || channel < minimum || channel > maximum)
							throw std::runtime_error(std::string("Block emission field '") + key + "' is outside its valid range");
						*channels[index] = channel;
					}
					return result;
				};
				_emission.position = emissionVector("position", _emission.position, 0.0f, 1.0f);
				_emission.wallPositionSpecified = emission.HasMember("wallPosition");
				_emission.wallPosition = emissionVector(
					"wallPosition", _emission.position, 0.0f, 1.0f);
				_emission.halfExtent = emissionVector(
					"halfExtent", _emission.halfExtent, 0.0f, 0.5f);
				if (emission.HasMember("uvBounds")) {
					const auto& bounds = emission["uvBounds"];
					if (!bounds.IsArray() || bounds.Size() != 4)
						throw std::runtime_error("Block emission field 'uvBounds' must be a 4-number array");
					float* channels[] = { &_emission.uvBounds.x, &_emission.uvBounds.y,
						&_emission.uvBounds.z, &_emission.uvBounds.w };
					for (rapidjson::SizeType index = 0; index < 4; ++index) {
						if (!bounds[index].IsNumber())
							throw std::runtime_error("Block emission field 'uvBounds' must contain numbers");
						const float channel = bounds[index].GetFloat();
						if (!std::isfinite(channel) || channel < 0.0f || channel > 1.0f)
							throw std::runtime_error("Block emission field 'uvBounds' must be between 0 and 1");
						*channels[index] = channel;
					}
					if (_emission.uvBounds.x > _emission.uvBounds.z ||
						_emission.uvBounds.y > _emission.uvBounds.w)
						throw std::runtime_error("Block emission field 'uvBounds' minimum exceeds maximum");
				}
            }

			if (doc.HasMember("hardness")) {
				if (!doc["hardness"].IsNumber())
					throw std::runtime_error("Block field 'hardness' must be a number");
				_hardness = doc["hardness"].GetFloat();
				if (!std::isfinite(_hardness) || _hardness < -1.0f || _hardness > 1000.0f)
					throw std::runtime_error("Block hardness must be between -1 and 1000");
			}
			if (doc.HasMember("harvestLevel")) {
				if (!doc["harvestLevel"].IsInt())
					throw std::runtime_error("Block field 'harvestLevel' must be an integer");
				_harvestLevel = doc["harvestLevel"].GetInt();
				if (_harvestLevel < 0 || _harvestLevel > 4)
					throw std::runtime_error("Block harvestLevel must be between 0 and 4");
				if (_item)
					_toolHarvestLevel = _harvestLevel;
			}
			if (doc.HasMember("toolHarvestLevel")) {
				if (!doc["toolHarvestLevel"].IsInt())
					throw std::runtime_error("Block field 'toolHarvestLevel' must be an integer");
				_toolHarvestLevel = doc["toolHarvestLevel"].GetInt();
				if (_toolHarvestLevel < 0 || _toolHarvestLevel > 4)
					throw std::runtime_error("Block toolHarvestLevel must be between 0 and 4");
			}
			if (doc.HasMember("tool")) {
				if (!doc["tool"].IsString())
					throw std::runtime_error("Block field 'tool' must be a string");
				const std::string tool = doc["tool"].GetString();
				if (tool == "none") _preferredTool = blockToolClass::none;
				else if (tool == "pickaxe") _preferredTool = blockToolClass::pickaxe;
				else if (tool == "shovel") _preferredTool = blockToolClass::shovel;
				else if (tool == "axe") _preferredTool = blockToolClass::axe;
				else if (tool == "hoe") _preferredTool = blockToolClass::hoe;
				else if (tool == "sword") _preferredTool = blockToolClass::sword;
				else throw std::runtime_error("Block field 'tool' must be none, pickaxe, shovel, axe, hoe, or sword");
			}
			if (doc.HasMember("requiresTool"))
				_requiresTool = optionalBool("requiresTool", _requiresTool);
			if (doc.HasMember("miningSpeed")) {
				if (!doc["miningSpeed"].IsNumber())
					throw std::runtime_error("Block field 'miningSpeed' must be a number");
				_miningSpeed = doc["miningSpeed"].GetFloat();
				if (!std::isfinite(_miningSpeed) || _miningSpeed < 0.1f || _miningSpeed > 64.0f)
					throw std::runtime_error("Block miningSpeed must be between 0.1 and 64");
				if (_item && _preferredTool != blockToolClass::none && _toolHarvestLevel < 0)
					_toolHarvestLevel = 0;
			}
			if (doc.HasMember("food")) {
				const auto& food = doc["food"];
				if (food.IsNumber()) {
					_foodHunger = food.GetFloat();
					_foodSaturation = _foodHunger * 0.6f;
				}
				else if (food.IsObject()) {
					validateMembers(food, { "hunger", "saturation" }, "food");
					if (food.HasMember("hunger")) {
						if (!food["hunger"].IsNumber())
							throw std::runtime_error("Block food.hunger must be a number");
						_foodHunger = food["hunger"].GetFloat();
					}
					if (food.HasMember("saturation")) {
						if (!food["saturation"].IsNumber())
							throw std::runtime_error("Block food.saturation must be a number");
						_foodSaturation = food["saturation"].GetFloat();
					}
				}
				else {
					throw std::runtime_error("Block field 'food' must be a number or {hunger, saturation}");
				}
				if (!std::isfinite(_foodHunger) || _foodHunger < 0.0f || _foodHunger > 20.0f ||
					!std::isfinite(_foodSaturation) || _foodSaturation < 0.0f || _foodSaturation > 20.0f)
					throw std::runtime_error("Block food values must be between 0 and 20");
			}
			if (doc.HasMember("armorReduction")) {
				if (!doc["armorReduction"].IsNumber())
					throw std::runtime_error("Block field 'armorReduction' must be a number");
				_armorReduction = doc["armorReduction"].GetFloat();
				if (!std::isfinite(_armorReduction) || _armorReduction < 0.0f || _armorReduction > 1.0f)
					throw std::runtime_error("Block armorReduction must be between 0 and 1");
			}
			if (doc.HasMember("heldStyle")) {
				if (!doc["heldStyle"].IsString())
					throw std::runtime_error("Block field 'heldStyle' must be a string");
				const std::string value = doc["heldStyle"].GetString();
				if (value == "block") _heldStyle = heldItemStyle::block;
				else if (value == "slab") _heldStyle = heldItemStyle::slab;
				else if (value == "sprite") _heldStyle = heldItemStyle::sprite;
				else if (value == "tool") _heldStyle = heldItemStyle::tool;
				else if (value == "rod") _heldStyle = heldItemStyle::rod;
				else if (value == "cross") _heldStyle = heldItemStyle::cross;
				else throw std::runtime_error("Block heldStyle must be block, slab, sprite, tool, rod, or cross");
			}
			if (doc.HasMember("soundMaterial")) {
				if (!doc["soundMaterial"].IsString())
					throw std::runtime_error("Block field 'soundMaterial' must be a string");
				const std::string value = doc["soundMaterial"].GetString();
				if (value == "stone") _soundMaterial = blockSoundMaterial::stone;
				else if (value == "wood") _soundMaterial = blockSoundMaterial::wood;
				else if (value == "glass") _soundMaterial = blockSoundMaterial::glass;
				else if (value == "grass") _soundMaterial = blockSoundMaterial::grass;
				else if (value == "gravel") _soundMaterial = blockSoundMaterial::gravel;
				else if (value == "sand") _soundMaterial = blockSoundMaterial::sand;
				else if (value == "snow") _soundMaterial = blockSoundMaterial::snow;
				else if (value == "cloth") _soundMaterial = blockSoundMaterial::cloth;
				else throw std::runtime_error("Unknown block soundMaterial '" + value + "'");
			}
			if (doc.HasMember("lightTransmission")) {
				const auto& value = doc["lightTransmission"];
				if (!value.IsArray() || value.Size() != 3u)
					throw std::runtime_error("Block lightTransmission must be an [r, g, b] array");
				float channels[3]{};
				for (rapidjson::SizeType i = 0; i < 3u; ++i) {
					if (!value[i].IsNumber())
						throw std::runtime_error("Block lightTransmission channels must be numbers");
					channels[i] = value[i].GetFloat();
					if (!std::isfinite(channels[i]) || channels[i] < 0.0f || channels[i] > 1.0f)
						throw std::runtime_error("Block lightTransmission channels must be between 0 and 1");
				}
				_lightTransmission = { channels[0], channels[1], channels[2] };
			}
			_connectsToPanes = optionalBool("connectsToPanes", false);
			_flatIcon = optionalBool("flatIcon", false);
		}
    };

}
