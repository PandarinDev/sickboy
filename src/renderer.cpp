#include "renderer.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/glad.h>

#include <stdexcept>

namespace sickboy {

    Renderer::Renderer() {
        if (!gladLoadGLLoader((GLADloadproc) glfwGetProcAddress)) {
            throw std::runtime_error("Failed to load OpenGL function pointers.");
        }
        glClearColor(0.1f, 0.5f, 0.95f, 1.0f);
    }

    void Renderer::clear_buffers() const {
        glClear(GL_COLOR_BUFFER_BIT);
    }

}