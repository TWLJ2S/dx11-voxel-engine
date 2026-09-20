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
        bool _occludes = false;
        bool _solid = false;
        RENDER_MODE _renderMode = RENDER_MODE_OPAQUE;
        float _opacity = 1.0f;
        float _roughness = 1.0f;
        float _metallic = 0.0f;
        std::string _name;
		std::string _texture;
		std::array<std::string, BLOCK_FACE_COUNT> _faceTextures;

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

        uint32_t materialForFace(uint32_t face) const {
            if (face >= _faceMaterials.size() ||
                _faceMaterials[face] == INHERIT_MATERIAL)
                return _material;

            return _faceMaterials[face];
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
				"faceMaterials", "texture", "textures", "roughness", "metallic",
				"renderMode", "opacity", "emission"
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

            _id = requiredUint("id");
            if (_id == 0)
                throw std::runtime_error("Block id 0 is reserved for air");
            if (!doc.HasMember("name") || !doc["name"].IsString() || doc["name"].GetStringLength() == 0)
                throw std::runtime_error("Block field 'name' must be a non-empty string");
            _name = doc["name"].GetString();
			if (_name.size() > 64)
				throw std::runtime_error("Block name must not exceed 64 characters");
			for (const unsigned char character : _name) {
				if (!(std::islower(character) || std::isdigit(character) ||
					character == '_' || character == '-' || character == '.' || character == ':'))
					throw std::runtime_error("Block name must use lowercase letters, digits, '_', '-', '.', or ':'");
			}

            _material = optionalUint("material", 0);
            _model = optionalUint("model", 0);
            _hitbox = optionalUint("hitbox", 0);
            _solid = optionalBool("solid", true);

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
				validateMembers(emission, { "r", "g", "b", "intensity", "radius" }, "emission");
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
            }
        }
    };

}
