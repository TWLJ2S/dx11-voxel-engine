#pragma once

/* Nuklear + D3D11 host for the voxel engine (SDL3 input). */

#include <SDL3/SDL.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <stdexcept>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_STANDARD_BOOL
#include <nuklear/nuklear.h>

namespace ac {

	class nuklearHost {
	public:
		void create(ID3D11Device* device, ID3D11DeviceContext* context, int width, int height);
		void destroy();
		void resize(ID3D11DeviceContext* context, int width, int height);
		void processEvent(const SDL_Event& event);
		void beginInput();
		void endInput();
		void setMouseScale(float scaleX, float scaleY);
		void syncMouse(float windowX, float windowY);
		void render(ID3D11DeviceContext* context);

		struct nk_context* ctx() { return _ctx; }
		const struct nk_context* ctx() const { return _ctx; }
		bool ready() const { return _ctx != nullptr; }
		bool wantCaptureMouse() const;
		bool wantCaptureKeyboard() const;

		static struct nk_image imageFromSrv(ID3D11ShaderResourceView* srv, int w, int h);
		static struct nk_image subImageFromSrv(
			ID3D11ShaderResourceView* srv,
			int atlasW, int atlasH,
			float uvMinX, float uvMinY, float uvMaxX, float uvMaxY);

	private:
		struct nk_context* _ctx = nullptr;
		int _width = 0;
		int _height = 0;
		float _mouseScaleX = 1.0f;
		float _mouseScaleY = 1.0f;
		bool _mouseDown[3]{};
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> _noDepthState;
	};

}
