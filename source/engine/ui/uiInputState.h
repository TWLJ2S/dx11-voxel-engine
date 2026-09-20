#pragma once

#include <core/window.h>
#include <core/inputKeys.h>

#include <array>
#include <DirectXMath.h>

namespace ac {

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

}
