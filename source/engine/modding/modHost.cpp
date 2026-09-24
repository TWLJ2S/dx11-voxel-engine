#include "modHost.h"

#include <stdexcept>
#include <string>
#include <algorithm>

namespace ac::modding {

	gameModHost::gameModHost(
		blockRegistryPort& blocks,
		worldPort& world,
		logPort& logger
	) : _blocks(blocks), _world(world), _logger(logger) {}

	gameModHost::~gameModHost() {
		stop();
	}

	void gameModHost::addRuntime(std::unique_ptr<modRuntimePort> runtime) {
		if (_started) throw std::logic_error("Mod runtimes must be installed before the host starts");
		if (!runtime) throw std::invalid_argument("Cannot install a null mod runtime");
		_runtimes.push_back(std::move(runtime));
	}

	void gameModHost::installPorts(optionalGamePorts ports) {
		if (_started) throw std::logic_error("Game ports must be installed before the mod host starts");
		_optionalPorts = ports;
	}

	bool gameModHost::add(
		const resourceId& registry, const resourceId& entry, std::string value) {
		if (_registriesFrozen || value.size() > 1024u * 1024u) return false;
		if (_optionalPorts.registries && _optionalPorts.registries->size(registry) != 0u)
			return false;
		customRegistry& target = _customRegistries[registry];
		return target.values.emplace(entry, std::move(value)).second;
	}

	std::optional<uint32_t> gameModHost::find(
		const resourceId& registry, const resourceId& entry) const {
		if (_optionalPorts.registries) {
			const auto external = _optionalPorts.registries->find(registry, entry);
			if (external) return external;
		}
		if (!_registriesFrozen) return std::nullopt;
		const auto found = _customRegistries.find(registry);
		if (found == _customRegistries.end()) return std::nullopt;
		const auto entryFound = std::lower_bound(
			found->second.handles.begin(), found->second.handles.end(), entry);
		if (entryFound == found->second.handles.end() || *entryFound != entry) return std::nullopt;
		return static_cast<uint32_t>(std::distance(found->second.handles.begin(), entryFound));
	}

	std::optional<resourceId> gameModHost::persistentId(
		const resourceId& registry, uint32_t handle) const {
		if (_optionalPorts.registries) {
			const auto external = _optionalPorts.registries->persistentId(registry, handle);
			if (external) return external;
		}
		const auto found = _customRegistries.find(registry);
		if (found == _customRegistries.end() || handle >= found->second.handles.size())
			return std::nullopt;
		return found->second.handles[handle];
	}

	uint32_t gameModHost::size(const resourceId& registry) const {
		if (_optionalPorts.registries) {
			const uint32_t external = _optionalPorts.registries->size(registry);
			if (external != 0u) return external;
		}
		const auto found = _customRegistries.find(registry);
		return found == _customRegistries.end()
			? 0u : static_cast<uint32_t>(found->second.values.size());
	}

	std::optional<std::string> gameModHost::value(
		const resourceId& registry, uint32_t handle) const {
		const auto found = _customRegistries.find(registry);
		if (found == _customRegistries.end() || handle >= found->second.handles.size())
			return std::nullopt;
		const auto entry = found->second.values.find(found->second.handles[handle]);
		return entry == found->second.values.end()
			? std::nullopt : std::optional<std::string>(entry->second);
	}

	void gameModHost::loadRegistryDocuments(const contentPackSet& packs) {
		for (const auto& [pack, path] : packs.paths("registries")) {
			const rapidjson::Document document = jsonLoader::load(path.string());
			if (!document.IsObject() || !document.HasMember("schemaVersion") ||
				!document["schemaVersion"].IsUint() || document["schemaVersion"].GetUint() != 1u ||
				!document.HasMember("registry") || !document["registry"].IsString() ||
				!document.HasMember("entries") || !document["entries"].IsArray())
				throw std::runtime_error(path.string() + ": invalid registry document");
			const resourceId registry = resourceId::qualify(pack->id, document["registry"].GetString());
			for (const rapidjson::Value& item : document["entries"].GetArray()) {
				if (!item.IsObject() || !item.HasMember("id") || !item["id"].IsString() ||
					!item.HasMember("value") || !item["value"].IsString())
					throw std::runtime_error(path.string() + ": registry entries require string id and value");
				const resourceId entry = resourceId::qualify(pack->id, item["id"].GetString());
				if (!add(registry, entry, item["value"].GetString()))
					throw std::runtime_error(path.string() + ": duplicate or immutable registry entry " +
						entry.string());
			}
		}
	}

	void gameModHost::freezeRegistries() {
		for (auto& [id, registry] : _customRegistries) {
			(void)id;
			registry.handles.clear();
			registry.handles.reserve(registry.values.size());
			for (const auto& [entry, value] : registry.values) {
				(void)value;
				registry.handles.push_back(entry);
			}
			std::sort(registry.handles.begin(), registry.handles.end());
		}
		_registriesFrozen = true;
	}

	modRuntimePort& gameModHost::runtimeFor(modCodeKind kind) {
		for (const auto& runtime : _runtimes)
			if (runtime->supports(kind)) return *runtime;
		throw std::runtime_error(kind == modCodeKind::wasm
			? "A pack declares a WASM entrypoint, but no WASM runtime is installed"
			: "A pack declares a native entrypoint, but native mods are disabled");
	}

	void gameModHost::loadEntrypoint(const contentPack& pack, modCodeKind kind) {
		modRuntimePort& runtime = runtimeFor(kind);
		std::unique_ptr<modModule> module = runtime.load(pack, kind, *this);
		if (!module) throw std::runtime_error(
			"Runtime '" + std::string(runtime.name()) + "' returned no module for pack '" + pack.id + "'");
		try {
			module->start(*this);
		}
		catch (...) {
			try { module->stop(*this); }
			catch (...) {}
			_events.removeOwner(module->id());
			_commands.removeOwner(module->id().nameSpace());
			throw;
		}
		_logger.write(logLevel::info, pack.id,
			"Loaded code module using " + std::string(runtime.name()));
		_modules.push_back(std::move(module));
	}

	void gameModHost::start(const contentPackSet& packs) {
		if (_started) throw std::logic_error("Mod host is already running");
		_registriesFrozen = false;
		loadRegistryDocuments(packs);
		engineStartingEvent starting;
		_events.publish(starting);

		try {
			for (const contentPack& pack : packs.ordered()) {
				if (pack.entrypoints.wasm) loadEntrypoint(pack, modCodeKind::wasm);
				if (pack.entrypoints.native) loadEntrypoint(pack, modCodeKind::native);
			}
			registriesMutableEvent mutableRegistries;
			_events.publish(mutableRegistries);
			freezeRegistries();
			registriesFrozenEvent frozen;
			_events.publish(frozen);
			_started = true;
			engineStartedEvent started;
			_events.publish(started);
		}
		catch (...) {
			stop();
			throw;
		}
	}

	void gameModHost::stop() noexcept {
		if (_started) {
			engineStoppingEvent stopping;
			_events.publish(stopping);
		}
		for (auto iterator = _modules.rbegin(); iterator != _modules.rend(); ++iterator) {
			const resourceId owner = (*iterator)->id();
			try { (*iterator)->stop(*this); }
			catch (...) {}
			_events.removeOwner(owner);
			_commands.removeOwner(owner.nameSpace());
		}
		_modules.clear();
		_customRegistries.clear();
		_registriesFrozen = false;
		_started = false;
	}

	void gameModHost::publishTick(uint64_t tick, uint8_t subtick, float deltaTime) {
		serverTickEvent event{ tick, subtick, deltaTime };
		_events.publish(event);
	}
}
