#pragma once

#include "gameApi.h"
#include "commandRegistry.h"

#include <cctype>
#include <functional>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <utility>

#include <Windows.h>

namespace ac::modding {
	class callbackCommandOutput final : public commandOutput {
		std::function<void(std::string_view)> _reply;

	public:
		explicit callbackCommandOutput(std::function<void(std::string_view)> reply)
			: _reply(std::move(reply)) {}
		void reply(std::string_view message) override { _reply(message); }
	};

	class callbackBlockRegistryPort final : public blockRegistryPort {
	public:
		using findCallback = std::function<std::optional<uint32_t>(const resourceId&)>;
		using describeCallback = std::function<std::optional<blockDescription>(uint32_t)>;
		using persistentIdCallback = std::function<std::optional<resourceId>(uint32_t)>;

	private:
		findCallback _find;
		describeCallback _describe;
		persistentIdCallback _persistentId;

	public:
		callbackBlockRegistryPort(
			findCallback find,
			describeCallback describe,
			persistentIdCallback persistentId
		) : _find(std::move(find)), _describe(std::move(describe)),
			_persistentId(std::move(persistentId)) {}

		std::optional<uint32_t> find(const resourceId& id) const override { return _find(id); }
		std::optional<blockDescription> describe(uint32_t id) const override { return _describe(id); }
		std::optional<resourceId> persistentId(uint32_t id) const override { return _persistentId(id); }
	};

	class callbackWorldPort final : public worldPort {
	public:
		using readCallback = std::function<std::optional<uint32_t>(blockPosition)>;
		using writeCallback = std::function<bool(blockPosition, uint32_t, const worldWriteOptions&)>;

	private:
		readCallback _read;
		writeCallback _write;

	public:
		callbackWorldPort(readCallback read, writeCallback write)
			: _read(std::move(read)), _write(std::move(write)) {}

		std::optional<uint32_t> readBlock(blockPosition position) const override {
			return _read(position);
		}
		bool writeBlock(blockPosition position, uint32_t state, const worldWriteOptions& options) override {
			return _write(position, state, options);
		}
	};

	class streamLogPort final : public logPort {
	public:
		void write(logLevel level, std::string_view owner, std::string_view message) override {
			std::ostream& stream = level == logLevel::error ? std::cerr : std::cout;
			stream << "[mod:" << owner << "] " << message << '\n';
		}
	};

	class callbackEntityPort final : public entityPort {
	public:
		using spawnCallback = std::function<std::optional<entityId>(const entitySpawnRequest&)>;
		using describeCallback = std::function<std::optional<entityDescription>(entityId)>;
		using allCallback = std::function<std::vector<entityId>()>;
		using teleportCallback = std::function<bool(entityId, float3)>;
		using damageCallback = std::function<bool(entityId, float, float3, float)>;
		using removeCallback = std::function<bool(entityId)>;

	private:
		spawnCallback _spawn;
		describeCallback _describe;
		allCallback _all;
		teleportCallback _teleport;
		damageCallback _damage;
		removeCallback _remove;

	public:
		callbackEntityPort(spawnCallback spawn, describeCallback describe, allCallback all,
			teleportCallback teleport, damageCallback damage, removeCallback remove)
			: _spawn(std::move(spawn)), _describe(std::move(describe)), _all(std::move(all)),
			_teleport(std::move(teleport)), _damage(std::move(damage)), _remove(std::move(remove)) {}

		std::optional<entityId> spawn(const entitySpawnRequest& value) override { return _spawn(value); }
		std::optional<entityDescription> describe(entityId id) const override { return _describe(id); }
		std::vector<entityId> all() const override { return _all(); }
		bool teleport(entityId id, float3 value) override { return _teleport(id, value); }
		bool damage(entityId id, float amount, float3 direction, float knockback) override {
			return _damage(id, amount, direction, knockback);
		}
		bool remove(entityId id) override { return _remove(id); }
	};

	class callbackPresentationPort final : public presentationPort {
		std::function<bool(const particleBurst&)> _emit;
	public:
		explicit callbackPresentationPort(std::function<bool(const particleBurst&)> emit)
			: _emit(std::move(emit)) {}
		bool emit(const particleBurst& burst) override { return _emit(burst); }
	};

	class callbackUiPort final : public uiPort {
		std::function<std::string()> _screen;
		std::function<void(std::string_view)> _notify;
	public:
		callbackUiPort(std::function<std::string()> screen,
			std::function<void(std::string_view)> notify)
			: _screen(std::move(screen)), _notify(std::move(notify)) {}
		std::string currentScreen() const override { return _screen(); }
		void notify(std::string_view message) override { _notify(message); }
	};

