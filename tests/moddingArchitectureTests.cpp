#include <modding/gameApiAdapters.h>
#include <modding/modHost.h>
#include <modding/runtimeRegistry.h>
#include <modding/wasmRuntime.h>
#include <modding/wasmtimeBackend.h>

#include <wasmtime/wat.hh>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {
	class testBlocks final : public ac::modding::blockRegistryPort {
	public:
		std::optional<uint32_t> find(const ac::modding::resourceId& id) const override {
			return id.string() == "core:stone" ? std::optional<uint32_t>(1u) : std::nullopt;
		}
		std::optional<ac::modding::blockDescription> describe(uint32_t id) const override {
			if (id != 1u) return std::nullopt;
			return ac::modding::blockDescription{
				ac::modding::resourceId("core:stone"), 1u, true, false, 1.5f
			};
		}
		std::optional<ac::modding::resourceId> persistentId(uint32_t id) const override {
			return id == 1u
				? std::optional<ac::modding::resourceId>(ac::modding::resourceId("core:stone"))
				: std::nullopt;
		}
	};

	class testWorld final : public ac::modding::worldPort {
	public:
		uint32_t writtenState = 0;

		std::optional<uint32_t> readBlock(ac::modding::blockPosition position) const override {
			return position.x == 1 && position.y == 2 && position.z == 3
				? std::optional<uint32_t>(7u)
				: std::nullopt;
		}
		bool writeBlock(
			ac::modding::blockPosition position,
			uint32_t state,
			const ac::modding::worldWriteOptions&) override {
			if (position.x != 1 || position.y != 2 || position.z != 3) return false;
			writtenState = state;
			return true;
		}
	};

	class testLog final : public ac::modding::logPort {
	public:
		std::vector<std::string> messages;
		void write(ac::modding::logLevel, std::string_view, std::string_view message) override {
			messages.emplace_back(message);
		}
	};

	class testOutput final : public ac::modding::commandOutput {
	public:
		std::vector<std::string> messages;
		void reply(std::string_view message) override { messages.emplace_back(message); }
	};
}

