#pragma once

#include <d3d11.h>
#include <DirectXCollision.h>
#include <DirectXMath.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <queue>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <assets/gpuAsset.h>
#include <core/animationTimeline.h>
#include <core/shader.h>
#include <core/texture.h>
#include <renderer/buffer.h>

namespace ac {

	enum class entityBehavior : uint8_t { passive, hostile };
	enum class entityPathBackend : uint8_t { cpu, gpu };
	enum class entityBoxFace : uint8_t { left, right, front, back, top, bottom, count };

	struct entityFaceUv {
		// Pixel coordinates in the entity texture atlas: left, top, right, bottom.
		DirectX::XMFLOAT4 rectangle{};
		uint8_t quarterTurns = 0;
		bool defined = false;
	};

	struct entityBoxDefinition {
		DirectX::XMFLOAT3 minimum{};
		DirectX::XMFLOAT3 maximum{};
		DirectX::XMFLOAT4 color{ 1, 1, 1, 1 };
		DirectX::XMFLOAT3 pivot{};
		DirectX::XMFLOAT3 rotationDegrees{};
		DirectX::XMFLOAT2 uv{};
		DirectX::XMFLOAT3 textureDimensions{};
		std::array<entityFaceUv, static_cast<size_t>(entityBoxFace::count)> faces{};
		bool textureRotateX = false;
		uint32_t bone = 0;
	};

	struct entityCollisionBox {
		DirectX::XMFLOAT3 minimum{};
		DirectX::XMFLOAT3 maximum{};
		DirectX::XMFLOAT3 pivot{};
		DirectX::XMFLOAT3 rotationDegrees{};
	};

	struct entityMeshVertex {
		DirectX::XMFLOAT3 position{};
		// Pixel coordinates in the entity atlas.
		DirectX::XMFLOAT2 uv{};
	};

	struct entityPolygonFace { std::vector<uint32_t> indices; };

	struct entityMeshDefinition {
		std::vector<entityMeshVertex> vertices;
		std::vector<entityPolygonFace> faces;
		DirectX::XMFLOAT4 color{ 1, 1, 1, 1 };
		DirectX::XMFLOAT3 pivot{};
		DirectX::XMFLOAT3 rotationDegrees{};
		uint32_t bone = 0;
	};

	// Collision meshes are convex. Several hulls can be combined to represent a
	// concave entity while retaining exact face/edge collision tests.
	struct entityCollisionHull {
		std::vector<DirectX::XMFLOAT3> vertices;
		std::vector<entityPolygonFace> faces;
		DirectX::XMFLOAT3 pivot{};
		DirectX::XMFLOAT3 rotationDegrees{};
	};

	struct entityRenderLayerDefinition {
		std::string texture;
		DirectX::XMFLOAT2 textureSize{ 64.0f, 64.0f };
		std::vector<entityBoxDefinition> boxes;
		std::vector<entityMeshDefinition> meshes;
		bool hideWhenSheared = false;
	};

	struct livingEntityDefinition {
		std::string name;
		entityBehavior behavior = entityBehavior::passive;
		float speed = 1.5f;
		float acceleration = 7.0f;
		float turnRate = 7.0f;
		float scale = 1.0f;
		float walkRate = 5.0f;
		float walkAmplitude = 0.6f;
		float headBob = 0.03f;
		std::vector<animationTimingSegment> movementTiming;
		animationCurve movementCurve;
		float maxHealth = 10.0f;
		float attackDamage = 2.0f;
		float attackRange = 1.65f;
		float attackCooldown = 1.0f;
		float knockback = 3.0f;
		bool shearable = false;
		float hitboxWidth = 0.6f;
		float hitboxHeight = 1.8f;
		std::string texture;
		DirectX::XMFLOAT2 textureSize{ 64.0f, 64.0f };
		std::vector<entityBoxDefinition> boxes;
		std::vector<entityMeshDefinition> meshes;
		std::vector<entityCollisionBox> collisionBoxes;
		std::vector<entityCollisionHull> collisionHulls;
		std::vector<entityRenderLayerDefinition> renderLayers;
	};

	class entityDefinitionRegistry {
		std::vector<livingEntityDefinition> _definitions;
		std::unordered_map<std::string, uint32_t> _ids;

		static DirectX::XMFLOAT3 vec3(const rapidjson::Value& object, const char* member) {
			if (!object.HasMember(member) || !object[member].IsArray() || object[member].Size() != 3 ||
				!object[member][0].IsNumber() || !object[member][1].IsNumber() || !object[member][2].IsNumber())
				throw std::runtime_error(std::string("Entity field '") + member + "' must contain 3 numbers");
			const auto& value = object[member];
			return { value[0].GetFloat(), value[1].GetFloat(), value[2].GetFloat() };
		}

		static DirectX::XMFLOAT2 vec2(const rapidjson::Value& object, const char* member) {
			if (!object.HasMember(member) || !object[member].IsArray() || object[member].Size() != 2 ||
				!object[member][0].IsNumber() || !object[member][1].IsNumber())
				throw std::runtime_error(std::string("Entity field '") + member + "' must contain 2 numbers");
			return { object[member][0].GetFloat(), object[member][1].GetFloat() };
		}

		static DirectX::XMFLOAT4 vec4(const rapidjson::Value& object, const char* member) {
			if (!object.HasMember(member) || !object[member].IsArray() || object[member].Size() != 4)
				throw std::runtime_error(std::string("Entity field '") + member + "' must contain 4 numbers");
			const auto& value = object[member];
			for (rapidjson::SizeType index = 0; index < 4; ++index)
				if (!value[index].IsNumber())
					throw std::runtime_error(std::string("Entity field '") + member + "' must contain 4 numbers");
			return { value[0].GetFloat(), value[1].GetFloat(), value[2].GetFloat(), value[3].GetFloat() };
		}

		static DirectX::XMFLOAT3 arrayVec3(const rapidjson::Value& value, const std::string& source) {
			if (!value.IsArray() || value.Size() != 3 || !value[0].IsNumber() ||
				!value[1].IsNumber() || !value[2].IsNumber())
				throw std::runtime_error(source + ": expected a 3-number vector");
			return { value[0].GetFloat(), value[1].GetFloat(), value[2].GetFloat() };
		}

		static DirectX::XMFLOAT2 arrayVec2(const rapidjson::Value& value, const std::string& source) {
			if (!value.IsArray() || value.Size() != 2 || !value[0].IsNumber() || !value[1].IsNumber())
				throw std::runtime_error(source + ": expected a 2-number vector");
			return { value[0].GetFloat(), value[1].GetFloat() };
		}

		static std::vector<entityPolygonFace> polygonFaces(
			const rapidjson::Value& faces, size_t vertexCount, const std::string& source
		) {
			if (!faces.IsArray()) throw std::runtime_error(source + ": polygon faces must be an array");
			std::vector<entityPolygonFace> result;
			for (const rapidjson::Value& face : faces.GetArray()) {
				if (!face.IsArray() || face.Size() < 3)
					throw std::runtime_error(source + ": each polygon face requires at least 3 vertex indices");
				entityPolygonFace polygon;
				for (const rapidjson::Value& index : face.GetArray()) {
					if (!index.IsUint() || index.GetUint() >= vertexCount)
						throw std::runtime_error(source + ": polygon face contains an invalid vertex index");
					polygon.indices.push_back(index.GetUint());
				}
				result.push_back(std::move(polygon));
			}
			if (result.empty()) throw std::runtime_error(source + ": polygon mesh requires at least one face");
			return result;
		}

		static DirectX::XMFLOAT4 partColor(const rapidjson::Value& part, const std::string& source) {
			if (!part.HasMember("color")) return { 1, 1, 1, 1 };
			const auto& color = part["color"];
			if (!color.IsArray() || color.Size() < 3)
				throw std::runtime_error(source + ": entity model color requires at least 3 numbers");
			return { color[0].GetFloat(), color[1].GetFloat(), color[2].GetFloat(),
				color.Size() > 3 ? color[3].GetFloat() : 1.0f };
		}

		static entityFaceUv faceUv(const rapidjson::Value& faces, const char* name) {
			entityFaceUv result;
			if (!faces.HasMember(name)) return result;
			const rapidjson::Value& value = faces[name];
			if (value.IsArray()) {
				// Array-only face entries are parsed directly to avoid requiring an object.
				if (value.Size() != 4) throw std::runtime_error(std::string("Entity face '") + name + "' UV must contain 4 numbers");
				for (rapidjson::SizeType index = 0; index < 4; ++index)
					if (!value[index].IsNumber()) throw std::runtime_error(std::string("Entity face '") + name + "' UV must contain 4 numbers");
				result.rectangle = { value[0].GetFloat(), value[1].GetFloat(), value[2].GetFloat(), value[3].GetFloat() };
			}
			else if (value.IsObject()) {
				result.rectangle = vec4(value, "uv");
				if (value.HasMember("rotation") && value["rotation"].IsInt()) {
					const int rotation = value["rotation"].GetInt();
					if (rotation % 90 != 0) throw std::runtime_error(std::string("Entity face '") + name + "' rotation must be a multiple of 90");
					result.quarterTurns = static_cast<uint8_t>(((rotation / 90) % 4 + 4) % 4);
				}
			}
			else throw std::runtime_error(std::string("Entity face '") + name + "' must be a UV array or object");
			result.defined = true;
			return result;
		}

		static std::filesystem::path assetPath(
			const std::filesystem::path& definitionFile, const std::string& value
		) {
			const std::filesystem::path direct(value);
			if (std::filesystem::exists(direct)) return direct;
			return definitionFile.parent_path() / direct;
		}

		template<typename Target>
		static void loadParts(
			const rapidjson::Value& parts, const std::string& source,
			Target& definition
		) {
			if (!parts.IsArray())
				throw std::runtime_error(source + ": entity model parts must be an array");
			for (const rapidjson::Value& part : parts.GetArray()) {
				if (!part.IsObject())
					throw std::runtime_error(source + ": entity model part must be an object");
				if (part.HasMember("vertices")) {
					if (!part["vertices"].IsArray() || part["vertices"].Empty())
						throw std::runtime_error(source + ": mesh vertices must be a non-empty array");
					entityMeshDefinition mesh;
					mesh.pivot = part.HasMember("pivot") ? vec3(part, "pivot") : DirectX::XMFLOAT3{};
					mesh.rotationDegrees = part.HasMember("rotation") ? vec3(part, "rotation") : DirectX::XMFLOAT3{};
					mesh.bone = part.HasMember("bone") && part["bone"].IsUint()
						? std::min(part["bone"].GetUint(), 7u) : 0u;
					mesh.color = partColor(part, source);
					for (const rapidjson::Value& vertex : part["vertices"].GetArray()) {
						if (!vertex.IsObject() || !vertex.HasMember("position") || !vertex.HasMember("uv"))
							throw std::runtime_error(source + ": mesh vertex requires position and uv");
						mesh.vertices.push_back({ arrayVec3(vertex["position"], source), arrayVec2(vertex["uv"], source) });
					}
					if (!part.HasMember("faces")) throw std::runtime_error(source + ": mesh part requires faces");
					mesh.faces = polygonFaces(part["faces"], mesh.vertices.size(), source);
					definition.meshes.push_back(std::move(mesh));
					continue;
				}
				entityBoxDefinition box;
				box.minimum = vec3(part, "min");
				box.maximum = vec3(part, "max");
				box.pivot = vec3(part, "pivot");
				box.rotationDegrees = part.HasMember("rotation")
					? vec3(part, "rotation") : DirectX::XMFLOAT3{};
				box.uv = part.HasMember("uv") ? vec2(part, "uv") : DirectX::XMFLOAT2{};
				box.textureDimensions = part.HasMember("textureDimensions")
					? vec3(part, "textureDimensions") : DirectX::XMFLOAT3{};
				if (part.HasMember("textureOrientation")) {
					if (!part["textureOrientation"].IsString())
						throw std::runtime_error(source + ": textureOrientation must be a string");
					const std::string orientation = part["textureOrientation"].GetString();
					if (orientation == "rotate_x_90") box.textureRotateX = true;
					else if (orientation != "default")
						throw std::runtime_error(source + ": unsupported textureOrientation '" + orientation + "'");
				}
				box.bone = part.HasMember("bone") && part["bone"].IsUint()
					? std::min(part["bone"].GetUint(), 7u) : 0u;
				box.color = partColor(part, source);
				if (part.HasMember("faces")) {
					const rapidjson::Value& faces = part["faces"];
					if (!faces.IsObject()) throw std::runtime_error(source + ": entity part faces must be an object");
					box.faces[static_cast<size_t>(entityBoxFace::left)] = faceUv(faces, "left");
					box.faces[static_cast<size_t>(entityBoxFace::right)] = faceUv(faces, "right");
					box.faces[static_cast<size_t>(entityBoxFace::front)] = faceUv(faces, "front");
					box.faces[static_cast<size_t>(entityBoxFace::back)] = faceUv(faces, "back");
					box.faces[static_cast<size_t>(entityBoxFace::top)] = faceUv(faces, "top");
					box.faces[static_cast<size_t>(entityBoxFace::bottom)] = faceUv(faces, "bottom");
				}
				definition.boxes.push_back(box);
			}
		}

