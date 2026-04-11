#pragma once

#include "mmu.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include <optional>

namespace sickboy {

    // The value for these modes is actually important as it is written
    // to the LCD status register upon every mode change that happens.
    enum class PPUMode : std::uint8_t {
        HORIZONTAL_BLANK = 0,
        VERTICAL_BLANK = 1,
        OAM_SCAN = 2,
        DRAWING = 3
    };

    struct PPU {

        static constexpr std::uint8_t LCD_WIDTH = 160;
        static constexpr std::uint8_t LCD_HEIGHT = 144;
        static constexpr std::uint8_t MAX_SCANLINES = 154;
        static constexpr std::uint16_t VBLANK_DOTS_PER_SCANLINE = 456;

        using Frame = std::array<std::uint8_t, LCD_WIDTH * LCD_HEIGHT>;

        struct OAMEntry {
            std::uint8_t y;
            std::uint8_t x;
            std::uint8_t tile_index;
            std::uint8_t flags;
        };

        // During OAM search only index and Y position are locked
        struct ScannedOAMEntry {
            std::uint8_t index;
            std::uint8_t y_position;
        };

        // TODO: Ensure this by adding attributes/macros for all platforms
        static_assert(sizeof(OAMEntry) == 4, "OAMEntry is not tightly packed.");

        using TileEntry = std::array<std::uint16_t, 8>;

        std::shared_ptr<MMU> memory;
        PPUMode mode;
        std::uint16_t current_mode_dots;
        std::uint16_t last_draw_dots_length;
        std::uint8_t current_scanline;
        std::uint8_t current_column;
        std::vector<ScannedOAMEntry> scanline_intersecting_objects;
        bool scanline_intersecting_window;
        bool scanline_window_line_incremented;
        std::uint8_t window_tile_line;
        Frame frame;

        PPU(const std::shared_ptr<MMU>& memory);

        bool is_lcd_and_ppu_enabled() const;

        // Returns true if a new frame should be rendered
        bool tick();

    private:

        struct BackgroundTileMapInfo {
            std::uint16_t tile_idx;
            std::uint8_t x_offset;
            std::uint8_t y_offset;
        };

        struct ObjectPixelInfo {
            std::uint8_t color_idx;
            std::uint8_t palette_idx;
            bool draw_below_background;
        };

        enum class TileIndexComputationMethod {
            BACKGROUND,
            WINDOW
        };

        void increment_scanline();
        void draw_pixel();
        void execute_oam_scan();
        std::uint8_t fetch_background_color_index(std::uint8_t control_byte, TileIndexComputationMethod idx_compute_method) const;
        std::optional<ObjectPixelInfo> fetch_object_pixel_info() const;
        BackgroundTileMapInfo compute_background_tilemap_info() const;
        BackgroundTileMapInfo compute_window_tilemap_info() const;

    };

}