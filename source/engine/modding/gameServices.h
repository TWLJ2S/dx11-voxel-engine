#pragma once

#include "resourceId.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ac::modding {

	struct float3 {
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
	};

	struct color4 {
		float red = 1.0f;
		float green = 1.0f;
		float blue = 1.0f;
		float alpha = 1.0f;
	};

	using entityId = uint64_t;

	struct entityDescription {
		entityId id = 0;
		resourceId type;
		float3 position;
		float3 velocity;
		float yaw = 0.0f;
		float pitch = 0.0f;
		float health = 0.0f;
		uint32_t flags = 0;
	};

	struct entitySpawnRequest {
		resourceId type;
		float3 position;
		float yaw = 0.0f;
		uint32_t flags = 0;
	};

	class entityPort {
	public:
		virtual ~entityPort() = default;
		virtual std::optional<entityId> spawn(const entitySpawnRequest& request) = 0;
		virtual std::optional<entityDescription> describe(entityId id) const = 0;
		virtual std::vector<entityId> all() const = 0;
		virtual bool teleport(entityId id, float3 position) = 0;
		virtual bool damage(entityId id, float amount, float3 direction = {}, float knockback = 0.0f) = 0;
		virtual bool remove(entityId id) = 0;
	};

	struct particleBurst {
		float3 position;
		color4 color;
		uint32_t count = 1;
		float speed = 1.0f;
		float lifetime = 0.7f;
		float size = 0.14f;
	};

	// Presentation is command based. Mods never receive renderer, device, or UI
	// pointers, so the engine remains free to replace DirectX or its UI backend.
	class presentationPort {
	public:
		virtual ~presentationPort() = default;
		virtual bool emit(const particleBurst& burst) = 0;
	};

	class uiPort {
	public:
		virtual ~uiPort() = default;
		virtual std::string currentScreen() const = 0;
		virtual void notify(std::string_view message) = 0;
	};

	// Storage keys are relative logical names. Implementations must confine data
	// to the calling mod's directory and write atomically.
	class storagePort {
	public:
		virtual ~storagePort() = default;
		virtual std::optional<std::vector<uint8_t>> read(
			std::string_view owner, std::string_view key) const = 0;
		virtual bool write(
			std::string_view owner, std::string_view key, std::span<const uint8_t> value) = 0;
		virtual bool erase(std::string_view owner, std::string_view key) = 0;
	};

	// A catalog unifies lookup without exposing concrete registry containers.
	// Registry and entry names are stable resource IDs; returned handles are only
	// valid for the current process.
	class registryCatalogPort {
	public:
		virtual ~registryCatalogPort() = default;
		virtual std::optional<uint32_t> find(
			const resourceId& registry, const resourceId& entry) const = 0;
		virtual std::optional<resourceId> persistentId(
			const resourceId& registry, uint32_t handle) const = 0;
		virtual uint32_t size(const resourceId& registry) const = 0;
		virtual bool add(const resourceId&, const resourceId&, std::string) { return false; }
		virtual std::optional<std::string> value(
			const resourceId&, uint32_t) const { return std::nullopt; }
		virtual bool frozen() const noexcept { return true; }
	};

	class networkPort {
	public:
		virtual ~networkPort() = default;
		virtual bool available() const noexcept = 0;
		virtual bool send(const resourceId& channel, std::span<const uint8_t> payload) = 0;
	};

	struct optionalGamePorts {
		entityPort* entities = nullptr;
		presentationPort* presentation = nullptr;
		uiPort* ui = nullptr;
		storagePort* storage = nullptr;
		registryCatalogPort* registries = nullptr;
		networkPort* network = nullptr;
	};
}
