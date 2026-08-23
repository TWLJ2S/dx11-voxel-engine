#pragma once
#include <d3d11.h>

#include <assetManager/cpuAsset.h>

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
        staticRenderer(ID3D11Device* device, ID3D11DeviceContext* context)
            : _device(device), _context(context) {
            _objectBuffer.create(device);
        }

        void submit(const renderCommand& command) {
            _commands.push_back(command);
        }

        void render() {
            for (auto& command : _commands) {
                if (!command._model) continue;

                _objectBuffer.update(_context, { command._transform });
                _objectBuffer.bindVS(_context, 0);

                if (command._material) {
                    command._material->bind(_context);
                }

                command._model->bind(_context);
                command._model->draw(_context);
            }

            _commands.clear();
        }
    };
}