#include "wasmtimeBackend.h"

#include <wasmtime.hh>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <unordered_set>
#include <variant>
#include <vector>

namespace ac::modding {
	namespace {
		constexpr uint64_t fuelPerCall = 5'000'000u;

		std::vector<uint8_t> readModule(const std::filesystem::path& path) {
			std::ifstream input(path, std::ios::binary | std::ios::ate);
			if (!input) throw std::runtime_error("Cannot open WASM module: " + path.string());
			const std::streamsize size = input.tellg();
			if (size <= 0) throw std::runtime_error("WASM module is empty: " + path.string());
			input.seekg(0, std::ios::beg);
			std::vector<uint8_t> bytes(static_cast<size_t>(size));
			if (!input.read(reinterpret_cast<char*>(bytes.data()), size))
				throw std::runtime_error("Cannot read WASM module: " + path.string());
			return bytes;
		}

		wasmtime::Engine makeEngine() {
			wasmtime::Config config;
			config.consume_fuel(true);
			config.max_wasm_stack(2u * 1024u * 1024u);
			return wasmtime::Engine(std::move(config));
		}

		class wasmtimeInstance final : public wasmInstancePort {
			resourceId _owner;
			std::string _packId;
			std::unordered_set<std::string> _capabilities;
			gameApi& _api;
			wasmtime::Store _store;
			std::optional<wasmtime::Instance> _instance;
			std::optional<wasmtime::Func> _initialize;
			std::optional<wasmtime::Func> _shutdown;
			std::optional<wasmtime::Func> _serverTick;
			std::optional<wasmtime::Func> _blockChanging;
			std::optional<wasmtime::Func> _blockChanged;
			std::optional<wasmtime::Func> _worldOpened;
			std::optional<wasmtime::Func> _worldSaving;
			std::optional<wasmtime::Func> _worldSaved;
			std::optional<wasmtime::Func> _worldClosed;
			std::optional<wasmtime::Func> _resourcesReloaded;
			std::optional<wasmtime::Func> _registriesFrozen;
			std::optional<wasmtime::Func> _engineStarted;
			std::optional<wasmtime::Func> _command;
			std::vector<std::string> _commandNames;
			const std::vector<std::string>* _activeArguments = nullptr;
			commandOutput* _activeOutput = nullptr;
			bool _started = false;

			bool permitted(std::string_view capability) const {
				return _capabilities.contains(std::string(capability));
			}

			void log(logLevel level, std::string_view message) {
				_api.logger().write(level, _packId, message);
			}

			std::optional<std::string_view> guestString(
				wasmtime::Caller caller,
				int32_t pointer,
				int32_t length
			) {
				if (pointer < 0 || length < 0) return std::nullopt;
				const auto memoryExport = caller.get_export("memory");
				if (!memoryExport) return std::nullopt;
				const auto* memory = std::get_if<wasmtime::Memory>(&*memoryExport);
				if (!memory) return std::nullopt;
				const std::span<uint8_t> bytes = memory->data(caller.context());
				const size_t start = static_cast<size_t>(pointer);
				const size_t count = static_cast<size_t>(length);
				if (start > bytes.size() || count > bytes.size() - start) return std::nullopt;
				return std::string_view(
					reinterpret_cast<const char*>(bytes.data() + start), count);
			}

			std::optional<std::span<const uint8_t>> guestBytes(
				wasmtime::Caller caller, int32_t pointer, int32_t length) {
				if (pointer < 0 || length < 0) return std::nullopt;
				const auto memoryExport = caller.get_export("memory");
				if (!memoryExport) return std::nullopt;
				const auto* memory = std::get_if<wasmtime::Memory>(&*memoryExport);
				if (!memory) return std::nullopt;
				const std::span<uint8_t> bytes = memory->data(caller.context());
				const size_t start = static_cast<size_t>(pointer);
				const size_t count = static_cast<size_t>(length);
				if (start > bytes.size() || count > bytes.size() - start) return std::nullopt;
				return std::span<const uint8_t>(bytes.data() + start, count);
			}

