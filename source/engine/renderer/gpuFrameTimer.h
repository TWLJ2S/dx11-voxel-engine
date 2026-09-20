#pragma once

#include <core/debug.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace ac {
	class gpuFrameTimer {
		struct queryFrame {
			Microsoft::WRL::ComPtr<ID3D11Query> disjoint;
			Microsoft::WRL::ComPtr<ID3D11Query> start;
			Microsoft::WRL::ComPtr<ID3D11Query> end;
			bool pending = false;
		};

		std::array<queryFrame, 4> _frames{};
		size_t _writeIndex = 0;
		bool _recording = false;
		float _milliseconds = 0.0f;

	public:
		void create(ID3D11Device* device);
		void resolve(ID3D11DeviceContext* context);
		void begin(ID3D11DeviceContext* context);
		void end(ID3D11DeviceContext* context);
		float milliseconds() const;
	};
}
