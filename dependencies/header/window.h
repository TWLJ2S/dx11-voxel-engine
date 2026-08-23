#pragma once

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <stdexcept>
#include <array>
#include <chrono>
#include <thread>

namespace ac {

    struct keyCallBack {
        int key = 0;
        int scanCode = 0;
        int action = 0;
        int mods = 0;
        bool update = 0;
    };

    struct cursorCallBack {
        template<typename T>
        struct bitMask {
            T _data = 0;

            inline void press(int button) { _data |= (1u << button); }

            inline void release(int button) { _data &= (T)~(1u << button); }

            inline bool getButton(int button) const { return _data & (1u << button); }

            inline bool operator[](int button) const { return _data & (1u << button); }

            inline void toggle(int button) { _data ^= (1u << button); }
        };

        int mods = 0;
        bitMask<UINT8> action = {};

        bool buttonUpdate = 0;
        bool posUpdate = 0;
        bool scrollUpdate = 0;
        bool updateDelta = 0;

        double scrollX = 0.0;
        double scrollY = 0.0;

        double x = 0.0;
        double y = 0.0;

        double lastX = 0.0;
        double lastY = 0.0;

		double deltaX = 0.0;
		double deltaY = 0.0;
    };

    struct windowCallBack {
        GLFWwindow* window = nullptr;
        GLFWmonitor* monitor = nullptr;
        GLFWwindow* share = nullptr;
        HWND hwnd = nullptr;

        int width = 0;
        int height = 0;

        bool frameUpdate = 0;
        bool isFocused = 1;
        bool focusUpdate = 0;

        const char* name = "";

        UINT focusedDeltaTimeLimit = 1000000000 / 300;
        UINT unfocusedDeltaTimeLimit = 1000000000 / 30;
        float targetAspect = 4.0f / 3.0f;
        float aspect = 0.0f;
        double deltaTime = 0.0;
        double currentTime = 0.0;
        std::chrono::nanoseconds spinThreshold = std::chrono::nanoseconds(2000000);
        std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point currentFrame = startTime;
        std::chrono::steady_clock::time_point lastFrame = startTime;
    };

    class window {
    private:

        static void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
            if (width <= 0 || height <= 0) return;

            ac::window* self = static_cast<ac::window*>(glfwGetWindowUserPointer(window));

            self->_windowCallBack.width = width;
            self->_windowCallBack.height = height;
            self->_windowCallBack.aspect = (float)width / height;
            self->_windowCallBack.frameUpdate = 1;
        }

        static void mouse_pos_callback(GLFWwindow* window, double xpos, double ypos) {
            ac::window* self = static_cast<ac::window*>(glfwGetWindowUserPointer(window));

            self->_cursorCallBack.x = xpos;
            self->_cursorCallBack.y = ypos;
            self->_cursorCallBack.posUpdate = 1;
        }

        static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
            ac::window* self = static_cast<ac::window*>(glfwGetWindowUserPointer(window));

            self->_cursorCallBack.mods = mods;

