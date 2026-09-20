#pragma once
#include <d3d11.h>

#include <assets/cpuAsset.h>

namespace ac {

    struct objectData {
        DirectX::XMFLOAT4X4 transform;
    };

    struct renderCommand {  
        gpuModel* _model = nullptr;
        gpuMaterial* _material = nullptr;
        DirectX::XMFLOAT4X4 _transform = {};
    };

    class staticRenderer {
    private:
        ID3D11Device* _device;
        ID3D11DeviceContext* _context;

        std::vector<renderCommand> _commands;
        constantBuffer<objectData> _objectBuffer;

    public:
        staticRenderer(ID3D11Device* device, ID3D11DeviceContext* context);

        void submit(const renderCommand& command);

        void render();
    };
}
