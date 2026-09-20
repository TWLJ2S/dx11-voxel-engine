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
    virtual void onResize(std::uint32_t width, std::uint32_t height);
    virtual void onShutdown();

public:
    applicationPipeline(window& window, graphicsContext& graphics);

    virtual ~applicationPipeline();

    applicationPipeline(const applicationPipeline&) = delete;
    applicationPipeline& operator=(const applicationPipeline&) = delete;

    void setClearColor(float red, float green, float blue, float alpha = 1.0f);

    int run();
};

}
