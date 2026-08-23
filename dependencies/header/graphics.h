#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgi1_5.h>
#include <wrl/client.h>
#include <stdint.h>
#include <iostream>
#include <string>
#include "debug.h"
#include "utility.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;
namespace ac {

class graphicsContext {
private:

    HRESULT createDevice() {

        UINT flags = 0;

        if (_enableDebug) {
            flags |= D3D11_CREATE_DEVICE_DEBUG;
        }

        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0
        };

        return DX_CHECK(
            D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_HARDWARE,
                nullptr,
                flags,
                featureLevels,
                2,
                D3D11_SDK_VERSION,
                &_device,
                &_featureLevel,
                &_context
            )
        );
    }

    HRESULT createSwapChain() {

        ComPtr<IDXGIFactory2> factory;

        HRESULT result = DX_CHECK(
            CreateDXGIFactory1(
                IID_PPV_ARGS(&factory)
            )
        );

        if (FAILED(result)) return result;

		ComPtr<IDXGIFactory5> factory5;
		BOOL allowTearing = FALSE;
		if (SUCCEEDED(factory.As(&factory5)) &&
			SUCCEEDED(factory5->CheckFeatureSupport(
				DXGI_FEATURE_PRESENT_ALLOW_TEARING,
				&allowTearing,
				sizeof(allowTearing)
			)))
			_allowTearing = allowTearing == TRUE;

        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};

        swapChainDesc.Width = _width;
        swapChainDesc.Height = _height;
        swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.BufferCount = 2;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.Flags = _allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

        ComPtr<IDXGISwapChain1> swapChain;

        result = DX_CHECK(
            factory->CreateSwapChainForHwnd(
                _device.Get(),
                _window,
                &swapChainDesc,
                nullptr,
                nullptr,
                &swapChain
            )
        );

        if (FAILED(result)) return result;

        return DX_CHECK(swapChain.As(&_swapChain));
    }


    HRESULT createRenderTarget() {

        ComPtr<ID3D11Texture2D> backBuffer;

        HRESULT result = DX_CHECK(
            _swapChain->GetBuffer(
                0,
                IID_PPV_ARGS(&backBuffer)
            )
        );

        if (FAILED(result)) {
            return result;
        }


        return DX_CHECK(
            _device->CreateRenderTargetView(
                backBuffer.Get(),
                nullptr,
                &_renderTarget
            )
        );
    }

    HRESULT createDepthBuffer() {
        D3D11_TEXTURE2D_DESC depthDesc = {};

        depthDesc.Width = _width;
        depthDesc.Height = _height;
        depthDesc.MipLevels = 1;
        depthDesc.ArraySize = 1;
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;


        HRESULT result = DX_CHECK(
            _device->CreateTexture2D(
                &depthDesc,
                nullptr,
                &_depthBuffer
            )
        );

        if (FAILED(result)) {
            return result;
        }


        return DX_CHECK(
            _device->CreateDepthStencilView(
                _depthBuffer.Get(),
                nullptr,
                &_depthView
            )
        );
    }


    void createViewport() {

        _viewport.Width = static_cast<float>(_width);
        _viewport.Height = static_cast<float>(_height);
        _viewport.MinDepth = 0.0f;
        _viewport.MaxDepth = 1.0f;
        _viewport.TopLeftX = 0.0f;
        _viewport.TopLeftY = 0.0f;
    }

    HWND _window = nullptr;

    uint32_t _width = 0;
    uint32_t _height = 0;

    bool _enableDebug = false;
    bool _vsync = true;
	bool _allowTearing = false;

    HRESULT _result = E_FAIL;

    D3D_FEATURE_LEVEL _featureLevel =
        D3D_FEATURE_LEVEL_11_0;


    ComPtr<ID3D11Device> _device;

    ComPtr<ID3D11DeviceContext> _context;

    ComPtr<IDXGISwapChain1> _swapChain;

    ComPtr<ID3D11RenderTargetView> _renderTarget;

    ComPtr<ID3D11Texture2D> _depthBuffer;

    ComPtr<ID3D11DepthStencilView> _depthView;

    D3D11_VIEWPORT _viewport = {};

