#include "wasmRuntime.h"

#include <array>
#include <fstream>
#include <stdexcept>
#include <string>

namespace ac::modding {
	namespace {
		class wasmModule final : public modModule {
			resourceId _id;
			std::unique_ptr<wasmInstancePort> _instance;

		public:
			wasmModule(resourceId id, std::unique_ptr<wasmInstancePort> instance)
				: _id(std::move(id)), _instance(std::move(instance)) {}

			const resourceId& id() const noexcept override { return _id; }
			void start(gameApi& api) override { _instance->initialize(api); }
			void stop(gameApi& api) noexcept override { _instance->shutdown(api); }
		};

		void validateWasmModule(const std::filesystem::path& path) {
			std::ifstream input(path, std::ios::binary);
			if (!input) throw std::runtime_error("Cannot open WASM module: " + path.string());
			std::array<unsigned char, 8> header{};
			input.read(reinterpret_cast<char*>(header.data()), header.size());
			static constexpr std::array<unsigned char, 8> expected{
				0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00
			};
			if (input.gcount() != static_cast<std::streamsize>(header.size()) || header != expected)
				throw std::runtime_error("Invalid or unsupported WASM module: " + path.string());
		}
	}

	wasmModRuntime::wasmModRuntime(std::unique_ptr<wasmBackendPort> backend)
		: _backend(std::move(backend)) {
		if (!_backend) throw std::invalid_argument("WASM runtime requires a backend");
	}

	std::string_view wasmModRuntime::name() const noexcept { return _backend->name(); }
	bool wasmModRuntime::supports(modCodeKind kind) const noexcept {
		return kind == modCodeKind::wasm;
	}

	std::unique_ptr<modModule> wasmModRuntime::load(
		const contentPack& pack,
		modCodeKind kind,
		gameApi& api
	) {
		if (kind != modCodeKind::wasm || !pack.entrypoints.wasm)
			throw std::invalid_argument("Pack has no WASM entrypoint: " + pack.id);
		validateWasmModule(*pack.entrypoints.wasm);
		std::unique_ptr<wasmInstancePort> instance =
			_backend->instantiate(pack, *pack.entrypoints.wasm, api);
		if (!instance) throw std::runtime_error("WASM backend returned no instance for " + pack.id);
		return std::make_unique<wasmModule>(
			resourceId(pack.id + ":main"), std::move(instance));
	}
}
