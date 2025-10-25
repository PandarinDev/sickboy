#pragma once

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <string>

namespace sickboy {

    struct Window {

        Window(const std::string& title, int width, int height);
        ~Window();
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
        Window(Window&&);
        Window& operator=(Window&&);

        void poll_events() const;
        bool should_close() const;
        void swap_buffers() const;

    private:

        GLFWwindow* handle;

    };

}