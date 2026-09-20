#include "debug.h"

#include <iomanip>
#include <iostream>

namespace ac {
	std::string hResultToString(HRESULT resultCode) {
		wchar_t* buffer = nullptr;
		const DWORD size = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			resultCode,
			MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
			reinterpret_cast<LPWSTR>(&buffer),
			0,
			nullptr);
		if (!size || !buffer) return "Unknown error";
		const std::wstring wideMessage(buffer, size);
		LocalFree(buffer);
		const int length = WideCharToMultiByte(CP_UTF8, 0, wideMessage.c_str(), -1, nullptr, 0, nullptr, nullptr);
		std::string message(length, '\0');
		WideCharToMultiByte(CP_UTF8, 0, wideMessage.c_str(), -1, message.data(), length, nullptr, nullptr);
		message.pop_back();
		return message;
	}

	void dxReport(HRESULT resultCode, const char* expression, const char* file, int line) {
		std::cerr << "\nD3D11 ERROR\n"
			<< expression << '\n'
			<< "Message: " << hResultToString(resultCode) << '\n'
			<< "HRESULT: 0x" << std::hex << resultCode << std::dec << '\n'
			<< "File: " << file << '\n'
			<< "Line: " << line << std::endl;
#ifdef _DEBUG
		__debugbreak();
#endif
	}

	void enableD3DDebug(ID3D11Device* device) {
#ifdef _DEBUG
		Microsoft::WRL::ComPtr<ID3D11InfoQueue> queue;
		if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&queue)))) {
			queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
			queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
		}
#else
		(void)device;
#endif
	}
}
