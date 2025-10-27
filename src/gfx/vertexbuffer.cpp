#include "gfx/vertexbuffer.h"

#include <utility>

namespace sickboy::gfx {

    VBO VBO::create(const std::vector<float>& data) {
        GLuint handle;
        glGenBuffers(1, &handle);
        glBindBuffer(GL_ARRAY_BUFFER, handle);
        glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STATIC_DRAW);
        return VBO(handle);
    }

    VBO VBO::create_quad(float size) {
        return VBO::create({
            // Bottom left
            -size, -size,
            0.0f, 0.0f,
            // Bottom right
            size, -size,
            1.0f, 0.0f,
            // Top right
            size, size,
            1.0f, 1.0f,
            // Bottom left
            -size, -size,
            0.0f, 0.0f,
            // Top right
            size, size,
            1.0f, 1.0f,
            // Top left
            -size, size,
            0.0f, 1.0f
        });
    }

    VBO::VBO(GLuint handle) : handle(handle) {}

    VBO::~VBO() {
        glDeleteBuffers(1, &handle);
    }

    VBO::VBO(VBO&& other) : handle(std::exchange(other.handle, 0)) {}

    VBO& VBO::operator=(VBO&& other) {
        handle = std::exchange(other.handle, 0);
        return *this;
    }

    void VBO::bind() const {
        glBindBuffer(GL_ARRAY_BUFFER, handle);
    }

    VAO VAO::create(VBO&& vbo) {
        GLuint handle;
        glGenVertexArrays(1, &handle);
        glBindVertexArray(handle);
        vbo.bind();
        static constexpr auto stride = 4 * sizeof(float);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, 0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*) (2 * sizeof(float)));

        return VAO(handle, std::move(vbo));
    }

    VAO::VAO(GLuint handle, VBO&& vbo) : handle(handle), vbo(std::move(vbo)) {}

    VAO::~VAO() {
        glDeleteVertexArrays(1, &handle);
    }

    VAO::VAO(VAO&& other) : handle(std::exchange(other.handle, 0)), vbo(std::move(other.vbo)) {}

    VAO& VAO::operator=(VAO&& other) {
        handle = std::exchange(other.handle, 0);
        vbo = std::move(other.vbo);
        return *this;
    }

    void VAO::bind() const {
        glBindVertexArray(handle);
    }

}