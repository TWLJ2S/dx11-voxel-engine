#pragma once

#include <DirectXTex/DirectXTex.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <header/debug.h>
#include <assetManager/assetManager.h>

#pragma comment(lib, "DirectXTex.lib")

namespace ac {

    inline std::wstring toWide(const std::string& path) {
        if (path.empty()) return {};

        const int size = MultiByteToWideChar(
            CP_UTF8,
            0,
            path.c_str(),
            -1,
            nullptr,
            0
        );

        if (size <= 0) {
            throw std::runtime_error("Failed to convert path to wide string: " + path);
        }

        std::wstring wide(static_cast<size_t>(size - 1), L'\0');
        MultiByteToWideChar(
            CP_UTF8,
            0,
            path.c_str(),
            -1,
            wide.data(),
            size
        );

        return wide;
    }

    inline std::wstring fileExtension(const std::wstring& path) {
        const auto dot = path.find_last_of(L'.');
        if (dot == std::wstring::npos) return {};

        std::wstring ext = path.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        return ext;
    }

    inline HRESULT loadImageFromFile(
        const std::wstring& path,
        DirectX::ScratchImage& image,
        DirectX::TexMetadata& metadata
    ) {
        const std::wstring ext = fileExtension(path);

        if (ext == L".dds") {
            return DirectX::LoadFromDDSFile(
                path.c_str(),
                DirectX::DDS_FLAGS_NONE,
                &metadata,
                image
            );
        }

        if (ext == L".tga") {
            return DirectX::LoadFromTGAFile(
                path.c_str(),
                DirectX::TGA_FLAGS_NONE,
                &metadata,
                image
            );
        }

        if (ext == L".hdr") {
            return DirectX::LoadFromHDRFile(
                path.c_str(),
                &metadata,
                image
            );
        }

        return DirectX::LoadFromWICFile(
            path.c_str(),
            DirectX::WIC_FLAGS_NONE,
            &metadata,
            image
        );
    }

    inline HRESULT createGpuTextureFromImage(
        ID3D11Device* device,
        const DirectX::ScratchImage& image,
        const DirectX::TexMetadata& metadata,
        texture& outTexture
    ) {
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;

        HRESULT result = DirectX::CreateTexture(
            device,
            image.GetImages(),
            image.GetImageCount(),
            metadata,
            resource.GetAddressOf()
        );

        if (FAILED(result)) {
            return result;
        }

        result = resource.As(&outTexture._texture);
        if (FAILED(result)) {
            return result;
        }

        result = DirectX::CreateShaderResourceView(
            device,
            image.GetImages(),
            image.GetImageCount(),
            metadata,
            outTexture._shaderResourceView.GetAddressOf()
        );

        if (FAILED(result)) {
            return result;
        }

        outTexture._width = static_cast<uint32_t>(metadata.width);
        outTexture._height = static_cast<uint32_t>(metadata.height);
        outTexture._mipLevels = static_cast<uint32_t>(metadata.mipLevels);
        outTexture._format = metadata.format;

        return S_OK;
    }

    inline texture loadTextureFromFile(ID3D11Device* device, const std::string& path) {
        if (!device) {
            throw std::runtime_error("Cannot load texture without a D3D11 device");
        }

        if (!std::filesystem::exists(path)) {
            throw std::runtime_error("Texture file not found: " + path);
        }

        DirectX::ScratchImage image;
        DirectX::TexMetadata metadata{};

        const std::wstring widePath = toWide(path);
        HRESULT result = loadImageFromFile(widePath, image, metadata);

        if (FAILED(result)) {
            throw std::runtime_error(
                "Failed to load texture: " + path + " (" + hResultToString(result) + ")"
            );
        }

        texture loaded{};
        result = createGpuTextureFromImage(device, image, metadata, loaded);

        if (FAILED(result)) {
            throw std::runtime_error(
                "Failed to create GPU texture: " + path + " (" + hResultToString(result) + ")"
            );
        }

        return loaded;
    }

    class textureLibrary {
    private:
        std::vector<std::unique_ptr<texture>> _owned;
        std::unordered_map<std::string, texture*> _byPath;

    public:
        texture* load(ID3D11Device* device, const std::string& path) {
            if (auto existing = _byPath.find(path); existing != _byPath.end()) {
                return existing->second;
            }

            auto loaded = std::make_unique<texture>(loadTextureFromFile(device, path));
            texture* ptr = loaded.get();
            _owned.push_back(std::move(loaded));
            _byPath.emplace(path, ptr);
            return ptr;
        }

        texture* get(const std::string& path) const {
            auto i = _byPath.find(path);
            return i != _byPath.end() ? i->second : nullptr;
        }

        bool contains(const std::string& path) const {
            return _byPath.contains(path);
        }
    };

	class blockTextureSet {
	private:
		std::vector<texture*> _textures;
		structuredBuffer<DirectX::XMFLOAT4> _emissions;
		bool _hasEmissions = false;

	public:
		void load(
			ID3D11Device* device,
			textureLibrary& library,
			const std::vector<std::string>& paths
		) {
			if (paths.empty())
				throw std::runtime_error("Block texture list is empty");
			if (paths.size() > 64)
				throw std::runtime_error("At most 64 block textures are supported");
			_textures.clear();
			_textures.reserve(paths.size());
			for (const std::string& path : paths)
				_textures.push_back(library.load(device, path));
		}

		void loadEmissions(ID3D11Device* device, const staticAssetManager& definitions) {
			std::vector<DirectX::XMFLOAT4> emissions(64, { 0, 0, 0, 0 });
			for (uint32_t id : definitions.ids()) {
				const blockDefinition* definition = definitions.get(id);
				if (!definition || definition->_emission.intensity <= 0.0f) continue;
				for (uint32_t face = 0; face < BLOCK_FACE_COUNT; ++face) {
					const uint32_t material = definition->materialForFace(face);
					if (material >= emissions.size()) continue;
					emissions[material] = {
						definition->_emission.color.x,
						definition->_emission.color.y,
						definition->_emission.color.z,
						definition->_emission.intensity
					};
				}
			}
			_emissions.create(device, static_cast<UINT>(emissions.size()), emissions.data());
			_hasEmissions = true;
		}

		void bind(ID3D11DeviceContext* context) {
			std::vector<ID3D11ShaderResourceView*> views;
			views.reserve(_textures.size());
			for (texture* item : _textures)
				views.push_back(item ? item->_shaderResourceView.Get() : nullptr);
			context->PSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
			if (_hasEmissions) _emissions.bindPS(context, 66);
		}

		size_t size() const { return _textures.size(); }

		texture* get(size_t index) const {
			return index < _textures.size() ? _textures[index] : nullptr;
		}
	};

    inline gpuMaterial loadMaterialFromJson(
        ID3D11Device* device,
        textureLibrary& library,
        const std::string& jsonPath
    ) {
        const auto doc = ac::jsonLoader::load(jsonPath);
        gpuMaterial material{};

        auto resolveTexture = [&](const char* key) -> texture* {
            if (!doc.HasMember(key)) return nullptr;

            const auto& value = doc[key];
            if (!value.IsString()) return nullptr;

            const std::string path = value.GetString();
            return library.load(device, path);
        };

        material.albedo = resolveTexture("albedo");
        material.normal = resolveTexture("normal");
        material.roughness = resolveTexture("roughness");
        material.metallic = resolveTexture("metallic");

        if (!material.albedo) {
            throw std::runtime_error("Material is missing an albedo texture: " + jsonPath);
        }

        return material;
    }

} // namespace ac
