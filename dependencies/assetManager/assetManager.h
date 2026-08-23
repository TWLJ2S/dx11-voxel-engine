#pragma once

#include <string>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <vector>
#include <rapidjson/rapidjson.h>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/error/en.h>

#include "cpuAsset.h"
#include <renderer/buffer.h>

namespace ac {

    struct modelData {
        std::vector<vertex> _vertex;
        std::vector<uint32_t> _index;
    };

    inline modelData loadModelFromJson(const std::string& path) {
        auto doc = jsonLoader::load(path);

        if (!doc.HasMember("vertices") || !doc["vertices"].IsArray())
            throw std::runtime_error("Model json missing 'vertices' array: " + path);

        if (!doc.HasMember("indices") || !doc["indices"].IsArray())
            throw std::runtime_error("Model json missing 'indices' array: " + path);

        modelData result;

        for (auto& v : doc["vertices"].GetArray()) {
            vertex vert{};

            const auto& pos = v["position"].GetArray();
            vert._position = { pos[0].GetFloat(), pos[1].GetFloat(), pos[2].GetFloat() };

            const auto& nrm = v["normal"].GetArray();
            vert._normal = { nrm[0].GetFloat(), nrm[1].GetFloat(), nrm[2].GetFloat() };

            const auto& uv = v["uv"].GetArray();
            vert._uv = { uv[0].GetFloat(), uv[1].GetFloat() };

            vert._material = jsonLoader::uintValue(v, "material", 0);
            vert._ao = jsonLoader::uintValue(v, "ao", 0);

            result._vertex.push_back(vert);
        }

        for (auto& i : doc["indices"].GetArray()) {
            result._index.push_back(i.GetUint());
        }

        return result;
    }

    template<typename T>
    class assetManager {
    protected:

        std::unordered_map<uint32_t, std::pair<std::unique_ptr<T>, std::string>> _asset;

        void add(uint32_t id, std::unique_ptr<T> asset, std::string name) {
            if (!_asset.emplace(id, std::make_pair(std::move(asset), std::move(name))).second)
                throw std::runtime_error("Duplicate asset id");
        }

    public:

        void load(const std::string& directory) {
            for (auto& file : std::filesystem::directory_iterator(directory)) {
                if (file.path().extension() != ".json") continue;

                auto doc = jsonLoader::load(file.path().string());
                auto asset = std::make_unique<T>();
                asset->load(doc);

                add(doc["id"].GetUint(), std::move(asset), doc["name"].GetString());
            }
        }

        void load(std::unique_ptr<T> asset) {
            if (!asset) throw std::runtime_error("Cannot load null asset");

            add(asset->_id, std::move(asset), asset->_name);
        }

        void load(std::unique_ptr<T> asset, uint32_t id) {
            if (!asset) throw std::runtime_error("Cannot load null asset");

            asset->_id = id;
            add(id, std::move(asset), asset->_name);
        }

        T* get(uint32_t id) {
            auto i = _asset.find(id);
            return i != _asset.end() ? i->second.first.get() : nullptr;
        }

        const std::string& getName(uint32_t id) {
            static const std::string empty;

            auto i = _asset.find(id);
            return i != _asset.end() ? i->second.second : empty;
        }
    };

    class modelManager {
    private:
        std::unordered_map<uint32_t, std::unique_ptr<model>> _asset;

    public:
        void load(std::unique_ptr<model> asset, uint32_t id) {
            if (!asset)
                throw std::runtime_error("Cannot load null model");

            if (!_asset.emplace(id, std::move(asset)).second)
                throw std::runtime_error("Duplicate model id");
        }

        model* get(uint32_t id) {
            auto i = _asset.find(id);
            return i != _asset.end() ? i->second.get() : nullptr;
        }

        const model* get(uint32_t id) const {
            auto i = _asset.find(id);
            return i != _asset.end() ? i->second.get() : nullptr;
        }
    };

    class hitboxManager : public assetManager<hitbox> {};

    class materialManager : public assetManager<material> {};

    class textureManager : public assetManager<texture> {};

    class animationManager : public assetManager<animation> {};

    class staticAssetManager {
    protected:
        std::unordered_map<uint32_t, std::pair<std::unique_ptr<blockDefinition>, std::string>> _asset;
		std::unordered_map<std::string, uint32_t> _byName;
		std::vector<std::string> _texturePaths;

