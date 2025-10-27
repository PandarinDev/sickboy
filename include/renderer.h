#pragma once

#include "gfx/shader.h"
#include "gfx/texture.h"
#include "gfx/vertexbuffer.h"
#include "ppu.h"

#include <memory>

namespace sickboy {

    struct Renderer {

        Renderer();

        void clear_buffers() const;
        void check_errors() const;

        void render(const PPU::CroppedFrame& frame) const;

    private:

        std::unique_ptr<gfx::ShaderProgram> shader;
        std::unique_ptr<gfx::Texture> ppu_texture;
        std::unique_ptr<gfx::VAO> vertex_array;

    };

}