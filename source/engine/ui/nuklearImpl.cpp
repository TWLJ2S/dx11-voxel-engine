/* Nuklear implementation TU — keep NK_*_IMPLEMENTATION isolated here. */

#include <d3d11.h>

/* nuklear_d3d11.h is written against the C COM macros. Map them to C++ COM. */
#ifndef COBJMACROS
#define ID3D11DeviceContext_IASetInputLayout(ctx, a) (ctx)->IASetInputLayout(a)
#define ID3D11DeviceContext_IASetVertexBuffers(ctx, a, b, c, d, e) (ctx)->IASetVertexBuffers(a, b, c, d, e)
#define ID3D11DeviceContext_IASetIndexBuffer(ctx, a, b, c) (ctx)->IASetIndexBuffer(a, b, c)
#define ID3D11DeviceContext_IASetPrimitiveTopology(ctx, a) (ctx)->IASetPrimitiveTopology(a)
#define ID3D11DeviceContext_VSSetShader(ctx, a, b, c) (ctx)->VSSetShader(a, b, c)
#define ID3D11DeviceContext_VSSetConstantBuffers(ctx, a, b, c) (ctx)->VSSetConstantBuffers(a, b, c)
#define ID3D11DeviceContext_PSSetShader(ctx, a, b, c) (ctx)->PSSetShader(a, b, c)
#define ID3D11DeviceContext_PSSetSamplers(ctx, a, b, c) (ctx)->PSSetSamplers(a, b, c)
#define ID3D11DeviceContext_OMSetBlendState(ctx, a, b, c) (ctx)->OMSetBlendState(a, b, c)
#define ID3D11DeviceContext_RSSetState(ctx, a) (ctx)->RSSetState(a)
#define ID3D11DeviceContext_RSSetViewports(ctx, a, b) (ctx)->RSSetViewports(a, b)
#define ID3D11DeviceContext_Map(ctx, a, b, c, d, e) (ctx)->Map(a, b, c, d, e)
#define ID3D11DeviceContext_Unmap(ctx, a, b) (ctx)->Unmap(a, b)
#define ID3D11DeviceContext_PSSetShaderResources(ctx, a, b, c) (ctx)->PSSetShaderResources(a, b, c)
#define ID3D11DeviceContext_RSSetScissorRects(ctx, a, b) (ctx)->RSSetScissorRects(a, b)
#define ID3D11DeviceContext_DrawIndexed(ctx, a, b, c) (ctx)->DrawIndexed(a, b, c)
#define ID3D11Device_AddRef(dev) (dev)->AddRef()
#define ID3D11Device_CreateRasterizerState(dev, a, b) (dev)->CreateRasterizerState(a, b)
#define ID3D11Device_CreateVertexShader(dev, a, b, c, d) (dev)->CreateVertexShader(a, b, c, d)
#define ID3D11Device_CreateInputLayout(dev, a, b, c, d, e) (dev)->CreateInputLayout(a, b, c, d, e)
#define ID3D11Device_CreateBuffer(dev, a, b, c) (dev)->CreateBuffer(a, b, c)
#define ID3D11Device_CreatePixelShader(dev, a, b, c, d) (dev)->CreatePixelShader(a, b, c, d)
#define ID3D11Device_CreateBlendState(dev, a, b) (dev)->CreateBlendState(a, b)
#define ID3D11Device_CreateSamplerState(dev, a, b) (dev)->CreateSamplerState(a, b)
#define ID3D11Device_CreateTexture2D(dev, a, b, c) (dev)->CreateTexture2D(a, b, c)
#define ID3D11Device_CreateShaderResourceView(dev, a, b, c) (dev)->CreateShaderResourceView(a, b, c)
#define ID3D11Device_Release(dev) (dev)->Release()
#define ID3D11Texture2D_Release(obj) (obj)->Release()
#define ID3D11SamplerState_Release(obj) (obj)->Release()
#define ID3D11ShaderResourceView_Release(obj) (obj)->Release()
#define ID3D11Buffer_Release(obj) (obj)->Release()
#define ID3D11BlendState_Release(obj) (obj)->Release()
#define ID3D11PixelShader_Release(obj) (obj)->Release()
#define ID3D11VertexShader_Release(obj) (obj)->Release()
#define ID3D11InputLayout_Release(obj) (obj)->Release()
#define ID3D11RasterizerState_Release(obj) (obj)->Release()
#endif

#define NK_IMPLEMENTATION
#define NK_D3D11_IMPLEMENTATION
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_STANDARD_BOOL

