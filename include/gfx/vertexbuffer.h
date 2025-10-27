#pragma once

#include <glad/glad.h>

#include <vector>

namespace sickboy::gfx {

    struct VBO {

        static VBO create(const std::vector<float>& data);
        static VBO create_quad(float size);

        GLuint handle;

        VBO(GLuint handle);
        ~VBO();
        VBO(const VBO&) = delete;
        VBO& operator=(const VBO&) = delete;
        VBO(VBO&&);
        VBO& operator=(VBO&&);

        void bind() const;

    };

    struct VAO {

        static VAO create(VBO&& vbo);

        GLuint handle;
        VBO vbo;

        VAO(GLuint handle, VBO&& vbo);
        ~VAO();
        VAO(const VAO&) = delete;
        VAO& operator=(const VAO&) = delete;
        VAO(VAO&&);
        VAO& operator=(VAO&&);

        void bind() const;

    };

}