	class callbackStoragePort final : public storagePort {
	public:
		using readCallback = std::function<std::optional<std::vector<uint8_t>>(
			std::string_view, std::string_view)>;
		using writeCallback = std::function<bool(
			std::string_view, std::string_view, std::span<const uint8_t>)>;
		using eraseCallback = std::function<bool(std::string_view, std::string_view)>;
	private:
		readCallback _read;
		writeCallback _write;
		eraseCallback _erase;
	public:
		callbackStoragePort(readCallback read, writeCallback write, eraseCallback erase)
			: _read(std::move(read)), _write(std::move(write)), _erase(std::move(erase)) {}
		std::optional<std::vector<uint8_t>> read(
			std::string_view owner, std::string_view key) const override { return _read(owner, key); }
		bool write(std::string_view owner, std::string_view key,
			std::span<const uint8_t> value) override { return _write(owner, key, value); }
		bool erase(std::string_view owner, std::string_view key) override { return _erase(owner, key); }
	};

	class directoryStoragePort final : public storagePort {
		std::function<std::filesystem::path()> _root;
		static constexpr uintmax_t maximumValueSize = 4u * 1024u * 1024u;

		static bool safePart(std::string_view value, bool paths) {
			if (value.empty() || value.size() > 160u || value.front() == '/' || value.back() == '/')
				return false;
			std::string segment;
			for (const unsigned char character : value) {
				if (character == '/') {
					if (!paths || segment.empty() || segment == "." || segment == "..") return false;
					segment.clear();
					continue;
				}
				if (!(std::islower(character) || std::isdigit(character) ||
					character == '_' || character == '-' || character == '.')) return false;
				segment.push_back(static_cast<char>(character));
			}
			return !segment.empty() && segment != "." && segment != "..";
		}

		std::optional<std::filesystem::path> pathFor(
			std::string_view owner, std::string_view key) const {
			if (!safePart(owner, false) || !safePart(key, true)) return std::nullopt;
			const std::filesystem::path root = _root();
			if (root.empty()) return std::nullopt;
			return root / "mod_data" / std::string(owner) / std::filesystem::path(key);
		}

	public:
		explicit directoryStoragePort(std::function<std::filesystem::path()> root)
			: _root(std::move(root)) {}

		std::optional<std::vector<uint8_t>> read(
			std::string_view owner, std::string_view key) const override {
			const auto target = pathFor(owner, key);
			if (!target) return std::nullopt;
			std::error_code error;
			const uintmax_t size = std::filesystem::file_size(*target, error);
			if (error || size > maximumValueSize) return std::nullopt;
			std::ifstream input(*target, std::ios::binary);
			if (!input) return std::nullopt;
			std::vector<uint8_t> result(static_cast<size_t>(size));
			if (size != 0u && !input.read(reinterpret_cast<char*>(result.data()),
				static_cast<std::streamsize>(size))) return std::nullopt;
			return result;
		}

		bool write(std::string_view owner, std::string_view key,
			std::span<const uint8_t> value) override {
			if (value.size() > maximumValueSize) return false;
			const auto target = pathFor(owner, key);
			if (!target) return false;
			std::error_code error;
			std::filesystem::create_directories(target->parent_path(), error);
			if (error) return false;
			std::filesystem::path temporary = *target;
			temporary += ".tmp";
			std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
			if (!output) return false;
			if (!value.empty()) output.write(reinterpret_cast<const char*>(value.data()),
				static_cast<std::streamsize>(value.size()));
			output.flush();
			const bool good = output.good();
			output.close();
			if (!good) return false;
			if (MoveFileExW(temporary.c_str(), target->c_str(),
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
			std::filesystem::remove(temporary, error);
			return false;
		}

		bool erase(std::string_view owner, std::string_view key) override {
			const auto target = pathFor(owner, key);
			if (!target) return false;
			std::error_code error;
			const bool removed = std::filesystem::remove(*target, error);
			return removed && !error;
		}
	};

	class callbackRegistryCatalogPort final : public registryCatalogPort {
	public:
		using findCallback = std::function<std::optional<uint32_t>(const resourceId&, const resourceId&)>;
		using persistentIdCallback = std::function<std::optional<resourceId>(const resourceId&, uint32_t)>;
		using sizeCallback = std::function<uint32_t(const resourceId&)>;
	private:
		findCallback _find;
		persistentIdCallback _persistentId;
		sizeCallback _size;
	public:
		callbackRegistryCatalogPort(findCallback find, persistentIdCallback persistentId, sizeCallback size)
			: _find(std::move(find)), _persistentId(std::move(persistentId)), _size(std::move(size)) {}
		std::optional<uint32_t> find(const resourceId& registry, const resourceId& entry) const override {
			return _find(registry, entry);
		}
		std::optional<resourceId> persistentId(const resourceId& registry, uint32_t handle) const override {
			return _persistentId(registry, handle);
		}
		uint32_t size(const resourceId& registry) const override { return _size(registry); }
	};

	class callbackNetworkPort final : public networkPort {
		std::function<bool()> _available;
		std::function<bool(const resourceId&, std::span<const uint8_t>)> _send;
	public:
		callbackNetworkPort(std::function<bool()> available,
			std::function<bool(const resourceId&, std::span<const uint8_t>)> send)
			: _available(std::move(available)), _send(std::move(send)) {}
		bool available() const noexcept override {
			try { return _available(); } catch (...) { return false; }
		}
		bool send(const resourceId& channel, std::span<const uint8_t> payload) override {
			return _send(channel, payload);
		}
	};
}