			int32_t writeGuest(
				wasmtime::Caller caller,
				int32_t pointer,
				int32_t capacity,
				std::string_view value
			) {
				if (pointer < 0 || capacity < 0) return -1;
				const auto memoryExport = caller.get_export("memory");
				if (!memoryExport) return -1;
				const auto* memory = std::get_if<wasmtime::Memory>(&*memoryExport);
				if (!memory) return -1;
				const std::span<uint8_t> bytes = memory->data(caller.context());
				const size_t start = static_cast<size_t>(pointer);
				const size_t count = std::min(value.size(), static_cast<size_t>(capacity));
				if (start > bytes.size() || count > bytes.size() - start) return -1;
				std::memcpy(bytes.data() + start, value.data(), count);
				return static_cast<int32_t>(count);
			}

			int32_t writeGuestBytes(
				wasmtime::Caller caller, int32_t pointer, int32_t capacity,
				std::span<const uint8_t> value) {
				if (pointer < 0 || capacity < 0) return -1;
				const auto memoryExport = caller.get_export("memory");
				if (!memoryExport) return -1;
				const auto* memory = std::get_if<wasmtime::Memory>(&*memoryExport);
				if (!memory) return -1;
				const std::span<uint8_t> bytes = memory->data(caller.context());
				const size_t start = static_cast<size_t>(pointer);
				const size_t count = std::min(value.size(), static_cast<size_t>(capacity));
				if (start > bytes.size() || count > bytes.size() - start) return -1;
				if (count != 0u) std::memcpy(bytes.data() + start, value.data(), count);
				return static_cast<int32_t>(count);
			}

			static void requireLink(
				wasmtime::Result<std::monostate> result,
				std::string_view function
			) {
				if (!result)
					throw std::runtime_error(
						"Cannot define WASM host function '" + std::string(function) +
						"': " + result.err().message());
			}

			std::optional<wasmtime::Func> exportedFunction(std::string_view name) {
				const auto item = _instance->get(_store, name);
				if (!item) return std::nullopt;
				const auto* function = std::get_if<wasmtime::Func>(&*item);
				if (!function)
					throw std::runtime_error("WASM export '" + std::string(name) + "' is not a function");
				return *function;
			}

			std::vector<wasmtime::Val> call(
				const wasmtime::Func& function,
				std::initializer_list<wasmtime::Val> arguments,
				std::string_view exportName
			) {
				auto fuel = _store.context().set_fuel(fuelPerCall);
				if (!fuel) throw std::runtime_error("Cannot replenish WASM fuel: " + fuel.err().message());
				auto result = function.call(_store, arguments);
				if (!result)
					throw std::runtime_error(
						"WASM export '" + std::string(exportName) + "' trapped: " +
						result.err().message());
				return result.ok();
			}

			void callNoexcept(
				const wasmtime::Func& function,
				std::initializer_list<wasmtime::Val> arguments,
				std::string_view exportName
			) noexcept {
				try { (void)call(function, arguments, exportName); }
				catch (const std::exception& error) { log(logLevel::error, error.what()); }
			}

			void executeCommand(int32_t commandId, commandContext& context) {
				if (!_command)
					throw std::runtime_error("Module registered a command without exporting ac_command");
				if (_activeArguments || _activeOutput)
					throw std::runtime_error("Re-entrant WASM command execution is not supported");
				_activeArguments = &context.arguments;
				_activeOutput = &context.output;
				try {
					const std::vector<wasmtime::Val> result = call(
						*_command,
						{ commandId, static_cast<int32_t>(context.arguments.size()) },
						"ac_command");
					if (!result.empty() && result.front().i32() != 0)
						throw std::runtime_error("WASM command returned failure status " +
							std::to_string(result.front().i32()));
				}
				catch (...) {
					_activeArguments = nullptr;
					_activeOutput = nullptr;
					throw;
				}
				_activeArguments = nullptr;
				_activeOutput = nullptr;
			}

