#pragma once

#include "gameEvents.h"
#include "gameServices.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ac::modding {

	class commandRegistry;

	enum class logLevel : uint8_t {
		debug,
		info,
		warning,
		error
	};

	struct blockDescription {
		resourceId id;
		uint32_t runtimeId = 0;
		bool solid = false;
		bool item = false;
		float hardness = 0.0f;
	};

	struct worldWriteOptions {
		bool notifyFluids = true;
		bool notifyRedstone = true;
		bool notifyFallingBlocks = true;
		bool immediateRemesh = true;
	};

	class blockRegistryPort {
	public:
		virtual ~blockRegistryPort() = default;
		virtual std::optional<uint32_t> find(const resourceId& id) const = 0;
		virtual std::optional<blockDescription> describe(uint32_t runtimeId) const = 0;
		virtual std::optional<resourceId> persistentId(uint32_t runtimeId) const = 0;
	};

	class worldPort {
	public:
		virtual ~worldPort() = default;
		virtual std::optional<uint32_t> readBlock(blockPosition position) const = 0;
		virtual bool writeBlock(
			blockPosition position,
			uint32_t state,
			const worldWriteOptions& options = {}) = 0;
	};

	class logPort {
	public:
		virtual ~logPort() = default;
		virtual void write(logLevel level, std::string_view owner, std::string_view message) = 0;
	};

	class gameApi {
	public:
		static constexpr uint32_t version = 2u;

		virtual ~gameApi() = default;
		virtual blockRegistryPort& blocks() = 0;
		virtual worldPort& world() = 0;
		virtual commandRegistry& commands() = 0;
		virtual gameEventBus& events() = 0;
		virtual logPort& logger() = 0;
		virtual entityPort* entities() noexcept = 0;
		virtual presentationPort* presentation() noexcept = 0;
		virtual uiPort* ui() noexcept = 0;
		virtual storagePort* storage() noexcept = 0;
		virtual registryCatalogPort* registries() noexcept = 0;
		virtual networkPort* network() noexcept = 0;
	};
}
