#include "renderer.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/glad.h>

#include <string>
#include <stdexcept>

namespace sickboy {

    Renderer::Renderer() {
        // Load OGL function pointers
        if (!gladLoadGLLoader((GLADloadproc) glfwGetProcAddress)) {
            throw std::runtime_error("Failed to load OpenGL function pointers.");
        }

        // Create shader program
        std::vector<gfx::Shader> shaders;
        shaders.emplace_back(gfx::Shader::create(GL_VERTEX_SHADER, ""
            "#version 330 core\n"
            "layout(location = 0) in vec2 in_Position;\n"
            "layout(location = 1) in vec2 in_TextureCoordinate;\n"
            "out vec2 fs_TextureCoordinate;"
            "void main() {\n"
            "fs_TextureCoordinate = in_TextureCoordinate;\n"
            "gl_Position = vec4(in_Position, 0.0, 1.0);\n"
            "}\n"
        ));
        shaders.emplace_back(gfx::Shader::create(GL_FRAGMENT_SHADER, ""
            "#version 330 core\n"
            "uniform sampler2D u_Texture;\n"
            "in vec2 fs_TextureCoordinate;\n"
            "layout(location = 0) out vec4 out_Color;\n"
            "void main() {\n"
            "out_Color = texture(u_Texture, fs_TextureCoordinate);\n"
            "}\n"
        ));
        shader = std::make_unique<gfx::ShaderProgram>(gfx::ShaderProgram::create(shaders));

        // Create render target texture
        ppu_texture = std::make_unique<gfx::Texture>(gfx::Texture::create(PPU::LCD_WIDTH, PPU::LCD_HEIGHT));

        // Create VBO/VAO with a simple quad that we will use to display our texture
        gfx::VBO vertex_buffer = gfx::VBO::create_quad(1.0f);
        vertex_array = std::make_unique<gfx::VAO>(gfx::VAO::create(std::move(vertex_buffer)));
    }

    void Renderer::clear_buffers() const {
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void Renderer::check_errors() const {
        auto error = glGetError();
        if (error != GL_NO_ERROR) {
            throw std::runtime_error("OpenGL error received '" + std::to_string(error) + "'.");
        }
    }

    void Renderer::render(const PPU::CroppedFrame& frame) const {
        shader->use();
        ppu_texture->upload_grayscale(frame.data(), PPU::LCD_WIDTH, PPU::LCD_HEIGHT);
        vertex_array->bind();
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

}