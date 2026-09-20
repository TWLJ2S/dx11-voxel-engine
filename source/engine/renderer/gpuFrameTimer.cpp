#include "gpuFrameTimer.h"

namespace ac {
	void gpuFrameTimer::create(ID3D11Device* device) {
		D3D11_QUERY_DESC timestamp{ D3D11_QUERY_TIMESTAMP, 0u };
		D3D11_QUERY_DESC disjoint{ D3D11_QUERY_TIMESTAMP_DISJOINT, 0u };
		for (queryFrame& frame : _frames) {
			DX_CHECK(device->CreateQuery(&disjoint, frame.disjoint.GetAddressOf()));
			DX_CHECK(device->CreateQuery(&timestamp, frame.start.GetAddressOf()));
			DX_CHECK(device->CreateQuery(&timestamp, frame.end.GetAddressOf()));
		}
	}

	void gpuFrameTimer::resolve(ID3D11DeviceContext* context) {
		for (queryFrame& frame : _frames) {
			if (!frame.pending) continue;
			D3D11_QUERY_DATA_TIMESTAMP_DISJOINT frequency{};
			if (context->GetData(frame.disjoint.Get(), &frequency, sizeof(frequency), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
				continue;
			uint64_t start = 0;
			uint64_t end = 0;
			if (context->GetData(frame.start.Get(), &start, sizeof(start), 0u) == S_OK &&
				context->GetData(frame.end.Get(), &end, sizeof(end), 0u) == S_OK &&
				!frequency.Disjoint && frequency.Frequency != 0u && end >= start) {
				_milliseconds = static_cast<float>(
					static_cast<double>(end - start) * 1000.0 / static_cast<double>(frequency.Frequency));
			}
			frame.pending = false;
		}
	}

	void gpuFrameTimer::begin(ID3D11DeviceContext* context) {
		resolve(context);
		for (size_t attempt = 0; attempt < _frames.size(); ++attempt) {
			queryFrame& frame = _frames[_writeIndex];
			if (!frame.pending) {
				context->Begin(frame.disjoint.Get());
				context->End(frame.start.Get());
				_recording = true;
				return;
			}
			_writeIndex = (_writeIndex + 1u) % _frames.size();
		}
	}

	void gpuFrameTimer::end(ID3D11DeviceContext* context) {
		if (!_recording) return;
		queryFrame& frame = _frames[_writeIndex];
		context->End(frame.end.Get());
		context->End(frame.disjoint.Get());
		frame.pending = true;
		_recording = false;
		_writeIndex = (_writeIndex + 1u) % _frames.size();
	}

	float gpuFrameTimer::milliseconds() const { return _milliseconds; }
}