			void defineImports(wasmtime::Linker& linker) {
				requireLink(linker.func_wrap("ac", "log",
					[this](wasmtime::Caller caller, int32_t level, int32_t pointer, int32_t length) {
						const auto message = guestString(caller, pointer, length);
						if (!message) return;
						const logLevel bounded = static_cast<logLevel>(std::clamp(level, 0, 3));
						log(bounded, *message);
					}), "log");

				requireLink(linker.func_wrap("ac", "reply",
					[this](wasmtime::Caller caller, int32_t pointer, int32_t length) {
						const auto message = guestString(caller, pointer, length);
						if (_activeOutput && message) _activeOutput->reply(*message);
					}), "reply");

				requireLink(linker.func_wrap("ac", "block_find",
					[this](wasmtime::Caller caller, int32_t pointer, int32_t length) -> int32_t {
						if (!permitted("registry.blocks.read")) return -1;
						const auto name = guestString(caller, pointer, length);
						if (!name) return -1;
						try {
							const auto id = _api.blocks().find(resourceId(std::string(*name)));
							return id ? static_cast<int32_t>(*id) : -1;
						}
						catch (...) { return -1; }
					}), "block_find");

				requireLink(linker.func_wrap("ac", "block_read",
					[this](int32_t x, int32_t y, int32_t z) -> int64_t {
						if (!permitted("world.read")) return int64_t{ 0 };
						const auto state = _api.world().readBlock({ x, y, z });
						return state
							? static_cast<int64_t>((uint64_t{ 1 } << 32u) | *state)
							: int64_t{ 0 };
					}), "block_read");

				requireLink(linker.func_wrap("ac", "block_write",
					[this](int32_t x, int32_t y, int32_t z, int32_t state, int32_t flags) -> int32_t {
						if (!permitted("world.write")) return 0;
						worldWriteOptions options;
						options.notifyFluids = (flags & 1) != 0;
						options.notifyRedstone = (flags & 2) != 0;
						options.notifyFallingBlocks = (flags & 4) != 0;
						options.immediateRemesh = (flags & 8) != 0;
						return _api.world().writeBlock(
							{ x, y, z }, static_cast<uint32_t>(state), options) ? 1 : 0;
					}), "block_write");

				requireLink(linker.func_wrap("ac", "register_command",
					[this](wasmtime::Caller caller, int32_t pointer, int32_t length) -> int32_t {
						if (!permitted("commands.register")) return -1;
						const auto name = guestString(caller, pointer, length);
						if (!name || name->empty()) return -1;
						try {
							const int32_t commandId = static_cast<int32_t>(_commandNames.size());
							const std::string commandName(*name);
							commandDefinition definition{
								resourceId(_packId + ":" + commandName),
								"Command provided by " + _packId,
								"/" + commandName,
								{ commandName },
								[this, commandId](commandContext& context) {
									executeCommand(commandId, context);
								},
								{}
							};
							_api.commands().add(std::move(definition));
							_commandNames.push_back(commandName);
							return commandId;
						}
						catch (const std::exception& error) {
							log(logLevel::error, error.what());
							return -1;
						}
					}), "register_command");

				requireLink(linker.func_wrap("ac", "argument_length",
					[this](int32_t index) -> int32_t {
						if (!_activeArguments || index < 0 ||
							static_cast<size_t>(index) >= _activeArguments->size()) return -1;
						return static_cast<int32_t>((*_activeArguments)[index].size());
					}), "argument_length");

				requireLink(linker.func_wrap("ac", "argument_read",
					[this](wasmtime::Caller caller, int32_t index, int32_t pointer, int32_t capacity) -> int32_t {
						if (!_activeArguments || index < 0 ||
							static_cast<size_t>(index) >= _activeArguments->size()) return -1;
						return writeGuest(caller, pointer, capacity, (*_activeArguments)[index]);
					}), "argument_read");

				requireLink(linker.func_wrap("ac", "registry_find",
					[this](wasmtime::Caller caller, int32_t registryPointer, int32_t registryLength,
						int32_t entryPointer, int32_t entryLength) -> int32_t {
						if (!permitted("registries.read") || !_api.registries()) return -1;
						const auto registry = guestString(caller, registryPointer, registryLength);
						const auto entry = guestString(caller, entryPointer, entryLength);
						if (!registry || !entry) return -1;
						try {
							const auto handle = _api.registries()->find(
								resourceId(std::string(*registry)), resourceId(std::string(*entry)));
							return handle ? static_cast<int32_t>(*handle) : -1;
						}
						catch (...) { return -1; }
					}), "registry_find");

				requireLink(linker.func_wrap("ac", "registry_size",
					[this](wasmtime::Caller caller, int32_t pointer, int32_t length) -> int32_t {
						if (!permitted("registries.read") || !_api.registries()) return -1;
						const auto registry = guestString(caller, pointer, length);
						if (!registry) return -1;
						try { return static_cast<int32_t>(_api.registries()->size(
							resourceId(std::string(*registry)))); }
						catch (...) { return -1; }
					}), "registry_size");

				requireLink(linker.func_wrap("ac", "registry_add",
					[this](wasmtime::Caller caller, int32_t registryPointer, int32_t registryLength,
						int32_t entryPointer, int32_t entryLength,
						int32_t valuePointer, int32_t valueLength) -> int32_t {
						if (!permitted("registries.write") || !_api.registries()) return 0;
						const auto registry = guestString(caller, registryPointer, registryLength);
						const auto entry = guestString(caller, entryPointer, entryLength);
						const auto value = guestString(caller, valuePointer, valueLength);
						if (!registry || !entry || !value) return 0;
						try { return _api.registries()->add(
							resourceId(std::string(*registry)), resourceId(std::string(*entry)),
							std::string(*value)) ? 1 : 0; }
						catch (...) { return 0; }
					}), "registry_add");

				requireLink(linker.func_wrap("ac", "registry_value_length",
					[this](wasmtime::Caller caller, int32_t registryPointer,
						int32_t registryLength, int32_t handle) -> int32_t {
						if (!permitted("registries.read") || !_api.registries() || handle < 0) return -1;
						const auto registry = guestString(caller, registryPointer, registryLength);
						if (!registry) return -1;
						try {
							const auto value = _api.registries()->value(
								resourceId(std::string(*registry)), static_cast<uint32_t>(handle));
							return value ? static_cast<int32_t>(value->size()) : -1;
						}
						catch (...) { return -1; }
					}), "registry_value_length");

				requireLink(linker.func_wrap("ac", "registry_value_read",
					[this](wasmtime::Caller caller, int32_t registryPointer, int32_t registryLength,
						int32_t handle, int32_t outputPointer, int32_t capacity) -> int32_t {
						if (!permitted("registries.read") || !_api.registries() || handle < 0) return -1;
						const auto registry = guestString(caller, registryPointer, registryLength);
						if (!registry) return -1;
						try {
							const auto value = _api.registries()->value(
								resourceId(std::string(*registry)), static_cast<uint32_t>(handle));
							return value ? writeGuest(caller, outputPointer, capacity, *value) : -1;
						}
						catch (...) { return -1; }
					}), "registry_value_read");

				requireLink(linker.func_wrap("ac", "entity_spawn",
					[this](wasmtime::Caller caller, int32_t pointer, int32_t length,
						float x, float y, float z, float yaw) -> int64_t {
						if (!permitted("entities.spawn") || !_api.entities()) return int64_t{ 0 };
						const auto type = guestString(caller, pointer, length);
						if (!type) return int64_t{ 0 };
						try {
							const auto id = _api.entities()->spawn({
								resourceId(std::string(*type)), { x, y, z }, yaw, 0u });
							return id ? static_cast<int64_t>(*id) : int64_t{ 0 };
						}
						catch (...) { return int64_t{ 0 }; }
					}), "entity_spawn");

				requireLink(linker.func_wrap("ac", "entity_exists",
					[this](int64_t id) -> int32_t {
						if (!permitted("entities.read") || !_api.entities() || id <= 0) return 0;
						return _api.entities()->describe(static_cast<entityId>(id)) ? 1 : 0;
					}), "entity_exists");

				requireLink(linker.func_wrap("ac", "entity_position",
					[this](int64_t id, int32_t axis) -> float {
						if (!permitted("entities.read") || !_api.entities() || id <= 0) return 0.0f;
						const auto value = _api.entities()->describe(static_cast<entityId>(id));
						if (!value) return 0.0f;
						if (axis == 0) return value->position.x;
						if (axis == 1) return value->position.y;
						return axis == 2 ? value->position.z : 0.0f;
					}), "entity_position");

				requireLink(linker.func_wrap("ac", "entity_health",
					[this](int64_t id) -> float {
						if (!permitted("entities.read") || !_api.entities() || id <= 0) return 0.0f;
						const auto value = _api.entities()->describe(static_cast<entityId>(id));
						return value ? value->health : 0.0f;
					}), "entity_health");

				requireLink(linker.func_wrap("ac", "entity_teleport",
					[this](int64_t id, float x, float y, float z) -> int32_t {
						if (!permitted("entities.write") || !_api.entities() || id <= 0) return 0;
						return _api.entities()->teleport(
							static_cast<entityId>(id), { x, y, z }) ? 1 : 0;
					}), "entity_teleport");

				requireLink(linker.func_wrap("ac", "entity_damage",
					[this](int64_t id, float amount, float x, float y, float z, float knockback) -> int32_t {
						if (!permitted("entities.write") || !_api.entities() || id <= 0) return 0;
						return _api.entities()->damage(static_cast<entityId>(id), amount,
							{ x, y, z }, knockback) ? 1 : 0;
					}), "entity_damage");

				requireLink(linker.func_wrap("ac", "entity_remove",
					[this](int64_t id) -> int32_t {
						if (!permitted("entities.write") || !_api.entities() || id <= 0) return 0;
						return _api.entities()->remove(static_cast<entityId>(id)) ? 1 : 0;
					}), "entity_remove");

				requireLink(linker.func_wrap("ac", "particle_burst",
					[this](float x, float y, float z, int32_t rgba, int32_t count,
						float speed, float lifetime, float size) -> int32_t {
						if (!permitted("render.particles") || !_api.presentation() || count <= 0) return 0;
						const uint32_t color = static_cast<uint32_t>(rgba);
						particleBurst burst;
						burst.position = { x, y, z };
						burst.color = {
							static_cast<float>(color & 0xffu) / 255.0f,
							static_cast<float>((color >> 8u) & 0xffu) / 255.0f,
							static_cast<float>((color >> 16u) & 0xffu) / 255.0f,
							static_cast<float>((color >> 24u) & 0xffu) / 255.0f };
						burst.count = static_cast<uint32_t>(std::clamp(count, 1, 256));
						burst.speed = speed;
						burst.lifetime = lifetime;
						burst.size = size;
						return _api.presentation()->emit(burst) ? 1 : 0;
					}), "particle_burst");

				requireLink(linker.func_wrap("ac", "ui_notify",
					[this](wasmtime::Caller caller, int32_t pointer, int32_t length) {
						if (!permitted("ui.notify") || !_api.ui()) return;
						const auto message = guestString(caller, pointer, length);
						if (message) _api.ui()->notify(*message);
					}), "ui_notify");

				requireLink(linker.func_wrap("ac", "ui_screen_length",
					[this]() -> int32_t {
						if (!permitted("ui.read") || !_api.ui()) return -1;
						return static_cast<int32_t>(_api.ui()->currentScreen().size());
					}), "ui_screen_length");

				requireLink(linker.func_wrap("ac", "ui_screen_read",
					[this](wasmtime::Caller caller, int32_t pointer, int32_t capacity) -> int32_t {
						if (!permitted("ui.read") || !_api.ui()) return -1;
						return writeGuest(caller, pointer, capacity, _api.ui()->currentScreen());
					}), "ui_screen_read");

				requireLink(linker.func_wrap("ac", "storage_length",
					[this](wasmtime::Caller caller, int32_t pointer, int32_t length) -> int32_t {
						if (!permitted("storage.read") || !_api.storage()) return -1;
						const auto key = guestString(caller, pointer, length);
						if (!key) return -1;
						const auto value = _api.storage()->read(_packId, *key);
						return value ? static_cast<int32_t>(value->size()) : -1;
					}), "storage_length");

				requireLink(linker.func_wrap("ac", "storage_read",
					[this](wasmtime::Caller caller, int32_t keyPointer, int32_t keyLength,
						int32_t outputPointer, int32_t capacity) -> int32_t {
						if (!permitted("storage.read") || !_api.storage()) return -1;
						const auto key = guestString(caller, keyPointer, keyLength);
						if (!key) return -1;
						const auto value = _api.storage()->read(_packId, *key);
						return value ? writeGuestBytes(caller, outputPointer, capacity, *value) : -1;
					}), "storage_read");

				requireLink(linker.func_wrap("ac", "storage_write",
					[this](wasmtime::Caller caller, int32_t keyPointer, int32_t keyLength,
						int32_t valuePointer, int32_t valueLength) -> int32_t {
						if (!permitted("storage.write") || !_api.storage()) return 0;
						const auto key = guestString(caller, keyPointer, keyLength);
						const auto value = guestBytes(caller, valuePointer, valueLength);
						return key && value && _api.storage()->write(_packId, *key, *value) ? 1 : 0;
					}), "storage_write");

				requireLink(linker.func_wrap("ac", "storage_remove",
					[this](wasmtime::Caller caller, int32_t pointer, int32_t length) -> int32_t {
						if (!permitted("storage.write") || !_api.storage()) return 0;
						const auto key = guestString(caller, pointer, length);
						return key && _api.storage()->erase(_packId, *key) ? 1 : 0;
					}), "storage_remove");

				requireLink(linker.func_wrap("ac", "network_available",
					[this]() -> int32_t {
						return _api.network() && _api.network()->available() ? 1 : 0;
					}), "network_available");

				requireLink(linker.func_wrap("ac", "network_send",
					[this](wasmtime::Caller caller, int32_t channelPointer, int32_t channelLength,
						int32_t payloadPointer, int32_t payloadLength) -> int32_t {
						if (!permitted("network.send") || !_api.network() || !_api.network()->available()) return 0;
						const auto channel = guestString(caller, channelPointer, channelLength);
						const auto payload = guestBytes(caller, payloadPointer, payloadLength);
						if (!channel || !payload) return 0;
						try { return _api.network()->send(
							resourceId(std::string(*channel)), *payload) ? 1 : 0; }
						catch (...) { return 0; }
					}), "network_send");
			}