int runChecks() {
	using namespace ac::modding;

	runtimeRegistry<int> deterministicRegistry;
	deterministicRegistry.add(resourceId("test:z"), 2);
	deterministicRegistry.add(resourceId("test:a"), 1);
	deterministicRegistry.freeze();
	if (deterministicRegistry.handle(resourceId("test:a")) != 0u ||
		deterministicRegistry.handle(resourceId("test:z")) != 1u) {
		std::cerr << "runtime registry handles are not deterministic\n";
		return 1;
	}

	static constexpr std::string_view wat = R"WAT(
(module
  (import "ac" "log" (func $log (param i32 i32 i32)))
  (import "ac" "reply" (func $reply (param i32 i32)))
  (import "ac" "block_find" (func $block_find (param i32 i32) (result i32)))
  (import "ac" "block_read" (func $block_read (param i32 i32 i32) (result i64)))
  (import "ac" "block_write" (func $block_write (param i32 i32 i32 i32 i32) (result i32)))
  (import "ac" "register_command" (func $register_command (param i32 i32) (result i32)))
  (import "ac" "registry_add" (func $registry_add (param i32 i32 i32 i32 i32 i32) (result i32)))
  (import "ac" "particle_burst" (func $particle_burst (param f32 f32 f32 i32 i32 f32 f32 f32) (result i32)))
  (import "ac" "ui_notify" (func $ui_notify (param i32 i32)))
  (import "ac" "storage_write" (func $storage_write (param i32 i32 i32 i32) (result i32)))
  (import "ac" "network_available" (func $network_available (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "wasm initialized")
  (data (i32.const 32) "tick")
  (data (i32.const 48) "hello")
  (data (i32.const 64) "hello from wasm")
  (data (i32.const 96) "core:stone")
  (data (i32.const 128) "smoke:effects")
  (data (i32.const 144) "smoke:glow")
  (data (i32.const 160) "value")
  (data (i32.const 176) "state.bin")
  (data (i32.const 192) "saved")
  (data (i32.const 208) "ui")
  (func (export "ac_init") (param $version i32) (result i32)
    i32.const 1 i32.const 0 i32.const 16 call $log
    i32.const 96 i32.const 10 call $block_find
    i32.const 1 i32.ne
    if i32.const 2 return end
    i32.const 1 i32.const 2 i32.const 3 call $block_read
    i64.const 4294967303 i64.ne
    if i32.const 3 return end
    i32.const 1 i32.const 2 i32.const 3 i32.const 9 i32.const 15 call $block_write
    i32.const 1 i32.ne
    if i32.const 4 return end
    i32.const 48 i32.const 5 call $register_command drop
    i32.const 128 i32.const 13 i32.const 144 i32.const 10
      i32.const 160 i32.const 5 call $registry_add
    i32.const 1 i32.ne
    if i32.const 5 return end
    f32.const 1 f32.const 2 f32.const 3 i32.const -1 i32.const 4
      f32.const 2 f32.const 1 f32.const 0.1 call $particle_burst drop
    i32.const 208 i32.const 2 call $ui_notify
    i32.const 176 i32.const 9 i32.const 192 i32.const 5 call $storage_write
    i32.const 1 i32.ne
    if i32.const 6 return end
    call $network_available
    if i32.const 7 return end
    i32.const 0)
  (func (export "ac_shutdown"))
  (func (export "ac_server_tick") (param i64 i32 f32)
    i32.const 0 i32.const 32 i32.const 4 call $log)
  (func (export "ac_block_changing")
    (param i32 i32 i32 i32 i32) (result i64)
    i64.const 42)
  (func (export "ac_block_changed") (param i32 i32 i32 i32 i32))
  (func (export "ac_command") (param i32 i32) (result i32)
    i32.const 64 i32.const 15 call $reply
    i32.const 0))
)WAT";

	auto binary = wasmtime::wat2wasm(wat);
	if (!binary) {
		std::cerr << "could not compile smoke WAT: " << binary.err().message() << '\n';
		return 2;
	}

	const std::filesystem::path directory =
		std::filesystem::temp_directory_path() / "ac-wasm-modding-smoke";
	std::filesystem::remove_all(directory);
	std::filesystem::create_directories(directory);
	{
		directoryStoragePort storage([&directory]() { return directory; });
		const std::vector<uint8_t> value{ 'm', 'o', 'd' };
		if (!storage.write("smoke", "settings/state.bin", value) ||
			storage.read("smoke", "settings/state.bin") != value ||
			storage.write("smoke", "../escape.bin", value)) {
			std::cerr << "namespaced mod storage confinement failed\n";
			return 7;
		}
	}
	const std::filesystem::path modulePath = directory / "smoke.wasm";
	{
		std::ofstream output(modulePath, std::ios::binary);
		const std::vector<uint8_t>& bytes = binary.ok_ref();
		output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	}

	testBlocks blocks;
	testWorld world;
	testLog log;
	{
		gameModHost registryHost(blocks, world, log);
		const resourceId registry("smoke:effects");
		const resourceId entry("smoke:glow");
		if (!registryHost.registries()->add(registry, entry, "{\"strength\":2}") ) {
			std::cerr << "custom registry staging failed\n";
			return 8;
		}
		ac::contentPackSet emptyPacks;
		registryHost.start(emptyPacks);
		if (registryHost.registries()->find(registry, entry) != 0u ||
			registryHost.registries()->value(registry, 0u) != "{\"strength\":2}" ||
			registryHost.registries()->add(registry, resourceId("smoke:late"), "late")) {
			std::cerr << "custom registry freeze failed\n";
			return 9;
		}
		registryHost.stop();
	}
	gameModHost host(blocks, world, log);
	uint32_t particleCount = 0;
	callbackPresentationPort presentation([&particleCount](const particleBurst& burst) {
		particleCount += burst.count;
		return true;
	});
	std::vector<std::string> uiMessages;
	callbackUiPort ui([]() { return std::string("Playing"); },
		[&uiMessages](std::string_view message) { uiMessages.emplace_back(message); });
	directoryStoragePort modStorage([&directory]() { return directory; });
	callbackNetworkPort network([]() { return false; },
		[](const resourceId&, std::span<const uint8_t>) { return false; });
	host.installPorts({ nullptr, &presentation, &ui, &modStorage, nullptr, &network });
	wasmModRuntime runtime(std::make_unique<wasmtimeBackend>());
	ac::contentPack pack;
	pack.id = "smoke";
	pack.root = directory;
	pack.entrypoints.wasm = modulePath;
	pack.capabilities = {
		"registry.blocks.read", "world.read", "world.write", "commands.register",
		"registries.write", "render.particles", "ui.notify", "storage.write"
	};

	std::unique_ptr<modModule> module = runtime.load(pack, modCodeKind::wasm, host);
	module->start(host);
	if (world.writtenState != 9u) {
		std::cerr << "WASM world port did not receive block_write\n";
		return 3;
	}
	const auto stored = modStorage.read("smoke", "state.bin");
	if (particleCount != 4u || uiMessages != std::vector<std::string>{ "ui" } ||
		!stored || *stored != std::vector<uint8_t>{ 's', 'a', 'v', 'e', 'd' } ||
		host.registries()->size(resourceId("smoke:effects")) != 1u) {
		std::cerr << "extended WASM service ports failed\n";
		return 10;
	}

	testOutput output;
	if (host.commands().execute("/hello", host, output) != commandExecution::executed ||
		output.messages != std::vector<std::string>{ "hello from wasm" }) {
		std::cerr << "WASM command registration or callback failed\n";
		return 4;
	}

	host.publishTick(12u, 0u, 1.0f / 128.0f);
	blockChangingEvent changing{ { 4, 5, 6 }, 1u, 2u };
	if (host.events().publish(changing) != eventResult::continueDispatch ||
		changing.nextState != 42u) {
		std::cerr << "WASM block interception failed\n";
		return 5;
	}
	if (std::find(log.messages.begin(), log.messages.end(), "wasm initialized") == log.messages.end() ||
		std::find(log.messages.begin(), log.messages.end(), "tick") == log.messages.end()) {
		std::cerr << "WASM logging or tick dispatch failed\n";
		return 6;
	}

	module->stop(host);
	host.events().removeOwner(module->id());
	host.commands().removeOwner(module->id().nameSpace());
	module.reset();
	std::filesystem::remove_all(directory);
	std::cout << "modding architecture checks passed\n";
	return 0;
}

int main() {
	try {
		return runChecks();
	}
	catch (const std::exception& error) {
		std::cerr << "unhandled exception: " << error.what() << '\n';
		return 100;
	}
}
