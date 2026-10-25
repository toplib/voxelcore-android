/// \file window/detail/AndroidWindow.cpp
/// \brief Android (Native Activity + EGL + GLES3) window & input backend.
///
/// Replaces GLFWWindow.cpp on Android (`__ANDROID__` builds). The engine code
/// uses GLFW key-codes as canonical Keycode values (see window/input.hpp), so
/// Android key events are translated here via `androidKeyToGlfw`.
///
/// Touch controls mapping:
/// - primary touch drag -> cursor movement (camera look) through
///   Input::simulateCursorPos;
/// - secondary finger drag -> virtual "WASD" movement keys (up = forward,
///   left = strafe left, down = backward, right = strafe right);
/// - short tap without movement -> BUTTON_1 click (UI interaction);
/// - two-finger pinch -> scroll (getScroll());
/// - AKEYCODE_BACK -> Escape binding.

#if defined(__ANDROID__)

#include "window/Window.hpp"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/log.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stack>
#include <thread>
#include <unordered_map>
#include <vector>

#include "debug/Logger.hpp"
#include "graphics/core/ImageData.hpp"
#include "graphics/core/Texture.hpp"
#include "settings.hpp"
#include "util/platform.hpp"
#include "window/glfw_compat.hpp"
#include "window/input.hpp"

static debug::Logger logger("android-window");

namespace {

struct TouchPoint {
    int32_t id = -1;
    float x = 0.0f;
    float y = 0.0f;
    float startX = 0.0f;
    float startY = 0.0f;
    bool isPrimary = false;
};

}  // namespace

