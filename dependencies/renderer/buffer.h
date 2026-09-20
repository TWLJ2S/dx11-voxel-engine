#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <bit>
#include <DirectXMath.h>

#include <header/debug.h>

namespace ac {

    struct vertex {
        DirectX::XMFLOAT3 _position;
        DirectX::XMFLOAT3 _normal;
        DirectX::XMFLOAT2 _uv;

		uint32_t _material;
		uint32_t _ao;
		float _opacity = 1.0f;
    };

    struct instanceData {
        DirectX::XMFLOAT4X4 transform;
        uint32_t materialID;
    };

    struct bufferDesc {
        UINT size = 0;
        UINT bind = 0;
        D3D11_USAGE usage = D3D11_USAGE_DEFAULT;
        UINT cpuAccess = 0;
        UINT misc = 0;
        UINT stride = 0;
    };

    class gpuBuffer {
    public:
        Microsoft::WRL::ComPtr<ID3D11Buffer> _buffer;
        UINT _size = 0;
        UINT _capacity = 0;

    public:
        ID3D11Buffer* get() const { return _buffer.Get(); }

        void create(ID3D11Device* device, const bufferDesc& desc, const void* data = nullptr) {
            _size = _capacity = desc.size;

            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = desc.size;
            bd.BindFlags = desc.bind;
            bd.Usage = desc.usage;
            bd.CPUAccessFlags = desc.cpuAccess;
            bd.MiscFlags = desc.misc;
            bd.StructureByteStride = desc.stride;

            D3D11_SUBRESOURCE_DATA sd{};
            sd.pSysMem = data;

            DX_CHECK(device->CreateBuffer(
                &bd,
                data ? &sd : nullptr,
                &_buffer
            ));

            if (!_buffer) throw std::runtime_error("Failed creating buffer");
        }

        void update(ID3D11DeviceContext* ctx, const void* data, UINT size) {
            if (size > _capacity)
                throw std::runtime_error("Buffer overflow");

            D3D11_MAPPED_SUBRESOURCE map{};

            DX_CHECK(ctx->Map(
                _buffer.Get(),
                0,
                D3D11_MAP_WRITE_DISCARD,
                0,
                &map
            ));

            memcpy(map.pData, data, size);

            ctx->Unmap(_buffer.Get(), 0);

            _size = size;
        }
    };

    class vertexBuffer : public gpuBuffer {
    private:
        UINT _stride = sizeof(vertex);
    public:        
        void create(ID3D11Device* device, const std::vector<vertex>& v, bool dynamic = false) {
            bufferDesc desc{};

            desc.size = sizeof(vertex) * (UINT)v.size();
            desc.bind = D3D11_BIND_VERTEX_BUFFER;

            if (dynamic) {
                desc.usage = D3D11_USAGE_DYNAMIC;
                desc.cpuAccess = D3D11_CPU_ACCESS_WRITE;
            }

            gpuBuffer::create(device, desc, v.data());
        }

		void upload(ID3D11Device* device, ID3D11DeviceContext* ctx, const std::vector<vertex>& vertices) {
			const UINT required = static_cast<UINT>(sizeof(vertex) * vertices.size());
			if (required == 0) {
				_size = 0;
				return;
			}

			if (!_buffer || required > _capacity) {
				bufferDesc desc{};
				desc.size = std::max<UINT>(4096u, std::bit_ceil(required));
				desc.bind = D3D11_BIND_VERTEX_BUFFER;
				desc.usage = D3D11_USAGE_DYNAMIC;
				desc.cpuAccess = D3D11_CPU_ACCESS_WRITE;
				gpuBuffer::create(device, desc);
			}
			gpuBuffer::update(ctx, vertices.data(), required);
		}


        void bind(ID3D11DeviceContext* ctx, UINT slot = 0) {
            UINT offset = 0;

            ctx->IASetVertexBuffers(
                slot,
                1,
                _buffer.GetAddressOf(),
                &_stride,
                &offset
            );
        }
    };

    class indexBuffer : public gpuBuffer {
    private:
        UINT _count = 0;
    public:
        void create(ID3D11Device* device, const std::vector<uint32_t>& i, bool dynamic = false) {
            _count = (UINT)i.size();

            bufferDesc desc{};

            desc.size = sizeof(uint32_t) * _count;
            desc.bind = D3D11_BIND_INDEX_BUFFER;

            if (dynamic) {
                desc.usage = D3D11_USAGE_DYNAMIC;
                desc.cpuAccess = D3D11_CPU_ACCESS_WRITE;
            }

            gpuBuffer::create(device, desc, i.data());
        }