        void add(uint32_t id, std::unique_ptr<blockDefinition> asset, std::string name) {
			if (!asset)
				throw std::runtime_error("Cannot load null block definition");
			if (id == 0)
				throw std::runtime_error("Block id 0 is reserved for air");
			if (name.empty())
				throw std::runtime_error("Block name cannot be empty");
			if (_asset.contains(id))
				throw std::runtime_error("Duplicate block id " + std::to_string(id));
			if (_byName.contains(name))
				throw std::runtime_error("Duplicate block name '" + name + "'");

			_byName.emplace(name, id);
			_asset.emplace(id, std::make_pair(std::move(asset), std::move(name)));
        }

    public:
        void load(const std::string& directory) {
			const std::filesystem::path root(directory);
			if (!std::filesystem::exists(root))
				throw std::runtime_error("Block directory does not exist: " + root.string());
			if (!std::filesystem::is_directory(root))
				throw std::runtime_error("Block path is not a directory: " + root.string());

			std::vector<std::filesystem::path> files;
			for (const auto& entry : std::filesystem::directory_iterator(root)) {
				if (!entry.is_regular_file()) continue;
				std::string extension = entry.path().extension().string();
				std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
					return static_cast<char>(std::tolower(c));
				});
				if (extension == ".json") files.push_back(entry.path());
			}
			std::sort(files.begin(), files.end());
			if (files.empty())
				throw std::runtime_error("No block JSON files found in: " + root.string());

			struct stagedBlock {
				std::unique_ptr<blockDefinition> definition;
				std::string source;
			};
			std::vector<stagedBlock> staged;
			std::unordered_set<uint32_t> stagedIds;
			std::unordered_set<std::string> stagedNames;

			auto stage = [&](const rapidjson::Value& value, const std::string& source) {
				auto definition = std::make_unique<blockDefinition>();
				try {
					definition->load(value);
				}
				catch (const std::exception& error) {
					throw std::runtime_error(source + ": " + error.what());
				}

				if (_asset.contains(definition->_id) || !stagedIds.insert(definition->_id).second)
					throw std::runtime_error(source + ": duplicate block id " + std::to_string(definition->_id));
				if (_byName.contains(definition->_name) || !stagedNames.insert(definition->_name).second)
					throw std::runtime_error(source + ": duplicate block name '" + definition->_name + "'");
				staged.push_back({ std::move(definition), source });
			};

			for (const auto& path : files) {
				const auto doc = jsonLoader::load(path.string());
				if (doc.IsArray()) {
					if (doc.Empty())
						throw std::runtime_error(path.string() + ": block array must not be empty");
					for (rapidjson::SizeType i = 0; i < doc.Size(); ++i)
						stage(doc[i], path.string() + "[" + std::to_string(i) + "]");
				}
				else if (doc.IsObject() && doc.HasMember("blocks")) {
					if (doc.MemberCount() != 1)
						throw std::runtime_error(path.string() + ": block-pack object may only contain the 'blocks' field");
					if (!doc["blocks"].IsArray())
						throw std::runtime_error(path.string() + ": field 'blocks' must be an array");
					const auto& blocks = doc["blocks"];
					if (blocks.Empty())
						throw std::runtime_error(path.string() + ": field 'blocks' must not be empty");
					for (rapidjson::SizeType i = 0; i < blocks.Size(); ++i)
						stage(blocks[i], path.string() + ".blocks[" + std::to_string(i) + "]");
				}
				else {
					stage(doc, path.string());
				}
			}
			if (staged.empty())
				throw std::runtime_error("No block definitions found in: " + root.string());

			// Texture-backed definitions assign compact material slots automatically.
			std::unordered_map<std::string, uint32_t> textureSlots;
			for (auto& item : staged) {
				if (!item.definition->hasTextures()) continue;
				for (uint32_t face = 0; face < BLOCK_FACE_COUNT; ++face) {
					const std::string& path = item.definition->textureForFace(face);
					auto [found, inserted] = textureSlots.emplace(path, static_cast<uint32_t>(_texturePaths.size()));
					if (inserted) _texturePaths.push_back(path);
					item.definition->setFaceMaterial(static_cast<BLOCK_FACE>(face), found->second);
				}
				item.definition->_material = item.definition->materialForFace(0);
			}
			if (_texturePaths.size() > 64)
				throw std::runtime_error("Block pack uses more than 64 unique textures");

