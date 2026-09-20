#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include <header/debug.h>
#include <header/shader.h>
#include <header/window.h>
#include <renderer/buffer.h>

namespace ac {

	enum class uiAnchor {
		topLeft,
		topCenter,
		topRight,
		centerLeft,
		center,
		centerRight,
		bottomLeft,
		bottomCenter,
		bottomRight
	};

	struct uiRect {
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;

		bool contains(float pointX, float pointY) const {
			return pointX >= x && pointX <= x + width &&
				pointY >= y && pointY <= y + height;
		}
	};

	struct uiElement {
		DirectX::XMFLOAT2 position = { 0.0f, 0.0f };
		DirectX::XMFLOAT2 size = { 0.0f, 0.0f };
		DirectX::XMFLOAT2 pivot = { 0.0f, 0.0f };
		DirectX::XMFLOAT4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
		DirectX::XMFLOAT2 uvMin = { 0.0f, 0.0f };
		DirectX::XMFLOAT2 uvMax = { 1.0f, 1.0f };
		ID3D11ShaderResourceView* texture = nullptr;
		uiAnchor anchor = uiAnchor::topLeft;
		bool visible = true;
		bool interactive = false;
		std::string text;
		float fontSize = 18.0f;
		DirectX::XMFLOAT2 textOffset = { 0.0f, 0.0f };
		DirectX::XMFLOAT4 textColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	};

	using uiElementId = size_t;

	class uiInputState {
	private:
		std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> _mouseDown{};
		std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> _mousePressed{};
		std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> _mouseReleased{};
		std::array<bool, GLFW_KEY_LAST + 1> _keysDown{};
		std::array<bool, GLFW_KEY_LAST + 1> _keysPressed{};
		std::array<bool, GLFW_KEY_LAST + 1> _keysReleased{};

	public:
		DirectX::XMFLOAT2 mousePosition = { 0.0f, 0.0f };
		DirectX::XMFLOAT2 scrollDelta = { 0.0f, 0.0f };

		void update(const window& source) {
			mousePosition = {
				static_cast<float>(source.getCursorX()),
				static_cast<float>(source.getCursorY())
			};
			scrollDelta = {
				static_cast<float>(source.getScrollX()),
				static_cast<float>(source.getScrollY())
			};

			for (int button = 0; button <= GLFW_MOUSE_BUTTON_LAST; ++button) {
				_mouseDown[button] = source.getMouseHold(button);
				_mousePressed[button] = source.getMousePress(button);
				_mouseReleased[button] = source.getMouseRelease(button);
			}
			for (int key = 0; key <= GLFW_KEY_LAST; ++key) {
				_keysDown[key] = source.getKey(key);
				_keysPressed[key] = source.getKeyPress(key);
				_keysReleased[key] = source.getKeyRelease(key);
			}
		}

		bool mouseDown(int button) const {
			return button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST && _mouseDown[button];
		}

		bool mousePressed(int button) const {
			return button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST && _mousePressed[button];
		}

		bool mouseReleased(int button) const {
			return button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST && _mouseReleased[button];
		}

		bool keyDown(int key) const {
			return key >= 0 && key <= GLFW_KEY_LAST && _keysDown[key];
		}

		bool keyPressed(int key) const {
			return key >= 0 && key <= GLFW_KEY_LAST && _keysPressed[key];
		}

		bool keyReleased(int key) const {
			return key >= 0 && key <= GLFW_KEY_LAST && _keysReleased[key];
		}
	};

	class uiCanvas {
	private:
		std::vector<uiElement> _elements;
		std::optional<uiElementId> _hovered;
		std::optional<uiElementId> _active;
		std::optional<uiElementId> _focused;
		std::optional<uiElementId> _clicked;
		std::optional<uiElementId> _dragSource;
		std::optional<uiElementId> _dropTarget;

		static DirectX::XMFLOAT2 anchorPosition(uiAnchor anchor, float width, float height) {
			switch (anchor) {
			case uiAnchor::topCenter: return { width * 0.5f, 0.0f };
			case uiAnchor::topRight: return { width, 0.0f };
			case uiAnchor::centerLeft: return { 0.0f, height * 0.5f };
			case uiAnchor::center: return { width * 0.5f, height * 0.5f };
			case uiAnchor::centerRight: return { width, height * 0.5f };
			case uiAnchor::bottomLeft: return { 0.0f, height };
			case uiAnchor::bottomCenter: return { width * 0.5f, height };
			case uiAnchor::bottomRight: return { width, height };
			default: return { 0.0f, 0.0f };
			}
		}