#include <nuklear/nuklear.h>
#include <nuklear/nuklear_d3d11.h>

/* Implementation macros must not stay defined when nuklearHost.h re-includes
   nuklear.h — the NK_IMPLEMENTATION block sits outside the header guard. */
#undef NK_IMPLEMENTATION
#undef NK_D3D11_IMPLEMENTATION

#include <ui/nuklearHost.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <wrl/client.h>

namespace ac {
	namespace {
		constexpr unsigned MAX_VERTEX_BUFFER = 1024 * 1024;
		constexpr unsigned MAX_INDEX_BUFFER = 256 * 1024;

		nk_keys mapKey(SDL_Keycode key, SDL_Keymod mod) {
			const bool ctrl = (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
			switch (key) {
			case SDLK_LEFT: return NK_KEY_LEFT;
			case SDLK_RIGHT: return NK_KEY_RIGHT;
			case SDLK_UP: return NK_KEY_UP;
			case SDLK_DOWN: return NK_KEY_DOWN;
			case SDLK_DELETE: return NK_KEY_DEL;
			case SDLK_BACKSPACE: return NK_KEY_BACKSPACE;
			case SDLK_RETURN: case SDLK_KP_ENTER: return NK_KEY_ENTER;
			case SDLK_TAB: return NK_KEY_TAB;
			case SDLK_HOME: return ctrl ? NK_KEY_TEXT_START : NK_KEY_TEXT_LINE_START;
			case SDLK_END: return ctrl ? NK_KEY_TEXT_END : NK_KEY_TEXT_LINE_END;
			case SDLK_PAGEUP: return NK_KEY_SCROLL_UP;
			case SDLK_PAGEDOWN: return NK_KEY_SCROLL_DOWN;
			// Clipboard / edit chords only with Ctrl/Cmd — never steal plain a/c/v/x/z/r.
			case SDLK_C: return ctrl ? NK_KEY_COPY : NK_KEY_NONE;
			case SDLK_V: return ctrl ? NK_KEY_PASTE : NK_KEY_NONE;
			case SDLK_X: return ctrl ? NK_KEY_CUT : NK_KEY_NONE;
			case SDLK_Z: return ctrl ? NK_KEY_TEXT_UNDO : NK_KEY_NONE;
			case SDLK_R: return ctrl ? NK_KEY_TEXT_REDO : NK_KEY_NONE;
			case SDLK_A: return ctrl ? NK_KEY_TEXT_SELECT_ALL : NK_KEY_NONE;
			case SDLK_LSHIFT: case SDLK_RSHIFT: return NK_KEY_SHIFT;
			case SDLK_LCTRL: case SDLK_RCTRL: return NK_KEY_CTRL;
			default: return NK_KEY_NONE;
			}
		}
	}

