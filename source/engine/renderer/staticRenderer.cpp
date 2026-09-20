#include "staticRenderer.h"

namespace ac {
	staticRenderer::staticRenderer(ID3D11Device* device, ID3D11DeviceContext* context)
		: _device(device), _context(context) {
		_objectBuffer.create(device);
	}

	void staticRenderer::submit(const renderCommand& command) {
		_commands.push_back(command);
	}

	void staticRenderer::render() {
		for (renderCommand& command : _commands) {
			if (!command._model) continue;
			_objectBuffer.update(_context, { command._transform });
			_objectBuffer.bindVS(_context, 0);
			if (command._material) command._material->bind(_context);
			command._model->bind(_context);
			command._model->draw(_context);
		}
		_commands.clear();
	}
}
