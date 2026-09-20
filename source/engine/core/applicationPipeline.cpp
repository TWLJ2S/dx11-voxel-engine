#include "applicationPipeline.h"

#include <DirectXTex/DirectXTex.h>
#include <wincodec.h>

#include <cstdlib>
#include <filesystem>

namespace ac {
	applicationPipeline::applicationPipeline(window& window, graphicsContext& graphics)
		: _window(window), _graphics(graphics) {}

	applicationPipeline::~applicationPipeline() = default;

	void applicationPipeline::onResize(std::uint32_t, std::uint32_t) {}
	void applicationPipeline::onShutdown() {}

	void applicationPipeline::setClearColor(float red, float green, float blue, float alpha) {
		_clearColor[0] = red;
		_clearColor[1] = green;
		_clearColor[2] = blue;
		_clearColor[3] = alpha;
	}

	int applicationPipeline::run() {
		HRESULT result = S_OK;
		std::uint64_t frameIndex = 0;
		std::uint64_t captureFrame = UINT64_MAX;
		std::filesystem::path capturePath;
		char* captureFrameValue = nullptr;
		size_t captureFrameLength = 0;
		_dupenv_s(&captureFrameValue, &captureFrameLength, "AC_CAPTURE_FRAME");
		if (captureFrameValue) {
			char* end = nullptr;
			const unsigned long long parsed = std::strtoull(captureFrameValue, &end, 10);
			if (end != captureFrameValue && *end == '\0') captureFrame = parsed;
		}
		std::free(captureFrameValue);
		char* capturePathValue = nullptr;
		size_t capturePathLength = 0;
		_dupenv_s(&capturePathValue, &capturePathLength, "AC_CAPTURE_PATH");
		if (capturePathValue) capturePath = capturePathValue;
		std::free(capturePathValue);
		while (_window.run()) {
			const int width = _window.getWidth();
			const int height = _window.getHeight();
			if (width <= 0 || height <= 0) continue;

			if (_window.getWindowCallBack().frameUpdate) {
				result = _graphics.resizeViewport(
					static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
				if (FAILED(result)) break;
				onResize(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
			}

			const frameContext frame{ frameIndex, static_cast<float>(_window.getDeltaTime()) };
			if (!onUpdate(frame)) break;
			_graphics.beginFrame(_clearColor[0], _clearColor[1], _clearColor[2], _clearColor[3]);
			onRender(frame);
			bool captured = false;
			if (frameIndex == captureFrame && !capturePath.empty()) {
				Microsoft::WRL::ComPtr<ID3D11Resource> backBuffer;
				_graphics.getRenderTarget()->GetResource(backBuffer.GetAddressOf());
				DirectX::ScratchImage image;
				result = DirectX::CaptureTexture(
					_graphics.getDevice(), _graphics.getContext(), backBuffer.Get(), image);
				if (SUCCEEDED(result)) {
					std::error_code directoryError;
					if (capturePath.has_parent_path())
						std::filesystem::create_directories(capturePath.parent_path(), directoryError);
					result = DirectX::SaveToWICFile(
						*image.GetImage(0, 0, 0), DirectX::WIC_FLAGS_FORCE_RGB,
						GUID_ContainerFormatPng, capturePath.c_str());
				}
				captured = SUCCEEDED(result);
			}
			result = _graphics.endFrame();
			if (FAILED(result)) break;
			if (captured) break;
			++frameIndex;
		}

		onShutdown();
		return FAILED(result) ? -1 : 0;
	}
}
