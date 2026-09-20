#pragma once

#include <core/shader.h>
#include <core/debug.h>
#include <renderer/buffer.h>

#include <DirectXMath.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>

namespace ac {

	struct scenePostSettings {
		int antialiasing = 2; // 0 off, 1 FXAA, 2 SMAA
		bool bloom = true;
		bool gtao = true;
		bool fog = true;
		int renderScalePercent = 100;
		float weatherFog = 1.0f;
		float weatherExposure = 1.0f;
		float weatherWetness = 0.0f;
		DirectX::XMFLOAT2 weatherSurfaceOrigin{};
		DirectX::XMFLOAT2 weatherSurfaceSize{};
	};

	class scenePostProcess {
	public:
		struct constants {
			DirectX::XMFLOAT4X4 inverseViewProjection{};
			DirectX::XMFLOAT2 texelSize{ 1.0f, 1.0f };
			DirectX::XMFLOAT2 aoTexelSize{ 1.0f, 1.0f };
			DirectX::XMFLOAT3 cameraPosition{};
			float aoStrength = 0.0f;
			DirectX::XMFLOAT3 fogColor{ 0.55f, 0.70f, 0.88f };
			float fogDensity = 0.0f;
			float fogHeight = 64.0f;
			float fogHeightFalloff = 0.035f;
			float bloomThreshold = 1.05f;
			float bloomIntensity = 0.0f;
			float exposure = 1.05f;
			float time = 0.0f;
			float nearPlane = 0.01f;
			float weatherWetness = 0.0f;
			DirectX::XMFLOAT2 weatherSurfaceOrigin{};
			DirectX::XMFLOAT2 weatherSurfaceSize{};
		};

		UINT width = 0;
		UINT height = 0;
		UINT bloomWidth = 0;
		UINT bloomHeight = 0;
		UINT aoWidth = 0;
		UINT aoHeight = 0;

		Microsoft::WRL::ComPtr<ID3D11Texture2D> hdrColor;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> hdrTarget;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> hdrView;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> depth;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthTarget;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depthView;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> ldrColor;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> ldrTarget;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ldrView;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> ldrColorB;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> ldrTargetB;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ldrViewB;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> aoHalfColor;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> aoHalfTarget;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> aoHalfView;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> aoColor;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> aoTarget;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> aoView;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> bloomColor[2];
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> bloomTarget[2];
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> bloomView[2];
		Microsoft::WRL::ComPtr<ID3D11Texture2D> edgeColor;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> edgeTarget;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> edgeView;

		shaderProgram gtao;
		shaderProgram gtaoUpsample;
		shaderProgram bloomExtract;
		shaderProgram bloomBlurH;
		shaderProgram bloomBlurV;
		shaderProgram composite;
		shaderProgram smaaEdge;
		shaderProgram smaaBlend;
		shaderProgram fxaa;
		shaderProgram blit;
		constantBuffer<constants> buffer;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> linearSampler;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> pointSampler;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthOff;
		Microsoft::WRL::ComPtr<ID3D11BlendState> blendOff;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer;