		public:
			wasmtimeInstance(
				wasmtime::Engine& engine,
				const contentPack& pack,
				const std::filesystem::path& modulePath,
				gameApi& api
			) : _owner(pack.id + ":main"), _packId(pack.id),
				_capabilities(pack.capabilities), _api(api), _store(engine) {
				auto initialFuel = _store.context().set_fuel(fuelPerCall);
				if (!initialFuel)
					throw std::runtime_error(
						"Cannot initialize WASM fuel: " + initialFuel.err().message());
				std::vector<uint8_t> bytes = readModule(modulePath);
				auto compiled = wasmtime::Module::compile(engine, std::span<uint8_t>(bytes));
				if (!compiled)
					throw std::runtime_error("Cannot compile WASM module '" + modulePath.string() +
						"': " + compiled.err().message());

				wasmtime::Linker linker(engine);
				defineImports(linker);
				auto instance = linker.instantiate(_store, compiled.ok_ref());
				if (!instance)
					throw std::runtime_error("Cannot instantiate WASM module '" + modulePath.string() +
						"': " + instance.err().message());
				_instance = instance.ok();
				_initialize = exportedFunction("ac_init");
				_shutdown = exportedFunction("ac_shutdown");
				_serverTick = exportedFunction("ac_server_tick");
				_blockChanging = exportedFunction("ac_block_changing");
				_blockChanged = exportedFunction("ac_block_changed");
				_worldOpened = exportedFunction("ac_world_opened");
				_worldSaving = exportedFunction("ac_world_saving");
				_worldSaved = exportedFunction("ac_world_saved");
				_worldClosed = exportedFunction("ac_world_closed");
				_resourcesReloaded = exportedFunction("ac_resources_reloaded");
				_registriesFrozen = exportedFunction("ac_registries_frozen");
				_engineStarted = exportedFunction("ac_engine_started");
				_command = exportedFunction("ac_command");
				if (!_initialize)
					throw std::runtime_error("WASM module must export ac_init");
			}

