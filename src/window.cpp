#include "window.h"

#include <stdexcept>

namespace sickboy {

    Window::Window(const std::string& title, int width, int height) : handle(nullptr) {
        if (!glfwInit()) {
            throw std::runtime_error("Failed to initialize GLFW.");
        }
        handle = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
        if (!handle) {
            throw std::runtime_error("Failed to create GLFW window.");
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