            if (action == GLFW_PRESS) {
                self->_cursorCallBack.action.press(button);
				if (button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST)
					self->_mousePressed[button] = true;
            }
            else if (action == GLFW_RELEASE) {
                self->_cursorCallBack.action.release(button);
				if (button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST)
					self->_mouseReleased[button] = true;
            }
			self->_cursorCallBack.buttonUpdate = 1;
        }

        static void window_focus_callback(GLFWwindow* window, int focused) {
            ac::window* self = static_cast<ac::window*>(glfwGetWindowUserPointer(window));

            self->_windowCallBack.isFocused = focused;
            self->_windowCallBack.focusUpdate = 1;
			if (!focused) {
				self->_keysDown.fill(false);
				self->_cursorCallBack.action._data = 0;
			}
        }

        static void window_key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
            ac::window* self = static_cast<ac::window*>(glfwGetWindowUserPointer(window));

            self->_keyCallBack.key = key;
            self->_keyCallBack.scanCode = scancode;
            self->_keyCallBack.action = action;
            self->_keyCallBack.mods = mods;
            self->_keyCallBack.update = 1;

			if (key >= 0 && key <= GLFW_KEY_LAST) {
				if (action == GLFW_PRESS) {
					self->_keysDown[key] = true;
					self->_keysPressed[key] = true;
				}
				else if (action == GLFW_RELEASE) {
					self->_keysDown[key] = false;
					self->_keysReleased[key] = true;
				}
			}
        }

        static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
            ac::window* self = static_cast<ac::window*>(glfwGetWindowUserPointer(window));

            self->_cursorCallBack.scrollX += xoffset;
            self->_cursorCallBack.scrollY += yoffset;
            self->_cursorCallBack.scrollUpdate = 1;
        }

        inline void begin() {
            _windowCallBack.frameUpdate = 0;
            _cursorCallBack.scrollUpdate = 0;
            _cursorCallBack.buttonUpdate = 0;
            _cursorCallBack.posUpdate = 0;
            _keyCallBack.update = 0;
			_mousePressed.fill(false);
			_mouseReleased.fill(false);
			_keysPressed.fill(false);
			_keysReleased.fill(false);

            _cursorCallBack.scrollX = 0;
            _cursorCallBack.scrollY = 0;

            glfwPollEvents();

            if (_cursorCallBack.updateDelta) {
                _cursorCallBack.deltaX = _cursorCallBack.x - _cursorCallBack.lastX;
                _cursorCallBack.deltaY = _cursorCallBack.lastY - _cursorCallBack.y;
            }
            else {
                _cursorCallBack.deltaX = 0;
                _cursorCallBack.deltaY = 0;
            }

            _cursorCallBack.lastX = _cursorCallBack.x;
            _cursorCallBack.lastY = _cursorCallBack.y;

            if (_windowCallBack.focusUpdate) [[unlikely]] {
                _cursorCallBack.lastX = _cursorCallBack.x;
                _cursorCallBack.lastY = _cursorCallBack.y;

				_cursorCallBack.deltaX = 0.0;
				_cursorCallBack.deltaY = 0.0;

                _windowCallBack.focusUpdate = 0;

                _windowCallBack.currentFrame = std::chrono::steady_clock::now();
                _windowCallBack.lastFrame = _windowCallBack.currentFrame;
                _windowCallBack.deltaTime = _windowCallBack.focusedDeltaTimeLimit;

                return;
            }

            _windowCallBack.currentFrame = std::chrono::steady_clock::now();
            _windowCallBack.deltaTime = std::chrono::duration<float>(_windowCallBack.currentFrame - _windowCallBack.lastFrame).count();
            _windowCallBack.currentTime = std::chrono::duration<float>(_windowCallBack.currentFrame - _windowCallBack.startTime).count();
            _windowCallBack.lastFrame = _windowCallBack.currentFrame;

            if (_windowCallBack.isFocused) {
                if (_windowCallBack.focusedDeltaTimeLimit) {
                    auto nextFrame = _windowCallBack.currentFrame + std::chrono::nanoseconds(_windowCallBack.focusedDeltaTimeLimit);
                    auto sleepUntil = nextFrame - _windowCallBack.spinThreshold;

                    if (sleepUntil > _windowCallBack.currentFrame) std::this_thread::sleep_until(sleepUntil);

                    while (std::chrono::steady_clock::now() < nextFrame) std::this_thread::yield();
                }
            }
            else {
                std::this_thread::sleep_until(_windowCallBack.currentFrame + std::chrono::nanoseconds(_windowCallBack.unfocusedDeltaTimeLimit));
                return;
            }
        }

        keyCallBack _keyCallBack;
        cursorCallBack _cursorCallBack;
        windowCallBack _windowCallBack;
		std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> _mousePressed{};
		std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> _mouseReleased{};
		std::array<bool, GLFW_KEY_LAST + 1> _keysDown{};
		std::array<bool, GLFW_KEY_LAST + 1> _keysPressed{};
		std::array<bool, GLFW_KEY_LAST + 1> _keysReleased{};

    public:
        window() = delete;

        window(int width, int height, const char* name, GLFWmonitor* monitor = nullptr, GLFWwindow* share = nullptr, int hint = GLFW_CLIENT_API, int value = GLFW_NO_API) {
            glfwWindowHint(hint, value);

            _windowCallBack.window = glfwCreateWindow(width, height, name, monitor, nullptr);

            if (!_windowCallBack.window) {
                throw std::runtime_error("Failed to create GLFW window");
            }

            glfwFocusWindow(_windowCallBack.window);

            glfwSetWindowUserPointer(_windowCallBack.window, this);

            glfwSetKeyCallback(_windowCallBack.window, window_key_callback);
            glfwSetFramebufferSizeCallback(_windowCallBack.window, framebuffer_size_callback);
            glfwSetWindowFocusCallback(_windowCallBack.window, window_focus_callback);
            glfwSetScrollCallback(_windowCallBack.window, scroll_callback);
            glfwSetMouseButtonCallback(_windowCallBack.window, mouse_button_callback);
            glfwSetCursorPosCallback(_windowCallBack.window, mouse_pos_callback);

            glfwSetInputMode(_windowCallBack.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

            _windowCallBack.hwnd = glfwGetWin32Window(_windowCallBack.window);

            _windowCallBack.width = width;
            _windowCallBack.height = height;
            _windowCallBack.name = name;
            _windowCallBack.aspect = (float)width / height;

            _windowCallBack.currentFrame = std::chrono::steady_clock::now();
            _windowCallBack.lastFrame = _windowCallBack.currentFrame;

            begin();
        }

        ~window() {
            glfwDestroyWindow(_windowCallBack.window);
        }

        bool run() {
            begin();
            return !(getKeyPress(GLFW_KEY_HOME) || glfwWindowShouldClose(_windowCallBack.window));
        }

        void createChildWindow() {
            glfwCreateWindow(400, 300, "test", NULL, _windowCallBack.window);
        }

        inline void setFocusedFpsLimit(UINT limit) { _windowCallBack.focusedDeltaTimeLimit = UINT(1000000000.0 / (double)limit); }

        inline void setFocusedDeltaTimeLimit(UINT limit) { _windowCallBack.focusedDeltaTimeLimit = limit; }

        inline void setUnfocusedFpsLimit(UINT limit) { _windowCallBack.unfocusedDeltaTimeLimit = UINT(1000000000.0 / (double)limit); }

        inline void setUnfocusedDeltaTimeLimit(UINT limit) { _windowCallBack.unfocusedDeltaTimeLimit = limit; }

        inline void setFrameBufferSizeCallBack(GLFWframebuffersizefun func) { glfwSetFramebufferSizeCallback(_windowCallBack.window, func); }

        inline void setKeyCallback(GLFWkeyfun func) { glfwSetKeyCallback(_windowCallBack.window, func); }

        inline void setWindowFocusCallback(GLFWwindowfocusfun func) { glfwSetWindowFocusCallback(_windowCallBack.window, func); }

        inline void setScrollCallback(GLFWscrollfun func) { glfwSetScrollCallback(_windowCallBack.window, func); }

        inline void setMouseButtonCallback(GLFWmousebuttonfun func) { glfwSetMouseButtonCallback(_windowCallBack.window, func); }

        inline void setCursorPosCallback(GLFWcursorposfun func) { glfwSetCursorPosCallback(_windowCallBack.window, func); }

        inline const cursorCallBack& getCursorCallBack() const { return _cursorCallBack; }

        inline const keyCallBack& getKeyCallBack() const { return _keyCallBack; }

        inline const windowCallBack& getWindowCallBack() const { return _windowCallBack; }

        inline const UINT8 getCursorButton() const { return _cursorCallBack.action._data; }

        inline const UINT8 getCursorAction() const { return _cursorCallBack.action._data; }

        inline const int getCursorMode() const { return _cursorCallBack.mods; }

        inline const int getWidth() const { return _windowCallBack.width; }

        inline const int getHeight() const { return _windowCallBack.height; }

        inline GLFWwindow* getGlfwWindow() const { return _windowCallBack.window; }

        inline HWND getHwnd() const { return _windowCallBack.hwnd; }

        inline const float getTargetAspect() const { return _windowCallBack.targetAspect; }

        inline const float getAspect() const { return _windowCallBack.aspect; }

        inline void setTargetAspect(float aspect) { _windowCallBack.targetAspect = aspect; }

        inline const double getCursorX() const { return _cursorCallBack.x; }

        inline const double getCursorY() const { return _cursorCallBack.y; }

        inline void setCursorX(const double& other) { _cursorCallBack.x = other; }

        inline void setCursorY(const double& other) { _cursorCallBack.y = other; }

        inline const double getLastCursorX() const { return _cursorCallBack.lastX; }

        inline const double getLastCursorY() const { return _cursorCallBack.lastY; }

        inline void setLastCursorX(const double& other) { _cursorCallBack.lastX = other; }

        inline void setLastCursorY(const double& other) { _cursorCallBack.lastY = other; }

        inline const double getDeltaCursorX() const { return _cursorCallBack.deltaX; }

        inline const double getDeltaCursorY() const { return _cursorCallBack.deltaY; }

		inline void setUpdateDeltaCursor(bool update) { _cursorCallBack.updateDelta = update; }

        inline void setDeltaCursorX(const double& other) { _cursorCallBack.deltaX = other; }

        inline void setDeltaCursorY(const double& other) { _cursorCallBack.deltaY = other; }

        inline const double getScrollX() const { return _cursorCallBack.scrollX; }

        inline const double getScrollY() const { return _cursorCallBack.scrollY; }

        inline void setScrollX(const double& other) { _cursorCallBack.scrollX = other; }

        inline void setScrollY(const double& other) { _cursorCallBack.scrollY = other; }

        inline bool getMousePress(int button) const {
			return button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST && _mousePressed[button];
		}

        inline bool getMouseHold(int button) const {
			return button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST && _cursorCallBack.action[button];
		}

        inline bool getMouseRelease(int button) const {
			return button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST && _mouseReleased[button];
		}

        inline bool getMouse(int button, int action) const {
			return action == GLFW_PRESS ? getMouseHold(button) : !getMouseHold(button);
		}

        inline const double getDeltaTime() const { return _windowCallBack.deltaTime; }

        inline const double getTime() const { return _windowCallBack.currentTime; }

        inline const double getFps() const { return 1.0 / _windowCallBack.deltaTime; }

        inline int getFocusedFpsLimit() const {
			return _windowCallBack.focusedDeltaTimeLimit == 0
				? 0
				: static_cast<int>(1'000'000'000u / _windowCallBack.focusedDeltaTimeLimit);
		}

        inline bool getKey(int key) const {
			return key >= 0 && key <= GLFW_KEY_LAST && _keysDown[key];
		}

        inline bool getKeyPress(int key) const {
			return key >= 0 && key <= GLFW_KEY_LAST && _keysPressed[key];
        }

        inline bool getKeyHold(int key) const {
			return getKey(key);
        }

        inline bool getKeyRelease(int key) const {
			return key >= 0 && key <= GLFW_KEY_LAST && _keysReleased[key];
        }

        inline const void setKeyCallback(void(*callBack)(GLFWwindow* window, int key, int scancode, int action, int mods)) const {
            glfwSetKeyCallback(_windowCallBack.window, callBack);
        }

        inline const void setFramebufferSizeCallback(void(*callBack)(GLFWwindow* window, int width, int height)) const {
            glfwSetFramebufferSizeCallback(_windowCallBack.window, callBack);
        }

        inline const void setWindowFocusCallback(void(*callBack)(GLFWwindow* window, int focused)) const {
            glfwSetWindowFocusCallback(_windowCallBack.window, callBack);
        }

        inline const void setScrollCallback(void(*callBack)(GLFWwindow* window, double xoffset, double yoffset)) const {
            glfwSetScrollCallback(_windowCallBack.window, callBack);
        }

        inline const void setMouseButtonCallback(void(*callBack)(GLFWwindow* window, int button, int action, int mods)) const {
            glfwSetMouseButtonCallback(_windowCallBack.window, callBack);
        }

        inline const void setCursorPosCallback(void(*callBack)(GLFWwindow* window, double xpos, double ypos)) const {
            glfwSetCursorPosCallback(_windowCallBack.window, callBack);
        }

        inline const void setInputMode(int mode, int value) const {
            glfwSetInputMode(_windowCallBack.window, mode, value);
        }
    };

} // namespace ac
