#pragma once

#include "mmu.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

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

        // A frame actually has 256*256 dimensions which is later cropped into
        // an LCD_WIDTH*LCD_HEIGHT region using the scroll registers.
        using FullFrame = std::array<std::uint8_t, 256 * 256>;
        using CroppedFrame = std::array<std::uint8_t, LCD_WIDTH * LCD_HEIGHT>;

        struct OAMEntry {
            std::uint8_t y;
            std::uint8_t x;
            std::uint8_t tile_index;
            std::uint8_t flags;
        };

        // TODO: Ensure this by adding attributes/macros for all platforms
        static_assert(sizeof(OAMEntry) == 4, "OAMEntry is not tightly packed.");

        using TileEntry = std::array<std::uint16_t, 8>;

        std::shared_ptr<MMU> memory;
        PPUMode mode;
        std::uint16_t current_mode_dots;
        std::uint16_t last_draw_dots_length;
        std::uint8_t current_scanline;

        PPU(const std::shared_ptr<MMU>& memory);

        bool is_lcd_and_ppu_enabled() const;

        // Returns true if a new frame should be rendered
        bool tick();
        CroppedFrame compute_frame() const;
        std::vector<std::uint8_t> dump_vram() const;

    private:

        void increment_scanline();
        void draw_objects(std::uint8_t color_palette, FullFrame& frame) const;
        void draw_tile(
            const TileEntry& tile,
            std::uint8_t x_offset,
            std::uint8_t y_offset,
            std::uint8_t color_palette,
            bool is_object,
            FullFrame& frame) const;
        CroppedFrame crop_frame(const FullFrame& frame) const;

    };

}