		static void loadCollisionHulls(const rapidjson::Value& hulls, const std::string& source,
			livingEntityDefinition& definition) {
			if (!hulls.IsArray()) throw std::runtime_error(source + ": hitbox hulls must be an array");
			for (const rapidjson::Value& value : hulls.GetArray()) {
				if (!value.IsObject() || !value.HasMember("vertices") || !value["vertices"].IsArray() ||
					!value.HasMember("faces"))
					throw std::runtime_error(source + ": collision hull requires vertices and faces");
				entityCollisionHull hull;
				for (const rapidjson::Value& vertex : value["vertices"].GetArray())
					hull.vertices.push_back(arrayVec3(vertex, source));
				if (hull.vertices.size() < 4) throw std::runtime_error(source + ": collision hull requires at least 4 vertices");
				hull.faces = polygonFaces(value["faces"], hull.vertices.size(), source);
				hull.pivot = value.HasMember("pivot") ? vec3(value, "pivot") : DirectX::XMFLOAT3{};
				hull.rotationDegrees = value.HasMember("rotation") ? vec3(value, "rotation") : DirectX::XMFLOAT3{};
				definition.collisionHulls.push_back(std::move(hull));
			}
		}

		static void loadCollisionBoxes(const rapidjson::Value& boxes, const std::string& source,
			livingEntityDefinition& definition) {
			if (!boxes.IsArray()) throw std::runtime_error(source + ": hitbox boxes must be an array");
			for (const rapidjson::Value& value : boxes.GetArray()) {
				if (!value.IsObject()) throw std::runtime_error(source + ": hitbox box must be an object");
				entityCollisionBox box;
				box.minimum = vec3(value, "min");
				box.maximum = vec3(value, "max");
				box.pivot = value.HasMember("pivot") ? vec3(value, "pivot") : DirectX::XMFLOAT3{
					(box.minimum.x + box.maximum.x) * 0.5f,
					(box.minimum.y + box.maximum.y) * 0.5f,
					(box.minimum.z + box.maximum.z) * 0.5f };
				box.rotationDegrees = value.HasMember("rotation") ? vec3(value, "rotation") : DirectX::XMFLOAT3{};
				definition.collisionBoxes.push_back(box);
			}
		}

	public:
		void load(const std::vector<std::filesystem::path>& files) {
			for (const std::filesystem::path& file : files) {
				const rapidjson::Document document = jsonLoader::load(file.string());
				if (!document.IsObject() || !document.HasMember("schemaVersion") ||
					!document["schemaVersion"].IsUint() || document["schemaVersion"].GetUint() != 1u ||
					!document.HasMember("entities") || !document["entities"].IsArray())
					throw std::runtime_error(file.string() + ": invalid entity definition document");
				for (const rapidjson::Value& value : document["entities"].GetArray()) {
					if (!value.IsObject() || !value.HasMember("name") || !value["name"].IsString())
						throw std::runtime_error(file.string() + ": entity requires a name");
					livingEntityDefinition definition;
					definition.name = value["name"].GetString();
					if (definition.name.find(':') == std::string::npos)
						throw std::runtime_error(file.string() + ": entity names must be namespaced");
					if (value.HasMember("behavior") && value["behavior"].IsString())
						definition.behavior = std::string(value["behavior"].GetString()) == "hostile"
							? entityBehavior::hostile : entityBehavior::passive;
					if (value.HasMember("speed") && value["speed"].IsNumber())
						definition.speed = std::clamp(value["speed"].GetFloat(), 0.1f, 12.0f);
					if (value.HasMember("acceleration") && value["acceleration"].IsNumber())
						definition.acceleration = std::clamp(value["acceleration"].GetFloat(), 0.2f, 40.0f);
					if (value.HasMember("turnRate") && value["turnRate"].IsNumber())
						definition.turnRate = std::clamp(value["turnRate"].GetFloat(), 0.5f, 20.0f);
					if (value.HasMember("scale") && value["scale"].IsNumber())
						definition.scale = std::clamp(value["scale"].GetFloat(), 0.1f, 8.0f);
					if (value.HasMember("maxHealth") && value["maxHealth"].IsNumber())
						definition.maxHealth = std::clamp(value["maxHealth"].GetFloat(), 1.0f, 2048.0f);
					if (value.HasMember("combat") && value["combat"].IsObject()) {
						const auto& combat = value["combat"];
						if (combat.HasMember("damage") && combat["damage"].IsNumber())
							definition.attackDamage = (std::max)(0.0f, combat["damage"].GetFloat());
						if (combat.HasMember("range") && combat["range"].IsNumber())
							definition.attackRange = (std::max)(0.25f, combat["range"].GetFloat());
						if (combat.HasMember("cooldown") && combat["cooldown"].IsNumber())
							definition.attackCooldown = (std::max)(0.1f, combat["cooldown"].GetFloat());
						if (combat.HasMember("knockback") && combat["knockback"].IsNumber())
							definition.knockback = (std::max)(0.0f, combat["knockback"].GetFloat());
					}
					if (value.HasMember("shearable") && value["shearable"].IsBool())
						definition.shearable = value["shearable"].GetBool();
					if (value.HasMember("texture") && value["texture"].IsString())
						definition.texture = assetPath(file, value["texture"].GetString()).generic_string();
					if (value.HasMember("textureSize")) {
						definition.textureSize = vec2(value, "textureSize");
						if (definition.textureSize.x <= 0.0f || definition.textureSize.y <= 0.0f)
							throw std::runtime_error(file.string() + ": textureSize must be positive");
					}
					if (value.HasMember("hitbox")) {
						const auto& hitbox = value["hitbox"];
						if (!hitbox.IsObject()) throw std::runtime_error(file.string() + ": hitbox must be an object");
						if (hitbox.HasMember("boxes")) loadCollisionBoxes(hitbox["boxes"], file.string(), definition);
						if (hitbox.HasMember("hulls")) loadCollisionHulls(hitbox["hulls"], file.string(), definition);
						if (hitbox.HasMember("width") && hitbox["width"].IsNumber())
							definition.hitboxWidth = std::clamp(hitbox["width"].GetFloat(), 0.1f, 4.0f);
						if (hitbox.HasMember("height") && hitbox["height"].IsNumber())
							definition.hitboxHeight = std::clamp(hitbox["height"].GetFloat(), 0.1f, 8.0f);
						if (definition.collisionBoxes.empty() && definition.collisionHulls.empty() &&
							(!hitbox.HasMember("width") || !hitbox["width"].IsNumber() ||
							 !hitbox.HasMember("height") || !hitbox["height"].IsNumber()))
							throw std::runtime_error(file.string() + ": hitbox requires boxes or numeric width and height");
					}
					if (value.HasMember("animation") && value["animation"].IsObject()) {
						const auto& animation = value["animation"];
						if (animation.HasMember("walkRate") && animation["walkRate"].IsNumber())
							definition.walkRate = animation["walkRate"].GetFloat();
						if (animation.HasMember("walkAmplitude") && animation["walkAmplitude"].IsNumber())
							definition.walkAmplitude = animation["walkAmplitude"].GetFloat();
						if (animation.HasMember("headBob") && animation["headBob"].IsNumber())
							definition.headBob = animation["headBob"].GetFloat();
						if (animation.HasMember("timing"))
							definition.movementTiming = loadAnimationTiming(
								animation["timing"], file.string() + ": animation.timing");
						if (animation.HasMember("timeCurve"))
							definition.movementCurve = loadAnimationCurve(
								animation["timeCurve"], file.string() + ": animation.timeCurve");
					}
					if (value.HasMember("model") && value["model"].IsString()) {
						const std::filesystem::path modelPath = assetPath(file, value["model"].GetString());
						const rapidjson::Document model = jsonLoader::load(modelPath.string());
						if (!model.IsObject() || !model.HasMember("schemaVersion") ||
							!model["schemaVersion"].IsUint() || model["schemaVersion"].GetUint() != 1u ||
							!model.HasMember("parts"))
							throw std::runtime_error(modelPath.string() + ": invalid entity model document");
						loadParts(model["parts"], modelPath.string(), definition);
					}
					else if (value.HasMember("parts")) loadParts(value["parts"], file.string(), definition);
					if (value.HasMember("layers")) {
						if (!value["layers"].IsArray())
							throw std::runtime_error(file.string() + ": entity layers must be an array");
						for (const rapidjson::Value& layerValue : value["layers"].GetArray()) {
							if (!layerValue.IsObject() || !layerValue.HasMember("model") ||
								!layerValue["model"].IsString() || !layerValue.HasMember("texture") ||
								!layerValue["texture"].IsString())
								throw std::runtime_error(file.string() + ": entity layer requires model and texture strings");
							entityRenderLayerDefinition layer;
							layer.texture = assetPath(file, layerValue["texture"].GetString()).generic_string();
							if (layerValue.HasMember("textureSize")) {
								layer.textureSize = vec2(layerValue, "textureSize");
								if (layer.textureSize.x <= 0.0f || layer.textureSize.y <= 0.0f)
									throw std::runtime_error(file.string() + ": layer textureSize must be positive");
							}
							if (layerValue.HasMember("hideWhenSheared")) {
								if (!layerValue["hideWhenSheared"].IsBool())
									throw std::runtime_error(file.string() + ": hideWhenSheared must be boolean");
								layer.hideWhenSheared = layerValue["hideWhenSheared"].GetBool();
							}
							const std::filesystem::path layerModelPath = assetPath(file, layerValue["model"].GetString());
							const rapidjson::Document layerModel = jsonLoader::load(layerModelPath.string());
							if (!layerModel.IsObject() || !layerModel.HasMember("schemaVersion") ||
								!layerModel["schemaVersion"].IsUint() || layerModel["schemaVersion"].GetUint() != 1u ||
								!layerModel.HasMember("parts"))
								throw std::runtime_error(layerModelPath.string() + ": invalid entity layer model document");
							loadParts(layerModel["parts"], layerModelPath.string(), layer);
							definition.renderLayers.push_back(std::move(layer));
						}
					}
					if (definition.boxes.empty() && definition.meshes.empty())
						throw std::runtime_error(file.string() + ": entity requires a non-empty model or parts array");
					const auto found = _ids.find(definition.name);
					if (found == _ids.end()) {
						const uint32_t id = static_cast<uint32_t>(_definitions.size());
						_ids.emplace(definition.name, id);
						_definitions.push_back(std::move(definition));
					}
					else {
						_definitions[found->second] = std::move(definition);
					}
				}
			}
		}