/// Translate Android key-code to the GLFW key-code used by engine Keycodes.
/// Returns GLFW_KEY_UNKNOWN when there is no equivalent.
static int androidKeyToGlfw(int32_t keyCode) {
    switch (keyCode) {
        case AKEYCODE_SPACE: return GLFW_KEY_SPACE;
        case AKEYCODE_APOSTROPHE: return GLFW_KEY_APOSTROPHE;
        case AKEYCODE_COMMA: return GLFW_KEY_COMMA;
        case AKEYCODE_MINUS: return GLFW_KEY_MINUS;
        case AKEYCODE_PERIOD: return GLFW_KEY_PERIOD;
        case AKEYCODE_SLASH: return GLFW_KEY_SLASH;
        case AKEYCODE_SEMICOLON: return GLFW_KEY_SEMICOLON;
        case AKEYCODE_EQUALS: return GLFW_KEY_EQUAL;
        case AKEYCODE_LEFT_BRACKET: return GLFW_KEY_LEFT_BRACKET;
        case AKEYCODE_RIGHT_BRACKET: return GLFW_KEY_RIGHT_BRACKET;
        case AKEYCODE_GRAVE: return GLFW_KEY_GRAVE_ACCENT;
        case AKEYCODE_BACKSLASH: return GLFW_KEY_BACKSLASH;
        case AKEYCODE_0: return GLFW_KEY_NUM_0;
        case AKEYCODE_1: return GLFW_KEY_NUM_1;
        case AKEYCODE_2: return GLFW_KEY_NUM_2;
        case AKEYCODE_3: return GLFW_KEY_NUM_3;
        case AKEYCODE_4: return GLFW_KEY_NUM_4;
        case AKEYCODE_5: return GLFW_KEY_NUM_5;
        case AKEYCODE_6: return GLFW_KEY_NUM_6;
        case AKEYCODE_7: return GLFW_KEY_NUM_7;
        case AKEYCODE_8: return GLFW_KEY_NUM_8;
        case AKEYCODE_9: return GLFW_KEY_NUM_9;
        case AKEYCODE_A: return GLFW_KEY_A;
        case AKEYCODE_B: return GLFW_KEY_B;
        case AKEYCODE_C: return GLFW_KEY_C;
        case AKEYCODE_D: return GLFW_KEY_D;
        case AKEYCODE_E: return GLFW_KEY_E;
        case AKEYCODE_F: return GLFW_KEY_F;
        case AKEYCODE_G: return GLFW_KEY_G;
        case AKEYCODE_H: return GLFW_KEY_H;
        case AKEYCODE_I: return GLFW_KEY_I;
        case AKEYCODE_J: return GLFW_KEY_J;
        case AKEYCODE_K: return GLFW_KEY_K;
        case AKEYCODE_L: return GLFW_KEY_L;
        case AKEYCODE_M: return GLFW_KEY_M;
        case AKEYCODE_N: return GLFW_KEY_N;
        case AKEYCODE_O: return GLFW_KEY_O;
        case AKEYCODE_P: return GLFW_KEY_P;
        case AKEYCODE_Q: return GLFW_KEY_Q;
        case AKEYCODE_R: return GLFW_KEY_R;
        case AKEYCODE_S: return GLFW_KEY_S;
        case AKEYCODE_T: return GLFW_KEY_T;
        case AKEYCODE_U: return GLFW_KEY_U;
        case AKEYCODE_V: return GLFW_KEY_V;
        case AKEYCODE_W: return GLFW_KEY_W;
        case AKEYCODE_X: return GLFW_KEY_X;
        case AKEYCODE_Y: return GLFW_KEY_Y;
        case AKEYCODE_Z: return GLFW_KEY_Z;
        case AKEYCODE_ENTER:
        case AKEYCODE_NUMPAD_ENTER: return GLFW_KEY_ENTER;
        case AKEYCODE_TAB: return GLFW_KEY_TAB;
        case AKEYCODE_BACKSPACE: return GLFW_KEY_BACKSPACE;
        case AKEYCODE_ESCAPE: return GLFW_KEY_ESCAPE;
        case AKEYCODE_DEL: return GLFW_KEY_DELETE;
        case AKEYCODE_FORWARD_DEL: return GLFW_KEY_DELETE;
        case AKEYCODE_MOVE_HOME: return GLFW_KEY_HOME;
        case AKEYCODE_MOVE_END: return GLFW_KEY_END;
        case AKEYCODE_PAGE_UP: return GLFW_KEY_PAGE_UP;
        case AKEYCODE_PAGE_DOWN: return GLFW_KEY_PAGE_DOWN;
        case AKEYCODE_INSERT: return GLFW_KEY_INSERT;
        case AKEYCODE_DPAD_LEFT: return GLFW_KEY_LEFT;
        case AKEYCODE_DPAD_RIGHT: return GLFW_KEY_RIGHT;
        case AKEYCODE_DPAD_UP: return GLFW_KEY_UP;
        case AKEYCODE_DPAD_DOWN: return GLFW_KEY_DOWN;
        case AKEYCODE_CAPS_LOCK: return GLFW_KEY_CAPS_LOCK;
        case AKEYCODE_NUM_LOCK: return GLFW_KEY_NUM_LOCK;
        case AKEYCODE_SHIFT_LEFT: return GLFW_KEY_LEFT_SHIFT;
        case AKEYCODE_SHIFT_RIGHT: return GLFW_KEY_RIGHT_SHIFT;
        case AKEYCODE_CTRL_LEFT: return GLFW_KEY_LEFT_CONTROL;
        case AKEYCODE_CTRL_RIGHT: return GLFW_KEY_RIGHT_CONTROL;
        case AKEYCODE_ALT_LEFT: return GLFW_KEY_LEFT_ALT;
        case AKEYCODE_ALT_RIGHT: return GLFW_KEY_RIGHT_ALT;
        case AKEYCODE_META_LEFT: return GLFW_KEY_LEFT_SUPER;
        case AKEYCODE_META_RIGHT: return GLFW_KEY_RIGHT_SUPER;
        case AKEYCODE_MENU: return GLFW_KEY_MENU;
        case AKEYCODE_F1: return GLFW_KEY_F1;
        case AKEYCODE_F2: return GLFW_KEY_F2;
        case AKEYCODE_F3: return GLFW_KEY_F3;
        case AKEYCODE_F4: return GLFW_KEY_F4;
        case AKEYCODE_F5: return GLFW_KEY_F5;
        case AKEYCODE_F6: return GLFW_KEY_F6;
        case AKEYCODE_F7: return GLFW_KEY_F7;
        case AKEYCODE_F8: return GLFW_KEY_F8;
        case AKEYCODE_F9: return GLFW_KEY_F9;
        case AKEYCODE_F10: return GLFW_KEY_F10;
        case AKEYCODE_F11: return GLFW_KEY_F11;
        case AKEYCODE_F12: return GLFW_KEY_F12;
        case AKEYCODE_NUMPAD_0: return GLFW_KEY_KP_0;
        case AKEYCODE_NUMPAD_1: return GLFW_KEY_KP_1;
        case AKEYCODE_NUMPAD_2: return GLFW_KEY_KP_2;
        case AKEYCODE_NUMPAD_3: return GLFW_KEY_KP_3;
        case AKEYCODE_NUMPAD_4: return GLFW_KEY_KP_4;
        case AKEYCODE_NUMPAD_5: return GLFW_KEY_KP_5;
        case AKEYCODE_NUMPAD_6: return GLFW_KEY_KP_6;
        case AKEYCODE_NUMPAD_7: return GLFW_KEY_KP_7;
        case AKEYCODE_NUMPAD_8: return GLFW_KEY_KP_8;
        case AKEYCODE_NUMPAD_9: return GLFW_KEY_KP_9;
        case AKEYCODE_NUMPAD_DIVIDE: return GLFW_KEY_KP_DIVIDE;
        case AKEYCODE_NUMPAD_MULTIPLY: return GLFW_KEY_KP_MULTIPLY;
        case AKEYCODE_NUMPAD_SUBTRACT: return GLFW_KEY_KP_SUBTRACT;
        case AKEYCODE_NUMPAD_ADD: return GLFW_KEY_KP_ADD;
        case AKEYCODE_NUMPAD_DOT: return GLFW_KEY_KP_DECIMAL;
        case AKEYCODE_NUMPAD_COMMA: return GLFW_KEY_KP_SEPARATOR;
        case AKEYCODE_VOLUME_UP: return GLFW_KEY_F13;
        case AKEYCODE_VOLUME_DOWN: return GLFW_KEY_F14;
        case AKEYCODE_VOLUME_MUTE: return GLFW_KEY_F15;
        default: return GLFW_KEY_UNKNOWN;
    }
}