		void upload(ID3D11Device* device, ID3D11DeviceContext* ctx, const std::vector<uint32_t>& indices) {
			const UINT required = static_cast<UINT>(sizeof(uint32_t) * indices.size());
			_count = static_cast<UINT>(indices.size());
			if (required == 0) {
				_size = 0;
				return;
			}

			if (!_buffer || required > _capacity) {
				bufferDesc desc{};
				desc.size = std::max<UINT>(4096u, std::bit_ceil(required));
				desc.bind = D3D11_BIND_INDEX_BUFFER;
				desc.usage = D3D11_USAGE_DYNAMIC;
				desc.cpuAccess = D3D11_CPU_ACCESS_WRITE;
				gpuBuffer::create(device, desc);
			}
			gpuBuffer::update(ctx, indices.data(), required);
		}


        void bind(ID3D11DeviceContext* ctx) {
            ctx->IASetIndexBuffer(
                _buffer.Get(),
                DXGI_FORMAT_R32_UINT,
                0
            );
        }

        UINT count() const { return _count; }
    };

    template<typename T>
    class constantBuffer : public gpuBuffer {

        static_assert(sizeof(T) % 16 == 0);

    public:

        void create(ID3D11Device* device, const T* data = nullptr) {
            bufferDesc desc{};

            desc.size = (sizeof(T) + 15) & ~15;
            desc.bind = D3D11_BIND_CONSTANT_BUFFER;

            desc.usage = D3D11_USAGE_DYNAMIC;
            desc.cpuAccess = D3D11_CPU_ACCESS_WRITE;

            gpuBuffer::create(device, desc, data);

            if (!_buffer)
                throw std::runtime_error("Constant buffer creation failed");
        }


        void update(ID3D11DeviceContext* ctx, const T& data) {
            gpuBuffer::update(ctx, &data, sizeof(T));
        }

        void bindVS(ID3D11DeviceContext* ctx, UINT slot) {
            if (!ctx)
                throw std::runtime_error("Invalid device context");
            ctx->VSSetConstantBuffers(slot, 1, _buffer.GetAddressOf());
        }

        void bindPS(ID3D11DeviceContext* ctx, UINT slot) {
            ctx->PSSetConstantBuffers(
                slot,
                1,
                _buffer.GetAddressOf()
            );
        }

		void bindCS(ID3D11DeviceContext* ctx, UINT slot) {
			ctx->CSSetConstantBuffers(
				slot,
				1,
				_buffer.GetAddressOf()
			);
		}

		void bindGS(ID3D11DeviceContext* ctx, UINT slot) {
			ctx->GSSetConstantBuffers(
				slot,
				1,
				_buffer.GetAddressOf()
			);
		}

		void bindHS(ID3D11DeviceContext* ctx, UINT slot) {
			ctx->HSSetConstantBuffers(
				slot,
				1,
				_buffer.GetAddressOf()
			);
		}

		void bindDS(ID3D11DeviceContext* ctx, UINT slot) {
			ctx->DSSetConstantBuffers(
				slot,
				1,
				_buffer.GetAddressOf()
			);
		}
    };

    template<typename T>
    class structuredBuffer : public gpuBuffer {
    private:
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _srv;

    public:

        void create(ID3D11Device* device, UINT count, const T* data = nullptr) {
            bufferDesc desc{};

            desc.size = sizeof(T) * count;
            desc.bind = D3D11_BIND_SHADER_RESOURCE;
            desc.misc = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            desc.stride = sizeof(T);
            desc.usage = D3D11_USAGE_DEFAULT;

            gpuBuffer::create(device, desc, data);

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.Buffer.NumElements = count;

            DX_CHECK(device->CreateShaderResourceView(
                _buffer.Get(),
                &srvDesc,
                &_srv
            ));
        }


        void update(ID3D11DeviceContext* ctx, const T* data, UINT count) {
			if (!data || count == 0) return;
			D3D11_BOX destination{};
			destination.left = 0;
			destination.right = sizeof(T) * count;
			destination.top = 0;
			destination.bottom = 1;
			destination.front = 0;
			destination.back = 1;
            ctx->UpdateSubresource(
                _buffer.Get(),
                0,
				&destination,
                data,
                0,
                0
            );
        }


        void bindPS(ID3D11DeviceContext* ctx, UINT slot) {
            ctx->PSSetShaderResources(
                slot,
                1,
                _srv.GetAddressOf()
            );
        }

		void updateRange(ID3D11DeviceContext* ctx, const T* data, UINT first, UINT count) {
			if (!data || count == 0) return;
			const UINT byteOffset = static_cast<UINT>(sizeof(T)) * first;
			D3D11_BOX destination{};
			destination.left = byteOffset;
			destination.right = byteOffset + static_cast<UINT>(sizeof(T)) * count;
			destination.top = 0;
			destination.bottom = 1;
			destination.front = 0;
			destination.back = 1;
			ctx->UpdateSubresource(_buffer.Get(), 0, &destination, data, 0, 0);
		}

        void bindVS(ID3D11DeviceContext* ctx, UINT slot) {
            ctx->VSSetShaderResources(
                slot,
                1,
                _srv.GetAddressOf()
            );
        }
    };

}
