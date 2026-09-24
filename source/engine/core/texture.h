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

#include <core/debug.h>
#include <assets/assetManager.h>

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

    inline bool shouldPromoteToRgba8(DXGI_FORMAT format) {
        if (format == DXGI_FORMAT_R8G8B8A8_UNORM) return false;
        if (DirectX::IsCompressed(format) || DirectX::IsPlanar(format) || DirectX::IsPacked(format))
            return false;
        switch (format) {
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R32G32B32_FLOAT:
            return false;
        default:
            return DirectX::IsBGR(format) ||
                format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
                format == DXGI_FORMAT_B5G6R5_UNORM ||
                format == DXGI_FORMAT_B5G5R5A1_UNORM ||
                format == DXGI_FORMAT_B4G4R4A4_UNORM;
        }
    }

    // WIC PNG/BMP often decode as BGRA. Uploading those bytes as RGBA swaps red
    // and blue, so anything with a strong red channel looks cyan/blue.
    inline HRESULT convertToRgba8Unorm(
        DirectX::ScratchImage& image,
        DirectX::TexMetadata& metadata
    ) {
        if (!shouldPromoteToRgba8(metadata.format))
            return S_OK;
        DirectX::ScratchImage converted;
        const HRESULT result = DirectX::Convert(
            image.GetImages(),
            image.GetImageCount(),
            metadata,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DirectX::TEX_FILTER_DEFAULT,
            DirectX::TEX_THRESHOLD_DEFAULT,
            converted
        );
        if (FAILED(result))
            return result;
        image = std::move(converted);
        metadata = image.GetMetadata();
        return S_OK;
    }

    inline HRESULT loadImageFromFile(
        const std::wstring& path,
        DirectX::ScratchImage& image,
        DirectX::TexMetadata& metadata
    ) {
        const std::wstring ext = fileExtension(path);
        HRESULT result = E_FAIL;

        if (ext == L".dds") {
            result = DirectX::LoadFromDDSFile(
                path.c_str(),
                DirectX::DDS_FLAGS_NONE,
                &metadata,
                image
            );
        }
        else if (ext == L".tga") {
            result = DirectX::LoadFromTGAFile(
                path.c_str(),
                DirectX::TGA_FLAGS_NONE,
                &metadata,
                image
            );
        }
        else if (ext == L".hdr") {
            result = DirectX::LoadFromHDRFile(
                path.c_str(),
                &metadata,
                image
            );
        }
        else {
            result = DirectX::LoadFromWICFile(
                path.c_str(),
                DirectX::WIC_FLAGS_FORCE_RGB | DirectX::WIC_FLAGS_IGNORE_SRGB |
                    DirectX::WIC_FLAGS_NO_16BPP,
                &metadata,
                image
            );
        }
        if (FAILED(result))
            return result;
        return convertToRgba8Unorm(image, metadata);
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

    struct scratchTexture {
        DirectX::ScratchImage image;
        DirectX::TexMetadata metadata{};
        uint32_t frameCount = 1;
        float frameTime = 0.05f;
        bool interpolate = false;
    };

    inline scratchTexture loadScratchTextureFromFile(const std::string& path) {
        if (!std::filesystem::exists(path)) {
            throw std::runtime_error("Texture file not found: " + path);
        }

        scratchTexture loaded{};
        const std::wstring widePath = toWide(path);
        HRESULT result = loadImageFromFile(widePath, loaded.image, loaded.metadata);

        if (FAILED(result)) {
            throw std::runtime_error(
                "Failed to load texture: " + path + " (" + hResultToString(result) + ")"
            );
        }

        // Minecraft packs store animated blocks as a vertical strip of square
        // frames (often with a sibling .mcmeta). Detect that, honour custom
        // frame orders, and leave a square atlas the shader can index by time.
        loaded.frameCount = 1;
        loaded.frameTime = 0.05f;
        loaded.interpolate = false;
        const size_t width = loaded.metadata.width;
        const size_t height = loaded.metadata.height;
        const bool tallStrip = width > 0 && height > width && (height % width) == 0;
        if (tallStrip) {
            const uint32_t sourceFrames = static_cast<uint32_t>(height / width);
            loaded.frameCount = sourceFrames;
            loaded.frameTime = 1.0f / 20.0f;
            std::vector<uint32_t> sequence;
            for (uint32_t i = 0; i < sourceFrames; ++i)
                sequence.push_back(i);

            const std::filesystem::path metaPath =
                std::filesystem::path(path).string() + ".mcmeta";
            if (std::filesystem::exists(metaPath)) {
                try {
                    const auto doc = jsonLoader::load(metaPath.string());
                    if (doc.IsObject() && doc.HasMember("animation") &&
                        doc["animation"].IsObject()) {
                        const auto& animation = doc["animation"];
                        uint32_t frameTicks = 1;
                        if (animation.HasMember("frametime")) {
                            if (animation["frametime"].IsUint())
                                frameTicks = std::max(1u, animation["frametime"].GetUint());
                            else if (animation["frametime"].IsInt() &&
                                animation["frametime"].GetInt() > 0)
                                frameTicks = static_cast<uint32_t>(animation["frametime"].GetInt());
                        }
                        loaded.frameTime = static_cast<float>(frameTicks) / 20.0f;
                        if (animation.HasMember("interpolate") &&
                            animation["interpolate"].IsBool())
                            loaded.interpolate = animation["interpolate"].GetBool();
                        if (animation.HasMember("frames") && animation["frames"].IsArray() &&
                            !animation["frames"].Empty()) {
                            sequence.clear();
                            for (const auto& entry : animation["frames"].GetArray()) {
                                uint32_t index = 0;
                                if (entry.IsUint())
                                    index = entry.GetUint();
                                else if (entry.IsObject() && entry.HasMember("index") &&
                                    entry["index"].IsUint())
                                    index = entry["index"].GetUint();
                                else
                                    continue;
                                if (index >= sourceFrames)
                                    throw std::runtime_error(
                                        "Animation frame out of range in " + metaPath.string());
                                sequence.push_back(index);
                            }
                            if (sequence.empty())
                                throw std::runtime_error(
                                    "Empty animation frame list in " + metaPath.string());
                            loaded.frameCount = static_cast<uint32_t>(sequence.size());
                        }
                    }
                }
                catch (const std::exception&) {
                    // Keep the tall-strip defaults if the sidecar is malformed.
                }
            }

            const bool sequential = [&]() {
                if (sequence.size() != sourceFrames) return false;
                for (uint32_t i = 0; i < sourceFrames; ++i) {
                    if (sequence[i] != i) return false;
                }
                return true;
            }();
            // Rebuild whenever the playback order is not 0..N-1 so the GPU
            // only ever indexes a contiguous strip.
            if (!sequential) {
                DirectX::ScratchImage rebuilt;
                result = rebuilt.Initialize2D(
                    loaded.metadata.format,
                    width,
                    width * sequence.size(),
                    1,
                    1
                );
                if (FAILED(result)) {
                    throw std::runtime_error(
                        "Failed to allocate animated strip for: " + path);
                }
                const DirectX::Image* source = loaded.image.GetImage(0, 0, 0);
                const DirectX::Image* destination = rebuilt.GetImage(0, 0, 0);
                if (!source || !destination) {
                    throw std::runtime_error(
                        "Missing image planes while animating: " + path);
                }
                for (size_t i = 0; i < sequence.size(); ++i) {
                    const DirectX::Rect srcRect(
                        0,
                        sequence[i] * width,
                        width,
                        width
                    );
                    result = DirectX::CopyRectangle(
                        *source,
                        srcRect,
                        *destination,
                        DirectX::TEX_FILTER_DEFAULT,
                        0,
                        i * width
                    );
                    if (FAILED(result)) {
                        throw std::runtime_error(
                            "Failed to copy animation frame for: " + path);
                    }
                }
                loaded.image = std::move(rebuilt);
                loaded.metadata = loaded.image.GetMetadata();
                loaded.frameCount = static_cast<uint32_t>(sequence.size());
            }
        }

        return loaded;
    }

    inline texture loadTextureFromFile(ID3D11Device* device, const std::string& path) {
        if (!device) {
            throw std::runtime_error("Cannot load texture without a D3D11 device");
        }

        scratchTexture scratch = loadScratchTextureFromFile(path);
        texture loaded{};
        const HRESULT result = createGpuTextureFromImage(
            device, scratch.image, scratch.metadata, loaded);

        if (FAILED(result)) {
            throw std::runtime_error(
                "Failed to create GPU texture: " + path + " (" + hResultToString(result) + ")"
            );
        }

        loaded._frameCount = scratch.frameCount;
        loaded._frameTime = scratch.frameTime;
        loaded._interpolate = scratch.interpolate;
        return loaded;
    }

    // HUD/inventory textures must sample as UNORM so Nuklear writes the PNG's
    // authored bytes to the swap chain. An sRGB view would linearize them and
    // make the UI look darkened by world lighting.
    inline texture loadUiTextureFromFile(ID3D11Device* device, const std::string& path) {
        texture loaded = loadTextureFromFile(device, path);
        DXGI_FORMAT unorm = loaded._format;
        if (loaded._format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
            unorm = DXGI_FORMAT_R8G8B8A8_UNORM;
        else if (loaded._format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
            unorm = DXGI_FORMAT_B8G8R8A8_UNORM;
        if (unorm == loaded._format || !loaded._texture)
            return loaded;

        D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Format = unorm;
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MostDetailedMip = 0;
        desc.Texture2D.MipLevels = loaded._mipLevels > 0 ? loaded._mipLevels : 1u;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        if (SUCCEEDED(device->CreateShaderResourceView(
            loaded._texture.Get(), &desc, srv.GetAddressOf()))) {
            loaded._shaderResourceView = srv;
            loaded._format = unorm;
        }
        return loaded;
    }

    class textureLibrary {
    private:
        std::vector<std::unique_ptr<texture>> _owned;
        std::unordered_map<std::string, texture*> _byPath;

    public:
        texture* insert(const std::string& path, texture loaded) {
            if (auto existing = _byPath.find(path); existing != _byPath.end()) {
                return existing->second;
            }
            auto owned = std::make_unique<texture>(std::move(loaded));
            texture* ptr = owned.get();
            _owned.push_back(std::move(owned));
            _byPath.emplace(path, ptr);
            return ptr;
        }

        texture* load(ID3D11Device* device, const std::string& path) {
            if (auto existing = _byPath.find(path); existing != _byPath.end()) {
                return existing->second;
            }
            return insert(path, loadTextureFromFile(device, path));
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
		Microsoft::WRL::ComPtr<ID3D11Texture2D> _arrayTexture;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _arraySrv;
		structuredBuffer<DirectX::XMFLOAT4> _emissions;
		structuredBuffer<materialProperties> _properties;
		texture* _grassColormap = nullptr;
		texture* _foliageColormap = nullptr;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> _colormapSampler;
		bool _hasEmissions = false;
		bool _hasProperties = false;

		void buildArray(ID3D11Device* device, std::vector<scratchTexture> slices) {
			size_t maxWidth = 0;
			size_t maxHeight = 0;
			for (const scratchTexture& slice : slices) {
				maxWidth = (std::max)(maxWidth, slice.metadata.width);
				maxHeight = (std::max)(maxHeight, slice.metadata.height);
			}
			if (maxWidth == 0 || maxHeight == 0)
				throw std::runtime_error("Block texture array has zero dimensions");

			DirectX::ScratchImage arrayImage;
			HRESULT result = arrayImage.Initialize2D(
				DXGI_FORMAT_R8G8B8A8_UNORM,
				maxWidth,
				maxHeight,
				slices.size(),
				1
			);
			if (FAILED(result)) {
				throw std::runtime_error(
					"Failed to allocate block texture array (" + hResultToString(result) + ")");
			}

			for (size_t i = 0; i < slices.size(); ++i) {
				DirectX::ScratchImage resized;
				if (slices[i].metadata.width != maxWidth ||
					slices[i].metadata.height != maxHeight) {
					result = DirectX::Resize(
						*slices[i].image.GetImage(0, 0, 0),
						maxWidth,
						maxHeight,
						DirectX::TEX_FILTER_POINT,
						resized
					);
					if (FAILED(result)) {
						throw std::runtime_error(
							"Failed to resize block texture into array slice " +
							std::to_string(i) + " (" + hResultToString(result) + ")");
					}
				}
				else {
					resized = std::move(slices[i].image);
				}

				const DirectX::Image* source = resized.GetImage(0, 0, 0);
				const DirectX::Image* destination = arrayImage.GetImage(0, i, 0);
				if (!source || !destination) {
					throw std::runtime_error(
						"Missing planes while packing block texture array");
				}
				result = DirectX::CopyRectangle(
					*source,
					DirectX::Rect(0, 0, maxWidth, maxHeight),
					*destination,
					DirectX::TEX_FILTER_DEFAULT,
					0,
					0
				);
				if (FAILED(result)) {
					throw std::runtime_error(
						"Failed to copy block texture into array slice " +
						std::to_string(i) + " (" + hResultToString(result) + ")");
				}
			}

			Microsoft::WRL::ComPtr<ID3D11Resource> resource;
			result = DirectX::CreateTexture(
				device,
				arrayImage.GetImages(),
				arrayImage.GetImageCount(),
				arrayImage.GetMetadata(),
				resource.GetAddressOf()
			);
			if (FAILED(result)) {
				throw std::runtime_error(
					"Failed to create block texture array (" + hResultToString(result) + ")");
			}
			result = resource.As(&_arrayTexture);
			if (FAILED(result)) {
				throw std::runtime_error(
					"Block texture array is not a Texture2D (" + hResultToString(result) + ")");
			}

			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
			srv.Texture2DArray.MostDetailedMip = 0;
			srv.Texture2DArray.MipLevels = 1;
			srv.Texture2DArray.FirstArraySlice = 0;
			srv.Texture2DArray.ArraySize = static_cast<UINT>(slices.size());
			result = device->CreateShaderResourceView(
				_arrayTexture.Get(), &srv, _arraySrv.GetAddressOf());
			if (FAILED(result)) {
				throw std::runtime_error(
					"Failed to create block texture array SRV (" +
					hResultToString(result) + ")");
			}
		}

	public:
		void load(
			ID3D11Device* device,
			textureLibrary& library,
			const std::vector<std::string>& paths
		) {
			if (paths.empty())
				throw std::runtime_error("Block texture list is empty");
			if (paths.size() > BLOCK_TEXTURE_SLOTS)
				throw std::runtime_error(
					"At most " + std::to_string(BLOCK_TEXTURE_SLOTS) +
					" block textures are supported");
			_textures.clear();
			_textures.reserve(paths.size());
			std::vector<scratchTexture> slices;
			slices.reserve(paths.size());
			for (const std::string& path : paths) {
				scratchTexture scratch = loadScratchTextureFromFile(path);
				texture gpu{};
				const HRESULT createResult = createGpuTextureFromImage(
					device, scratch.image, scratch.metadata, gpu);
				if (FAILED(createResult)) {
					throw std::runtime_error(
						"Failed to create GPU texture: " + path + " (" +
						hResultToString(createResult) + ")");
				}
				gpu._frameCount = scratch.frameCount;
				gpu._frameTime = scratch.frameTime;
				gpu._interpolate = scratch.interpolate;
				_textures.push_back(library.insert(path, std::move(gpu)));

				if (scratch.metadata.format != DXGI_FORMAT_R8G8B8A8_UNORM) {
					DirectX::ScratchImage converted;
					const HRESULT convertResult = DirectX::Convert(
						scratch.image.GetImages(),
						scratch.image.GetImageCount(),
						scratch.metadata,
						DXGI_FORMAT_R8G8B8A8_UNORM,
						DirectX::TEX_FILTER_DEFAULT,
						DirectX::TEX_THRESHOLD_DEFAULT,
						converted
					);
					if (FAILED(convertResult)) {
						throw std::runtime_error(
							"Failed to convert block texture to RGBA: " + path +
							" (" + hResultToString(convertResult) + ")");
					}
					scratch.image = std::move(converted);
					scratch.metadata = scratch.image.GetMetadata();
				}
				slices.push_back(std::move(scratch));
			}
			_arrayTexture.Reset();
			_arraySrv.Reset();
			buildArray(device, std::move(slices));
		}

		// Biome colour ramps live outside the block pack: they are indexed by
		// climate, not by material, so they never consume a texture slot.
		void loadColormaps(
			ID3D11Device* device,
			textureLibrary& library,
			const std::string& grassPath,
			const std::string& foliagePath
		) {
			_grassColormap = library.load(device, grassPath);
			_foliageColormap = library.load(device, foliagePath);
			if (!_grassColormap || !_foliageColormap)
				throw std::runtime_error("Failed to load the grass/foliage colormaps");

			// Filtering the ramp rather than point-sampling it is what keeps a
			// wide biome transition from banding into visible colour steps.
			D3D11_SAMPLER_DESC description{};
			description.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			description.AddressU = description.AddressV = description.AddressW =
				D3D11_TEXTURE_ADDRESS_CLAMP;
			description.MaxLOD = D3D11_FLOAT32_MAX;
			DX_CHECK(device->CreateSamplerState(
				&description, _colormapSampler.GetAddressOf()));
		}

		void loadMaterialProperties(
			ID3D11Device* device,
			const staticAssetManager& definitions
		) {
			std::vector<materialProperties> table = definitions.materialTable();
			for (size_t i = 0; i < _textures.size() && i < table.size(); ++i) {
				const texture* item = _textures[i];
				if (!item) continue;
				table[i].frameCount = std::max(1u, item->_frameCount);
				table[i].frameTime = item->_frameTime > 0.0f ? item->_frameTime : 0.05f;
				table[i].interpolate = item->_interpolate ? 1u : 0u;
			}
			_properties.create(device, static_cast<UINT>(table.size()), table.data());
			_hasProperties = true;
		}

		void loadEmissions(ID3D11Device* device, const staticAssetManager& definitions) {
			std::vector<DirectX::XMFLOAT4> emissions(
				std::max<size_t>(_textures.size(), 1), { 0, 0, 0, 0 });
			for (uint32_t id : definitions.ids()) {
				const blockDefinition* definition = definitions.get(id);
				if (!definition || definition->_emission.intensity <= 0.0f) continue;
				for (uint32_t face = 0; face < BLOCK_FACE_COUNT; ++face) {
					if ((definition->_emission.faceMask & (1u << face)) == 0) continue;
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
			ID3D11ShaderResourceView* arrayView = _arraySrv.Get();
			context->PSSetShaderResources(SRV_BLOCK_TEXTURE_ARRAY, 1, &arrayView);
			if (_hasEmissions) _emissions.bindPS(context, SRV_MATERIAL_EMISSIONS);
			if (_hasProperties) _properties.bindPS(context, SRV_MATERIAL_PROPERTIES);
			if (_grassColormap) {
				ID3D11ShaderResourceView* views[] = {
					_grassColormap->_shaderResourceView.Get(),
					_foliageColormap ? _foliageColormap->_shaderResourceView.Get() : nullptr
				};
				context->PSSetShaderResources(SRV_GRASS_COLORMAP, 2, views);
				context->PSSetSamplers(4, 1, _colormapSampler.GetAddressOf());
			}
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
