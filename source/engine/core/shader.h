// shaderProgram.h
#pragma once

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <renderer/buffer.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace ac {

	Microsoft::WRL::ComPtr<ID3DBlob> compileShaderBytecode(
		const std::wstring& filePath,
		const std::string& entryPoint,
		const std::string& target,
		const D3D_SHADER_MACRO* macros = nullptr
	);

    struct TransformMatrices {
        DirectX::XMMATRIX modelMatrix;
        DirectX::XMMATRIX viewMatrix;
        DirectX::XMMATRIX projectionMatrix;
    };

    class pipelineStateBase {
    public:
        pipelineStateBase() = default;
        virtual ~pipelineStateBase() = default;
        virtual void bind(ID3D11DeviceContext* context) const = 0;
    };

    class shaderProgram : public pipelineStateBase {
    private:
        Microsoft::WRL::ComPtr<ID3D11VertexShader> _vertexShader;
		Microsoft::WRL::ComPtr<ID3DBlob> _vertexShaderBlob;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> _pixelShader;
		Microsoft::WRL::ComPtr<ID3DBlob> _pixelShaderBlob;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> _inputLayout;
        Microsoft::WRL::ComPtr<ID3D11Buffer> _constantBuffer;

    public:
        shaderProgram() = default;

        void createConstantBuffer(ID3D11Device* device) {
            D3D11_BUFFER_DESC bufferDesc = {};
            bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
            bufferDesc.ByteWidth = sizeof(TransformMatrices);
            bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            bufferDesc.MiscFlags = 0;
            bufferDesc.StructureByteStride = 0;

            DX_CHECK(device->CreateBuffer(&bufferDesc, nullptr, &_constantBuffer));
        }

		HRESULT initVertexShader(ID3D11Device* device, const std::wstring& path, const std::string& entryPoint = "main", const std::string& target = "vs_5_0") {
			_vertexShaderBlob = compileShaderBytecode(path, entryPoint, target);

            HRESULT hr = DX_CHECK(device->CreateVertexShader(_vertexShaderBlob->GetBufferPointer(), _vertexShaderBlob->GetBufferSize(), nullptr, &_vertexShader));

			if (!_vertexShader) throw std::runtime_error("Failed to create vertex shader.");

            return hr;
		}

        HRESULT initPixelShader(
			ID3D11Device* device,
			const std::wstring& path,
			const std::string& entryPoint = "main",
			const std::string& target = "ps_5_0",
			const D3D_SHADER_MACRO* macros = nullptr
		) {
			_pixelShaderBlob = compileShaderBytecode(path, entryPoint, target, macros);

            HRESULT hr = DX_CHECK(device->CreatePixelShader(_pixelShaderBlob->GetBufferPointer(), _pixelShaderBlob->GetBufferSize(), nullptr, &_pixelShader));

            if (!_pixelShader) throw std::runtime_error("Failed to create pixel shader.");

            return hr;
        }

		HRESULT initInputLayout(ID3D11Device* device, const std::vector<D3D11_INPUT_ELEMENT_DESC>& layoutDesc) {
			HRESULT hr = DX_CHECK(device->CreateInputLayout(
				layoutDesc.data(),
				static_cast<UINT>(layoutDesc.size()),
				_vertexShaderBlob->GetBufferPointer(),
				_vertexShaderBlob->GetBufferSize(),
				&_inputLayout
			));
			if (!_inputLayout) {
				throw std::runtime_error("Failed to create input layout.");
			}
			return hr;
		}

        void updateConstantBuffer(ID3D11DeviceContext* context, const TransformMatrices& matrices) {
            D3D11_MAPPED_SUBRESOURCE mappedResource;
            DX_CHECK(context->Map(_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource));

            auto* data = static_cast<TransformMatrices*>(mappedResource.pData);
            *data = matrices;

            context->Unmap(_constantBuffer.Get(), 0);
        }

        void bind(ID3D11DeviceContext* context) const override {
            bindShaders(context);
            if (_constantBuffer) {
                context->VSSetConstantBuffers(0, 1, _constantBuffer.GetAddressOf());
            }
        }

        void bindShaders(ID3D11DeviceContext* context) const {
            context->IASetInputLayout(_inputLayout.Get());
            context->VSSetShader(_vertexShader.Get(), nullptr, 0);
            context->PSSetShader(_pixelShader.Get(), nullptr, 0);
        }
    };

    class computeShader
    {
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> _shader;

    public:

        void bind(ID3D11DeviceContext* ctx)
        {
            ctx->CSSetShader(
                _shader.Get(),
                nullptr,
                0
            );
        }
    };
}
