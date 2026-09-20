#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <string>

#pragma comment(lib, "d3d11.lib")

namespace ac {

    std::string hResultToString(HRESULT hr);

    void dxReport(HRESULT hr, const char* expression, const char* file, int line);

#ifdef _DEBUG
#define DX_CHECK(x) ([&]() { HRESULT hr__ = (x); if (FAILED(hr__)) ac::dxReport(hr__, #x, __FILE__, __LINE__); return hr__; })()
#else
#define DX_CHECK(x) (x)
#endif

    void enableD3DDebug(ID3D11Device* device);

}
