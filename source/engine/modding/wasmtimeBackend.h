#pragma once

#include "wasmRuntime.h"

#include <memory>

namespace ac::modding {

	class wasmtimeBackend final : public wasmBackendPort {
		class implementation;
		std::unique_ptr<implementation> _implementation;

	public:
		wasmtimeBackend();
		~wasmtimeBackend() override;

		wasmtimeBackend(const wasmtimeBackend&) = delete;
		wasmtimeBackend& operator=(const wasmtimeBackend&) = delete;

		std::string_view name() const noexcept override;
		std::unique_ptr<wasmInstancePort> instantiate(
			const contentPack& pack,
			const std::filesystem::path& module,
			gameApi& api) override;
	};
}