/// Android-side implementation of the abstract Input interface. All event
/// state is pushed from the Native Activity callbacks (main thread).
class AndroidInput : public Input {
public:
    static inline constexpr int KEYS_BUFFER_SIZE = 1036;
    static inline constexpr int MOUSE_KEYS_OFFSET = 1024;

    void onKeyEvent(int32_t keyCode, bool pressed) {
        int glfwKey = androidKeyToGlfw(keyCode);
        if (glfwKey == GLFW_KEY_UNKNOWN) {
            return;
        }
        setKeyState(glfwKey, pressed);
    }

    void setKeyState(int key, bool pressed) {
        if (key < 0 || key >= KEYS_BUFFER_SIZE) {
            return;
        }
        bool prevPressed = keys[key];
        keys[key] = pressed;
        frames[key] = currentFrame;
        if (pressed && !prevPressed) {
            const auto& callbacks = keyCallbacks.find(static_cast<Keycode>(key));
            if (callbacks != keyCallbacks.end()) {
                callbacks->second.notify();
            }
        }
        if (pressed && key < MOUSE_KEYS_OFFSET) {
            pressedKeys.push_back(static_cast<Keycode>(key));
        }
    }

    void onMouseCallback(int button, bool pressed) {
        setKeyState(button + MOUSE_KEYS_OFFSET, pressed);
    }

    void onCursorPosition(double xpos, double ypos) {
        if (cursorDrag) {
            delta.x += static_cast<float>(xpos - cursor.x);
            delta.y += static_cast<float>(ypos - cursor.y);
        } else {
            cursorDrag = true;
        }
        cursor.x = xpos;
        cursor.y = ypos;
    }

    void onCodepoint(uint32_t codepoint) {
        codepoints.push_back(codepoint);
    }

    void addScroll(double offset) {
        scroll += static_cast<int>(offset);
    }

    void pollEvents(bool waitForRefresh) override {
        delta.x = 0.0f;
        delta.y = 0.0f;
        scroll = 0;
        currentFrame++;
        codepoints.clear();
        pressedKeys.clear();

        for (auto& [_, binding] : bindings.getAll()) {
            if (!binding.enabled) {
                binding.state = false;
                continue;
            }
            binding.justChanged = false;

            bool newstate = false;
            switch (binding.type) {
                case InputType::KEYBOARD:
                    newstate = pressed(static_cast<Keycode>(binding.code));
                    break;
                case InputType::MOUSE:
                    newstate = clicked(static_cast<Mousecode>(binding.code));
                    break;
            }

            if (newstate) {
                if (!binding.state) {
                    binding.state = true;
                    binding.justChanged = true;
                    binding.onactived.notify();
                }
            } else {
                if (binding.state) {
                    binding.state = false;
                    binding.justChanged = true;
                }
            }
        }
    }

    const char* getClipboardText() const override {
        // clipboard access requires JNI through the activity; not needed for
        // touch-only gameplay
        return "";
    }

    void setClipboardText(const char*) override {
    }

    int getScroll() override {
        return scroll;
    }

    bool pressed(Keycode key) const override {
        int keycode = static_cast<int>(key);
        if (keycode < 0 || keycode >= KEYS_BUFFER_SIZE) {
            return false;
        }
        return keys[keycode];
    }

