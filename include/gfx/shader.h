#pragma once

#include <glad/glad.h>

#include <string>
#include <vector>

namespace sickboy::gfx {

    struct Shader {

        static Shader create(GLenum type, const std::string& source);

        GLuint handle;

        Shader(GLuint handle);
        ~Shader();
        Shader(const Shader&) = delete;
        Shader& operator=(const Shader&) = delete;
        Shader(Shader&&);
        Shader& operator=(Shader&&);

    };

    struct ShaderProgram {

        static ShaderProgram create(const std::vector<Shader>& shaders);

        GLuint handle;

        ShaderProgram(GLuint handle);
        ~ShaderProgram();
        ShaderProgram(const ShaderProgram&) = delete;
        ShaderProgram& operator=(const ShaderProgram&) = delete;
        ShaderProgram(ShaderProgram&&);
        ShaderProgram& operator=(ShaderProgram&&);

        void use() const;

    };

}