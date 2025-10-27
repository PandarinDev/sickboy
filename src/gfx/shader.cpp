#include "gfx/shader.h"

#include <utility>
#include <stdexcept>

namespace sickboy::gfx {

    Shader Shader::create(GLenum type, const std::string& source) {
        auto handle = glCreateShader(type);
        auto source_ptr = source.data();
        auto source_size = static_cast<int>(source.size());
        glShaderSource(handle, 1, &source_ptr, &source_size);
        glCompileShader(handle);
        GLint compile_status = GL_FALSE;
        glGetShaderiv(handle, GL_COMPILE_STATUS, &compile_status);
        if (compile_status != GL_TRUE) {
            throw std::runtime_error("Failed to compile OGL shader.");
        }
        return Shader(handle);
    }

    Shader::Shader(GLuint handle) : handle(handle) {}

    Shader::~Shader() {
        glDeleteShader(handle);
    }

    Shader::Shader(Shader&& other) : handle(std::exchange(other.handle, 0)) {}

    Shader& Shader::operator=(Shader&& other) {
        handle = std::exchange(other.handle, 0);
        return *this;
    }

    ShaderProgram ShaderProgram::create(const std::vector<Shader>& shaders) {
        auto handle = glCreateProgram();
        for (const auto& shader : shaders) {
            glAttachShader(handle, shader.handle);
        }
        glLinkProgram(handle);
        for (const auto& shader : shaders) {
            glDetachShader(handle, shader.handle);
        }
        GLint link_status = GL_FALSE;
        glGetProgramiv(handle, GL_LINK_STATUS, &link_status);
        if (link_status != GL_TRUE) {
            throw std::runtime_error("Failed to link OGL shader program.");
        }

        return ShaderProgram(handle);
    }

    ShaderProgram::ShaderProgram(GLuint handle) : handle(handle) {}

    ShaderProgram::~ShaderProgram() {
        glDeleteProgram(handle);
    }

    ShaderProgram::ShaderProgram(ShaderProgram&& other) : handle(std::exchange(other.handle, 0)) {}

    ShaderProgram& ShaderProgram::operator=(ShaderProgram&& other) {
        handle = std::exchange(other.handle, 0);
        return *this;
    }

    void ShaderProgram::use() const {
        glUseProgram(handle);
    }

}