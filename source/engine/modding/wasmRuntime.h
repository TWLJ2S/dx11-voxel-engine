#pragma once

#include "modHost.h"

#include <filesystem>
#include <memory>

namespace ac::modding {

	// Runtime-specific WASM instances sit behind this port. The engine-facing
	// architecture does not depend on Wasmtime headers or its object model.
	class wasmInstancePort {
	public:
		virtual ~wasmInstancePort() = default;
		virtual void initialize(gameApi& api) = 0;
		virtual void shutdown(gameApi& api) noexcept = 0;
	};

	class wasmBackendPort {
	public:
		virtual ~wasmBackendPort() = default;
		virtual std::string_view name() const noexcept = 0;
		virtual std::unique_ptr<wasmInstancePort> instantiate(
			const contentPack& pack,
			const std::filesystem::path& module,
			gameApi& api) = 0;
	};

	class wasmModRuntime final : public modRuntimePort {
		std::unique_ptr<wasmBackendPort> _backend;

	public:
		explicit wasmModRuntime(std::unique_ptr<wasmBackendPort> backend);
		std::string_view name() const noexcept override;
		bool supports(modCodeKind kind) const noexcept override;
		std::unique_ptr<modModule> load(
			const contentPack& pack,
			modCodeKind kind,
			gameApi& api) override;
	};
}
