#pragma once

#include <core/debug.h>
#include <core/shader.h>
#include <renderer/buffer.h>

#include <DirectXMath.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <algorithm>
#include <cstdint>

namespace ac {

	// Screen-space crosshair drawn as its own DX11 pass (not through Nuklear).
	class crosshairRenderer {
	public:
		void create(ID3D11Device* device) {
			_program.initVertexShader(device, L"assets/shader/crosshair.hlsl", "vertexMain", "vs_5_0");
			_program.initPixelShader(device, L"assets/shader/crosshair.hlsl", "pixelMain", "ps_5_0");
			_program.initInputLayout(device, {
				{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 }
			});
			_constantBuffer.create(device);

			struct vertex {
				DirectX::XMFLOAT2 corner;
				DirectX::XMFLOAT2 axis;
			};
			// Two unit quads: horizontal arm (axis=1,0), vertical arm (axis=0,1).
			const std::array<vertex, 8> vertices = {{
				{{-1, -1}, {1, 0}}, {{1, -1}, {1, 0}}, {{1, 1}, {1, 0}}, {{-1, 1}, {1, 0}},
				{{-1, -1}, {0, 1}}, {{1, -1}, {0, 1}}, {{1, 1}, {0, 1}}, {{-1, 1}, {0, 1}}
			}};
			const std::array<uint16_t, 12> indices = {{
				0, 1, 2, 0, 2, 3,
				4, 5, 6, 4, 6, 7
			}};

			D3D11_BUFFER_DESC vb{};
			vb.ByteWidth = sizeof(vertices);
			vb.Usage = D3D11_USAGE_IMMUTABLE;
			vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			D3D11_SUBRESOURCE_DATA vdata{ vertices.data() };
			DX_CHECK(device->CreateBuffer(&vb, &vdata, _vertexBuffer.GetAddressOf()));

			D3D11_BUFFER_DESC ib{};
			ib.ByteWidth = sizeof(indices);
			ib.Usage = D3D11_USAGE_IMMUTABLE;
			ib.BindFlags = D3D11_BIND_INDEX_BUFFER;
			D3D11_SUBRESOURCE_DATA idata{ indices.data() };
			DX_CHECK(device->CreateBuffer(&ib, &idata, _indexBuffer.GetAddressOf()));

			D3D11_BLEND_DESC blend{};
			blend.RenderTarget[0].BlendEnable = TRUE;
			blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
			blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
			blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
			blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
			blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			DX_CHECK(device->CreateBlendState(&blend, _blendState.GetAddressOf()));

			D3D11_DEPTH_STENCIL_DESC depth{};
			depth.DepthEnable = FALSE;
			depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
			DX_CHECK(device->CreateDepthStencilState(&depth, _depthState.GetAddressOf()));

			D3D11_RASTERIZER_DESC rasterizer{};
			rasterizer.FillMode = D3D11_FILL_SOLID;
			rasterizer.CullMode = D3D11_CULL_NONE;
			rasterizer.DepthClipEnable = FALSE;
			DX_CHECK(device->CreateRasterizerState(&rasterizer, _rasterizerState.GetAddressOf()));
		}

