#pragma once

#include <DirectXTex/DirectXTex.h>
#include <d3d11.h>

#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")

#include <header/utility.h>
#include <renderer/buffer.h>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

namespace ac {

    struct jsonLoader {
        static rapidjson::Document load(const std::string& path) {
            std::ifstream file(path, std::ios::binary);

            if (!file.is_open()) throw std::runtime_error("Cannot open json: " + path);

			constexpr std::streamoff MAX_JSON_SIZE = 16 * 1024 * 1024;
			file.seekg(0, std::ios::end);
			const std::streamoff size = file.tellg();
			if (size < 0)
				throw std::runtime_error("Cannot determine json size: " + path);
			if (size == 0)
				throw std::runtime_error("JSON file is empty: " + path);
			if (size > MAX_JSON_SIZE)
				throw std::runtime_error("JSON file exceeds 16 MiB limit: " + path);
			file.seekg(0, std::ios::beg);

			std::string content(static_cast<size_t>(size), '\0');
			if (!file.read(content.data(), size))
				throw std::runtime_error("Failed while reading json: " + path);

            // Strip a UTF-8 BOM if present (common when files are saved via Notepad on Windows)
            if (content.size() >= 3 &&
                static_cast<unsigned char>(content[0]) == 0xEF &&
                static_cast<unsigned char>(content[1]) == 0xBB &&
                static_cast<unsigned char>(content[2]) == 0xBF)
            {
                content.erase(0, 3);
            }

            rapidjson::Document document;
			document.Parse<rapidjson::kParseValidateEncodingFlag>(content.data(), content.size());

            if (document.HasParseError()) {
                throw std::runtime_error(
                    "JSON parse error: " + path +
                    " at offset " + std::to_string(document.GetErrorOffset()) +
                    ": " + rapidjson::GetParseError_En(document.GetParseError())
                );
            }

            return document;
        }


        static uint32_t uintValue(
            const rapidjson::GenericValue<rapidjson::UTF8<>>& object,
            const char* name,
            uint32_t defaultValue = 0
        )
        {
            if (object.HasMember(name)) return object[name].GetUint();

            return defaultValue;
        }


        static float floatValue(const rapidjson::Value& object, const char* name, float defaultValue = 0.0f) {
            if (object.HasMember(name)) return object[name].GetFloat();

            return defaultValue;
        }

    };

    struct gpuModel {
        vertexBuffer _vertex;
        indexBuffer _index;

        void bind(ID3D11DeviceContext* ctx) {
            _vertex.bind(ctx);
            _index.bind(ctx);
        }

        void draw(ID3D11DeviceContext* ctx) {
            ctx->DrawIndexed(_index.count(), 0, 0);
        }
    };

    struct texture {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> _texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _shaderResourceView;

        uint32_t _width = 0;
        uint32_t _height = 0;

        uint32_t _mipLevels = 1;

        DXGI_FORMAT _format = DXGI_FORMAT_UNKNOWN;

        HRESULT create(
            ID3D11Device* device,
            const D3D11_TEXTURE2D_DESC& desc,
            const D3D11_SUBRESOURCE_DATA* data = nullptr
        ) {
            _width = desc.Width;
            _height = desc.Height;
            _mipLevels = desc.MipLevels;
            _format = desc.Format;

            HRESULT result =
                device->CreateTexture2D(
                    &desc,
                    data,
                    &_texture
                );

            if (FAILED(result)) {
                return result;
            }

            return device->CreateShaderResourceView(
                _texture.Get(),
                nullptr,
                &_shaderResourceView
            );
        }

        void bind(
            ID3D11DeviceContext* context,
            uint32_t slot = 0
        ) {
            context->PSSetShaderResources(
                slot,
                1,
                _shaderResourceView.GetAddressOf()
            );
        }
    };

    struct materialConstants {
        DirectX::XMFLOAT4 baseColor = { 1.0f, 1.0f , 1.0f, 1.0f };

        float roughness = 1.0f;
        float metallic = 0.0f;

        float normalStrength = 1.0f;
        float emissionStrength = 0.0f;
    };

    struct gpuMaterial {
        texture* albedo = nullptr;
        texture* normal = nullptr;
        texture* roughness = nullptr;
        texture* metallic = nullptr;
        texture* ao = nullptr;
        texture* emission = nullptr;

        DirectX::XMFLOAT4 color = { 1,1,1,1 };

        float roughnessValue = 1.0f;
        float metallicValue = 0.0f;      

        void bind(ID3D11DeviceContext* context)
        {
            if (albedo)
                albedo->bind(context, 0);

            if (normal)
                normal->bind(context, 1);

            if (roughness)
                roughness->bind(context, 2);

            if (metallic)
                metallic->bind(context, 3);

            if (ao)
                ao->bind(context, 4);

            if (emission)
                emission->bind(context, 5);
        }
    };

    struct staticRenderObject {
        gpuModel* _model = nullptr;
        gpuMaterial* _material = nullptr;

        DirectX::XMFLOAT4X4 _transform = {};
    };

    struct model {

        std::vector<vertex> _vertex;
        std::vector<uint32_t> _index;

        gpuModel _gpu;

        void load(const rapidjson::Document& doc) {
            if (!doc.HasMember("vertices") || !doc["vertices"].IsArray())
                throw std::runtime_error("Model missing 'vertices'");

            if (!doc.HasMember("indices") || !doc["indices"].IsArray())
                throw std::runtime_error("Model missing 'indices'");

            for (auto& v : doc["vertices"].GetArray()) {
                vertex vert{};

                const auto& pos = v["position"].GetArray();
                vert._position = {
                    pos[0].GetFloat(),
                    pos[1].GetFloat(),
                    pos[2].GetFloat()
                };

                const auto& nrm = v["normal"].GetArray();
                vert._normal = {
                    nrm[0].GetFloat(),
                    nrm[1].GetFloat(),
                    nrm[2].GetFloat()
                };

                const auto& uv = v["uv"].GetArray();
                vert._uv = {
                    uv[0].GetFloat(),
                    uv[1].GetFloat()
                };

                vert._material =
                    jsonLoader::uintValue(v, "material", 0);

                vert._ao =
                    jsonLoader::uintValue(v, "ao", 0);

                _vertex.push_back(vert);
            }

            for (auto& i : doc["indices"].GetArray())
                _index.push_back(i.GetUint());
        }
    };

    struct staticObject {
        uint32_t _asset;

        DirectX::XMFLOAT3 _position = {};
        DirectX::XMFLOAT4 _rotation = { 0, 0, 0, 1 };
        DirectX::XMFLOAT3 _scale = { 1, 1, 1 };

        DirectX::XMFLOAT4X4 _transform;
    };

    struct chunkObject {
        uint32_t _mesh;

        DirectX::XMINT3 _position;
    };

    struct gpuShader {
        Microsoft::WRL::ComPtr<ID3D11VertexShader> _vertexShader;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> _pixelShader;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> _inputLayout;
    };
}
