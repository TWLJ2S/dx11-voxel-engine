#include "light.h"
#include <assets/cpuAsset.h>

namespace ac {
	void lightManager::init(ID3D11Device* device) {
		buffer.create(device, 4096);
		D3D11_SHADER_RESOURCE_VIEW_DESC description{};
		description.Format = DXGI_FORMAT_UNKNOWN;
		description.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
		description.BufferEx.NumElements = 4096;
		DX_CHECK(device->CreateShaderResourceView(
			buffer._buffer.Get(), &description, srv.GetAddressOf()));
	}

	void lightManager::bind(ID3D11DeviceContext* context) {
		context->PSSetShaderResources(SRV_LIGHTS, 1, srv.GetAddressOf());
	}
}