		const livingEntityDefinition* get(uint32_t id) const {
			return id < _definitions.size() ? &_definitions[id] : nullptr;
		}
		std::optional<uint32_t> find(std::string_view name) const {
			const auto found = _ids.find(std::string(name));
			return found == _ids.end() ? std::nullopt : std::optional<uint32_t>(found->second);
		}
		std::string_view name(uint32_t id) const {
			const livingEntityDefinition* definition = get(id);
			return definition ? std::string_view(definition->name) : std::string_view{};
		}
		size_t size() const { return _definitions.size(); }
	};

	struct entityNavigationGrid {
		static constexpr uint32_t width = 32;
		static constexpr uint32_t height = 32;
		static constexpr uint32_t count = width * height;
		int32_t originX = 0;
		int32_t originZ = 0;
		std::array<int32_t, count> floorY{};
		std::array<uint8_t, count> walkable{};

		static uint32_t index(uint32_t x, uint32_t z) { return z * width + x; }
	};

	class entityPathfinder {
		static constexpr uint32_t INF = 0x3fffffffu;
		ID3D11DeviceContext* _context = nullptr;
		Microsoft::WRL::ComPtr<ID3D11ComputeShader> _shader;
		Microsoft::WRL::ComPtr<ID3D11Buffer> _cost[2];
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _costView[2];
		Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> _costUav[2];
		Microsoft::WRL::ComPtr<ID3D11Buffer> _cells;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _cellsView;
		Microsoft::WRL::ComPtr<ID3D11Buffer> _staging;
		struct alignas(16) pathConstants { uint32_t width, height, count, unused; };
		constantBuffer<pathConstants> _constants;
		bool _gpuAvailable = false;

		static void createStructured(
			ID3D11Device* device,
			Microsoft::WRL::ComPtr<ID3D11Buffer>& buffer,
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& srv,
			Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>* uav
		) {
			D3D11_BUFFER_DESC description{};
			description.ByteWidth = sizeof(uint32_t) * entityNavigationGrid::count;
			description.Usage = D3D11_USAGE_DEFAULT;
			description.BindFlags = D3D11_BIND_SHADER_RESOURCE | (uav ? D3D11_BIND_UNORDERED_ACCESS : 0u);
			description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			description.StructureByteStride = sizeof(uint32_t);
			DX_CHECK(device->CreateBuffer(&description, nullptr, buffer.GetAddressOf()));
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDescription{};
			srvDescription.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			srvDescription.Format = DXGI_FORMAT_UNKNOWN;
			srvDescription.Buffer.NumElements = entityNavigationGrid::count;
			DX_CHECK(device->CreateShaderResourceView(buffer.Get(), &srvDescription, srv.GetAddressOf()));
			if (uav) {
				D3D11_UNORDERED_ACCESS_VIEW_DESC uavDescription{};
				uavDescription.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
				uavDescription.Format = DXGI_FORMAT_UNKNOWN;
				uavDescription.Buffer.NumElements = entityNavigationGrid::count;
				DX_CHECK(device->CreateUnorderedAccessView(buffer.Get(), &uavDescription, uav->GetAddressOf()));
			}
		}

		static bool canStep(const entityNavigationGrid& grid, uint32_t from, uint32_t to) {
			return grid.walkable[to] != 0 &&
				std::abs(grid.floorY[from] - grid.floorY[to]) <= 1;
		}

		static bool canTraverse(const entityNavigationGrid& grid, uint32_t from,
			uint32_t to, int dx, int dz) {
			if (!canStep(grid, from, to)) return false;
			if (dx == 0 || dz == 0) return true;
			const uint32_t x = from % grid.width;
			const uint32_t z = from / grid.width;
			const uint32_t sideX = grid.index(static_cast<uint32_t>(static_cast<int>(x) + dx), z);
			const uint32_t sideZ = grid.index(x, static_cast<uint32_t>(static_cast<int>(z) + dz));
			return canStep(grid, from, sideX) && canStep(grid, from, sideZ);
		}

		static std::vector<uint32_t> reconstructFromCosts(
			const entityNavigationGrid& grid,
			uint32_t start,
			uint32_t goal,
			const std::array<uint32_t, entityNavigationGrid::count>& costs
		) {
			std::vector<uint32_t> path;
			if (start >= grid.count || goal >= grid.count || costs[start] >= INF) return path;
			uint32_t current = start;
			for (uint32_t step = 0; step < grid.count && current != goal; ++step) {
				const uint32_t x = current % grid.width;
				const uint32_t z = current / grid.width;
				uint32_t next = current;
				uint32_t best = costs[current];
				const std::array<std::pair<int, int>, 8> directions{{
					{-1,0},{1,0},{0,-1},{0,1},{-1,-1},{1,-1},{-1,1},{1,1} }};
				for (const auto [dx, dz] : directions) {
					const int nx = static_cast<int>(x) + dx;
					const int nz = static_cast<int>(z) + dz;
					if (nx < 0 || nz < 0 || nx >= static_cast<int>(grid.width) || nz >= static_cast<int>(grid.height)) continue;
					const uint32_t candidate = grid.index(static_cast<uint32_t>(nx), static_cast<uint32_t>(nz));
					if (canTraverse(grid, current, candidate, dx, dz) && costs[candidate] < best) {
						best = costs[candidate];
						next = candidate;
					}
				}
				if (next == current) return {};
				current = next;
				path.push_back(current);
			}
			return current == goal ? path : std::vector<uint32_t>{};
		}

		std::vector<uint32_t> findCpu(const entityNavigationGrid& grid, uint32_t start, uint32_t goal) const {
			struct openNode { uint32_t index; uint32_t score; bool operator<(const openNode& other) const { return score > other.score; } };
			std::array<uint32_t, entityNavigationGrid::count> cost;
			std::array<uint32_t, entityNavigationGrid::count> parent;
			cost.fill(INF);
			parent.fill(UINT32_MAX);
			std::priority_queue<openNode> open;
			cost[start] = 0;
			open.push({ start, 0 });
			while (!open.empty()) {
				const uint32_t current = open.top().index;
				open.pop();
				if (current == goal) break;
				const uint32_t x = current % grid.width;
				const uint32_t z = current / grid.width;
				const std::array<std::pair<int, int>, 8> directions{{
					{-1,0},{1,0},{0,-1},{0,1},{-1,-1},{1,-1},{-1,1},{1,1} }};
				for (const auto [dx, dz] : directions) {
					const int nx = static_cast<int>(x) + dx;
					const int nz = static_cast<int>(z) + dz;
					if (nx < 0 || nz < 0 || nx >= static_cast<int>(grid.width) || nz >= static_cast<int>(grid.height)) continue;
					const uint32_t next = grid.index(static_cast<uint32_t>(nx), static_cast<uint32_t>(nz));
					if (!canTraverse(grid, current, next, dx, dz)) continue;
					const uint32_t nextCost = cost[current] + (dx != 0 && dz != 0 ? 14u : 10u);
					if (nextCost >= cost[next]) continue;
					cost[next] = nextCost;
					parent[next] = current;
					const uint32_t hx = static_cast<uint32_t>(std::abs(nx - static_cast<int>(goal % grid.width)));
					const uint32_t hz = static_cast<uint32_t>(std::abs(nz - static_cast<int>(goal / grid.width)));
					const uint32_t heuristic = 10u * (std::max)(hx, hz) + 4u * (std::min)(hx, hz);
					open.push({ next, nextCost + heuristic });
				}
			}
			if (start != goal && parent[goal] == UINT32_MAX) return {};
			std::vector<uint32_t> reversed;
			for (uint32_t current = goal; current != start; current = parent[current]) {
				if (current == UINT32_MAX) return {};
				reversed.push_back(current);
			}
			std::reverse(reversed.begin(), reversed.end());
			return reversed;
		}

