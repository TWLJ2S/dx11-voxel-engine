#pragma once

#include <SDL3/SDL.h>
#include <core/inputKeys.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <array>
#include <chrono>
#include <functional>
#include <stdexcept>
#include <string>
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

            inline bool getButton(int button) const { return (_data & (1u << button)) != 0; }

            inline bool operator[](int button) const { return getButton(button); }

            inline void toggle(int button) { _data ^= (T)(1u << button); }
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
        SDL_Window* window = nullptr;
        HWND hwnd = nullptr;

        int width = 0;
        int height = 0;

        bool frameUpdate = 0;
        bool isFocused = 1;
        bool focusUpdate = 0;
        bool shouldClose = 0;

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
        static int mapMouseButton(int sdlButton) {
            switch (sdlButton) {
            case SDL_BUTTON_LEFT: return GLFW_MOUSE_BUTTON_LEFT;
            case SDL_BUTTON_RIGHT: return GLFW_MOUSE_BUTTON_RIGHT;
            case SDL_BUTTON_MIDDLE: return GLFW_MOUSE_BUTTON_MIDDLE;
            case SDL_BUTTON_X1: return 3;
            case SDL_BUTTON_X2: return 4;
            default: return -1;
            }
        }

        void refreshWindowMetrics() {
            int logicalW = 0, logicalH = 0, pixelW = 0, pixelH = 0;
            if (_windowCallBack.window) {
                SDL_GetWindowSize(_windowCallBack.window, &logicalW, &logicalH);
                SDL_GetWindowSizeInPixels(_windowCallBack.window, &pixelW, &pixelH);
            }
            _logicalWidth = logicalW > 0 ? logicalW : (std::max)(_logicalWidth, 1);
            _logicalHeight = logicalH > 0 ? logicalH : (std::max)(_logicalHeight, 1);
            const int width = pixelW > 0 ? pixelW : _logicalWidth;
            const int height = pixelH > 0 ? pixelH : _logicalHeight;
            if (width != _windowCallBack.width || height != _windowCallBack.height)
                _windowCallBack.frameUpdate = 1;
            _windowCallBack.width = width;
            _windowCallBack.height = height;
            _windowCallBack.aspect =
                static_cast<float>(_windowCallBack.width) /
                static_cast<float>(_windowCallBack.height);
        }

        float toPixelX(float x) const {
            return x * static_cast<float>(_windowCallBack.width) /
                static_cast<float>((std::max)(_logicalWidth, 1));
        }

        float toPixelY(float y) const {
            return y * static_cast<float>(_windowCallBack.height) /
                static_cast<float>((std::max)(_logicalHeight, 1));
        }

        void handleEvent(const SDL_Event& event) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                _windowCallBack.shouldClose = true;
                break;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                refreshWindowMetrics();
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                _windowCallBack.isFocused = true;
                _windowCallBack.focusUpdate = 1;
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                _windowCallBack.isFocused = false;
                _windowCallBack.focusUpdate = 1;
                _keysDown.fill(false);
                _cursorCallBack.action._data = 0;
                break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP: {
                const int key = static_cast<int>(event.key.scancode);
                const bool down = event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat;
                const bool up = event.type == SDL_EVENT_KEY_UP;
                _keyCallBack.key = key;
                _keyCallBack.scanCode = key;
                _keyCallBack.action = down ? GLFW_PRESS : GLFW_RELEASE;
                _keyCallBack.mods = 0;
                _keyCallBack.update = 1;
                if (key >= 0 && key <= GLFW_KEY_LAST) {
                    if (down) {
                        _keysDown[key] = true;
                        _keysPressed[key] = true;
                    }
                    else if (up) {
                        _keysDown[key] = false;
                        _keysReleased[key] = true;
                    }
                }
                break;
            }
            case SDL_EVENT_MOUSE_MOTION:
                _cursorCallBack.x = toPixelX(event.motion.x);
                _cursorCallBack.y = toPixelY(event.motion.y);
                if (_relativeMouse) {
                    _cursorCallBack.deltaX += event.motion.xrel;
                    _cursorCallBack.deltaY += -event.motion.yrel;
                }
                _cursorCallBack.posUpdate = 1;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                const int button = mapMouseButton(event.button.button);
                if (button < 0) break;
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    _cursorCallBack.action.press(button);
                    if (button <= GLFW_MOUSE_BUTTON_LAST)
                        _mousePressed[button] = true;
                }
                else {
                    _cursorCallBack.action.release(button);
                    if (button <= GLFW_MOUSE_BUTTON_LAST)
                        _mouseReleased[button] = true;
                }
                _cursorCallBack.buttonUpdate = 1;
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL:
                _cursorCallBack.scrollX += event.wheel.x;
                _cursorCallBack.scrollY += event.wheel.y;
                _cursorCallBack.scrollUpdate = 1;
                break;
            case SDL_EVENT_TEXT_INPUT:
                if (_textInputEnabled && event.text.text)
                    _textInput.append(event.text.text);
                break;
            default:
                break;
            }
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
            if (_relativeMouse) {
                _cursorCallBack.deltaX = 0;
                _cursorCallBack.deltaY = 0;
            }

            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (_eventHook)
                    _eventHook(event);
                handleEvent(event);
            }

            if (!_relativeMouse) {
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
            }

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
            _windowCallBack.deltaTime = std::chrono::duration<float>(
                _windowCallBack.currentFrame - _windowCallBack.lastFrame).count();
            _windowCallBack.currentTime = std::chrono::duration<float>(
                _windowCallBack.currentFrame - _windowCallBack.startTime).count();
            _windowCallBack.lastFrame = _windowCallBack.currentFrame;

            if (_windowCallBack.isFocused) {
                if (_windowCallBack.focusedDeltaTimeLimit) {
                    auto nextFrame = _windowCallBack.currentFrame +
                        std::chrono::nanoseconds(_windowCallBack.focusedDeltaTimeLimit);
                    auto sleepUntil = nextFrame - _windowCallBack.spinThreshold;
                    if (sleepUntil > _windowCallBack.currentFrame)
                        std::this_thread::sleep_until(sleepUntil);
                    while (std::chrono::steady_clock::now() < nextFrame)
                        std::this_thread::yield();
                }
            }
            else {
                std::this_thread::sleep_until(
                    _windowCallBack.currentFrame +
                    std::chrono::nanoseconds(_windowCallBack.unfocusedDeltaTimeLimit));
            }
        }

        keyCallBack _keyCallBack;
        cursorCallBack _cursorCallBack;
        windowCallBack _windowCallBack;
        bool _relativeMouse = false;
        int _logicalWidth = 1;
        int _logicalHeight = 1;
        bool _textInputEnabled = false;
        std::string _textInput;
        std::function<void(const SDL_Event&)> _eventHook;
        std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> _mousePressed{};
        std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> _mouseReleased{};
        std::array<bool, SDL_SCANCODE_COUNT> _keysDown{};
        std::array<bool, SDL_SCANCODE_COUNT> _keysPressed{};
        std::array<bool, SDL_SCANCODE_COUNT> _keysReleased{};

    public:
        window() = delete;

        window(int width, int height, const char* name,
            void* /*monitor*/ = nullptr, void* /*share*/ = nullptr,
            int /*hint*/ = 0, int /*value*/ = 0) {
            if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS))
                throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());

            _windowCallBack.window = SDL_CreateWindow(
                name, width, height, SDL_WINDOW_RESIZABLE);
            if (!_windowCallBack.window) {
                SDL_Quit();
                throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
            }

            SDL_PropertiesID props = SDL_GetWindowProperties(_windowCallBack.window);
            _windowCallBack.hwnd = static_cast<HWND>(SDL_GetPointerProperty(
                props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
            if (!_windowCallBack.hwnd) {
                SDL_DestroyWindow(_windowCallBack.window);
                SDL_Quit();
                throw std::runtime_error("Failed to get Win32 HWND from SDL window");
            }

            SDL_RaiseWindow(_windowCallBack.window);

            _logicalWidth = width > 0 ? width : 1;
            _logicalHeight = height > 0 ? height : 1;
            _windowCallBack.name = name;
            _windowCallBack.currentFrame = std::chrono::steady_clock::now();
            _windowCallBack.lastFrame = _windowCallBack.currentFrame;
            refreshWindowMetrics();
            begin();
        }

        ~window() {
            if (_windowCallBack.window)
                SDL_DestroyWindow(_windowCallBack.window);
            SDL_Quit();
        }

        bool run() {
            begin();
            return !(getKeyPress(GLFW_KEY_HOME) || _windowCallBack.shouldClose);
        }

        inline void setFocusedFpsLimit(UINT limit) {
            _windowCallBack.focusedDeltaTimeLimit =
                UINT(1000000000.0 / (double)limit);
        }

        inline void setFocusedDeltaTimeLimit(UINT limit) {
            _windowCallBack.focusedDeltaTimeLimit = limit;
        }

        inline void setUnfocusedFpsLimit(UINT limit) {
            _windowCallBack.unfocusedDeltaTimeLimit =
                UINT(1000000000.0 / (double)limit);
        }

        inline void setUnfocusedDeltaTimeLimit(UINT limit) {
            _windowCallBack.unfocusedDeltaTimeLimit = limit;
        }

        inline const cursorCallBack& getCursorCallBack() const { return _cursorCallBack; }
        inline const keyCallBack& getKeyCallBack() const { return _keyCallBack; }
        inline const windowCallBack& getWindowCallBack() const { return _windowCallBack; }
        inline const UINT8 getCursorButton() const { return _cursorCallBack.action._data; }
        inline const UINT8 getCursorAction() const { return _cursorCallBack.action._data; }
        inline const int getCursorMode() const { return _cursorCallBack.mods; }
        inline const int getWidth() const { return _windowCallBack.width; }
        inline const int getHeight() const { return _windowCallBack.height; }
        inline int getLogicalWidth() const { return _logicalWidth; }
        inline int getLogicalHeight() const { return _logicalHeight; }
        inline float mouseToPixelX(float x) const { return toPixelX(x); }
        inline float mouseToPixelY(float y) const { return toPixelY(y); }
        inline SDL_Window* getSdlWindow() const { return _windowCallBack.window; }
        inline HWND getHwnd() const { return _windowCallBack.hwnd; }
        inline void setEventHook(std::function<void(const SDL_Event&)> hook) {
            _eventHook = std::move(hook);
        }
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
            return button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST &&
                _cursorCallBack.action[button];
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
        inline bool getKeyHold(int key) const { return getKey(key); }
        inline bool getKeyRelease(int key) const {
            return key >= 0 && key <= GLFW_KEY_LAST && _keysReleased[key];
        }

        inline void setInputMode(int mode, int value) {
            if (mode != GLFW_CURSOR) return;
            _relativeMouse = (value == GLFW_CURSOR_DISABLED);
            SDL_SetWindowRelativeMouseMode(_windowCallBack.window, _relativeMouse);
            if (_relativeMouse) {
                _cursorCallBack.deltaX = 0;
                _cursorCallBack.deltaY = 0;
            }
            else {
                float x = 0.0f, y = 0.0f;
                SDL_GetMouseState(&x, &y);
                _cursorCallBack.x = toPixelX(x);
                _cursorCallBack.y = toPixelY(y);
                _cursorCallBack.lastX = _cursorCallBack.x;
                _cursorCallBack.lastY = _cursorCallBack.y;
            }
        }

        void requestClose() { _windowCallBack.shouldClose = true; }

        void setTextInputEnabled(bool enabled) {
            if (_textInputEnabled == enabled)
                return;
            _textInputEnabled = enabled;
            if (!_windowCallBack.window)
                return;
            if (enabled) {
                _textInput.clear();
                SDL_StartTextInput(_windowCallBack.window);
            }
            else {
                SDL_StopTextInput(_windowCallBack.window);
                _textInput.clear();
            }
        }

        bool textInputEnabled() const { return _textInputEnabled; }

        std::string consumeTextInput() {
            std::string text = std::move(_textInput);
            _textInput.clear();
            return text;
        }
    };

} // namespace ac