		void create(ID3D11Device* device) {
			auto load = [device](shaderProgram& program, const char* pixel) {
				program.initVertexShader(device, L"assets/shader/postProcess.hlsl", "vertexMain", "vs_5_0");
				program.initPixelShader(device, L"assets/shader/postProcess.hlsl", pixel, "ps_5_0");
			};
			load(gtao, "gtaoPS");
			load(gtaoUpsample, "gtaoUpsamplePS");
			load(bloomExtract, "bloomExtractPS");
			load(bloomBlurH, "bloomBlurHPS");
			load(bloomBlurV, "bloomBlurVPS");
			load(composite, "compositePS");
			load(smaaEdge, "smaaEdgePS");
			load(smaaBlend, "smaaBlendPS");
			load(fxaa, "fxaaPS");
			load(blit, "blitPS");
			buffer.create(device);

			D3D11_SAMPLER_DESC sampler{};
			sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler.MaxLOD = D3D11_FLOAT32_MAX;
			DX_CHECK(device->CreateSamplerState(&sampler, linearSampler.GetAddressOf()));
			sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			DX_CHECK(device->CreateSamplerState(&sampler, pointSampler.GetAddressOf()));

			D3D11_DEPTH_STENCIL_DESC depthDescription{};
			depthDescription.DepthEnable = FALSE;
			depthDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			DX_CHECK(device->CreateDepthStencilState(&depthDescription, depthOff.GetAddressOf()));

			D3D11_BLEND_DESC blend{};
			blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			DX_CHECK(device->CreateBlendState(&blend, blendOff.GetAddressOf()));

			D3D11_RASTERIZER_DESC raster{};
			raster.FillMode = D3D11_FILL_SOLID;
			raster.CullMode = D3D11_CULL_NONE;
			raster.DepthClipEnable = TRUE;
			DX_CHECK(device->CreateRasterizerState(&raster, rasterizer.GetAddressOf()));
		}

		static void createColorTarget(
			ID3D11Device* device,
			UINT w, UINT h,
			DXGI_FORMAT format,
			Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture,
			Microsoft::WRL::ComPtr<ID3D11RenderTargetView>& target,
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& view
		) {
			texture.Reset();
			target.Reset();
			view.Reset();
			D3D11_TEXTURE2D_DESC description{};
			description.Width = w;
			description.Height = h;
			description.MipLevels = 1;
			description.ArraySize = 1;
			description.Format = format;
			description.SampleDesc.Count = 1;
			description.Usage = D3D11_USAGE_DEFAULT;
			description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			DX_CHECK(device->CreateTexture2D(&description, nullptr, texture.GetAddressOf()));
			DX_CHECK(device->CreateRenderTargetView(texture.Get(), nullptr, target.GetAddressOf()));
			DX_CHECK(device->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf()));
		}

		void ensure(ID3D11Device* device, UINT requestedWidth, UINT requestedHeight) {
			requestedWidth = (std::max)(requestedWidth, 1u);
			requestedHeight = (std::max)(requestedHeight, 1u);
			if (hdrColor && width == requestedWidth && height == requestedHeight)
				return;
			width = requestedWidth;
			height = requestedHeight;
			bloomWidth = (std::max)(width / 2u, 1u);
			bloomHeight = (std::max)(height / 2u, 1u);
			aoWidth = (std::max)(width / 2u, 1u);
			aoHeight = (std::max)(height / 2u, 1u);

			createColorTarget(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, hdrColor, hdrTarget, hdrView);
			createColorTarget(device, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, ldrColor, ldrTarget, ldrView);
			createColorTarget(device, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, ldrColorB, ldrTargetB, ldrViewB);
			createColorTarget(device, aoWidth, aoHeight, DXGI_FORMAT_R16G16_FLOAT, aoHalfColor, aoHalfTarget, aoHalfView);
			createColorTarget(device, width, height, DXGI_FORMAT_R8_UNORM, aoColor, aoTarget, aoView);
			createColorTarget(device, width, height, DXGI_FORMAT_R8G8_UNORM, edgeColor, edgeTarget, edgeView);
			createColorTarget(device, bloomWidth, bloomHeight, DXGI_FORMAT_R16G16B16A16_FLOAT,
				bloomColor[0], bloomTarget[0], bloomView[0]);
			createColorTarget(device, bloomWidth, bloomHeight, DXGI_FORMAT_R16G16B16A16_FLOAT,
				bloomColor[1], bloomTarget[1], bloomView[1]);

			depth.Reset();
			depthTarget.Reset();
			depthView.Reset();
			D3D11_TEXTURE2D_DESC depthDescription{};
			depthDescription.Width = width;
			depthDescription.Height = height;
			depthDescription.MipLevels = 1;
			depthDescription.ArraySize = 1;
			depthDescription.Format = DXGI_FORMAT_R24G8_TYPELESS;
			depthDescription.SampleDesc.Count = 1;
			depthDescription.Usage = D3D11_USAGE_DEFAULT;
			depthDescription.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
			DX_CHECK(device->CreateTexture2D(&depthDescription, nullptr, depth.GetAddressOf()));
			D3D11_DEPTH_STENCIL_VIEW_DESC dsv{};
			dsv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
			DX_CHECK(device->CreateDepthStencilView(depth.Get(), &dsv, depthTarget.GetAddressOf()));
			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
			srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srv.Texture2D.MipLevels = 1;
			DX_CHECK(device->CreateShaderResourceView(depth.Get(), &srv, depthView.GetAddressOf()));
		}