	public:
		uiElementId add(const uiElement& element) {
			_elements.push_back(element);
			return _elements.size() - 1;
		}

		void clear() {
			_elements.clear();
			_hovered.reset();
			_active.reset();
			_focused.reset();
			_clicked.reset();
			_dragSource.reset();
			_dropTarget.reset();
		}

		void updateInput(const uiInputState& input, float viewportWidth, float viewportHeight) {
			_hovered = topmostAt(
				input.mousePosition.x,
				input.mousePosition.y,
				viewportWidth,
				viewportHeight
			);
			_clicked.reset();
			_dragSource.reset();
			_dropTarget.reset();

			if (input.mousePressed(GLFW_MOUSE_BUTTON_LEFT)) {
				_active = _hovered;
				_focused = _hovered;
			}
			if (input.mouseReleased(GLFW_MOUSE_BUTTON_LEFT)) {
				if (_active) {
					_dragSource = _active;
					_dropTarget = _hovered;
					if (_active == _hovered)
						_clicked = _active;
				}
				_active.reset();
			}
		}

		std::optional<uiElementId> hovered() const { return _hovered; }
		std::optional<uiElementId> active() const { return _active; }
		std::optional<uiElementId> focused() const { return _focused; }
		std::optional<uiElementId> clicked() const { return _clicked; }
		std::optional<uiElementId> dragSource() const { return _dragSource; }
		std::optional<uiElementId> dropTarget() const { return _dropTarget; }
		bool isHovered(uiElementId id) const { return _hovered == id; }
		bool isActive(uiElementId id) const { return _active == id; }
		bool isFocused(uiElementId id) const { return _focused == id; }
		bool wasClicked(uiElementId id) const { return _clicked == id; }
		bool wasDropped(uiElementId source, uiElementId target) const {
			return _dragSource == source && _dropTarget == target && source != target;
		}

		uiElement& get(uiElementId id) { return _elements.at(id); }
		const uiElement& get(uiElementId id) const { return _elements.at(id); }
		const std::vector<uiElement>& elements() const { return _elements; }

		uiRect resolve(uiElementId id, float viewportWidth, float viewportHeight) const {
			return resolve(_elements.at(id), viewportWidth, viewportHeight);
		}

		uiRect resolve(const uiElement& element, float viewportWidth, float viewportHeight) const {
			const DirectX::XMFLOAT2 origin = anchorPosition(element.anchor, viewportWidth, viewportHeight);
			return {
				origin.x + element.position.x - element.size.x * element.pivot.x,
				origin.y + element.position.y - element.size.y * element.pivot.y,
				element.size.x,
				element.size.y
			};
		}

		bool hitTest(uiElementId id, float pointX, float pointY, float viewportWidth, float viewportHeight) const {
			const uiElement& element = _elements.at(id);
			return element.visible && element.interactive &&
				resolve(element, viewportWidth, viewportHeight).contains(pointX, pointY);
		}

		std::optional<uiElementId> topmostAt(
			float pointX,
			float pointY,
			float viewportWidth,
			float viewportHeight
		) const {
			for (size_t i = _elements.size(); i > 0; --i) {
				const uiElement& element = _elements[i - 1];
				if (element.visible && element.interactive &&
					resolve(element, viewportWidth, viewportHeight).contains(pointX, pointY))
					return i - 1;
			}
			return std::nullopt;
		}
	};

	class uiRenderer {
	private:
		struct uiVertex {
			DirectX::XMFLOAT2 position;
			DirectX::XMFLOAT2 uv;
			DirectX::XMFLOAT4 color;
		};

		struct uiFrameData {
			DirectX::XMFLOAT2 viewportSize;
			DirectX::XMFLOAT2 padding;
		};

		struct uiBatch {
			ID3D11ShaderResourceView* texture = nullptr;
			UINT firstIndex = 0;
			UINT indexCount = 0;
		};