		void render(
			ID3D11DeviceContext* context,
			float viewportWidth,
			float viewportHeight,
			float r, float g, float b,
			float armLengthPx = 16.0f,
			float thicknessPx = 2.0f
		) {
			if (viewportWidth < 1.0f || viewportHeight < 1.0f) return;

			Microsoft::WRL::ComPtr<ID3D11InputLayout> oldLayout;
			Microsoft::WRL::ComPtr<ID3D11Buffer> oldVertexBuffer;
			Microsoft::WRL::ComPtr<ID3D11Buffer> oldIndexBuffer;
			Microsoft::WRL::ComPtr<ID3D11Buffer> oldConstantBuffer;
			Microsoft::WRL::ComPtr<ID3D11VertexShader> oldVertexShader;
			Microsoft::WRL::ComPtr<ID3D11PixelShader> oldPixelShader;
			Microsoft::WRL::ComPtr<ID3D11BlendState> oldBlendState;
			Microsoft::WRL::ComPtr<ID3D11DepthStencilState> oldDepthState;
			Microsoft::WRL::ComPtr<ID3D11RasterizerState> oldRasterizerState;
			D3D11_PRIMITIVE_TOPOLOGY oldTopology{};
			DXGI_FORMAT oldIndexFormat{};
			UINT oldVertexStride = 0;
			UINT oldVertexOffset = 0;
			UINT oldIndexOffset = 0;
			UINT oldSampleMask = 0;
			UINT oldStencilReference = 0;
			float oldBlendFactor[4]{};

			context->IAGetInputLayout(oldLayout.GetAddressOf());
			context->IAGetPrimitiveTopology(&oldTopology);
			context->IAGetVertexBuffers(0, 1, oldVertexBuffer.GetAddressOf(), &oldVertexStride, &oldVertexOffset);
			context->IAGetIndexBuffer(oldIndexBuffer.GetAddressOf(), &oldIndexFormat, &oldIndexOffset);
			context->VSGetConstantBuffers(0, 1, oldConstantBuffer.GetAddressOf());
			context->VSGetShader(oldVertexShader.GetAddressOf(), nullptr, nullptr);
			context->PSGetShader(oldPixelShader.GetAddressOf(), nullptr, nullptr);
			context->OMGetBlendState(oldBlendState.GetAddressOf(), oldBlendFactor, &oldSampleMask);
			context->OMGetDepthStencilState(oldDepthState.GetAddressOf(), &oldStencilReference);
			context->RSGetState(oldRasterizerState.GetAddressOf());

			crosshairData data{};
			data.invViewport = { 2.0f / viewportWidth, 2.0f / viewportHeight };
			data.thicknessPx = (std::max)(1.0f, thicknessPx);
			data.armLengthPx = (std::max)(data.thicknessPx + 1.0f, armLengthPx);
			data.color = { r, g, b, 1.0f };
			_constantBuffer.update(context, data);
			_constantBuffer.bindVS(context, 0);
			_constantBuffer.bindPS(context, 0);

			_program.bind(context);
			const UINT stride = sizeof(float) * 4;
			const UINT offset = 0;
			ID3D11Buffer* vb = _vertexBuffer.Get();
			context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
			context->IASetIndexBuffer(_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->OMSetBlendState(_blendState.Get(), nullptr, 0xffffffffu);
			context->OMSetDepthStencilState(_depthState.Get(), 0);
			context->RSSetState(_rasterizerState.Get());
			context->DrawIndexed(12, 0, 0);

			context->IASetInputLayout(oldLayout.Get());
			context->IASetPrimitiveTopology(oldTopology);
			ID3D11Buffer* restoreVb = oldVertexBuffer.Get();
			context->IASetVertexBuffers(0, 1, &restoreVb, &oldVertexStride, &oldVertexOffset);
			context->IASetIndexBuffer(oldIndexBuffer.Get(), oldIndexFormat, oldIndexOffset);
			ID3D11Buffer* restoreCb = oldConstantBuffer.Get();
			context->VSSetConstantBuffers(0, 1, &restoreCb);
			context->VSSetShader(oldVertexShader.Get(), nullptr, 0);
			context->PSSetShader(oldPixelShader.Get(), nullptr, 0);
			context->OMSetBlendState(oldBlendState.Get(), oldBlendFactor, oldSampleMask);
			context->OMSetDepthStencilState(oldDepthState.Get(), oldStencilReference);
			context->RSSetState(oldRasterizerState.Get());
		}

	private:
		struct crosshairData {
			DirectX::XMFLOAT2 invViewport{};
			float thicknessPx = 2.0f;
			float armLengthPx = 16.0f;
			DirectX::XMFLOAT4 color{ 1, 1, 1, 1 };
		};

		ac::shaderProgram _program;
		ac::constantBuffer<crosshairData> _constantBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> _vertexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> _indexBuffer;
		Microsoft::WRL::ComPtr<ID3D11BlendState> _blendState;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> _depthState;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> _rasterizerState;
	};

}