		std::vector<uint32_t> findGpu(const entityNavigationGrid& grid, uint32_t start, uint32_t goal) {
			std::array<uint32_t, entityNavigationGrid::count> cells{};
			std::array<uint32_t, entityNavigationGrid::count> initial;
			initial.fill(INF);
			initial[goal] = 0;
			for (uint32_t i = 0; i < grid.count; ++i) {
				const uint32_t encodedHeight = static_cast<uint32_t>(std::clamp(grid.floorY[i] + 32768, 0, 65535));
				cells[i] = encodedHeight | (grid.walkable[i] ? 0x80000000u : 0u);
			}
			_context->UpdateSubresource(_cells.Get(), 0, nullptr, cells.data(), 0, 0);
			_context->UpdateSubresource(_cost[0].Get(), 0, nullptr, initial.data(), 0, 0);
			_constants.update(_context, { grid.width, grid.height, grid.count, 0 });
			_constants.bindCS(_context, 0);
			_context->CSSetShader(_shader.Get(), nullptr, 0);
			uint32_t current = 0;
			for (uint32_t iteration = 0; iteration < grid.width + grid.height; ++iteration) {
				const uint32_t output = 1u - current;
				ID3D11ShaderResourceView* views[2] = { _costView[current].Get(), _cellsView.Get() };
				_context->CSSetShaderResources(0, 2, views);
				ID3D11UnorderedAccessView* target = _costUav[output].Get();
				_context->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
				_context->Dispatch((grid.count + 63u) / 64u, 1, 1);
				ID3D11ShaderResourceView* nullViews[2]{};
				ID3D11UnorderedAccessView* nullUav = nullptr;
				_context->CSSetShaderResources(0, 2, nullViews);
				_context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
				current = output;
			}
			_context->CSSetShader(nullptr, nullptr, 0);
			_context->CopyResource(_staging.Get(), _cost[current].Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (FAILED(_context->Map(_staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return {};
			std::array<uint32_t, entityNavigationGrid::count> costs{};
			std::memcpy(costs.data(), mapped.pData, sizeof(costs));
			_context->Unmap(_staging.Get(), 0);
			return reconstructFromCosts(grid, start, goal, costs);
		}

	public:
		entityPathfinder(ID3D11Device* device, ID3D11DeviceContext* context) : _context(context) {
			try {
				const auto bytecode = compileShaderBytecode(
					L"assets/shader/entityPathfinding.hlsl", "main", "cs_5_0");
				DX_CHECK(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(),
					nullptr, _shader.GetAddressOf()));
				createStructured(device, _cost[0], _costView[0], &_costUav[0]);
				createStructured(device, _cost[1], _costView[1], &_costUav[1]);
				createStructured(device, _cells, _cellsView, nullptr);
				D3D11_BUFFER_DESC staging{};
				staging.ByteWidth = sizeof(uint32_t) * entityNavigationGrid::count;
				staging.Usage = D3D11_USAGE_STAGING;
				staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				DX_CHECK(device->CreateBuffer(&staging, nullptr, _staging.GetAddressOf()));
				_constants.create(device);
				_gpuAvailable = true;
			}
			catch (...) {
				_gpuAvailable = false;
			}
		}

		bool gpuAvailable() const { return _gpuAvailable; }
		std::vector<uint32_t> find(
			const entityNavigationGrid& grid, uint32_t start, uint32_t goal, entityPathBackend backend
		) {
			if (start >= grid.count || goal >= grid.count || !grid.walkable[start] || !grid.walkable[goal]) return {};
			if (backend == entityPathBackend::gpu && _gpuAvailable)
				return findGpu(grid, start, goal);
			return findCpu(grid, start, goal);
		}
	};

	struct livingEntity {
		uint64_t id = 0;
		uint32_t definition = 0;
		DirectX::XMFLOAT3 position{};
		float verticalVelocity = 0.0f;
		DirectX::XMFLOAT2 horizontalVelocity{};
		bool grounded = false;
		float yaw = 0.0f;
		float animationTime = 0.0f;
		float movementBlend = 0.0f;
		float health = 1.0f;
		float hurtCooldown = 0.0f;
		float hurtAnimation = 0.0f;
		float attackCooldownTimer = 0.0f;
		float attackAnimation = 0.0f;
		float knockbackTime = 0.0f;
		bool attackPending = false;
		bool sheared = false;
		float repathTimer = 0.0f;
		DirectX::XMFLOAT3 goal{};
		std::vector<DirectX::XMFLOAT3> path;
		size_t waypoint = 0;
	};

	struct entityRayHit {
		size_t entityIndex = 0;
		float distance = 0.0f;
	};

	class livingEntityRenderer {
		struct entityVertex {
			DirectX::XMFLOAT3 position;
			DirectX::XMFLOAT3 normal;
			DirectX::XMFLOAT4 color;
			DirectX::XMFLOAT2 uv;
			uint32_t bone;
		};
		struct mesh {
			Microsoft::WRL::ComPtr<ID3D11Buffer> vertex;
			Microsoft::WRL::ComPtr<ID3D11Buffer> index;
			uint32_t indexCount = 0;
			std::array<DirectX::XMFLOAT3, 8> pivots{};
			texture skin;
			bool textured = false;
		};
		struct layerMesh { mesh model; bool hideWhenSheared = false; };
		struct entityMeshes { mesh base; std::vector<layerMesh> layers; };
		struct alignas(16) objectData {
			DirectX::XMFLOAT4X4 world;
			uint32_t textured = 0;
			float hurtAmount = 0.0f;
			float padding[2]{};
		};
		struct alignas(16) boneData { std::array<DirectX::XMFLOAT4X4, 8> bones; };
		shaderProgram _program;
		constantBuffer<objectData> _objectBuffer;
		constantBuffer<boneData> _boneBuffer;
		std::vector<entityMeshes> _meshes;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> _depth;
		Microsoft::WRL::ComPtr<ID3D11BlendState> _blend;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> _raster;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> _sampler;

		static std::array<DirectX::XMFLOAT2, 4> faceUv(
			float left, float top, float right, float bottom, const DirectX::XMFLOAT2& size
		) {
			return {{
				{ left / size.x, bottom / size.y }, { left / size.x, top / size.y },
				{ right / size.x, top / size.y }, { right / size.x, bottom / size.y }
			}};
		}

		static std::array<DirectX::XMFLOAT2, 4> atlasUv(
			const entityBoxDefinition& box, entityBoxFace side,
			float left, float top, float right, float bottom,
			const DirectX::XMFLOAT2& size
		) {
			const entityFaceUv& explicitFace = box.faces[static_cast<size_t>(side)];
			if (explicitFace.defined) {
				left = explicitFace.rectangle.x;
				top = explicitFace.rectangle.y;
				right = explicitFace.rectangle.z;
				bottom = explicitFace.rectangle.w;
			}
			auto result = faceUv(left, top, right, bottom, size);
			for (uint8_t turn = 0; turn < explicitFace.quarterTurns; ++turn) {
				const auto previous = result;
				result = {{ previous[3], previous[0], previous[1], previous[2] }};
			}
			return result;
		}

		static void face(
			std::vector<entityVertex>& vertices, std::vector<uint32_t>& indices,
			const std::array<DirectX::XMFLOAT3, 4>& positions, DirectX::XMFLOAT3 normal,
			const std::array<DirectX::XMFLOAT2, 4>& uv, DirectX::XMFLOAT4 color, uint32_t bone
		) {
			const uint32_t first = static_cast<uint32_t>(vertices.size());
			for (size_t index = 0; index < positions.size(); ++index)
				vertices.push_back({ positions[index], normal, color, uv[index], bone });
			indices.insert(indices.end(), { first, first + 1, first + 2, first, first + 2, first + 3 });
		}

		static void transformedFace(
			std::vector<entityVertex>& vertices, std::vector<uint32_t>& indices,
			std::array<DirectX::XMFLOAT3, 4> positions, DirectX::XMFLOAT3 normal,
			const std::array<DirectX::XMFLOAT2, 4>& uv, DirectX::XMFLOAT4 color, uint32_t bone,
			const entityBoxDefinition& box
		) {
			using namespace DirectX;
			const XMMATRIX transform =
				XMMatrixTranslation(-box.pivot.x, -box.pivot.y, -box.pivot.z) *
				XMMatrixRotationRollPitchYaw(
					XMConvertToRadians(box.rotationDegrees.x),
					XMConvertToRadians(box.rotationDegrees.y),
					XMConvertToRadians(box.rotationDegrees.z)) *
				XMMatrixTranslation(box.pivot.x, box.pivot.y, box.pivot.z);
			for (XMFLOAT3& position : positions)
				XMStoreFloat3(&position, XMVector3TransformCoord(XMLoadFloat3(&position), transform));
			XMStoreFloat3(&normal, XMVector3Normalize(XMVector3TransformNormal(XMLoadFloat3(&normal), transform)));
			face(vertices, indices, positions, normal, uv, color, bone);
		}

		static void box(std::vector<entityVertex>& vertices, std::vector<uint32_t>& indices,
			const entityBoxDefinition& b, const DirectX::XMFLOAT2& textureSize) {
			const auto& a = b.minimum; const auto& c = b.maximum;
			const float width = b.textureDimensions.x > 0.0f ? b.textureDimensions.x
				: std::max(1.0f, std::round((c.x - a.x) * 16.0f));
			const float height = b.textureDimensions.y > 0.0f ? b.textureDimensions.y
				: std::max(1.0f, std::round((c.y - a.y) * 16.0f));
			const float depth = b.textureDimensions.z > 0.0f ? b.textureDimensions.z
				: std::max(1.0f, std::round((c.z - a.z) * 16.0f));
			const float u = b.uv.x, v = b.uv.y;
			if (b.textureRotateX) {
				// Minecraft quadruped bodies are authored as upright cuboids and then
				// pitched 90 degrees. Preserve that atlas orientation while using the
				// already-horizontal bounds supplied by the external model.
				transformedFace(vertices, indices, {{{a.x,a.y,a.z},{a.x,a.y,c.z},{a.x,c.y,c.z},{a.x,c.y,a.z}}}, {-1,0,0},
					atlasUv(b, entityBoxFace::left, u, v + depth, u + depth, v + depth + height, textureSize), b.color, b.bone, b);
				transformedFace(vertices, indices, {{{c.x,c.y,a.z},{c.x,c.y,c.z},{c.x,a.y,c.z},{c.x,a.y,a.z}}}, {1,0,0},
					atlasUv(b, entityBoxFace::right, u + depth + width, v + depth, u + depth * 2.0f + width, v + depth + height, textureSize), b.color, b.bone, b);
				transformedFace(vertices, indices, {{{c.x,a.y,a.z},{c.x,a.y,c.z},{a.x,a.y,c.z},{a.x,a.y,a.z}}}, {0,-1,0},
					atlasUv(b, entityBoxFace::front, u + depth, v + depth, u + depth + width, v + depth + height, textureSize), b.color, b.bone, b);
				transformedFace(vertices, indices, {{{a.x,c.y,a.z},{a.x,c.y,c.z},{c.x,c.y,c.z},{c.x,c.y,a.z}}}, {0,1,0},
					atlasUv(b, entityBoxFace::back, u + depth * 2.0f + width, v + depth, u + depth * 2.0f + width * 2.0f, v + depth + height, textureSize), b.color, b.bone, b);
				transformedFace(vertices, indices, {{{a.x,c.y,c.z},{a.x,a.y,c.z},{c.x,a.y,c.z},{c.x,c.y,c.z}}}, {0,0,1},
					atlasUv(b, entityBoxFace::top, u + depth, v, u + depth + width, v + depth, textureSize), b.color, b.bone, b);
				transformedFace(vertices, indices, {{{a.x,a.y,a.z},{a.x,c.y,a.z},{c.x,c.y,a.z},{c.x,a.y,a.z}}}, {0,0,-1},
					atlasUv(b, entityBoxFace::bottom, u + depth + width, v, u + depth + width * 2.0f, v + depth, textureSize), b.color, b.bone, b);
				return;
			}
			transformedFace(vertices, indices, {{{a.x,a.y,c.z},{a.x,c.y,c.z},{a.x,c.y,a.z},{a.x,a.y,a.z}}}, {-1,0,0},
				atlasUv(b, entityBoxFace::left, u, v + depth, u + depth, v + depth + height, textureSize), b.color, b.bone, b);
			transformedFace(vertices, indices, {{{c.x,a.y,a.z},{c.x,c.y,a.z},{c.x,c.y,c.z},{c.x,a.y,c.z}}}, {1,0,0},
				atlasUv(b, entityBoxFace::right, u + depth + width, v + depth, u + depth * 2.0f + width, v + depth + height, textureSize), b.color, b.bone, b);
			transformedFace(vertices, indices, {{{c.x,a.y,c.z},{c.x,c.y,c.z},{a.x,c.y,c.z},{a.x,a.y,c.z}}}, {0,0,1},
				atlasUv(b, entityBoxFace::front, u + depth, v + depth, u + depth + width, v + depth + height, textureSize), b.color, b.bone, b);
			transformedFace(vertices, indices, {{{a.x,a.y,a.z},{a.x,c.y,a.z},{c.x,c.y,a.z},{c.x,a.y,a.z}}}, {0,0,-1},
				atlasUv(b, entityBoxFace::back, u + depth * 2.0f + width, v + depth, u + depth * 2.0f + width * 2.0f, v + depth + height, textureSize), b.color, b.bone, b);
			transformedFace(vertices, indices, {{{a.x,c.y,a.z},{a.x,c.y,c.z},{c.x,c.y,c.z},{c.x,c.y,a.z}}}, {0,1,0},
				atlasUv(b, entityBoxFace::top, u + depth, v, u + depth + width, v + depth, textureSize), b.color, b.bone, b);
			transformedFace(vertices, indices, {{{a.x,a.y,c.z},{a.x,a.y,a.z},{c.x,a.y,a.z},{c.x,a.y,c.z}}}, {0,-1,0},
				atlasUv(b, entityBoxFace::bottom, u + depth + width, v, u + depth + width * 2.0f, v + depth, textureSize), b.color, b.bone, b);
		}

		static void polygonMesh(
			std::vector<entityVertex>& vertices, std::vector<uint32_t>& indices,
			const entityMeshDefinition& mesh, const DirectX::XMFLOAT2& textureSize
		) {
			using namespace DirectX;
			const XMMATRIX transform =
				XMMatrixTranslation(-mesh.pivot.x, -mesh.pivot.y, -mesh.pivot.z) *
				XMMatrixRotationRollPitchYaw(
					XMConvertToRadians(mesh.rotationDegrees.x),
					XMConvertToRadians(mesh.rotationDegrees.y),
					XMConvertToRadians(mesh.rotationDegrees.z)) *
				XMMatrixTranslation(mesh.pivot.x, mesh.pivot.y, mesh.pivot.z);
			std::vector<XMFLOAT3> positions;
			positions.reserve(mesh.vertices.size());
			for (const entityMeshVertex& vertex : mesh.vertices) {
				XMFLOAT3 position;
				XMStoreFloat3(&position, XMVector3TransformCoord(XMLoadFloat3(&vertex.position), transform));
				positions.push_back(position);
			}
			for (const entityPolygonFace& polygon : mesh.faces) {
				const XMVECTOR edgeA = XMVectorSubtract(
					XMLoadFloat3(&positions[polygon.indices[1]]), XMLoadFloat3(&positions[polygon.indices[0]]));
				const XMVECTOR edgeB = XMVectorSubtract(
					XMLoadFloat3(&positions[polygon.indices[2]]), XMLoadFloat3(&positions[polygon.indices[0]]));
				XMVECTOR faceNormal = XMVector3Cross(edgeA, edgeB);
				if (XMVectorGetX(XMVector3LengthSq(faceNormal)) < 1.0e-10f) continue;
				faceNormal = XMVector3Normalize(faceNormal);
				XMFLOAT3 normal;
				XMStoreFloat3(&normal, faceNormal);
				const uint32_t first = static_cast<uint32_t>(vertices.size());
				for (const uint32_t vertexIndex : polygon.indices) {
					const entityMeshVertex& source = mesh.vertices[vertexIndex];
					vertices.push_back({ positions[vertexIndex], normal, mesh.color,
						{ source.uv.x / textureSize.x, source.uv.y / textureSize.y }, mesh.bone });
				}
				for (uint32_t corner = 1; corner + 1 < polygon.indices.size(); ++corner)
					indices.insert(indices.end(), { first, first + corner, first + corner + 1u });
			}
		}

		static mesh createMesh(
			ID3D11Device* device,
			const std::vector<entityBoxDefinition>& boxes,
			const std::vector<entityMeshDefinition>& polygonMeshes,
			const DirectX::XMFLOAT2& textureSize,
			const std::string& texturePath
		) {
			mesh result;
			std::vector<entityVertex> vertices;
			std::vector<uint32_t> indices;
			for (const auto& part : boxes) {
				box(vertices, indices, part, textureSize);
				result.pivots[part.bone] = part.pivot;
			}
			for (const auto& part : polygonMeshes) {
				polygonMesh(vertices, indices, part, textureSize);
				result.pivots[part.bone] = part.pivot;
			}
			if (!texturePath.empty()) {
				result.skin = loadTextureFromFile(device, texturePath);
				result.textured = true;
			}
			D3D11_BUFFER_DESC vb{};
			vb.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(entityVertex));
			vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			D3D11_SUBRESOURCE_DATA vd{};
			vd.pSysMem = vertices.data();
			DX_CHECK(device->CreateBuffer(&vb, &vd, result.vertex.GetAddressOf()));
			D3D11_BUFFER_DESC ib{};
			ib.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint32_t));
			ib.BindFlags = D3D11_BIND_INDEX_BUFFER;
			D3D11_SUBRESOURCE_DATA data{};
			data.pSysMem = indices.data();
			DX_CHECK(device->CreateBuffer(&ib, &data, result.index.GetAddressOf()));
			result.indexCount = static_cast<uint32_t>(indices.size());
			return result;
		}

