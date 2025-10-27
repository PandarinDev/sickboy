#include "gfx/texture.h"

#include <utility>
#include <stdexcept>

namespace sickboy::gfx {

    Texture Texture::create(int width, int height) {
        GLuint handle;
        glGenTextures(1, &handle);
        glBindTexture(GL_TEXTURE_2D, handle);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        GLint swizzle_mask[] = { GL_RED, GL_RED, GL_RED, GL_ONE };
        glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle_mask);

        return Texture(handle);
    }

    Texture::Texture(GLuint handle) : handle(handle) {}

    Texture::~Texture() {
        glDeleteTextures(1, &handle);
    }

    Texture::Texture(Texture&& other) : handle(std::exchange(other.handle, 0)) {}

    Texture& Texture::operator=(Texture&& other) {
        handle = std::exchange(other.handle, 0);
        return *this;
    }

    void Texture::upload_grayscale(const std::uint8_t* data, int width, int height) const {
        glBindTexture(GL_TEXTURE_2D, handle);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, data);
    }

}