    bool jpressed(Keycode keycode) const override {
        return pressed(keycode) &&
               frames[static_cast<int>(keycode)] == currentFrame;
    }

    bool clicked(Mousecode code) const override {
        return pressed(
            static_cast<Keycode>(MOUSE_KEYS_OFFSET + static_cast<int>(code))
        );
    }

    bool jclicked(Mousecode code) const override {
        return clicked(code) &&
               frames[static_cast<int>(code) + MOUSE_KEYS_OFFSET] ==
                   currentFrame;
    }

    void simulateKey(Keycode key, bool pressed) override {
        setKeyState(static_cast<int>(key), pressed);
    }

    void simulateClick(int button, bool pressed) override {
        onMouseCallback(static_cast<int>(button), pressed);
    }

    void simulateCursorPos(double xpos, double ypos) override {
        onCursorPosition(xpos, ypos);
    }

    void simulateCodepoint(uint codepoint) override {
        onCodepoint(codepoint);
    }

    CursorState getCursor() const override {
        return {isCursorLocked(), cursor, delta};
    }

    bool isCursorLocked() const override {
        // there is no hardware cursor on touch devices; "locked" means that
        // absolute pointer positions are delivered instead of deltas
        return cursorLocked;
    }

    void toggleCursor() override {
        cursorDrag = false;
        cursorLocked = !cursorLocked;
    }

    Bindings& getBindings() override {
        return bindings;
    }

    const Bindings& getBindings() const override {
        return bindings;
    }

    ObserverHandler addKeyCallback(Keycode key, InputCallback callback) override {
        return keyCallbacks[key].add(std::move(callback));
    }

    ObserverHandler addMouseCallback(Mousecode button, InputCallback callback) override {
        return addKeyCallback(
            static_cast<Keycode>(MOUSE_KEYS_OFFSET + static_cast<int>(button)),
            std::move(callback)
        );
    }

    const std::vector<Keycode>& getPressedKeys() const override {
        return pressedKeys;
    }

    const std::vector<uint>& getCodepoints() const override {
        return codepoints;
    }

private:
    uint currentFrame = 0;
    uint frames[KEYS_BUFFER_SIZE] {};
    bool keys[KEYS_BUFFER_SIZE] {};
    int scroll = 0;
    bool cursorLocked = true;
    bool cursorDrag = false;
    glm::vec2 delta {};
    glm::vec2 cursor {};
    std::vector<Keycode> pressedKeys;
    std::vector<uint> codepoints;
    Bindings bindings;
    std::unordered_map<Keycode, util::HandlersList<>> keyCallbacks;
};
static_assert(!std::is_abstract<AndroidInput>());

static AndroidInput* g_input = nullptr;
static class AndroidWindow* g_window = nullptr;

class AndroidWindow : public Window {
public:
    AndroidInput& input;

    AndroidWindow(AndroidInput& input, DisplaySettings* settings)
        : Window({settings->width.get(), settings->height.get()}),
          input(input),
          settings(settings) {
    }

    ~AndroidWindow() override {
        destroyEgl();
    }