		void beginScene(ID3D11DeviceContext* context) {
			ID3D11RenderTargetView* target = hdrTarget.Get();
			context->OMSetRenderTargets(1, &target, depthTarget.Get());
			const float clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			context->ClearRenderTargetView(hdrTarget.Get(), clear);
			context->ClearDepthStencilView(depthTarget.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
			D3D11_VIEWPORT viewport{};
			viewport.Width = static_cast<float>(width);
			viewport.Height = static_cast<float>(height);
			viewport.MaxDepth = 1.0f;
			context->RSSetViewports(1, &viewport);
		}

		void drawFullscreen(
			ID3D11DeviceContext* context,
			shaderProgram& program,
			ID3D11RenderTargetView* target,
			UINT targetWidth,
			UINT targetHeight,
			ID3D11ShaderResourceView** views,
			UINT viewCount
		) {
			context->OMSetRenderTargets(1, &target, nullptr);
			D3D11_VIEWPORT viewport{};
			viewport.Width = static_cast<float>(targetWidth);
			viewport.Height = static_cast<float>(targetHeight);
			viewport.MaxDepth = 1.0f;
			context->RSSetViewports(1, &viewport);
			program.bindShaders(context);
			buffer.bindPS(context, 0);
			if (viewCount)
				context->PSSetShaderResources(0, viewCount, views);
			ID3D11SamplerState* samplers[2] = { linearSampler.Get(), pointSampler.Get() };
			context->PSSetSamplers(0, 2, samplers);
			context->OMSetDepthStencilState(depthOff.Get(), 0);
			context->OMSetBlendState(blendOff.Get(), nullptr, 0xffffffff);
			context->RSSetState(rasterizer.Get());
			context->IASetInputLayout(nullptr);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->Draw(3, 0);
			ID3D11ShaderResourceView* nullViews[4] = {};
			context->PSSetShaderResources(0, 4, nullViews);
		}

		void resolve(
			ID3D11DeviceContext* context,
			ID3D11RenderTargetView* dest,
			UINT destWidth,
			UINT destHeight,
			const DirectX::XMFLOAT4X4& view,
			const DirectX::XMFLOAT4X4& projection,
			const DirectX::XMFLOAT3& camera,
			const DirectX::XMFLOAT3& skyColor,
			float animationTime,
			float nearPlane,
			const scenePostSettings& settings
		) {
			if (!hdrView || !dest) return;

			const DirectX::XMMATRIX viewMatrix = DirectX::XMLoadFloat4x4(&view);
			const DirectX::XMMATRIX projectionMatrix = DirectX::XMLoadFloat4x4(&projection);
			const DirectX::XMMATRIX viewProjection = DirectX::XMMatrixMultiply(viewMatrix, projectionMatrix);
			constants data{};
			DirectX::XMStoreFloat4x4(
				&data.inverseViewProjection,
				DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(nullptr, viewProjection)));
			data.texelSize = {
				1.0f / static_cast<float>(width),
				1.0f / static_cast<float>(height)
			};
			data.aoTexelSize = {
				1.0f / static_cast<float>(aoWidth),
				1.0f / static_cast<float>(aoHeight)
			};
			data.cameraPosition = camera;
			data.aoStrength = settings.gtao ? 0.32f : 0.0f;
			data.fogColor = skyColor;
			data.fogDensity = settings.fog ? 0.0048f * std::max(settings.weatherFog, 0.0f) : 0.0f;
			data.bloomThreshold = 1.15f;
			data.bloomIntensity = settings.bloom ? 0.55f : 0.0f;
			data.exposure = 1.08f * std::max(settings.weatherExposure, 0.2f);
			data.time = animationTime;
			data.nearPlane = nearPlane;
			data.weatherWetness = settings.weatherWetness;
			data.weatherSurfaceOrigin = settings.weatherSurfaceOrigin;
			data.weatherSurfaceSize = settings.weatherSurfaceSize;
			buffer.update(context, data);

			ID3D11ShaderResourceView* nullViews[4] = {};
			context->PSSetShaderResources(0, 4, nullViews);
			context->OMSetRenderTargets(0, nullptr, nullptr);

			if (settings.gtao) {
				ID3D11ShaderResourceView* views[2] = { hdrView.Get(), depthView.Get() };
				drawFullscreen(context, gtao, aoHalfTarget.Get(), aoWidth, aoHeight, views, 2);
				ID3D11ShaderResourceView* upsampleViews[3] = {
					hdrView.Get(), depthView.Get(), aoHalfView.Get()
				};
				drawFullscreen(context, gtaoUpsample, aoTarget.Get(), width, height, upsampleViews, 3);
			}
			else {
				const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
				context->ClearRenderTargetView(aoTarget.Get(), white);
			}

			if (settings.bloom) {
				data.texelSize = {
					1.0f / static_cast<float>(bloomWidth),
					1.0f / static_cast<float>(bloomHeight)
				};
				buffer.update(context, data);
				ID3D11ShaderResourceView* extractViews[1] = { hdrView.Get() };
				drawFullscreen(context, bloomExtract, bloomTarget[0].Get(), bloomWidth, bloomHeight, extractViews, 1);
				ID3D11ShaderResourceView* blurH[1] = { bloomView[0].Get() };
				drawFullscreen(context, bloomBlurH, bloomTarget[1].Get(), bloomWidth, bloomHeight, blurH, 1);
				ID3D11ShaderResourceView* blurV[1] = { bloomView[1].Get() };
				drawFullscreen(context, bloomBlurV, bloomTarget[0].Get(), bloomWidth, bloomHeight, blurV, 1);
				data.texelSize = {
					1.0f / static_cast<float>(width),
					1.0f / static_cast<float>(height)
				};
				buffer.update(context, data);
			}
			else {
				const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
				context->ClearRenderTargetView(bloomTarget[0].Get(), black);
			}

			ID3D11ShaderResourceView* compositeViews[4] = {
				hdrView.Get(), depthView.Get(), aoView.Get(), bloomView[0].Get()
			};
			drawFullscreen(context, composite, ldrTarget.Get(), width, height, compositeViews, 4);

			ID3D11ShaderResourceView* resolved = ldrView.Get();
			if (settings.antialiasing == 2) {
				const float none[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
				context->ClearRenderTargetView(edgeTarget.Get(), none);
				ID3D11ShaderResourceView* edgeViews[1] = { ldrView.Get() };
				drawFullscreen(context, smaaEdge, edgeTarget.Get(), width, height, edgeViews, 1);
				ID3D11ShaderResourceView* blendViews[3] = { ldrView.Get(), ldrView.Get(), edgeView.Get() };
				drawFullscreen(context, smaaBlend, ldrTargetB.Get(), width, height, blendViews, 3);
				resolved = ldrViewB.Get();
			}
			else if (settings.antialiasing == 1) {
				ID3D11ShaderResourceView* fxaaViews[1] = { ldrView.Get() };
				drawFullscreen(context, fxaa, ldrTargetB.Get(), width, height, fxaaViews, 1);
				resolved = ldrViewB.Get();
			}

			ID3D11ShaderResourceView* blitViews[1] = { resolved };
			drawFullscreen(context, blit, dest, destWidth, destHeight, blitViews, 1);
		}
	};

}
