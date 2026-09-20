#pragma once

#include "graphics.h"
#include "window.h"

#include <cstdint>

namespace ac {

struct frameContext {
    std::uint64_t frameIndex = 0;
    float deltaTime = 0.0f;
};

class applicationPipeline {
private:
    window& _window;
    graphicsContext& _graphics;
    float _clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

protected:
    virtual bool onUpdate(const frameContext& frame) = 0;
    virtual void onRender(const frameContext& frame) = 0;
    virtual void onResize(std::uint32_t width, std::uint32_t height) {}
    virtual void onShutdown() {}

public:
    applicationPipeline(window& window, graphicsContext& graphics)
        : _window(window), _graphics(graphics) {}

    virtual ~applicationPipeline() = default;

    applicationPipeline(const applicationPipeline&) = delete;
    applicationPipeline& operator=(const applicationPipeline&) = delete;

    void setClearColor(float red, float green, float blue, float alpha = 1.0f) {
        _clearColor[0] = red;
        _clearColor[1] = green;
        _clearColor[2] = blue;
        _clearColor[3] = alpha;
    }

    int run() {
        HRESULT result = S_OK;
        std::uint64_t frameIndex = 0;

        while (_window.run()) {
            const int width = _window.getWidth();
            const int height = _window.getHeight();

            if (width <= 0 || height <= 0)
                continue;

            if (_window.getWindowCallBack().frameUpdate) {
                result = _graphics.resizeViewport(
                    static_cast<std::uint32_t>(width),
                    static_cast<std::uint32_t>(height)
                );

                if (FAILED(result))
                    break;

                onResize(
                    static_cast<std::uint32_t>(width),
                    static_cast<std::uint32_t>(height)
                );
            }

            const frameContext frame = {
                frameIndex,
                static_cast<float>(_window.getDeltaTime())
            };

            if (!onUpdate(frame))
                break;

            _graphics.beginFrame(
                _clearColor[0],
                _clearColor[1],
                _clearColor[2],
                _clearColor[3]
            );

            onRender(frame);

            result = _graphics.endFrame();
            if (FAILED(result))
                break;

            ++frameIndex;
        }

        onShutdown();
        return FAILED(result) ? -1 : 0;
    }
};

}
