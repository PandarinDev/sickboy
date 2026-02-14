#include "window.h"

#include <stdexcept>

namespace sickboy {

    Window::Window(const std::string& title, int width, int height) : handle(nullptr) {
        if (!glfwInit()) {
            throw std::runtime_error("Failed to initialize GLFW.");
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        handle = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
        if (!handle) {
            const char* error_description;
            glfwGetError(&error_description);
            throw std::runtime_error("Failed to create GLFW window: " + std::string(error_description));
        }
        glfwMakeContextCurrent(handle);
    }

    Window::~Window() {
        if (handle) {
            glfwDestroyWindow(handle);
        }
    }

    Window::Window(Window&& other) : handle(std::exchange(other.handle, nullptr)) {}

    Window& Window::operator=(Window&& other) {
        handle = std::exchange(other.handle, nullptr);
        return *this;
    }

    GLFWwindow* Window::get_handle() const {
        return handle;
    }

    void Window::poll_events() const {
        glfwPollEvents();
    }

    bool Window::should_close() const {
        return glfwWindowShouldClose(handle);
    }

    void Window::swap_buffers() const {
        glfwSwapBuffers(handle);
    }

}