    bool initEgl(ANativeWindow* nativeWindow) {
        eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (eglDisplay == EGL_NO_DISPLAY) {
            logger.error() << "eglGetDisplay failed";
            return false;
        }
        EGLint major = 0, minor = 0;
        if (eglInitialize(eglDisplay, &major, &minor) == EGL_FALSE) {
            logger.error() << "eglInitialize failed";
            return false;
        }
        const EGLint attribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 24,
            EGL_STENCIL_SIZE, 8,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
            EGL_NONE
        };
        EGLConfig config;
        EGLint numConfigs = 0;
        if (eglChooseConfig(eglDisplay, attribs, &config, 1, &numConfigs) ==
                EGL_FALSE ||
            numConfigs < 1) {
            logger.error() << "eglChooseConfig failed";
            return false;
        }
        eglSurface = eglCreateWindowSurface(
            eglDisplay, config, nativeWindow, nullptr
        );
        if (eglSurface == EGL_NO_SURFACE) {
            logger.error() << "eglCreateWindowSurface failed";
            return false;
        }
        const EGLint contextAttribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE
        };
        eglContext = eglCreateContext(eglDisplay, config, EGL_NO_CONTEXT,
                                      contextAttribs);
        if (eglContext == EGL_NO_CONTEXT) {
            logger.error() << "eglCreateContext failed";
            return false;
        }
        makeContextCurrent();
        glViewport(0, 0, size.x, size.y);
        glClearColor(0.0f, 0.0f, 0.0f, 1);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        GLint maxTextureSize[1] {static_cast<GLint>(Texture::MAX_RESOLUTION)};
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, maxTextureSize);
        if (maxTextureSize[0] > 0) {
            Texture::MAX_RESOLUTION = maxTextureSize[0];
            logger.info() << "max texture size is " << Texture::MAX_RESOLUTION;
        }
        logger.info() << "GL Vendor: "
                      << reinterpret_cast<const char*>(glGetString(GL_VENDOR));
        logger.info() << "GL Renderer: "
                      << reinterpret_cast<const char*>(
                             glGetString(GL_RENDERER));
        return true;
    }

    void destroyEgl() {
        if (eglDisplay != EGL_NO_DISPLAY) {
            eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
                           EGL_NO_CONTEXT);
            if (eglContext != EGL_NO_CONTEXT) {
                eglDestroyContext(eglDisplay, eglContext);
            }
            if (eglSurface != EGL_NO_SURFACE) {
                eglDestroySurface(eglDisplay, eglSurface);
            }
            eglTerminate(eglDisplay);
        }
        eglDisplay = EGL_NO_DISPLAY;
        eglContext = EGL_NO_CONTEXT;
        eglSurface = EGL_NO_SURFACE;
    }

    /// Surface was recreated (e.g. app resumed): rebuild EGL surface.
    void resetSurface(ANativeWindow* nativeWindow) {
        if (eglSurface != EGL_NO_SURFACE) {
            eglDestroySurface(eglDisplay, eglSurface);
            eglSurface = EGL_NO_SURFACE;
        }
        EGLConfig config;
        EGLint numConfigs = 0;
        eglGetConfigs(eglDisplay, &config, 1, &numConfigs);
        eglSurface = eglCreateWindowSurface(
            eglDisplay, config, nativeWindow, nullptr
        );
        makeContextCurrent();
        shouldRefresh = true;
    }

    bool makeContextCurrent() {
        if (eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext) ==
            EGL_FALSE) {
            logger.error() << "eglMakeCurrent failed: " << eglGetError();
            return false;
        }
        return true;
    }

    void setSize(int width, int height) {
        if (width <= 0 || height <= 0) {
            return;
        }
        glViewport(0, 0, width, height);
        size = {width, height};
        shouldRefresh = true;
    }

    void swapBuffers() override {
        makeContextCurrent();
        resetScissor();
        eglSwapBuffers(eglDisplay, eglSurface);
        if (framerate > 0) {
            auto elapsedTime = time() - prevSwap;
            auto frameTime = 1.0 / framerate;
            if (elapsedTime < frameTime) {
                platform::sleep(
                    static_cast<size_t>((frameTime - elapsedTime) * 1000)
                );
            }
        }
        prevSwap = time();
    }

    bool isMaximized() const override {
        return true;
    }

    bool isFocused() const override {
        return focused;
    }

    void setFocused(bool flag) {
        focused = flag;
        if (flag) {
            shouldRefresh = true;
        }
    }

    bool isIconified() const override {
        return !focused;
    }

    bool isShouldClose() const override {
        return shouldClose;
    }

    void setShouldClose(bool flag) override {
        shouldClose = flag;
    }

    void setCursor(CursorShape) override {
        // no mouse cursor on touch devices
    }

    void setMode(WindowMode mode) override {
        // device display is always "fullscreen"
        Window::mode = mode;
    }

    WindowMode getMode() const override {
        return WindowMode::FULLSCREEN;
    }

    void focus() override {
    }

    void setTitle(const std::string&) override {
    }

    void setIcon(const ImageData*) override {
    }

    void pushScissor(glm::vec4 area) override {
        if (scissorStack.empty()) {
            glEnable(GL_SCISSOR_TEST);
        }
        scissorStack.push(scissorArea);

        area.z += glm::ceil(area.x);
        area.w += glm::ceil(area.y);

        area.x = glm::max(area.x, scissorArea.x);
        area.y = glm::max(area.y, scissorArea.y);

        area.z = glm::min(area.z, scissorArea.z);
        area.w = glm::min(area.w, scissorArea.w);

        if (area.z < 0.0f || area.w < 0.0f) {
            glScissor(0, 0, 0, 0);
        } else {
            glScissor(
                area.x,
                size.y - area.w,
                std::max(0, static_cast<int>(glm::ceil(area.z - area.x))),
                std::max(0, static_cast<int>(glm::ceil(area.w - area.y)))
            );
        }
        scissorArea = area;
    }

    void resetScissor() override {
        scissorArea = glm::vec4(0.0f, 0.0f, size.x, size.y);
        scissorStack = std::stack<glm::vec4>();
        glDisable(GL_SCISSOR_TEST);
    }

    void popScissor() override {
        if (scissorStack.empty()) {
            logger.warning() << "extra Window::popScissor call";
            return;
        }
        glm::vec4 area = scissorStack.top();
        scissorStack.pop();
        if (area.z < 0.0f || area.w < 0.0f) {
            glScissor(0, 0, 0, 0);
        } else {
            glScissor(
                area.x,
                size.y - area.w,
                std::max(0, static_cast<int>(area.z - area.x)),
                std::max(0, static_cast<int>(area.w - area.y))
            );
        }
        if (scissorStack.empty()) {
            glDisable(GL_SCISSOR_TEST);
        }
        scissorArea = area;
    }

    void setShouldRefresh() override {
        shouldRefresh = true;
    }

    bool checkShouldRefresh() override {
        if (shouldRefresh) {
            shouldRefresh = false;
            return true;
        }
        return false;
    }

    double time() override {
        using clock = std::chrono::steady_clock;
        static const auto start = clock::now();
        auto now = clock::now();
        return std::chrono::duration<double>(now - start).count();
    }

    void setFramerate(int framerate) override {
        this->framerate = framerate;
        // vsync is controlled by the compositor on Android
        eglSwapInterval(eglDisplay, framerate == -1 ? 1 : 0);
    }

    std::unique_ptr<ImageData> takeScreenshot() override {
        makeContextCurrent();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        auto data = std::make_unique<ubyte[]>(size.x * size.y * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, size.x, size.y, GL_RGB, GL_UNSIGNED_BYTE,
                     data.get());
        return std::make_unique<ImageData>(
            ImageFormat::RGB888, size.x, size.y, data.release()
        );
    }

    // ---- touch controls ----

    /// Handle a MotionEvent with action masked out (AMOTION_EVENT_ACTION_*).
    /// Called from the Native Activity input callback.
    void onTouchEvent(AInputEvent* event) {
        int32_t action = AMotionEvent_getAction(event) &
                         AMOTION_EVENT_ACTION_MASK;
        size_t pointerCount = AMotionEvent_getPointerCount(event);
        float sensitivity = settings->touchSensitivity.get();

        auto fillPoints = [&]() {
            touches.clear();
            for (size_t i = 0; i < pointerCount; ++i) {
                TouchPoint tp;
                tp.id = AMotionEvent_getPointerId(event, i);
                tp.x = AMotionEvent_getX(event, i);
                tp.y = AMotionEvent_getY(event, i);
                touches.push_back(tp);
            }
        };

        switch (action) {
            case AMOTION_EVENT_ACTION_DOWN: {
                touches.clear();
                TouchPoint tp;
                tp.id = AMotionEvent_getPointerId(event, 0);
                tp.x = AMotionEvent_getX(event, 0);
                tp.y = AMotionEvent_getY(event, 0);
                tp.startX = tp.x;
                tp.startY = tp.y;
                tp.isPrimary = true;
                touches.push_back(tp);
                // tap starts a left click: UI elements react to BUTTON_1
                input.simulateClick(
                    static_cast<int>(Mousecode::BUTTON_1), true
                );
                input.simulateCursorPos(tp.x, tp.y);
                setShouldRefresh();
                break;
            }
            case AMOTION_EVENT_ACTION_POINTER_DOWN: {
                size_t index = (AMotionEvent_getAction(event) &
                                AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                               AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
                fillPoints();
                if (index < touches.size()) {
                    auto& tp = touches[index];
                    tp.startX = tp.x;
                    tp.startY = tp.y;
                    tp.isPrimary = false;
                }
                // second finger: release the click started by the first tap
                input.simulateClick(
                    static_cast<int>(Mousecode::BUTTON_1), false
                );
                pinchStartDistance = distanceBetween(touches);
                pinchLastDistance = pinchStartDistance;
                setShouldRefresh();
                break;
            }
            case AMOTION_EVENT_ACTION_MOVE: {
                fillPoints();
                if (touches.empty()) {
                    break;
                }
                // primary finger -> camera look (cursor delta)
                const auto& primary = touches.front();
                input.simulateCursorPos(primary.x, primary.y);

                // secondary finger -> WASD-like movement
                updateMovementKeys(sensitivity);

                // pinch -> scroll
                if (touches.size() >= 2) {
                    float dist = distanceBetween(touches);
                    if (pinchLastDistance > 1.0f && dist > 1.0f) {
                        float ratio = dist / pinchLastDistance;
                        // spread fingers => zoom in (scroll up)
                        input.addScroll(static_cast<double>(ratio - 1.0f) *
                                        10.0);
                    }
                    pinchLastDistance = dist;
                }
                setShouldRefresh();
                break;
            }
            case AMOTION_EVENT_ACTION_POINTER_UP: {
                size_t index = (AMotionEvent_getAction(event) &
                                AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                               AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
                int32_t releasedId = AMotionEvent_getPointerId(event, index);
                releaseMovementKey(releasedId);
                touches.erase(
                    std::remove_if(touches.begin(), touches.end(),
                                   [&](const TouchPoint& tp) {
                                       return tp.id == releasedId;
                                   }),
                    touches.end()
                );
                if (touches.size() < 2) {
                    pinchStartDistance = -1.0f;
                    pinchLastDistance = -1.0f;
                }
                setShouldRefresh();
                break;
            }
            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_CANCEL: {
                for (const auto& tp : touches) {
                    releaseMovementKey(tp.id);
                }
                // treat a stationary short touch as a completed click
                if (!touches.empty() && action == AMOTION_EVENT_ACTION_UP) {
                    const auto& tp = touches.front();
                    float dx = tp.x - tp.startX;
                    float dy = tp.y - tp.startY;
                    if (dx * dx + dy * dy < TAP_SLOP * TAP_SLOP) {
                        input.simulateClick(
                            static_cast<int>(Mousecode::BUTTON_1), true
                        );
                    }
                }
                input.simulateClick(
                    static_cast<int>(Mousecode::BUTTON_1), false
                );
                touches.clear();
                pinchStartDistance = -1.0f;
                pinchLastDistance = -1.0f;
                setShouldRefresh();
                break;
            }
        }
    }

    /// Dispatch keyboard/gamepad key events (external keyboards, gamepads,
    /// and AKEYCODE_BACK mapped to Escape).
    void onKeyDispatched(int32_t keyCode, bool pressed) {
        if (keyCode == AKEYCODE_BACK) {
            // back button behaves like Escape
            input.setKeyState(GLFW_KEY_ESCAPE, pressed);
            setShouldRefresh();
            return;
        }
        input.onKeyEvent(keyCode, pressed);
        setShouldRefresh();
    }

private:
    static inline constexpr float TAP_SLOP = 16.0f;

    struct MovementBinding {
        int32_t pointerId;
        Keycode key;
    };

    static float distanceBetween(const std::vector<TouchPoint>& pts) {
        if (pts.size() < 2) {
            return -1.0f;
        }
        float dx = pts[1].x - pts[0].x;
        float dy = pts[1].y - pts[0].y;
        return std::sqrt(dx * dx + dy * dy);
    }

    /// Secondary finger acts as an analog stick: its direction relative to
    /// the position where it touched down maps to forward/back/strafe keys.
    void updateMovementKeys(float sensitivity) {
        for (size_t i = 1; i < touches.size(); ++i) {
            auto& tp = touches[i];
            float dx = (tp.x - tp.startX) * sensitivity;
            float dy = (tp.y - tp.startY) * sensitivity;
            Keycode key = Keycode::UNKNOWN;
            if (std::fabs(dx) > DEAD_ZONE && std::fabs(dx) >= std::fabs(dy)) {
                key = dx > 0 ? Keycode::D : Keycode::A;
            } else if (std::fabs(dy) > DEAD_ZONE) {
                key = dy > 0 ? Keycode::S : Keycode::W;
            }
            // release previously bound key if direction changed
            for (auto it = movementBindings.begin();
                 it != movementBindings.end();) {
                if (it->pointerId == tp.id && it->key != key) {
                    input.simulateKey(it->key, false);
                    it = movementBindings.erase(it);
                } else {
                    ++it;
                }
            }
            if (key != Keycode::UNKNOWN) {
                bool already = false;
                for (const auto& mb : movementBindings) {
                    if (mb.pointerId == tp.id && mb.key == key) {
                        already = true;
                        break;
                    }
                }
                if (!already) {
                    input.simulateKey(key, true);
                    movementBindings.push_back({tp.id, key});
                }
            }
        }
    }

    void releaseMovementKey(int32_t pointerId) {
        for (auto it = movementBindings.begin();
             it != movementBindings.end();) {
            if (it->pointerId == pointerId) {
                input.simulateKey(it->key, false);
                it = movementBindings.erase(it);
            } else {
                ++it;
            }
        }
    }

    DisplaySettings* settings;
    bool shouldClose = false;
    bool focused = true;
    bool shouldRefresh = true;
    int framerate = -1;
    double prevSwap = 0.0;

    EGLDisplay eglDisplay = EGL_NO_DISPLAY;
    EGLContext eglContext = EGL_NO_CONTEXT;
    EGLSurface eglSurface = EGL_NO_SURFACE;

    std::stack<glm::vec4> scissorStack;
    glm::vec4 scissorArea {};

    std::vector<TouchPoint> touches;
    float pinchStartDistance = -1.0f;
    float pinchLastDistance = -1.0f;
    std::vector<MovementBinding> movementBindings;
    static inline constexpr float DEAD_ZONE = 24.0f;
};
static_assert(!std::is_abstract<AndroidWindow>());

// ---- global Native Activity entry points -----------------------------------

extern "C" {

/// Called by the Java activity (VoxelCoreActivity.nativeSetup) once the
/// ANativeWindow is available. Performs EGL initialization and starts the
/// engine main loop on the current (native activity) thread.
void Java_org_libsdl_app_SDLActivityNativeInit();  // doc only

JNIEXPORT void JNICALL
Java_com_voxelcore_engine_VoxelCoreActivity_nativeSetup(
    JNIEnv* env, jobject thiz, ANativeWindow* window, jobject /*surface*/
) {
    if (g_window == nullptr) {
        logger.error() << "nativeSetup called before Window::initialize";
        return;
    }
    if (!g_window->initEgl(window)) {
        g_window->setShouldClose(true);
        return;
    }
    g_window->setSize(
        ANativeWindow_getWidth(window), ANativeWindow_getHeight(window)
    );
}

}  // extern "C"

// The engine calls Window::initialize once at startup. On Android the real
// surface arrives asynchronously through nativeSetup, so initialize returns
// objects immediately and the caller must pump events until EGL is ready.
std::tuple<
    std::unique_ptr<Window>,
    std::unique_ptr<Input>
> Window::initialize(DisplaySettings* settings, std::string title) {
    logger.info() << "initializing Android backend; title=\"" << title << "\"";
    auto inputPtr = std::make_unique<AndroidInput>();
    auto windowPtr = std::make_unique<AndroidWindow>(*inputPtr, settings);
    g_input = inputPtr.get();
    g_window = windowPtr.get();
    input_util::initialize();
    return {std::move(windowPtr), std::move(inputPtr)};
}

// Route raw AInputEvents coming from the Native Activity looper into the
// window/input objects. Exported for the Java/JNI glue (MainActivity).
extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_voxelcore_engine_VoxelCoreActivity_nativeOnInputEvent(
    JNIEnv* /*env*/, jobject /*thiz*/, jobject jevent
) {
    if (g_window == nullptr || g_input == nullptr) {
        return JNI_FALSE;
    }
    AInputEvent* event =
        reinterpret_cast<AInputEvent*>(jevent);  // NOLINT read-only pointer
    int32_t type = AInputEvent_getType(event);
    if (type == AINPUT_EVENT_TYPE_KEY) {
        int32_t keyCode = AKeyEvent_getKeyCode(event);
        bool pressed = AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN;
        g_window->onKeyDispatched(keyCode, pressed);
    } else if (type == AINPUT_EVENT_TYPE_MOTION) {
        g_window->onTouchEvent(event);
    }
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_voxelcore_engine_VoxelCoreActivity_nativeOnResized(
    JNIEnv* /*env*/, jobject /*thiz*/, jint width, jint height
) {
    if (g_window != nullptr) {
        g_window->setSize(width, height);
    }
}

JNIEXPORT void JNICALL
Java_com_voxelcore_engine_VoxelCoreActivity_nativeOnFocus(
    JNIEnv* /*env*/, jobject /*thiz*/, jboolean focused
) {
    if (g_window != nullptr) {
        g_window->setFocused(focused == JNI_TRUE);
    }
}

}  // extern "C"

// ---- display namespace (shared GL clear helpers) ---------------------------

void display::clear() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void display::clearDepth() {
    glClear(GL_DEPTH_BUFFER_BIT);
}

void display::setBgColor(glm::vec3 color) {
    glClearColor(color.r, color.g, color.b, 1.0f);
}

void display::setBgColor(glm::vec4 color) {
    glClearColor(color.r, color.g, color.b, color.a);
}

#endif  // __ANDROID__