		ID3D11Device* _device = nullptr;
		shaderProgram _program;
		constantBuffer<uiFrameData> _frameBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> _vertexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> _indexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> _whiteTexture;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _whiteTextureView;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> _fontTexture;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _fontTextureView;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> _sampler;
		Microsoft::WRL::ComPtr<ID3D11BlendState> _blendState;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> _depthState;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> _rasterizerState;
		UINT _vertexCapacity = 0;
		UINT _indexCapacity = 0;
		static constexpr UINT FONT_COLUMNS = 16;
		static constexpr UINT FONT_ROWS = 6;
		static constexpr UINT FONT_CELL_WIDTH = 16;
		static constexpr UINT FONT_CELL_HEIGHT = 24;
		static constexpr float FONT_ADVANCE = 12.0f;

		void createFontAtlas(ID3D11Device* device) {
			constexpr UINT atlasWidth = FONT_COLUMNS * FONT_CELL_WIDTH;
			constexpr UINT atlasHeight = FONT_ROWS * FONT_CELL_HEIGHT;
			BITMAPINFO bitmapInfo{};
			bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			bitmapInfo.bmiHeader.biWidth = static_cast<LONG>(atlasWidth);
			bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(atlasHeight);
			bitmapInfo.bmiHeader.biPlanes = 1;
			bitmapInfo.bmiHeader.biBitCount = 32;
			bitmapInfo.bmiHeader.biCompression = BI_RGB;

			void* bitmapPixels = nullptr;
			HDC deviceContext = CreateCompatibleDC(nullptr);
			HBITMAP bitmap = CreateDIBSection(deviceContext, &bitmapInfo, DIB_RGB_COLORS, &bitmapPixels, nullptr, 0);
			HGDIOBJ oldBitmap = SelectObject(deviceContext, bitmap);
			HFONT font = CreateFontW(
				-18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
				DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas"
			);
			HGDIOBJ oldFont = SelectObject(deviceContext, font);
			SetBkMode(deviceContext, TRANSPARENT);
			SetTextColor(deviceContext, RGB(255, 255, 255));
			for (UINT character = 32; character <= 126; ++character) {
				const UINT glyph = character - 32;
				const int x = static_cast<int>((glyph % FONT_COLUMNS) * FONT_CELL_WIDTH + 2);
				const int y = static_cast<int>((glyph / FONT_COLUMNS) * FONT_CELL_HEIGHT + 1);
				const wchar_t value = static_cast<wchar_t>(character);
				TextOutW(deviceContext, x, y, &value, 1);
			}
			GdiFlush();

			uint32_t* pixels = static_cast<uint32_t*>(bitmapPixels);
			for (size_t index = 0; index < static_cast<size_t>(atlasWidth) * atlasHeight; ++index) {
				const uint8_t coverage = static_cast<uint8_t>(pixels[index] & 0xffu);
				pixels[index] = 0x00ffffffu | (static_cast<uint32_t>(coverage) << 24);
			}

			D3D11_TEXTURE2D_DESC description{};
			description.Width = atlasWidth;
			description.Height = atlasHeight;
			description.MipLevels = 1;
			description.ArraySize = 1;
			description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			description.SampleDesc.Count = 1;
			description.Usage = D3D11_USAGE_IMMUTABLE;
			description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			D3D11_SUBRESOURCE_DATA data{};
			data.pSysMem = bitmapPixels;
			data.SysMemPitch = atlasWidth * sizeof(uint32_t);
			DX_CHECK(device->CreateTexture2D(&description, &data, _fontTexture.GetAddressOf()));
			DX_CHECK(device->CreateShaderResourceView(_fontTexture.Get(), nullptr, _fontTextureView.GetAddressOf()));

			SelectObject(deviceContext, oldFont);
			SelectObject(deviceContext, oldBitmap);
			DeleteObject(font);
			DeleteObject(bitmap);
			DeleteDC(deviceContext);
		}

		void ensureBuffer(Microsoft::WRL::ComPtr<ID3D11Buffer>& buffer, UINT& capacity, UINT required, UINT bindFlags) {
			if (buffer && capacity >= required)
				return;

			capacity = std::max<UINT>(4096u, std::bit_ceil(required));
			D3D11_BUFFER_DESC description{};
			description.ByteWidth = capacity;
			description.Usage = D3D11_USAGE_DYNAMIC;
			description.BindFlags = bindFlags;
			description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			buffer.Reset();
			DX_CHECK(_device->CreateBuffer(&description, nullptr, buffer.GetAddressOf()));
		}

	public:
		uiRenderer() = default;