public:

    graphicsContext(
        HWND window,
        uint32_t width,
        uint32_t height,
        bool enableDebug = false
    ) {
        _window = window;
        _width = width;
        _height = height;
        _enableDebug = enableDebug;

        HRESULT result;

        result = createDevice();

        DX_CHECK(result);
        if (FAILED(result)) {
            _result = result;
            return;
        }

        if (_enableDebug) {
            ComPtr<ID3D11InfoQueue> infoQueue;

            if (SUCCEEDED(_device.As(&infoQueue)))
            {
                infoQueue->SetBreakOnSeverity(
                    D3D11_MESSAGE_SEVERITY_ERROR,
                    TRUE
                );

                infoQueue->SetBreakOnSeverity(
                    D3D11_MESSAGE_SEVERITY_CORRUPTION,
                    TRUE
                );
            }
        }

        result = createSwapChain();

        DX_CHECK(result);
        if (FAILED(result)) {
            _result = result;
            return;
        }


        result = createRenderTarget();

        DX_CHECK(result);
        if (FAILED(result)) {
            _result = result;
            return;
        }


        result = createDepthBuffer();

        DX_CHECK(result);
        if (FAILED(result)) {
            _result = result;
            return;
        }

        createViewport();

        _result = S_OK;
    }

    ~graphicsContext() {

    }


    HRESULT getResult() {
        return _result;
    }


    void beginFrame(
        float red,
        float green,
        float blue,
		float alpha
    ) {
        float clearColor[4] = {
            red,
            green,
            blue,
            alpha
        };

        _context->OMSetRenderTargets(
            1,
            _renderTarget.GetAddressOf(),
            _depthView.Get()
        );

        _context->ClearRenderTargetView(
            _renderTarget.Get(),
            clearColor
        );

        _context->ClearDepthStencilView(
            _depthView.Get(),
            D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
            1.0f,
            0
        );

        _context->RSSetViewports(
            1,
            &_viewport
        );
    }

    void beginFrame(vec4f clearColor) {

        _context->OMSetRenderTargets(
            1,
            _renderTarget.GetAddressOf(),
            _depthView.Get()
        );

        _context->ClearRenderTargetView(
            _renderTarget.Get(),
            clearColor.data()
        );

        _context->ClearDepthStencilView(
            _depthView.Get(),
            D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
            1.0f,
            0
        );

        _context->RSSetViewports(
            1,
            &_viewport
        );
    }


    HRESULT endFrame() {
		const UINT interval = _vsync ? 1u : 0u;
		const UINT flags = !_vsync && _allowTearing ? DXGI_PRESENT_ALLOW_TEARING : 0u;
        return DX_CHECK(_swapChain->Present(interval, flags));
    }

    HRESULT resizeViewport(uint32_t width, uint32_t height) {
        if (width == 0 || height == 0)
            return E_INVALIDARG;

        _context->OMSetRenderTargets(
            0,
            nullptr,
            nullptr
        );

        _renderTarget.Reset();
        _depthBuffer.Reset();
        _depthView.Reset();

        HRESULT result = DX_CHECK(
            _swapChain->ResizeBuffers(
                0,
                width,
                height,
                DXGI_FORMAT_UNKNOWN,
                _allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0
            )
        );

        if (FAILED(result))
            return result;

        _width = width;
        _height = height;

        createRenderTarget();
        createDepthBuffer();
        createViewport();

        return S_OK;
    }

    void setVsync(bool enabled) {
        _vsync = enabled;
    }

	bool getVsync() const { return _vsync; }


    ID3D11Device* getDevice() {
        return _device.Get();
    }


    ID3D11DeviceContext* getContext() {
        return _context.Get();
    }


    IDXGISwapChain1* getSwapChain() {
        return _swapChain.Get();
    }


    ID3D11RenderTargetView* getRenderTarget() {
        return _renderTarget.Get();
    }


    ID3D11DepthStencilView* getDepthView() {
        return _depthView.Get();
    }
};

}
