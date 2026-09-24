#pragma once

#include "gpuAsset.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ac {
	struct contentPackEntrypoints {
		std::optional<std::filesystem::path> wasm;
		std::optional<std::filesystem::path> native;
	};

    struct contentPack {
        std::string id;
        std::string version;
        std::filesystem::path root;
        std::vector<std::string> dependencies;
        std::unordered_map<std::string, std::vector<std::filesystem::path>> content;
        std::unordered_set<std::string> overrides;
		contentPackEntrypoints entrypoints;
		std::unordered_set<std::string> capabilities;
        bool core = false;
    };

    class contentPackSet {
        std::vector<contentPack> _ordered;

        static bool validId(const std::string& id) {
            if (id.empty()) return false;
            for (const unsigned char c : id) {
                if (!(std::islower(c) || std::isdigit(c) || c == '_' || c == '-'))
                    return false;
            }
            return true;
        }

        static std::filesystem::path safeContentPath(
            const std::filesystem::path& root,
            const std::string& relative,
            const std::string& source
        ) {
            const std::filesystem::path candidate = (root / relative).lexically_normal();
            const std::filesystem::path normalizedRoot = root.lexically_normal();
            auto rootIt = normalizedRoot.begin();
            auto candidateIt = candidate.begin();
            for (; rootIt != normalizedRoot.end(); ++rootIt, ++candidateIt) {
                if (candidateIt == candidate.end() || *rootIt != *candidateIt)
                    throw std::runtime_error(source + ": content path escapes its pack root: " + relative);
            }
            return candidate;
        }

        static contentPack loadManifest(const std::filesystem::path& manifest, bool core) {
            const rapidjson::Document document = jsonLoader::load(manifest.string());
            if (!document.IsObject())
                throw std::runtime_error(manifest.string() + ": pack manifest must be an object");
            if (!document.HasMember("schemaVersion") || !document["schemaVersion"].IsUint() ||
                document["schemaVersion"].GetUint() != 1u)
                throw std::runtime_error(manifest.string() + ": unsupported or missing schemaVersion");
            if (!document.HasMember("id") || !document["id"].IsString())
                throw std::runtime_error(manifest.string() + ": pack id must be a string");
            contentPack result;
            result.id = document["id"].GetString();
            if (!validId(result.id))
                throw std::runtime_error(manifest.string() + ": invalid pack id '" + result.id + "'");
            result.version = document.HasMember("version") && document["version"].IsString()
                ? document["version"].GetString() : "0.0.0";
            result.root = manifest.parent_path();
            result.core = core;

            if (document.HasMember("dependencies")) {
                if (!document["dependencies"].IsArray())
                    throw std::runtime_error(manifest.string() + ": dependencies must be an array");
                for (const rapidjson::Value& dependency : document["dependencies"].GetArray()) {
                    if (!dependency.IsString() || !validId(dependency.GetString()))
                        throw std::runtime_error(manifest.string() + ": invalid dependency id");
                    result.dependencies.emplace_back(dependency.GetString());
                }
            }
            if (document.HasMember("overrides")) {
                if (!document["overrides"].IsArray())
                    throw std::runtime_error(manifest.string() + ": overrides must be an array");
                for (const rapidjson::Value& value : document["overrides"].GetArray()) {
                    if (!value.IsString())
                        throw std::runtime_error(manifest.string() + ": override names must be strings");
                    result.overrides.emplace(value.GetString());
                }
            }
			if (document.HasMember("entrypoints")) {
				if (!document["entrypoints"].IsObject())
					throw std::runtime_error(manifest.string() + ": entrypoints must be an object");
				const rapidjson::Value& entrypoints = document["entrypoints"];
				auto readEntrypoint = [&](const char* name) -> std::optional<std::filesystem::path> {
					if (!entrypoints.HasMember(name)) return std::nullopt;
					if (!entrypoints[name].IsString())
						throw std::runtime_error(manifest.string() + ": entrypoint '" + name + "' must be a string");
					const std::filesystem::path path = safeContentPath(
						result.root, entrypoints[name].GetString(), manifest.string());
					if (!std::filesystem::is_regular_file(path))
						throw std::runtime_error(manifest.string() + ": entrypoint does not exist: " + path.string());
					return path;
				};
				result.entrypoints.wasm = readEntrypoint("wasm");
				result.entrypoints.native = readEntrypoint("native");
			}
			if (document.HasMember("capabilities")) {
				if (!document["capabilities"].IsArray())
					throw std::runtime_error(manifest.string() + ": capabilities must be an array");
				for (const rapidjson::Value& capability : document["capabilities"].GetArray()) {
					if (!capability.IsString() || std::string_view(capability.GetString()).empty())
						throw std::runtime_error(manifest.string() + ": capabilities must be non-empty strings");
					result.capabilities.emplace(capability.GetString());
				}
			}
            if (document.HasMember("content") && !document["content"].IsObject())
                throw std::runtime_error(manifest.string() + ": content must be an object");
            if (document.HasMember("content")) for (auto member = document["content"].MemberBegin();
                member != document["content"].MemberEnd(); ++member) {
                const std::string kind = member->name.GetString();
                auto addPath = [&](const rapidjson::Value& value) {
                    if (!value.IsString())
                        throw std::runtime_error(manifest.string() + ": content paths must be strings");
                    result.content[kind].push_back(safeContentPath(
                        result.root, value.GetString(), manifest.string()));
                };
                if (member->value.IsArray()) {
                    for (const rapidjson::Value& value : member->value.GetArray()) addPath(value);
                }
                else addPath(member->value);
            }
            return result;
        }

    public:
        static contentPackSet discover(
            const std::filesystem::path& coreManifest,
            const std::filesystem::path& modsDirectory
        ) {
            std::vector<contentPack> packs;
            packs.push_back(loadManifest(coreManifest, true));
            if (std::filesystem::is_directory(modsDirectory)) {
                std::vector<std::filesystem::path> manifests;
                for (const auto& entry : std::filesystem::directory_iterator(modsDirectory)) {
                    if (!entry.is_directory()) continue;
                    const std::filesystem::path manifest = entry.path() / "pack.json";
                    if (std::filesystem::is_regular_file(manifest)) manifests.push_back(manifest);
                }
                std::sort(manifests.begin(), manifests.end());
                for (const auto& manifest : manifests) packs.push_back(loadManifest(manifest, false));
            }

            std::unordered_map<std::string, size_t> byId;
            for (size_t index = 0; index < packs.size(); ++index) {
                if (!byId.emplace(packs[index].id, index).second)
                    throw std::runtime_error("Duplicate content pack id '" + packs[index].id + "'");
            }
            for (const contentPack& pack : packs) {
                for (const std::string& dependency : pack.dependencies) {
                    if (!byId.contains(dependency))
                        throw std::runtime_error("Pack '" + pack.id + "' requires missing pack '" + dependency + "'");
                }
            }

            contentPackSet result;
            std::vector<uint8_t> state(packs.size(), 0u);
            std::function<void(size_t)> visit = [&](size_t index) {
                if (state[index] == 2u) return;
                if (state[index] == 1u)
                    throw std::runtime_error("Content pack dependency cycle involving '" + packs[index].id + "'");
                state[index] = 1u;
                for (const std::string& dependency : packs[index].dependencies)
                    visit(byId.at(dependency));
                state[index] = 2u;
                result._ordered.push_back(std::move(packs[index]));
            };
            for (size_t index = 0; index < packs.size(); ++index) visit(index);
            return result;
        }

        const std::vector<contentPack>& ordered() const { return _ordered; }

		std::vector<std::string> kinds() const {
			std::unordered_set<std::string> unique;
			for (const contentPack& pack : _ordered)
				for (const auto& [kind, paths] : pack.content) {
					(void)paths;
					unique.insert(kind);
				}
			std::vector<std::string> result(unique.begin(), unique.end());
			std::sort(result.begin(), result.end());
			return result;
		}

        std::vector<std::pair<const contentPack*, std::filesystem::path>> paths(
            const std::string& kind
        ) const {
            std::vector<std::pair<const contentPack*, std::filesystem::path>> result;
            for (const contentPack& pack : _ordered) {
                const auto found = pack.content.find(kind);
                if (found == pack.content.end()) continue;
                for (const auto& path : found->second) result.emplace_back(&pack, path);
            }
            return result;
        }

        std::filesystem::path singleton(const std::string& kind) const {
            std::filesystem::path selected;
            std::string owner;
            for (const contentPack& pack : _ordered) {
                const auto found = pack.content.find(kind);
                if (found == pack.content.end()) continue;
                if (found->second.size() != 1u)
                    throw std::runtime_error("Pack '" + pack.id + "' must provide one '" + kind + "' file");
                if (!selected.empty() && !pack.overrides.contains(kind))
                    throw std::runtime_error("Pack '" + pack.id + "' replaces '" + kind +
                        "' from '" + owner + "' without declaring an override");
                selected = found->second.front();
                owner = pack.id;
            }
            if (selected.empty()) throw std::runtime_error("No content pack provides '" + kind + "'");
            return selected;
        }
    };

    template<typename T>
    class dataRegistry {
        std::vector<T> _values;
        std::vector<std::string> _names;
        std::unordered_map<std::string, uint32_t> _ids;

    public:
        uint32_t add(std::string name, T value) {
            if (name.empty() || name.find(':') == std::string::npos)
                throw std::runtime_error("Registry names must be namespaced: " + name);
            if (_ids.contains(name)) throw std::runtime_error("Duplicate registry name: " + name);
            const uint32_t id = static_cast<uint32_t>(_values.size());
            _ids.emplace(name, id);
            _names.push_back(std::move(name));
            _values.push_back(std::move(value));
            return id;
        }
        const T* get(uint32_t id) const { return id < _values.size() ? &_values[id] : nullptr; }
        const T* get(const std::string& name) const {
            const auto found = _ids.find(name);
            return found == _ids.end() ? nullptr : get(found->second);
        }
        uint32_t id(const std::string& name) const {
            const auto found = _ids.find(name);
            if (found == _ids.end()) throw std::runtime_error("Registry entry not found: " + name);
            return found->second;
        }
        size_t size() const { return _values.size(); }
        const std::vector<std::string>& names() const { return _names; }
    };
}