			void initialize(gameApi& api) override {
				(void)api;
				const std::vector<wasmtime::Val> result =
					call(*_initialize, { static_cast<int32_t>(gameApi::version) }, "ac_init");
				if (result.empty() || result.front().i32() != 0)
					throw std::runtime_error("WASM ac_init returned failure");

				if (_serverTick) _api.events().subscribe<serverTickEvent>(
					_owner, [this](serverTickEvent& event) {
						callNoexcept(*_serverTick,
							{ static_cast<int64_t>(event.tick), static_cast<int32_t>(event.subtick), event.deltaTime },
							"ac_server_tick");
						return eventResult::continueDispatch;
					});
				if (_blockChanging) _api.events().subscribe<blockChangingEvent>(
					_owner, [this](blockChangingEvent& event) {
						try {
							const auto result = call(*_blockChanging, {
								event.position.x, event.position.y, event.position.z,
								static_cast<int32_t>(event.previousState),
								static_cast<int32_t>(event.nextState)
							}, "ac_block_changing");
							if (result.empty()) return eventResult::continueDispatch;
							const uint64_t packed = static_cast<uint64_t>(result.front().i64());
							event.nextState = static_cast<uint32_t>(packed);
							return (packed >> 32u) != 0u
								? eventResult::cancel
								: eventResult::continueDispatch;
						}
						catch (const std::exception& error) {
							log(logLevel::error, error.what());
							return eventResult::continueDispatch;
						}
					});
				if (_blockChanged) _api.events().subscribe<blockChangedEvent>(
					_owner, [this](blockChangedEvent& event) {
						callNoexcept(*_blockChanged, {
							event.position.x, event.position.y, event.position.z,
							static_cast<int32_t>(event.previousState),
							static_cast<int32_t>(event.currentState)
						}, "ac_block_changed");
						return eventResult::continueDispatch;
					});
				if (_worldOpened) _api.events().subscribe<worldOpenedEvent>(
					_owner, [this](worldOpenedEvent& event) {
						callNoexcept(*_worldOpened, { static_cast<int64_t>(event.seed) }, "ac_world_opened");
						return eventResult::continueDispatch;
					});
				if (_worldSaving) _api.events().subscribe<worldSavingEvent>(
					_owner, [this](worldSavingEvent&) {
						callNoexcept(*_worldSaving, {}, "ac_world_saving");
						return eventResult::continueDispatch;
					});
				if (_worldSaved) _api.events().subscribe<worldSavedEvent>(
					_owner, [this](worldSavedEvent&) {
						callNoexcept(*_worldSaved, {}, "ac_world_saved");
						return eventResult::continueDispatch;
					});
				if (_worldClosed) _api.events().subscribe<worldClosedEvent>(
					_owner, [this](worldClosedEvent&) {
						callNoexcept(*_worldClosed, {}, "ac_world_closed");
						return eventResult::continueDispatch;
					});
				if (_resourcesReloaded) _api.events().subscribe<resourcesReloadedEvent>(
					_owner, [this](resourcesReloadedEvent&) {
						callNoexcept(*_resourcesReloaded, {}, "ac_resources_reloaded");
						return eventResult::continueDispatch;
					});
				if (_registriesFrozen) _api.events().subscribe<registriesFrozenEvent>(
					_owner, [this](registriesFrozenEvent&) {
						callNoexcept(*_registriesFrozen, {}, "ac_registries_frozen");
						return eventResult::continueDispatch;
					});
				if (_engineStarted) _api.events().subscribe<engineStartedEvent>(
					_owner, [this](engineStartedEvent&) {
						callNoexcept(*_engineStarted, {}, "ac_engine_started");
						return eventResult::continueDispatch;
					});
				_started = true;
			}

			void shutdown(gameApi& api) noexcept override {
				(void)api;
				if (_started && _shutdown) callNoexcept(*_shutdown, {}, "ac_shutdown");
				_started = false;
			}
		};
	}

	class wasmtimeBackend::implementation final {
	public:
		wasmtime::Engine engine = makeEngine();
	};

	wasmtimeBackend::wasmtimeBackend()
		: _implementation(std::make_unique<implementation>()) {}
	wasmtimeBackend::~wasmtimeBackend() = default;

	std::string_view wasmtimeBackend::name() const noexcept { return "Wasmtime 49"; }

	std::unique_ptr<wasmInstancePort> wasmtimeBackend::instantiate(
		const contentPack& pack,
		const std::filesystem::path& module,
		gameApi& api
	) {
		return std::make_unique<wasmtimeInstance>(
			_implementation->engine, pack, module, api);
	}
}
