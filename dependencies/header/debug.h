#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <iostream>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")

namespace ac {

    inline std::string hResultToString(HRESULT hr) {
        wchar_t* buffer = nullptr;
        DWORD size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, hr, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), (LPWSTR)&buffer, 0, nullptr);
        if (!size || !buffer) return "Unknown error";
        std::wstring wmsg(buffer, size);
        LocalFree(buffer);
        int len = WideCharToMultiByte(CP_UTF8, 0, wmsg.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string result(len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wmsg.c_str(), -1, result.data(), len, nullptr, nullptr);
        return result;
    }

    inline void dxReport(HRESULT hr, const char* expr, const char* file, int line) {
        std::cerr << "\nD3D11 ERROR\n"
            << expr << "\n"
            << "Message: " << hResultToString(hr) << "\n"
            << "HRESULT: 0x" << std::hex << hr << std::dec << "\n"
            << "File: " << file << "\n"
            << "Line: " << line << std::endl;
#ifdef _DEBUG
        __debugbreak();
#endif
    }

#ifdef _DEBUG
#define DX_CHECK(x) ([&]() { HRESULT hr__ = (x); if (FAILED(hr__)) ac::dxReport(hr__, #x, __FILE__, __LINE__); return hr__; })()
#else
#define DX_CHECK(x) (x)
#endif

    inline void enableD3DDebug(ID3D11Device* device) {
#ifdef _DEBUG
        Microsoft::WRL::ComPtr<ID3D11InfoQueue> queue;
        if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&queue)))) {
            queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
        }
#endif
    }

}
