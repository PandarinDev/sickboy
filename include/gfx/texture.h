#pragma once

#include <glad/glad.h>

#include <cstdint>

namespace sickboy::gfx {

    struct Texture {

        static Texture create(int width, int height);

        GLuint handle;

        Texture(GLuint handle);
        ~Texture();
        Texture(const Texture&) = delete;
        Texture& operator=(const Texture&) = delete;
        Texture(Texture&&);
        Texture& operator=(Texture&&);

        void upload_grayscale(const std::uint8_t* data, int width, int height) const;

    };

}