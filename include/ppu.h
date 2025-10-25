#pragma once

#include "mmu.h"

#include <array>
#include <cstdint>
#include <memory.h>

namespace sickboy {

    enum class PPUMode {
        VERTICAL_BLANK,
        OAM_SCAN,
        DRAWING,
        HORIZONTAL_BLANK
    };

    struct PPU {

        static constexpr std::uint8_t LCD_WIDTH = 160;
        static constexpr std::uint8_t LCD_HEIGHT = 144;
        static constexpr std::uint8_t MAX_SCANLINES = 154;
        static constexpr std::uint16_t VBLANK_DOTS_PER_SCANLINE = 456;

        using Frame = std::array<std::uint8_t, LCD_WIDTH * LCD_HEIGHT>;

        std::shared_ptr<MMU> memory;
        PPUMode mode;
        std::uint16_t current_mode_dots;
        std::uint16_t last_draw_dots_length;
        std::uint8_t current_scanline;

        PPU(const std::shared_ptr<MMU>& memory);

        // Returns true if a new frame should be rendered
        bool tick();
        Frame compute_frame() const;

    private:

        void increment_scanline();

    };

}