		static void drawMesh(ID3D11DeviceContext* context, mesh& model) {
			ID3D11ShaderResourceView* skin = model.textured ? model.skin._shaderResourceView.Get() : nullptr;
			context->PSSetShaderResources(0, 1, &skin);
			UINT stride = sizeof(entityVertex), offset = 0;
			ID3D11Buffer* vb = model.vertex.Get();
			context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
			context->IASetIndexBuffer(model.index.Get(), DXGI_FORMAT_R32_UINT, 0);
			context->DrawIndexed(model.indexCount, 0, 0);
		}

	public:
		livingEntityRenderer(ID3D11Device* device, const entityDefinitionRegistry& definitions) {
			_program.initVertexShader(device, L"assets/shader/entity.hlsl", "vertexMain", "vs_5_0");
			_program.initPixelShader(device, L"assets/shader/entity.hlsl", "pixelMain", "ps_5_0");
			D3D11_INPUT_ELEMENT_DESC layout[] = {
				{ "POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0 },
				{ "NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0 },
				{ "COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0 },
				{ "TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,40,D3D11_INPUT_PER_VERTEX_DATA,0 },
				{ "BONEID",0,DXGI_FORMAT_R32_UINT,0,48,D3D11_INPUT_PER_VERTEX_DATA,0 }
			};
			_program.initInputLayout(device, { std::begin(layout), std::end(layout) });
			_objectBuffer.create(device);
			_boneBuffer.create(device);
			_meshes.resize(definitions.size());
			for (uint32_t id = 0; id < definitions.size(); ++id) {
				const livingEntityDefinition& definition = *definitions.get(id);
				_meshes[id].base = createMesh(device, definition.boxes, definition.meshes,
					definition.textureSize, definition.texture);
				for (const entityRenderLayerDefinition& layer : definition.renderLayers)
					_meshes[id].layers.push_back({ createMesh(device, layer.boxes, layer.meshes,
						layer.textureSize, layer.texture), layer.hideWhenSheared });
			}
			D3D11_DEPTH_STENCIL_DESC depth{}; depth.DepthEnable = TRUE; depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
			DX_CHECK(device->CreateDepthStencilState(&depth, _depth.GetAddressOf()));
			D3D11_BLEND_DESC blend{}; blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			DX_CHECK(device->CreateBlendState(&blend, _blend.GetAddressOf()));
			D3D11_RASTERIZER_DESC raster{}; raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE; raster.DepthClipEnable = TRUE;
			DX_CHECK(device->CreateRasterizerState(&raster, _raster.GetAddressOf()));
			D3D11_SAMPLER_DESC sampler{};
			sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			// Entity skins are packed atlases. Sampling lower mip levels blends
			// unrelated neighbouring body parts, so always sample the authored texels.
			sampler.MinLOD = sampler.MaxLOD = 0.0f;
			DX_CHECK(device->CreateSamplerState(&sampler, _sampler.GetAddressOf()));
		}

		void render(ID3D11DeviceContext* context, const entityDefinitionRegistry& definitions,
			const std::vector<livingEntity>& entities, const DirectX::XMFLOAT3& camera) {
			_program.bindShaders(context);
			_objectBuffer.bindVS(context, 0);
			// pixelMain reads objectData.textured from b0 before sampling the entity
			// atlas. Without this bind, PS b0 contains stale state and textured mobs
			// fall back to their plain vertex colour.
			_objectBuffer.bindPS(context, 0);
			_boneBuffer.bindVS(context, 6);
			context->OMSetDepthStencilState(_depth.Get(), 0);
			context->OMSetBlendState(_blend.Get(), nullptr, 0xffffffff);
			context->RSSetState(_raster.Get());
			context->PSSetSamplers(0, 1, _sampler.GetAddressOf());
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			for (const livingEntity& entity : entities) {
				const livingEntityDefinition* definition = definitions.get(entity.definition);
				if (!definition || entity.definition >= _meshes.size()) continue;
				const float dx = entity.position.x - camera.x;
				const float dz = entity.position.z - camera.z;
				if (dx * dx + dz * dz > 160.0f * 160.0f) continue;
				using namespace DirectX;
				objectData object{};
				const float hurtPulse = entity.hurtAnimation > 0.0f
					? std::sin(entity.hurtAnimation * XM_PI) : 0.0f;
				XMStoreFloat4x4(&object.world, XMMatrixTranspose(
					XMMatrixScaling(definition->scale, definition->scale, definition->scale) *
					XMMatrixRotationZ(hurtPulse * 0.075f) *
					XMMatrixRotationY(entity.yaw) *
					XMMatrixTranslation(entity.position.x, entity.position.y, entity.position.z)));
				entityMeshes& models = _meshes[entity.definition];
				object.textured = models.base.textured ? 1u : 0u;
				object.hurtAmount = std::clamp(entity.hurtAnimation, 0.0f, 1.0f);
				boneData bones{};
				for (auto& transform : bones.bones) XMStoreFloat4x4(&transform, XMMatrixIdentity());
				const float rawCycle = entity.animationTime * definition->walkRate / XM_2PI;
				const float mappedCycle = definition->movementCurve.empty()
					? remapAnimationTime(rawCycle, definition->movementTiming)
					: std::floor(rawCycle) + std::clamp(
						definition->movementCurve.evaluate(rawCycle - std::floor(rawCycle)), 0.0f, 1.0f);
				const float cycle = mappedCycle * XM_2PI;
				const float attackProgress = 1.0f - std::clamp(entity.attackAnimation, 0.0f, 1.0f);
				const float attackSwing = entity.attackAnimation > 0.0f
					? -1.45f * std::sin(attackProgress * XM_PI) : 0.0f;
				for (uint32_t bone = 1; bone <= 4; ++bone) {
					const float phase = (bone == 1 || bone == 4) ? 0.0f : XM_PI;
					float angle = std::sin(cycle + phase) * definition->walkAmplitude * entity.movementBlend;
					if (bone == 1 || bone == 2) angle += attackSwing;
					const auto& p = models.base.pivots[bone];
					XMStoreFloat4x4(&bones.bones[bone], XMMatrixTranspose(
						XMMatrixTranslation(-p.x, -p.y, -p.z) * XMMatrixRotationX(angle) * XMMatrixTranslation(p.x, p.y, p.z)));
				}
				const auto& hp = models.base.pivots[5];
				XMStoreFloat4x4(&bones.bones[5], XMMatrixTranspose(
					XMMatrixTranslation(-hp.x, -hp.y, -hp.z) *
					XMMatrixRotationX(std::sin(cycle * 0.5f) * definition->headBob * entity.movementBlend) *
					XMMatrixTranslation(hp.x, hp.y, hp.z)));
				_objectBuffer.update(context, object);
				_boneBuffer.update(context, bones);
				drawMesh(context, models.base);
				for (layerMesh& layer : models.layers) {
					if (layer.hideWhenSheared && entity.sheared) continue;
					object.textured = layer.model.textured ? 1u : 0u;
					_objectBuffer.update(context, object);
					drawMesh(context, layer.model);
				}
			}
		}
	};

	class livingEntitySystem {
	public:
		using solidQuery = std::function<bool(int32_t, int32_t, int32_t)>;
		using attackCallback = std::function<void(const livingEntity&, float, float)>;

	private:
		entityDefinitionRegistry _definitions;
		entityPathfinder _pathfinder;
		livingEntityRenderer _renderer;
		std::vector<livingEntity> _entities;
		uint64_t _nextEntityId = 1;
		entityPathBackend _backend = entityPathBackend::gpu;
		std::mt19937 _random{ 0xE7717u };
		float _spawnDelay = 0.0f;
		static constexpr float entityGravity = 22.0f;

		static bool findFloor(const solidQuery& solid, int32_t x, int32_t z, int32_t referenceY, int32_t& floorY) {
			for (int32_t y = referenceY + 4; y >= referenceY - 7; --y) {
				if (y <= 0) continue;
				if (solid(x, y - 1, z) && !solid(x, y, z) && !solid(x, y + 1, z)) {
					floorY = y;
					return true;
				}
			}
			return false;
		}

		static uint32_t nearestWalkable(const entityNavigationGrid& grid, int x, int z) {
			x = std::clamp(x, 0, static_cast<int>(grid.width) - 1);
			z = std::clamp(z, 0, static_cast<int>(grid.height) - 1);
			for (int radius = 0; radius < 8; ++radius) {
				for (int dz = -radius; dz <= radius; ++dz) for (int dx = -radius; dx <= radius; ++dx) {
					if (std::abs(dx) != radius && std::abs(dz) != radius) continue;
					const int nx = x + dx, nz = z + dz;
					if (nx < 0 || nz < 0 || nx >= static_cast<int>(grid.width) || nz >= static_cast<int>(grid.height)) continue;
					const uint32_t index = grid.index(static_cast<uint32_t>(nx), static_cast<uint32_t>(nz));
					if (grid.walkable[index]) return index;
				}
			}
			return UINT32_MAX;
		}

		static std::vector<DirectX::XMFLOAT3> worldHullVertices(
			const entityCollisionHull& hull, const livingEntityDefinition& definition,
			const DirectX::XMFLOAT3& position, float yaw
		) {
			using namespace DirectX;
			const XMMATRIX transform =
				XMMatrixTranslation(-hull.pivot.x, -hull.pivot.y, -hull.pivot.z) *
				XMMatrixRotationRollPitchYaw(
					XMConvertToRadians(hull.rotationDegrees.x),
					XMConvertToRadians(hull.rotationDegrees.y),
					XMConvertToRadians(hull.rotationDegrees.z)) *
				XMMatrixTranslation(hull.pivot.x, hull.pivot.y, hull.pivot.z) *
				XMMatrixScaling(definition.scale, definition.scale, definition.scale) *
				XMMatrixRotationY(yaw) * XMMatrixTranslation(position.x, position.y, position.z);
			std::vector<XMFLOAT3> result;
			result.reserve(hull.vertices.size());
			for (const XMFLOAT3& source : hull.vertices) {
				XMFLOAT3 transformed;
				XMStoreFloat3(&transformed, XMVector3TransformCoord(XMLoadFloat3(&source), transform));
				result.push_back(transformed);
			}
			return result;
		}

		static bool separatedOnAxis(
			const std::vector<DirectX::XMFLOAT3>& vertices,
			DirectX::FXMVECTOR axis, const DirectX::XMFLOAT3& blockCenter
		) {
			using namespace DirectX;
			if (XMVectorGetX(XMVector3LengthSq(axis)) < 1.0e-10f) return false;
			float hullMinimum = std::numeric_limits<float>::max();
			float hullMaximum = -std::numeric_limits<float>::max();
			for (const XMFLOAT3& vertex : vertices) {
				const float projection = XMVectorGetX(XMVector3Dot(XMLoadFloat3(&vertex), axis));
				hullMinimum = std::min(hullMinimum, projection);
				hullMaximum = std::max(hullMaximum, projection);
			}
			const XMVECTOR absoluteAxis = XMVectorAbs(axis);
			const float radius = 0.5f * (XMVectorGetX(absoluteAxis) +
				XMVectorGetY(absoluteAxis) + XMVectorGetZ(absoluteAxis));
			const float center = XMVectorGetX(XMVector3Dot(XMLoadFloat3(&blockCenter), axis));
			return hullMaximum <= center - radius + 0.001f || hullMinimum >= center + radius - 0.001f;
		}

		static bool hullIntersectsBlock(
			const entityCollisionHull& hull,
			const std::vector<DirectX::XMFLOAT3>& vertices,
			const DirectX::XMFLOAT3& blockCenter
		) {
			using namespace DirectX;
			const std::array<XMVECTOR, 3> blockAxes{{
				XMVectorSet(1, 0, 0, 0), XMVectorSet(0, 1, 0, 0), XMVectorSet(0, 0, 1, 0) }};
			for (FXMVECTOR axis : blockAxes)
				if (separatedOnAxis(vertices, axis, blockCenter)) return false;
			for (const entityPolygonFace& face : hull.faces) {
				const XMVECTOR a = XMLoadFloat3(&vertices[face.indices[0]]);
				const XMVECTOR edgeA = XMVectorSubtract(XMLoadFloat3(&vertices[face.indices[1]]), a);
				const XMVECTOR edgeB = XMVectorSubtract(XMLoadFloat3(&vertices[face.indices[2]]), a);
				if (separatedOnAxis(vertices, XMVector3Cross(edgeA, edgeB), blockCenter)) return false;
				for (size_t corner = 0; corner < face.indices.size(); ++corner) {
					const XMVECTOR start = XMLoadFloat3(&vertices[face.indices[corner]]);
					const XMVECTOR end = XMLoadFloat3(&vertices[face.indices[(corner + 1) % face.indices.size()]]);
					const XMVECTOR edge = XMVectorSubtract(end, start);
					for (FXMVECTOR blockAxis : blockAxes)
						if (separatedOnAxis(vertices, XMVector3Cross(edge, blockAxis), blockCenter)) return false;
				}
			}
			return true;
		}

		static bool collides(
			const livingEntityDefinition& definition,
			const DirectX::XMFLOAT3& position, float yaw, const solidQuery& solid
		) {
			if (!definition.collisionBoxes.empty() || !definition.collisionHulls.empty()) {
				using namespace DirectX;
				for (const entityCollisionBox& shape : definition.collisionBoxes) {
					const XMFLOAT3 center{
						(shape.minimum.x + shape.maximum.x) * 0.5f,
						(shape.minimum.y + shape.maximum.y) * 0.5f,
						(shape.minimum.z + shape.maximum.z) * 0.5f };
					const XMFLOAT3 extents{
						(shape.maximum.x - shape.minimum.x) * 0.5f,
						(shape.maximum.y - shape.minimum.y) * 0.5f,
						(shape.maximum.z - shape.minimum.z) * 0.5f };
					BoundingOrientedBox local(center, extents, { 0, 0, 0, 1 });
					const XMVECTOR localRotation = XMQuaternionRotationRollPitchYaw(
						XMConvertToRadians(shape.rotationDegrees.x),
						XMConvertToRadians(shape.rotationDegrees.y),
						XMConvertToRadians(shape.rotationDegrees.z));
					const XMVECTOR pivot = XMLoadFloat3(&shape.pivot);
					const XMVECTOR localTranslation = XMVectorSubtract(pivot, XMVector3Rotate(pivot, localRotation));
					BoundingOrientedBox posed;
					local.Transform(posed, 1.0f, localRotation, localTranslation);
					BoundingOrientedBox world;
					posed.Transform(world, definition.scale, XMQuaternionRotationAxis(
						XMVectorSet(0, 1, 0, 0), yaw), XMLoadFloat3(&position));

					XMFLOAT3 corners[BoundingOrientedBox::CORNER_COUNT];
					world.GetCorners(corners);
					XMFLOAT3 low = corners[0], high = corners[0];
					for (size_t index = 1; index < BoundingOrientedBox::CORNER_COUNT; ++index) {
						low.x = std::min(low.x, corners[index].x); low.y = std::min(low.y, corners[index].y); low.z = std::min(low.z, corners[index].z);
						high.x = std::max(high.x, corners[index].x); high.y = std::max(high.y, corners[index].y); high.z = std::max(high.z, corners[index].z);
					}
					constexpr float inset = 0.001f;
					for (int32_t y = static_cast<int32_t>(std::floor(low.y + inset)); y <= static_cast<int32_t>(std::floor(high.y - inset)); ++y)
						for (int32_t z = static_cast<int32_t>(std::floor(low.z + inset)); z <= static_cast<int32_t>(std::floor(high.z - inset)); ++z)
							for (int32_t x = static_cast<int32_t>(std::floor(low.x + inset)); x <= static_cast<int32_t>(std::floor(high.x - inset)); ++x) {
								if (!solid(x, y, z)) continue;
								const BoundingBox block({ x + 0.5f, y + 0.5f, z + 0.5f }, { 0.5f, 0.5f, 0.5f });
								if (world.Intersects(block)) return true;
							}
				}
				for (const entityCollisionHull& hull : definition.collisionHulls) {
					const std::vector<XMFLOAT3> vertices = worldHullVertices(hull, definition, position, yaw);
					XMFLOAT3 low = vertices.front(), high = vertices.front();
					for (const XMFLOAT3& vertex : vertices) {
						low.x = std::min(low.x, vertex.x); low.y = std::min(low.y, vertex.y); low.z = std::min(low.z, vertex.z);
						high.x = std::max(high.x, vertex.x); high.y = std::max(high.y, vertex.y); high.z = std::max(high.z, vertex.z);
					}
					for (int32_t y = static_cast<int32_t>(std::floor(low.y)); y <= static_cast<int32_t>(std::floor(high.y)); ++y)
						for (int32_t z = static_cast<int32_t>(std::floor(low.z)); z <= static_cast<int32_t>(std::floor(high.z)); ++z)
							for (int32_t x = static_cast<int32_t>(std::floor(low.x)); x <= static_cast<int32_t>(std::floor(high.x)); ++x)
								if (solid(x, y, z) && hullIntersectsBlock(hull, vertices,
									{ x + 0.5f, y + 0.5f, z + 0.5f })) return true;
				}
				return false;
			}
			const float radius = definition.hitboxWidth * definition.scale * 0.5f;
			const float height = definition.hitboxHeight * definition.scale;
			constexpr float inset = 0.001f;
			const int32_t minX = static_cast<int32_t>(std::floor(position.x - radius + inset));
			const int32_t maxX = static_cast<int32_t>(std::floor(position.x + radius - inset));
			const int32_t minY = static_cast<int32_t>(std::floor(position.y + inset));
			const int32_t maxY = static_cast<int32_t>(std::floor(position.y + height - inset));
			const int32_t minZ = static_cast<int32_t>(std::floor(position.z - radius + inset));
			const int32_t maxZ = static_cast<int32_t>(std::floor(position.z + radius - inset));
			for (int32_t y = minY; y <= maxY; ++y)
				for (int32_t z = minZ; z <= maxZ; ++z)
					for (int32_t x = minX; x <= maxX; ++x)
						if (solid(x, y, z)) return true;
			return false;
		}

		static DirectX::XMFLOAT2 moveHorizontal(
			livingEntity& entity, const livingEntityDefinition& definition,
			float dx, float dz, const solidQuery& solid
		) {
			const float oldX = entity.position.x;
			const float oldZ = entity.position.z;
			DirectX::XMFLOAT3 candidate = entity.position;
			candidate.x += dx;
			if (!collides(definition, candidate, entity.yaw, solid)) entity.position.x = candidate.x;
			candidate = entity.position;
			candidate.z += dz;
			if (!collides(definition, candidate, entity.yaw, solid)) entity.position.z = candidate.z;
			return { entity.position.x - oldX, entity.position.z - oldZ };
		}

		static void simulateGravity(
			livingEntity& entity, const livingEntityDefinition& definition,
			float dt, const solidQuery& solid
		) {
			DirectX::XMFLOAT3 support = entity.position;
			support.y -= 0.055f;
			if (entity.grounded && !collides(definition, support, entity.yaw, solid))
				entity.grounded = false;
			if (entity.grounded && entity.verticalVelocity <= 0.0f) {
				entity.verticalVelocity = 0.0f;
				return;
			}
			entity.verticalVelocity = std::max(-38.0f, entity.verticalVelocity - entityGravity * dt);
			const float displacement = entity.verticalVelocity * dt;
			const int steps = std::max(1, static_cast<int>(std::ceil(std::abs(displacement) / 0.20f)));
			const float step = displacement / static_cast<float>(steps);
			for (int index = 0; index < steps; ++index) {
				DirectX::XMFLOAT3 candidate = entity.position;
				candidate.y += step;
				if (!collides(definition, candidate, entity.yaw, solid)) {
					entity.position.y = candidate.y;
					continue;
				}
				float safe = entity.position.y;
				float blocked = candidate.y;
				for (int iteration = 0; iteration < 8; ++iteration) {
					const float middle = (safe + blocked) * 0.5f;
					DirectX::XMFLOAT3 probe = entity.position;
					probe.y = middle;
					if (collides(definition, probe, entity.yaw, solid)) blocked = middle;
					else safe = middle;
				}
				entity.position.y = safe;
				entity.grounded = entity.verticalVelocity < 0.0f;
				entity.verticalVelocity = 0.0f;
				return;
			}
			entity.grounded = false;
		}

		entityNavigationGrid buildGrid(const livingEntity& entity, const solidQuery& solid) const {
			entityNavigationGrid grid;
			grid.originX = static_cast<int32_t>(std::floor(entity.position.x)) - static_cast<int32_t>(grid.width / 2);
			grid.originZ = static_cast<int32_t>(std::floor(entity.position.z)) - static_cast<int32_t>(grid.height / 2);
			const int32_t referenceY = static_cast<int32_t>(std::floor(entity.position.y));
			for (uint32_t z = 0; z < grid.height; ++z) for (uint32_t x = 0; x < grid.width; ++x) {
				const uint32_t index = grid.index(x, z);
				int32_t y = referenceY;
				grid.walkable[index] = findFloor(solid, grid.originX + static_cast<int32_t>(x),
					grid.originZ + static_cast<int32_t>(z), referenceY, y) ? 1u : 0u;
				grid.floorY[index] = y;
			}
			return grid;
		}

		void requestPath(livingEntity& entity, const DirectX::XMFLOAT3& desired, const solidQuery& solid) {
			const entityNavigationGrid grid = buildGrid(entity, solid);
			const uint32_t start = nearestWalkable(grid,
				static_cast<int>(std::floor(entity.position.x)) - grid.originX,
				static_cast<int>(std::floor(entity.position.z)) - grid.originZ);
			const uint32_t goal = nearestWalkable(grid,
				static_cast<int>(std::floor(desired.x)) - grid.originX,
				static_cast<int>(std::floor(desired.z)) - grid.originZ);
			entity.path.clear();
			entity.waypoint = 0;
			if (start == UINT32_MAX || goal == UINT32_MAX) return;
			for (const uint32_t index : _pathfinder.find(grid, start, goal, _backend)) {
				const uint32_t x = index % grid.width, z = index / grid.width;
				entity.path.push_back({ grid.originX + static_cast<float>(x) + 0.5f,
					static_cast<float>(grid.floorY[index]), grid.originZ + static_cast<float>(z) + 0.5f });
			}
			// String-pull across every collision-free flat section. The grid keeps
			// obstacle and step safety, while steering between the resulting corners
			// is continuous rather than restricted to axis-aligned cell hops.
			if (entity.path.size() > 1) {
				auto visible = [&](const DirectX::XMFLOAT3& from, const DirectX::XMFLOAT3& to) {
					if (std::abs(from.y - to.y) > 0.1f) return false;
					const float dx = to.x - from.x, dz = to.z - from.z;
					const float distance = std::sqrt(dx * dx + dz * dz);
					const int samples = (std::max)(1, static_cast<int>(std::ceil(distance / 0.18f)));
					for (int sample = 1; sample <= samples; ++sample) {
						const float t = static_cast<float>(sample) / static_cast<float>(samples);
						DirectX::XMFLOAT3 probe{ from.x + dx * t, from.y, from.z + dz * t };
						if (collides(*_definitions.get(entity.definition), probe, entity.yaw, solid)) return false;
					}
					return true;
				};
				std::vector<DirectX::XMFLOAT3> simplified;
				simplified.reserve(entity.path.size());
				DirectX::XMFLOAT3 anchor = entity.position;
				size_t index = 0;
				while (index < entity.path.size()) {
					size_t farthest = index;
					while (farthest + 1 < entity.path.size() && visible(anchor, entity.path[farthest + 1]))
						++farthest;
					simplified.push_back(entity.path[farthest]);
					anchor = entity.path[farthest];
					index = farthest + 1;
				}
				entity.path = std::move(simplified);
			}
		}

		uint64_t allocateEntityId() {
			while (_nextEntityId == 0 || std::any_of(_entities.begin(), _entities.end(),
				[this](const livingEntity& value) { return value.id == _nextEntityId; }))
				++_nextEntityId;
			return _nextEntityId++;
		}

		void spawnAmbient(const DirectX::XMFLOAT3& center, const solidQuery& solid) {
			static const std::array<std::pair<int, int>, 12> offsets{{
				{5,3},{-6,4},{4,-7},{-8,-3},{9,1},{1,9},{-4,-9},{10,-6},{-11,5},{7,10},{-9,-10},{12,8}
			}};
			const size_t count = std::min<size_t>(_definitions.size(), offsets.size());
			for (size_t id = 0; id < count; ++id) {
				const int32_t x = static_cast<int32_t>(std::floor(center.x)) + offsets[id].first;
				const int32_t z = static_cast<int32_t>(std::floor(center.z)) + offsets[id].second;
				int32_t y = static_cast<int32_t>(std::floor(center.y));
				if (!findFloor(solid, x, z, y, y)) continue;
				livingEntity entity;
				entity.id = allocateEntityId();
				entity.definition = static_cast<uint32_t>(id);
				entity.health = _definitions.get(static_cast<uint32_t>(id))->maxHealth;
				entity.position = { x + 0.5f, static_cast<float>(y), z + 0.5f };
				entity.grounded = true;
				entity.goal = entity.position;
				entity.repathTimer = 0.15f + static_cast<float>(id) * 0.18f;
				_entities.push_back(entity);
			}
		}

	public:
		livingEntitySystem(ID3D11Device* device, ID3D11DeviceContext* context,
			const std::vector<std::filesystem::path>& definitionFiles)
			: _definitions(), _pathfinder(device, context),
			  _renderer(([_definitionsPtr = &_definitions, &definitionFiles, device]() {
				  _definitionsPtr->load(definitionFiles);
				  return device;
			  })(), _definitions) {}

		void clear() { _entities.clear(); _spawnDelay = 0.0f; }
		bool gpuAvailable() const { return _pathfinder.gpuAvailable(); }
		bool gpuEnabled() const { return _backend == entityPathBackend::gpu && gpuAvailable(); }
		void setGpuEnabled(bool enabled) {
			_backend = enabled && gpuAvailable() ? entityPathBackend::gpu : entityPathBackend::cpu;
			for (auto& entity : _entities) entity.repathTimer = 0.0f;
		}
		size_t size() const { return _entities.size(); }
		const livingEntity* entity(size_t index) const {
			return index < _entities.size() ? &_entities[index] : nullptr;
		}
		livingEntity* entityById(uint64_t id) {
			const auto found = std::find_if(_entities.begin(), _entities.end(),
				[id](const livingEntity& value) { return value.id == id; });
			return found == _entities.end() ? nullptr : &*found;
		}
		const livingEntity* entityById(uint64_t id) const {
			const auto found = std::find_if(_entities.begin(), _entities.end(),
				[id](const livingEntity& value) { return value.id == id; });
			return found == _entities.end() ? nullptr : &*found;
		}
		std::string_view definitionName(uint32_t id) const { return _definitions.name(id); }
		std::optional<uint32_t> definitionId(std::string_view name) const {
			return _definitions.find(name);
		}
		uint32_t definitionCount() const { return static_cast<uint32_t>(_definitions.size()); }
		std::optional<uint64_t> spawnNamed(
			std::string_view type, const DirectX::XMFLOAT3& position, float yaw = 0.0f) {
			const std::optional<uint32_t> definition = _definitions.find(type);
			if (!definition) return std::nullopt;
			livingEntity entity;
			entity.id = allocateEntityId();
			entity.definition = *definition;
			entity.position = position;
			entity.yaw = yaw;
			entity.health = _definitions.get(*definition)->maxHealth;
			entity.goal = position;
			entity.repathTimer = 0.15f;
			_entities.push_back(entity);
			return entity.id;
		}
		std::vector<uint64_t> entityIds() const {
			std::vector<uint64_t> result;
			result.reserve(_entities.size());
			for (const livingEntity& value : _entities) result.push_back(value.id);
			return result;
		}
		bool teleport(uint64_t id, const DirectX::XMFLOAT3& position) {
			livingEntity* value = entityById(id);
			if (!value) return false;
			value->position = position;
			value->path.clear();
			value->goal = position;
			value->repathTimer = 0.0f;
			return true;
		}
		bool remove(uint64_t id) {
			const auto found = std::find_if(_entities.begin(), _entities.end(),
				[id](const livingEntity& value) { return value.id == id; });
			if (found == _entities.end()) return false;
			_entities.erase(found);
			return true;
		}
		bool damageById(uint64_t id, float amount,
			const DirectX::XMFLOAT3& direction = {}, float knockback = 0.0f) {
			const auto found = std::find_if(_entities.begin(), _entities.end(),
				[id](const livingEntity& value) { return value.id == id; });
			if (found == _entities.end()) return false;
			return damage(static_cast<size_t>(std::distance(_entities.begin(), found)),
				amount, direction, knockback);
		}
		bool damage(size_t index, float amount,
			const DirectX::XMFLOAT3& direction = {}, float knockback = 0.0f) {
			if (index >= _entities.size() || amount <= 0.0f) return false;
			livingEntity& target = _entities[index];
			if (target.hurtCooldown > 0.0f) return false;
			target.health = (std::max)(0.0f, target.health - amount);
			target.hurtCooldown = 0.45f;
			target.hurtAnimation = 1.0f;
			const float length = std::sqrt(direction.x * direction.x + direction.z * direction.z);
			if (knockback > 0.0f && length > 0.0001f) {
				target.horizontalVelocity.x = direction.x / length * knockback;
				target.horizontalVelocity.y = direction.z / length * knockback;
				target.verticalVelocity = (std::max)(target.verticalVelocity, knockback * 0.42f);
				target.grounded = false;
				target.knockbackTime = 0.28f;
				target.path.clear();
				target.repathTimer = 0.30f;
			}
			if (target.health <= 0.0f)
				_entities.erase(_entities.begin() + static_cast<std::ptrdiff_t>(index));
			return true;
		}

		size_t damageInArc(const DirectX::XMFLOAT3& origin,
			const DirectX::XMFLOAT3& forward, float radius, float minimumDot,
			float amount, float knockback, const solidQuery& solid) {
			const float forwardLength = std::sqrt(forward.x * forward.x + forward.z * forward.z);
			if (forwardLength <= 0.0001f || radius <= 0.0f || amount <= 0.0f) return 0;
			const float fx = forward.x / forwardLength;
			const float fz = forward.z / forwardLength;
			size_t hits = 0;
			// Reverse order keeps indices valid when a swept target dies.
			for (size_t remaining = _entities.size(); remaining > 0; --remaining) {
				const size_t index = remaining - 1;
				const livingEntity& candidate = _entities[index];
				const float dx = candidate.position.x - origin.x;
				const float dz = candidate.position.z - origin.z;
				const float distance = std::sqrt(dx * dx + dz * dz);
				if (distance > radius || std::abs(candidate.position.y - origin.y) > 1.75f)
					continue;
				if (distance > 0.0001f && (dx * fx + dz * fz) / distance < minimumDot)
					continue;
				bool blocked = false;
				const int samples = (std::max)(2, static_cast<int>(std::ceil(distance / 0.20f)));
				for (int sample = 1; sample < samples; ++sample) {
					const float t = static_cast<float>(sample) / static_cast<float>(samples);
					if (solid(static_cast<int32_t>(std::floor(origin.x + dx * t)),
						static_cast<int32_t>(std::floor(origin.y + 1.0f)),
						static_cast<int32_t>(std::floor(origin.z + dz * t)))) {
						blocked = true;
						break;
					}
				}
				if (blocked) continue;
				if (damage(index, amount, { dx, 0.0f, dz }, knockback)) ++hits;
			}
			return hits;
		}
		bool shear(size_t index) {
			if (index >= _entities.size()) return false;
			livingEntity& target = _entities[index];
			const livingEntityDefinition* definition = _definitions.get(target.definition);
			if (!definition || !definition->shearable || target.sheared) return false;
			target.sheared = true;
			return true;
		}

		std::optional<entityRayHit> raycast(
			const DirectX::XMFLOAT3& origin,
			const DirectX::XMFLOAT3& direction,
			float maximumDistance
		) const {
			using namespace DirectX;
			XMVECTOR rayDirection = XMLoadFloat3(&direction);
			if (XMVectorGetX(XMVector3LengthSq(rayDirection)) < 1.0e-8f) return std::nullopt;
			rayDirection = XMVector3Normalize(rayDirection);
			const XMVECTOR rayOrigin = XMLoadFloat3(&origin);
			std::optional<entityRayHit> closest;
			for (size_t entityIndex = 0; entityIndex < _entities.size(); ++entityIndex) {
				const livingEntity& entity = _entities[entityIndex];
				const livingEntityDefinition* definition = _definitions.get(entity.definition);
				if (!definition) continue;
				auto test = [&](const BoundingOrientedBox& box) {
					float distance = 0.0f;
					if (box.Intersects(rayOrigin, rayDirection, distance) && distance <= maximumDistance &&
						(!closest || distance < closest->distance))
						closest = entityRayHit{ entityIndex, distance };
				};
				if (definition->collisionBoxes.empty() && definition->collisionHulls.empty()) {
					const float radius = definition->hitboxWidth * definition->scale * 0.5f;
					const float height = definition->hitboxHeight * definition->scale;
					test(BoundingOrientedBox(
						{ entity.position.x, entity.position.y + height * 0.5f, entity.position.z },
						{ radius, height * 0.5f, radius }, { 0, 0, 0, 1 }));
					continue;
				}
				for (const entityCollisionBox& shape : definition->collisionBoxes) {
					const XMFLOAT3 center{
						(shape.minimum.x + shape.maximum.x) * 0.5f,
						(shape.minimum.y + shape.maximum.y) * 0.5f,
						(shape.minimum.z + shape.maximum.z) * 0.5f };
					const XMFLOAT3 extents{
						(shape.maximum.x - shape.minimum.x) * 0.5f,
						(shape.maximum.y - shape.minimum.y) * 0.5f,
						(shape.maximum.z - shape.minimum.z) * 0.5f };
					BoundingOrientedBox local(center, extents, { 0, 0, 0, 1 });
					const XMVECTOR localRotation = XMQuaternionRotationRollPitchYaw(
						XMConvertToRadians(shape.rotationDegrees.x),
						XMConvertToRadians(shape.rotationDegrees.y),
						XMConvertToRadians(shape.rotationDegrees.z));
					const XMVECTOR pivot = XMLoadFloat3(&shape.pivot);
					BoundingOrientedBox posed;
					local.Transform(posed, 1.0f, localRotation,
						XMVectorSubtract(pivot, XMVector3Rotate(pivot, localRotation)));
					BoundingOrientedBox world;
					posed.Transform(world, definition->scale,
						XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), entity.yaw),
						XMLoadFloat3(&entity.position));
					test(world);
				}
				for (const entityCollisionHull& hull : definition->collisionHulls) {
					const std::vector<XMFLOAT3> vertices = worldHullVertices(
						hull, *definition, entity.position, entity.yaw);
					for (const entityPolygonFace& face : hull.faces) {
						for (size_t corner = 1; corner + 1 < face.indices.size(); ++corner) {
							float distance = 0.0f;
							if (TriangleTests::Intersects(rayOrigin, rayDirection,
								XMLoadFloat3(&vertices[face.indices[0]]),
								XMLoadFloat3(&vertices[face.indices[corner]]),
								XMLoadFloat3(&vertices[face.indices[corner + 1]]), distance) &&
								distance <= maximumDistance && (!closest || distance < closest->distance))
								closest = entityRayHit{ entityIndex, distance };
						}
					}
				}
			}
			return closest;
		}

		void update(float deltaTime, const DirectX::XMFLOAT3& playerFeet, const solidQuery& solid,
			const attackCallback& onAttack = {}) {
			const float dt = std::clamp(deltaTime, 0.0f, 0.1f);
			if (_entities.empty()) {
				_spawnDelay += dt;
				if (_spawnDelay >= 0.6f) spawnAmbient(playerFeet, solid);
			}
			std::uniform_real_distribution<float> wander(-11.0f, 11.0f);
			for (livingEntity& entity : _entities) {
				const livingEntityDefinition* definition = _definitions.get(entity.definition);
				if (!definition) continue;
				entity.hurtCooldown = (std::max)(0.0f, entity.hurtCooldown - dt);
				entity.hurtAnimation = (std::max)(0.0f, entity.hurtAnimation - dt * 3.8f);
				entity.attackCooldownTimer = (std::max)(0.0f, entity.attackCooldownTimer - dt);
				entity.knockbackTime = (std::max)(0.0f, entity.knockbackTime - dt);
				auto meleeClear = [&]() {
					const float dx = playerFeet.x - entity.position.x;
					const float dy = playerFeet.y - entity.position.y;
					const float dz = playerFeet.z - entity.position.z;
					const float distance = std::sqrt(dx * dx + dz * dz);
					const int samples = (std::max)(2, static_cast<int>(std::ceil(distance / 0.20f)));
					for (int sample = 1; sample < samples; ++sample) {
						const float t = static_cast<float>(sample) / static_cast<float>(samples);
						if (solid(static_cast<int32_t>(std::floor(entity.position.x + dx * t)),
							static_cast<int32_t>(std::floor(entity.position.y + 1.0f + dy * t)),
							static_cast<int32_t>(std::floor(entity.position.z + dz * t)))) return false;
					}
					return true;
				};
				if (entity.attackAnimation > 0.0f) {
					entity.attackAnimation = (std::max)(0.0f, entity.attackAnimation - dt / 0.55f);
					if (entity.attackPending && entity.attackAnimation <= 0.55f) {
						const float hitDx = playerFeet.x - entity.position.x;
						const float hitDz = playerFeet.z - entity.position.z;
						const float hitDistance = std::sqrt(hitDx * hitDx + hitDz * hitDz);
						if (hitDistance <= definition->attackRange + 0.35f && meleeClear() && onAttack)
							onAttack(entity, definition->attackDamage, definition->knockback);
						entity.attackPending = false;
					}
				}
				const float playerDx = playerFeet.x - entity.position.x;
				const float playerDz = playerFeet.z - entity.position.z;
				const float playerDistance = std::sqrt(playerDx * playerDx + playerDz * playerDz);
				if (definition->behavior == entityBehavior::hostile &&
					playerDistance <= definition->attackRange && meleeClear() &&
					entity.attackCooldownTimer <= 0.0f) {
					entity.attackAnimation = 1.0f;
					entity.attackPending = true;
					entity.attackCooldownTimer = definition->attackCooldown;
				}
				entity.repathTimer -= dt;
				if (entity.repathTimer <= 0.0f) {
					if (definition->behavior == entityBehavior::hostile) {
						entity.goal = playerFeet;
						entity.repathTimer = 0.72f;
					}
					else if (entity.path.empty() || entity.waypoint >= entity.path.size()) {
						entity.goal = { entity.position.x + wander(_random), entity.position.y,
							entity.position.z + wander(_random) };
						entity.repathTimer = 2.2f;
					}
					else entity.repathTimer = 0.9f;
					requestPath(entity, entity.goal, solid);
				}
				while (entity.waypoint < entity.path.size()) {
					const auto& waypoint = entity.path[entity.waypoint];
					const float dx = waypoint.x - entity.position.x;
					const float dz = waypoint.z - entity.position.z;
					const float distance = std::sqrt(dx * dx + dz * dz);
					if (distance >= 0.18f) break;
					++entity.waypoint;
				}
				float desiredX = 0.0f, desiredZ = 0.0f;
				if (entity.waypoint < entity.path.size()) {
					const auto& waypoint = entity.path[entity.waypoint];
					float dx = waypoint.x - entity.position.x;
					float dz = waypoint.z - entity.position.z;
					const float distance = std::sqrt(dx * dx + dz * dz);
					if (entity.grounded && waypoint.y > entity.position.y + 0.35f) {
						const float rise = std::min(1.25f, waypoint.y - entity.position.y + 0.18f);
						entity.verticalVelocity = std::sqrt(2.0f * entityGravity * rise);
						entity.grounded = false;
					}
					if (distance > 0.0001f) {
						desiredX = dx / distance * definition->speed;
						desiredZ = dz / distance * definition->speed;
						// Ease into the final destination instead of snapping to a stop.
						if (entity.waypoint + 1 == entity.path.size()) {
							const float arrival = std::clamp(distance / 0.75f, 0.18f, 1.0f);
							desiredX *= arrival;
							desiredZ *= arrival;
						}
					}
				}
				if (definition->behavior == entityBehavior::hostile &&
					playerDistance <= definition->attackRange * 0.86f) {
					desiredX = 0.0f;
					desiredZ = 0.0f;
					if (playerDistance > 0.0001f) {
						const float wantedYaw = std::atan2(playerDx, playerDz);
						const float delta = std::remainder(wantedYaw - entity.yaw, DirectX::XM_2PI);
						entity.yaw += delta * (std::min)(1.0f, dt * definition->turnRate * 1.6f);
					}
				}
				auto approach = [](float current, float target, float amount) {
					return current < target ? std::min(current + amount, target)
						: std::max(current - amount, target);
				};
				const float acceleration = definition->acceleration * dt;
				if (entity.knockbackTime <= 0.0f) {
					entity.horizontalVelocity.x = approach(entity.horizontalVelocity.x, desiredX, acceleration);
					entity.horizontalVelocity.y = approach(entity.horizontalVelocity.y, desiredZ, acceleration);
				}
				else {
					const float drag = std::exp(-2.4f * dt);
					entity.horizontalVelocity.x *= drag;
					entity.horizontalVelocity.y *= drag;
				}
				const DirectX::XMFLOAT2 moved = moveHorizontal(entity, *definition,
					entity.horizontalVelocity.x * dt, entity.horizontalVelocity.y * dt, solid);
				if (std::abs(moved.x) < std::abs(entity.horizontalVelocity.x * dt) * 0.2f)
					entity.horizontalVelocity.x = 0.0f;
				if (std::abs(moved.y) < std::abs(entity.horizontalVelocity.y * dt) * 0.2f)
					entity.horizontalVelocity.y = 0.0f;
				const float actualSpeed = dt > 0.0001f
					? std::sqrt(moved.x * moved.x + moved.y * moved.y) / dt : 0.0f;
				const bool moving = actualSpeed > 0.015f;
				if (moving) {
					const float wantedYaw = std::atan2(moved.x, moved.y);
					const float delta = std::remainder(wantedYaw - entity.yaw, DirectX::XM_2PI);
					entity.yaw += delta * std::min(1.0f, dt * definition->turnRate);
				}
				simulateGravity(entity, *definition, dt, solid);
				entity.movementBlend += ((moving ? 1.0f : 0.0f) - entity.movementBlend) * std::min(1.0f, dt * 9.0f);
				const float speedRatio = std::clamp(actualSpeed / definition->speed, 0.0f, 1.15f);
				entity.animationTime += dt * (moving ? speedRatio : 0.10f);
			}
		}

		void render(ID3D11DeviceContext* context, const DirectX::XMFLOAT3& camera) {
			_renderer.render(context, _definitions, _entities, camera);
		}
	};

}