		explicit uiRenderer(ID3D11Device* device) { create(device); }

		void create(ID3D11Device* device) {
			_device = device;
			_program.initVertexShader(device, L"asset/shader/ui.hlsl", "vertexMain", "vs_5_0");
			_program.initPixelShader(device, L"asset/shader/ui.hlsl", "pixelMain", "ps_5_0");
			_program.initInputLayout(device, {
				{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(uiVertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(uiVertex, uv), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(uiVertex, color), D3D11_INPUT_PER_VERTEX_DATA, 0 }
			});
			_frameBuffer.create(device);

			const uint32_t whitePixel = 0xffffffffu;
			D3D11_TEXTURE2D_DESC textureDescription{};
			textureDescription.Width = 1;
			textureDescription.Height = 1;
			textureDescription.MipLevels = 1;
			textureDescription.ArraySize = 1;
			textureDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			textureDescription.SampleDesc.Count = 1;
			textureDescription.Usage = D3D11_USAGE_IMMUTABLE;
			textureDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			D3D11_SUBRESOURCE_DATA textureData{};
			textureData.pSysMem = &whitePixel;
			textureData.SysMemPitch = sizeof(whitePixel);
			DX_CHECK(device->CreateTexture2D(&textureDescription, &textureData, _whiteTexture.GetAddressOf()));
			DX_CHECK(device->CreateShaderResourceView(_whiteTexture.Get(), nullptr, _whiteTextureView.GetAddressOf()));
			createFontAtlas(device);

			D3D11_SAMPLER_DESC samplerDescription{};
			samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
			DX_CHECK(device->CreateSamplerState(&samplerDescription, _sampler.GetAddressOf()));

			D3D11_BLEND_DESC blendDescription{};
			blendDescription.RenderTarget[0].BlendEnable = TRUE;
			blendDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
			blendDescription.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			blendDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
			blendDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
			blendDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
			blendDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			blendDescription.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			DX_CHECK(device->CreateBlendState(&blendDescription, _blendState.GetAddressOf()));

			D3D11_DEPTH_STENCIL_DESC depthDescription{};
			depthDescription.DepthEnable = FALSE;
			depthDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			depthDescription.DepthFunc = D3D11_COMPARISON_ALWAYS;
			DX_CHECK(device->CreateDepthStencilState(&depthDescription, _depthState.GetAddressOf()));

			D3D11_RASTERIZER_DESC rasterizerDescription{};
			rasterizerDescription.FillMode = D3D11_FILL_SOLID;
			rasterizerDescription.CullMode = D3D11_CULL_NONE;
			rasterizerDescription.DepthClipEnable = TRUE;
			DX_CHECK(device->CreateRasterizerState(&rasterizerDescription, _rasterizerState.GetAddressOf()));
		}

		void render(ID3D11DeviceContext* context, const uiCanvas& canvas, float viewportWidth, float viewportHeight) {
			if (!_device || viewportWidth <= 0.0f || viewportHeight <= 0.0f)
				return;

			std::vector<uiVertex> vertices;
			std::vector<uint32_t> indices;
			std::vector<uiBatch> batches;
			vertices.reserve(canvas.elements().size() * 4);
			indices.reserve(canvas.elements().size() * 6);

			auto appendQuad = [&](ID3D11ShaderResourceView* texture, const uiRect& rectangle,
				DirectX::XMFLOAT2 uvMin, DirectX::XMFLOAT2 uvMax, DirectX::XMFLOAT4 color) {
				if (batches.empty() || batches.back().texture != texture)
					batches.push_back({ texture, static_cast<UINT>(indices.size()), 0 });
				const uint32_t firstVertex = static_cast<uint32_t>(vertices.size());
				vertices.push_back({ { rectangle.x, rectangle.y }, uvMin, color });
				vertices.push_back({ { rectangle.x + rectangle.width, rectangle.y }, { uvMax.x, uvMin.y }, color });
				vertices.push_back({ { rectangle.x + rectangle.width, rectangle.y + rectangle.height }, uvMax, color });
				vertices.push_back({ { rectangle.x, rectangle.y + rectangle.height }, { uvMin.x, uvMax.y }, color });
				indices.insert(indices.end(), {
					firstVertex, firstVertex + 1, firstVertex + 2,
					firstVertex, firstVertex + 2, firstVertex + 3
				});
				batches.back().indexCount += 6;
			};

			for (const uiElement& element : canvas.elements()) {
				if (!element.visible)
					continue;

				const uiRect rectangle = canvas.resolve(element, viewportWidth, viewportHeight);
				if (element.size.x > 0.0f && element.size.y > 0.0f) {
					ID3D11ShaderResourceView* texture = element.texture ? element.texture : _whiteTextureView.Get();
					appendQuad(texture, rectangle, element.uvMin, element.uvMax, element.color);
				}

				if (!element.text.empty() && _fontTextureView) {
					const float scale = element.fontSize / 18.0f;
					float cursorX = rectangle.x + element.textOffset.x;
					float cursorY = rectangle.y + element.textOffset.y;
					const float lineStart = cursorX;
					for (unsigned char character : element.text) {
						if (character == '\n') {
							cursorX = lineStart;
							cursorY += FONT_CELL_HEIGHT * scale;
							continue;
						}
						if (character < 32 || character > 126) character = '?';
						const UINT glyph = character - 32;
						const float u0 = static_cast<float>((glyph % FONT_COLUMNS) * FONT_CELL_WIDTH) /
							static_cast<float>(FONT_COLUMNS * FONT_CELL_WIDTH);
						const float v0 = static_cast<float>((glyph / FONT_COLUMNS) * FONT_CELL_HEIGHT) /
							static_cast<float>(FONT_ROWS * FONT_CELL_HEIGHT);
						const float u1 = u0 + 1.0f / FONT_COLUMNS;
						const float v1 = v0 + 1.0f / FONT_ROWS;
						appendQuad(
							_fontTextureView.Get(),
							{ cursorX, cursorY, FONT_CELL_WIDTH * scale, FONT_CELL_HEIGHT * scale },
							{ u0, v0 }, { u1, v1 }, element.textColor
						);
						cursorX += FONT_ADVANCE * scale;
					}
				}
			}

			if (indices.empty())
				return;

			const UINT vertexBytes = static_cast<UINT>(vertices.size() * sizeof(uiVertex));
			const UINT indexBytes = static_cast<UINT>(indices.size() * sizeof(uint32_t));
			ensureBuffer(_vertexBuffer, _vertexCapacity, vertexBytes, D3D11_BIND_VERTEX_BUFFER);
			ensureBuffer(_indexBuffer, _indexCapacity, indexBytes, D3D11_BIND_INDEX_BUFFER);

			D3D11_MAPPED_SUBRESOURCE mapped{};
			DX_CHECK(context->Map(_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
			memcpy(mapped.pData, vertices.data(), vertexBytes);
			context->Unmap(_vertexBuffer.Get(), 0);
			DX_CHECK(context->Map(_indexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
			memcpy(mapped.pData, indices.data(), indexBytes);
			context->Unmap(_indexBuffer.Get(), 0);

			Microsoft::WRL::ComPtr<ID3D11InputLayout> previousInputLayout;
			Microsoft::WRL::ComPtr<ID3D11Buffer> previousVertexBuffer;
			Microsoft::WRL::ComPtr<ID3D11Buffer> previousIndexBuffer;
			Microsoft::WRL::ComPtr<ID3D11Buffer> previousVertexConstantBuffer;
			Microsoft::WRL::ComPtr<ID3D11VertexShader> previousVertexShader;
			Microsoft::WRL::ComPtr<ID3D11PixelShader> previousPixelShader;
			Microsoft::WRL::ComPtr<ID3D11GeometryShader> previousGeometryShader;
			Microsoft::WRL::ComPtr<ID3D11HullShader> previousHullShader;
			Microsoft::WRL::ComPtr<ID3D11DomainShader> previousDomainShader;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> previousTexture;
			Microsoft::WRL::ComPtr<ID3D11SamplerState> previousSampler;
			Microsoft::WRL::ComPtr<ID3D11BlendState> previousBlendState;
			Microsoft::WRL::ComPtr<ID3D11DepthStencilState> previousDepthState;
			Microsoft::WRL::ComPtr<ID3D11RasterizerState> previousRasterizerState;
			D3D11_PRIMITIVE_TOPOLOGY previousTopology{};
			DXGI_FORMAT previousIndexFormat{};
			UINT previousVertexStride = 0;
			UINT previousVertexOffset = 0;
			UINT previousIndexOffset = 0;
			UINT previousSampleMask = 0;
			UINT previousStencilReference = 0;
			float previousBlendFactor[4]{};

			context->IAGetInputLayout(previousInputLayout.GetAddressOf());
			context->IAGetPrimitiveTopology(&previousTopology);
			context->IAGetVertexBuffers(0, 1, previousVertexBuffer.GetAddressOf(), &previousVertexStride, &previousVertexOffset);
			context->IAGetIndexBuffer(previousIndexBuffer.GetAddressOf(), &previousIndexFormat, &previousIndexOffset);
			context->VSGetConstantBuffers(0, 1, previousVertexConstantBuffer.GetAddressOf());
			context->VSGetShader(previousVertexShader.GetAddressOf(), nullptr, nullptr);
			context->PSGetShader(previousPixelShader.GetAddressOf(), nullptr, nullptr);
			context->GSGetShader(previousGeometryShader.GetAddressOf(), nullptr, nullptr);
			context->HSGetShader(previousHullShader.GetAddressOf(), nullptr, nullptr);
			context->DSGetShader(previousDomainShader.GetAddressOf(), nullptr, nullptr);
			context->PSGetShaderResources(0, 1, previousTexture.GetAddressOf());
			context->PSGetSamplers(0, 1, previousSampler.GetAddressOf());
			context->OMGetBlendState(previousBlendState.GetAddressOf(), previousBlendFactor, &previousSampleMask);
			context->OMGetDepthStencilState(previousDepthState.GetAddressOf(), &previousStencilReference);
			context->RSGetState(previousRasterizerState.GetAddressOf());

			_frameBuffer.update(context, { { viewportWidth, viewportHeight }, { 0.0f, 0.0f } });
			_frameBuffer.bindVS(context, 0);
			_program.bindShaders(context);
			context->GSSetShader(nullptr, nullptr, 0);
			context->HSSetShader(nullptr, nullptr, 0);
			context->DSSetShader(nullptr, nullptr, 0);
			context->PSSetSamplers(0, 1, _sampler.GetAddressOf());
			context->OMSetBlendState(_blendState.Get(), nullptr, 0xffffffffu);
			context->OMSetDepthStencilState(_depthState.Get(), 0);
			context->RSSetState(_rasterizerState.Get());
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			const UINT stride = sizeof(uiVertex);
			const UINT offset = 0;
			ID3D11Buffer* vertexBuffer = _vertexBuffer.Get();
			context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
			context->IASetIndexBuffer(_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
			for (const uiBatch& batch : batches) {
				context->PSSetShaderResources(0, 1, &batch.texture);
				context->DrawIndexed(batch.indexCount, batch.firstIndex, 0);
			}

			ID3D11Buffer* previousVertexBufferPointer = previousVertexBuffer.Get();
			ID3D11Buffer* previousVertexConstantBufferPointer = previousVertexConstantBuffer.Get();
			ID3D11ShaderResourceView* previousTexturePointer = previousTexture.Get();
			ID3D11SamplerState* previousSamplerPointer = previousSampler.Get();
			context->IASetInputLayout(previousInputLayout.Get());
			context->IASetPrimitiveTopology(previousTopology);
			context->IASetVertexBuffers(0, 1, &previousVertexBufferPointer, &previousVertexStride, &previousVertexOffset);
			context->IASetIndexBuffer(previousIndexBuffer.Get(), previousIndexFormat, previousIndexOffset);
			context->VSSetConstantBuffers(0, 1, &previousVertexConstantBufferPointer);
			context->VSSetShader(previousVertexShader.Get(), nullptr, 0);
			context->PSSetShader(previousPixelShader.Get(), nullptr, 0);
			context->GSSetShader(previousGeometryShader.Get(), nullptr, 0);
			context->HSSetShader(previousHullShader.Get(), nullptr, 0);
			context->DSSetShader(previousDomainShader.Get(), nullptr, 0);
			context->PSSetShaderResources(0, 1, &previousTexturePointer);
			context->PSSetSamplers(0, 1, &previousSamplerPointer);
			context->OMSetBlendState(previousBlendState.Get(), previousBlendFactor, previousSampleMask);
			context->OMSetDepthStencilState(previousDepthState.Get(), previousStencilReference);
			context->RSSetState(previousRasterizerState.Get());
		}
	};

} // namespace ac