			// Commit only after every file validates, preventing a partially loaded registry.
			for (auto& item : staged) {
				const uint32_t id = item.definition->_id;
				std::string name = item.definition->_name;
				add(id, std::move(item.definition), std::move(name));
			}
        }

        void load(std::unique_ptr<blockDefinition> asset) {
            if (!asset)
                throw std::runtime_error("Cannot load null asset");

            uint32_t id = asset->_id;
            std::string name = asset->_name;

            add(id, std::move(asset), std::move(name));
        }

        void load(std::unique_ptr<blockDefinition> asset, uint32_t id) {
            if (!asset)
                throw std::runtime_error("Cannot load null asset");

            asset->_id = id;

            std::string name = asset->_name;

            add(id, std::move(asset), std::move(name));
        }

		void loadValidated(
			const std::string& directory,
			const modelManager& models,
			uint32_t materialCount
		) {
			staticAssetManager staged;
			staged.load(directory);
			staged.validateReferences(models, materialCount);

			// Check every collision before moving anything into this registry.
			for (const auto& [id, entry] : staged._asset) {
				if (_asset.contains(id))
					throw std::runtime_error("Duplicate block id " + std::to_string(id));
				if (_byName.contains(entry.second))
					throw std::runtime_error("Duplicate block name '" + entry.second + "'");
			}

			for (auto& [id, entry] : staged._asset) {
				std::string name = entry.second;
				add(id, std::move(entry.first), std::move(name));
			}
		}

		void loadValidated(const std::string& directory, const modelManager& models) {
			staticAssetManager staged;
			staged.load(directory);
			staged.validateReferences(models);
			for (const auto& [id, entry] : staged._asset) {
				if (_asset.contains(id)) throw std::runtime_error("Duplicate block id " + std::to_string(id));
				if (_byName.contains(entry.second)) throw std::runtime_error("Duplicate block name '" + entry.second + "'");
			}
			_texturePaths = std::move(staged._texturePaths);
			for (auto& [id, entry] : staged._asset) {
				std::string name = entry.second;
				add(id, std::move(entry.first), std::move(name));
			}
		}

        blockDefinition* get(uint32_t id) {
            auto i = _asset.find(id);
            return i != _asset.end() ? i->second.first.get() : nullptr;
        }

        const blockDefinition* get(uint32_t id) const {
            auto i = _asset.find(id);
            return i != _asset.end() ? i->second.first.get() : nullptr;
        }

        uint32_t getId(const std::string& name) const {
			const auto found = _byName.find(name);
			if (found == _byName.end())
				throw std::runtime_error("Block not found: " + name);
			return found->second;
        }

        const std::string& getName(uint32_t id) const {
            static const std::string empty;

            auto i = _asset.find(id);
			return i != _asset.end() ? i->second.second : empty;
        }

		void validateReferences(const modelManager& models, uint32_t materialCount) const {
			for (const auto& [id, entry] : _asset) {
				const blockDefinition& block = *entry.first;
				if (!models.get(block._model))
					throw std::runtime_error("Block '" + block._name + "' references missing model " + std::to_string(block._model));
				if (block._material >= materialCount)
					throw std::runtime_error("Block '" + block._name + "' references missing material " + std::to_string(block._material));
				for (uint32_t face = 0; face < BLOCK_FACE_COUNT; ++face) {
					const uint32_t material = block.materialForFace(face);
					if (material >= materialCount)
						throw std::runtime_error("Block '" + block._name + "' face references missing material " + std::to_string(material));
				}
			}
		}

		void validateReferences(const modelManager& models) const {
			if (_texturePaths.empty())
				throw std::runtime_error("Block pack does not contain texture paths");
			for (const auto& [id, entry] : _asset) {
				const blockDefinition& block = *entry.first;
				if (!models.get(block._model))
					throw std::runtime_error("Block '" + block._name + "' references missing model " + std::to_string(block._model));
				if (!block.hasTextures())
					throw std::runtime_error("Block '" + block._name + "' is missing texture data");
			}
		}

		const std::vector<std::string>& texturePaths() const { return _texturePaths; }

		std::vector<uint32_t> ids() const {
			std::vector<uint32_t> result;
			result.reserve(_asset.size());
			for (const auto& [id, entry] : _asset)
				result.push_back(id);
			std::sort(result.begin(), result.end());
			return result;
		}

		size_t size() const { return _asset.size(); }
    };

    class dynamicAssetManager {
    private:

        std::vector<dynamicAsset> _asset;

    public:

        void add(dynamicAsset asset) {
            _asset.push_back(std::move(asset));
        }

        std::vector<dynamicAsset>& getAll() {
            return _asset;
        }

        void clear() {
            _asset.clear();
        }
    };

}
