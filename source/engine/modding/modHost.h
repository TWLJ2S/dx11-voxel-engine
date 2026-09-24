#pragma once

#include "commandRegistry.h"
#include "gameApi.h"

#include <assets/contentPack.h>

#include <cstdint>
#include <memory>
#include <string_view>
#include <string>
#include <unordered_map>
#include <vector>

namespace ac::modding {

	enum class modCodeKind : uint8_t {
		wasm,
		native
	};

	class modModule {
	public:
		virtual ~modModule() = default;
		virtual const resourceId& id() const noexcept = 0;
		virtual void start(gameApi& api) = 0;
		virtual void stop(gameApi& api) noexcept = 0;
	};

	// Runtime adapter port. Wasmtime, a native-DLL loader and test modules all
	// implement this same interface; none receives internal engine objects.
	class modRuntimePort {
	public:
		virtual ~modRuntimePort() = default;
		virtual std::string_view name() const noexcept = 0;
		virtual bool supports(modCodeKind kind) const noexcept = 0;
		virtual std::unique_ptr<modModule> load(
			const contentPack& pack,
			modCodeKind kind,
			gameApi& api) = 0;
	};

	class gameModHost final : public gameApi, public registryCatalogPort {
		struct customRegistry {
			std::unordered_map<resourceId, std::string, resourceIdHash> values;
			std::vector<resourceId> handles;
		};
		blockRegistryPort& _blocks;
		worldPort& _world;
		logPort& _logger;
		commandRegistry _commands;
		gameEventBus _events;
		std::vector<std::unique_ptr<modRuntimePort>> _runtimes;
		std::vector<std::unique_ptr<modModule>> _modules;
		optionalGamePorts _optionalPorts;
		std::unordered_map<resourceId, customRegistry, resourceIdHash> _customRegistries;
		bool _registriesFrozen = false;
		bool _started = false;

		modRuntimePort& runtimeFor(modCodeKind kind);
		void loadEntrypoint(const contentPack& pack, modCodeKind kind);
		void loadRegistryDocuments(const contentPackSet& packs);
		void freezeRegistries();

	public:
		gameModHost(blockRegistryPort& blocks, worldPort& world, logPort& logger);
		~gameModHost() override;

		gameModHost(const gameModHost&) = delete;
		gameModHost& operator=(const gameModHost&) = delete;

		void addRuntime(std::unique_ptr<modRuntimePort> runtime);
		void installPorts(optionalGamePorts ports);
		void start(const contentPackSet& packs);
		void stop() noexcept;
		void publishTick(uint64_t tick, uint8_t subtick, float deltaTime);
		const commandRegistry& registeredCommands() const noexcept { return _commands; }

		blockRegistryPort& blocks() override { return _blocks; }
		worldPort& world() override { return _world; }
		commandRegistry& commands() override { return _commands; }
		gameEventBus& events() override { return _events; }
		logPort& logger() override { return _logger; }
		entityPort* entities() noexcept override { return _optionalPorts.entities; }
		presentationPort* presentation() noexcept override { return _optionalPorts.presentation; }
		uiPort* ui() noexcept override { return _optionalPorts.ui; }
		storagePort* storage() noexcept override { return _optionalPorts.storage; }
		registryCatalogPort* registries() noexcept override { return this; }
		networkPort* network() noexcept override { return _optionalPorts.network; }

		std::optional<uint32_t> find(
			const resourceId& registry, const resourceId& entry) const override;
		std::optional<resourceId> persistentId(
			const resourceId& registry, uint32_t handle) const override;
		uint32_t size(const resourceId& registry) const override;
		bool add(const resourceId& registry, const resourceId& entry, std::string value) override;
		std::optional<std::string> value(
			const resourceId& registry, uint32_t handle) const override;
		bool frozen() const noexcept override { return _registriesFrozen; }
	};
}