	void nuklearHost::create(ID3D11Device* device, ID3D11DeviceContext* /*context*/, int width, int height) {
		_width = width;
		_height = height;
		_ctx = nk_d3d11_init(device, width, height, MAX_VERTEX_BUFFER, MAX_INDEX_BUFFER);
		if (!_ctx)
			throw std::runtime_error("Nuklear D3D11 init failed");

		D3D11_DEPTH_STENCIL_DESC depthDesc{};
		depthDesc.DepthEnable = FALSE;
		depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		depthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		depthDesc.StencilEnable = FALSE;
		if (FAILED(device->CreateDepthStencilState(&depthDesc, _noDepthState.ReleaseAndGetAddressOf())))
			throw std::runtime_error("Failed to create Nuklear depth-disabled state");

		struct nk_font_atlas* atlas = nullptr;
		nk_d3d11_font_stash_begin(&atlas);
		nk_d3d11_font_stash_end();

		// Minecraft-like muted gray menus (container GUIs use texture panels).
		struct nk_color table[NK_COLOR_COUNT];
		table[NK_COLOR_TEXT] = nk_rgba(255, 255, 255, 255);
		table[NK_COLOR_WINDOW] = nk_rgba(198, 198, 198, 255);
		table[NK_COLOR_HEADER] = nk_rgba(139, 139, 139, 255);
		table[NK_COLOR_BORDER] = nk_rgba(55, 55, 55, 255);
		table[NK_COLOR_BUTTON] = nk_rgba(112, 112, 112, 255);
		table[NK_COLOR_BUTTON_HOVER] = nk_rgba(140, 140, 140, 255);
		table[NK_COLOR_BUTTON_ACTIVE] = nk_rgba(80, 80, 80, 255);
		table[NK_COLOR_TOGGLE] = nk_rgba(85, 85, 85, 255);
		table[NK_COLOR_TOGGLE_HOVER] = nk_rgba(110, 110, 110, 255);
		table[NK_COLOR_TOGGLE_CURSOR] = nk_rgba(255, 255, 255, 255);
		table[NK_COLOR_SELECT] = nk_rgba(85, 85, 85, 255);
		table[NK_COLOR_SELECT_ACTIVE] = nk_rgba(112, 112, 112, 255);
		table[NK_COLOR_SLIDER] = nk_rgba(85, 85, 85, 255);
		table[NK_COLOR_SLIDER_CURSOR] = nk_rgba(160, 160, 160, 255);
		table[NK_COLOR_SLIDER_CURSOR_HOVER] = nk_rgba(190, 190, 190, 255);
		table[NK_COLOR_SLIDER_CURSOR_ACTIVE] = nk_rgba(160, 160, 160, 255);
		table[NK_COLOR_PROPERTY] = nk_rgba(85, 85, 85, 255);
		table[NK_COLOR_EDIT] = nk_rgba(0, 0, 0, 255);
		table[NK_COLOR_EDIT_CURSOR] = nk_rgba(255, 255, 255, 255);
		table[NK_COLOR_COMBO] = nk_rgba(85, 85, 85, 255);
		table[NK_COLOR_CHART] = nk_rgba(85, 85, 85, 255);
		table[NK_COLOR_CHART_COLOR] = nk_rgba(255, 255, 255, 255);
		table[NK_COLOR_CHART_COLOR_HIGHLIGHT] = nk_rgba(255, 0, 0, 255);
		table[NK_COLOR_SCROLLBAR] = nk_rgba(120, 120, 120, 255);
		table[NK_COLOR_SCROLLBAR_CURSOR] = nk_rgba(190, 190, 190, 255);
		table[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = nk_rgba(210, 210, 210, 255);
		table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_rgba(170, 170, 170, 255);
		table[NK_COLOR_TAB_HEADER] = nk_rgba(139, 139, 139, 255);
		table[NK_COLOR_KNOB] = nk_rgba(85, 85, 85, 255);
		table[NK_COLOR_KNOB_CURSOR] = nk_rgba(160, 160, 160, 255);
		table[NK_COLOR_KNOB_CURSOR_HOVER] = nk_rgba(190, 190, 190, 255);
		table[NK_COLOR_KNOB_CURSOR_ACTIVE] = nk_rgba(160, 160, 160, 255);
		nk_style_from_table(_ctx, table);

		_ctx->style.window.border = 2.0f;
		_ctx->style.window.rounding = 0.0f;
		_ctx->style.button.rounding = 0.0f;
		_ctx->style.button.border = 2.0f;
	}

	void nuklearHost::destroy() {
		if (!_ctx) return;
		nk_d3d11_shutdown();
		_ctx = nullptr;
		_noDepthState.Reset();
	}

	void nuklearHost::resize(ID3D11DeviceContext* context, int width, int height) {
		_width = width;
		_height = height;
		if (_ctx)
			nk_d3d11_resize(context, width, height);
	}

	void nuklearHost::beginInput() {
		if (_ctx) nk_input_begin(_ctx);
	}

	void nuklearHost::endInput() {
		if (_ctx) nk_input_end(_ctx);
	}

	void nuklearHost::setMouseScale(float scaleX, float scaleY) {
		_mouseScaleX = scaleX > 0.0f ? scaleX : 1.0f;
		_mouseScaleY = scaleY > 0.0f ? scaleY : 1.0f;
	}

	void nuklearHost::syncMouse(float windowX, float windowY) {
		if (!_ctx) return;
		nk_input_motion(_ctx,
			(int)std::lround(windowX * _mouseScaleX),
			(int)std::lround(windowY * _mouseScaleY));
	}

	void nuklearHost::processEvent(const SDL_Event& event) {
		if (!_ctx) return;
		struct nk_context* ctx = _ctx;

		switch (event.type) {
		case SDL_EVENT_MOUSE_MOTION:
			nk_input_motion(ctx,
				(int)std::lround(event.motion.x * _mouseScaleX),
				(int)std::lround(event.motion.y * _mouseScaleY));
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP: {
			const int down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
			const int x = (int)std::lround(event.button.x * _mouseScaleX);
			const int y = (int)std::lround(event.button.y * _mouseScaleY);
			if (event.button.button == SDL_BUTTON_LEFT)
				nk_input_button(ctx, NK_BUTTON_LEFT, x, y, down);
			else if (event.button.button == SDL_BUTTON_RIGHT)
				nk_input_button(ctx, NK_BUTTON_RIGHT, x, y, down);
			else if (event.button.button == SDL_BUTTON_MIDDLE)
				nk_input_button(ctx, NK_BUTTON_MIDDLE, x, y, down);
			if (event.button.button >= 1 && event.button.button <= 3)
				_mouseDown[event.button.button - 1] = down != 0;
			break;
		}
		case SDL_EVENT_MOUSE_WHEEL:
			nk_input_scroll(ctx, nk_vec2(event.wheel.x, event.wheel.y));
			break;
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP: {
			const int down = event.type == SDL_EVENT_KEY_DOWN;
			const nk_keys key = mapKey(event.key.key, event.key.mod);
			if (key != NK_KEY_NONE)
				nk_input_key(ctx, key, down);
			break;
		}
		case SDL_EVENT_TEXT_INPUT: {
			const char* text = event.text.text;
			if (!text || !text[0]) break;
			nk_rune unicode = 0;
			int consumed = 0;
			while ((consumed = nk_utf_decode(text, &unicode, 4)) > 0) {
				nk_input_unicode(ctx, unicode);
				text += consumed;
				if (!*text) break;
			}
			break;
		}
		default:
			break;
		}
	}

	void nuklearHost::render(ID3D11DeviceContext* context) {
		if (!_ctx || !context) return;

		// UI must never depth-test against the 3D scene (particles/place-break
		// often leave default depth-on + DSV bound, which clips the HUD).
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> oldRtv;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> oldDsv;
		context->OMGetRenderTargets(1, oldRtv.GetAddressOf(), oldDsv.GetAddressOf());
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> oldDepth;
		UINT oldStencilRef = 0;
		context->OMGetDepthStencilState(oldDepth.GetAddressOf(), &oldStencilRef);

		ID3D11RenderTargetView* rtv = oldRtv.Get();
		context->OMSetRenderTargets(1, &rtv, nullptr);
		if (_noDepthState)
			context->OMSetDepthStencilState(_noDepthState.Get(), 0);

		// Drop leftover scene lights/shadows/CBs/GS so HUD textures draw unlit
		// at authored color instead of picking up cave/torch/sun state.
		ID3D11ShaderResourceView* nullSrvs[14] = {};
		context->PSSetShaderResources(0, 14, nullSrvs);
		ID3D11SamplerState* nullSamplers[5] = {};
		context->PSSetSamplers(0, 5, nullSamplers);
		ID3D11Buffer* nullCBs[10] = {};
		context->PSSetConstantBuffers(0, 10, nullCBs);
		context->GSSetShader(nullptr, nullptr, 0);
		context->HSSetShader(nullptr, nullptr, 0);
		context->DSSetShader(nullptr, nullptr, 0);

		nk_d3d11_render(context, NK_ANTI_ALIASING_ON);

		context->PSSetShaderResources(0, 14, nullSrvs);
		context->OMSetRenderTargets(1, oldRtv.GetAddressOf(), oldDsv.Get());
		context->OMSetDepthStencilState(oldDepth.Get(), oldStencilRef);
	}

	bool nuklearHost::wantCaptureMouse() const {
		return _ctx && nk_item_is_any_active(_ctx);
	}

	bool nuklearHost::wantCaptureKeyboard() const {
		return _ctx && nk_item_is_any_active(_ctx);
	}

	struct nk_image nuklearHost::imageFromSrv(ID3D11ShaderResourceView* srv, int w, int h) {
		return nk_image_ptr(srv);
		(void)w; (void)h;
	}

	struct nk_image nuklearHost::subImageFromSrv(
		ID3D11ShaderResourceView* srv,
		int atlasW, int atlasH,
		float uvMinX, float uvMinY, float uvMaxX, float uvMaxY
	) {
		const unsigned short x = (unsigned short)std::clamp((int)(uvMinX * atlasW + 0.5f), 0, atlasW);
		const unsigned short y = (unsigned short)std::clamp((int)(uvMinY * atlasH + 0.5f), 0, atlasH);
		const unsigned short w = (unsigned short)std::max(1, (int)((uvMaxX - uvMinX) * atlasW + 0.5f));
		const unsigned short h = (unsigned short)std::max(1, (int)((uvMaxY - uvMinY) * atlasH + 0.5f));
		return nk_subimage_ptr(srv, (nk_ushort)atlasW, (nk_ushort)atlasH, nk_rect((float)x, (float)y, (float)w, (float)h));
	}

}
