#include "ppu.h"

#include <stdexcept>

#include <iostream>

namespace sickboy {

    std::uint16_t get_max_mode_dot_length(PPUMode mode) {
        switch (mode) {
            case PPUMode::VERTICAL_BLANK: return 4560;
            case PPUMode::OAM_SCAN: return 80;
            case PPUMode::DRAWING: return 289;
            case PPUMode::HORIZONTAL_BLANK: return 204;
            default: throw std::runtime_error("Unknown PPU mode in get_max_mode_dot_length.");
        }
    }

    PPUMode get_next_mode(PPUMode mode, std::uint16_t scanline) {
        switch (mode) {
            case PPUMode::VERTICAL_BLANK: return PPUMode::OAM_SCAN;
            case PPUMode::OAM_SCAN: return PPUMode::DRAWING;
            case PPUMode::DRAWING: return PPUMode::HORIZONTAL_BLANK;
            case PPUMode::HORIZONTAL_BLANK: {
                if (scanline >= PPU::LCD_HEIGHT) {
                    return PPUMode::VERTICAL_BLANK;
                }
                else {
                    return PPUMode::OAM_SCAN;
                }
            }
            default: throw std::runtime_error("Unknown PPU mode in get_next_mode.");
        }
    }

    PPU::PPU(const std::shared_ptr<MMU>& memory) :
        memory(memory), mode(PPUMode::OAM_SCAN), current_mode_dots(0), last_draw_dots_length(0), current_scanline(0) {}

    bool PPU::is_lcd_and_ppu_enabled() const {
        static const std::uint16_t LCD_CONTROL_BYTE_ADDRESS = 0xFF40;
        return memory->read(LCD_CONTROL_BYTE_ADDRESS) & 0b10000000;
    }

    bool PPU::tick() {
        // TODO: Implement VRAM locking while certain modes are active

        ++current_mode_dots;
        auto max_mode_length = get_max_mode_dot_length(mode);
        // Horizontal blank length is shortened if draw mode took up more than minimum time
        if (mode == PPUMode::HORIZONTAL_BLANK) {
            static constexpr auto min_draw_length = 172;
            max_mode_length -= last_draw_dots_length - min_draw_length;
        }
        // When in vertical blank mode we need to increment the scanline every Nth dot
        if (mode == PPUMode::VERTICAL_BLANK) {
            if (current_mode_dots % VBLANK_DOTS_PER_SCANLINE == 0) {
                increment_scanline();
            }
        }
        // If we reached the maximum number of dots in a mode change to the next mode
        if (current_mode_dots >= max_mode_length) {
            // Remember the drawing length to later reduce HBlank length
            if (mode == PPUMode::DRAWING) {
                last_draw_dots_length = current_mode_dots;
            }
            // Increment the scanline after every HBlank
            else if (mode == PPUMode::HORIZONTAL_BLANK) {
                increment_scanline();
            }
            mode = get_next_mode(mode, current_scanline);
            current_mode_dots = 0;
            // When we are switching to VBlank mode signal that a new frame needs to be rendered
            if (mode == PPUMode::VERTICAL_BLANK) {
                return true;
            }
        }
        return false;
    }


    void PPU::increment_scanline() {
        current_scanline = (current_scanline + 1) % MAX_SCANLINES;
        static constexpr std::uint16_t LCD_Y_COORD_ADDRESS = 0xFF44;
        memory->write(LCD_Y_COORD_ADDRESS, current_scanline);
    }

    std::uint8_t get_tile_color_index(std::uint16_t row_colors, std::uint8_t pixel) {
        std::uint8_t higher_bits = ((row_colors & 0xFF00) >> 8);
        std::uint8_t lower_bits = (row_colors & 0xFF);
        // High bits are the first pixels so pixel 0 is the 7th bit
        std::uint8_t shift = 7 - pixel;
        // Somewhat confusingly the high byte gives the lower bit of the returned color index
        return (((lower_bits & (1 << shift)) >> shift) << 1) | ((higher_bits & (1 << shift)) >> shift);
    }

    std::uint8_t color_index_to_grayscale_value(std::uint8_t color_palette, std::uint8_t color_index) {
        // TODO: Really unsure if these color values are correct or not, check in documentation
        static constexpr std::array<std::uint8_t, 4> COLOR_VALUE_LOOKUP = {
            0xFF, // 0 = White
            0xAB, // 1 = Light gray
            0x56, // 2 = Dark gray
            0x00, // 3 = Black
        };
        auto color_value = (color_palette & (0b11 << (color_index * 2))) >> (color_index * 2);
        return COLOR_VALUE_LOOKUP.at(color_value);
    }

    PPU::CroppedFrame PPU::compute_frame() const {
        PPU::FullFrame frame{};

        // TODO: We are currently ignoring some of LCD control data (such as OBJ size)
        static constexpr std::uint16_t LCD_CONTROL_BYTE_ADDRESS = 0xFF40;
        static constexpr std::uint16_t BACKGROUND_TILEMAP_START_ADDRESS = 0x9800;
        static constexpr std::uint16_t NUM_BACKGROUND_TILES = 32 * 32;
        static constexpr std::uint16_t COLOR_PALETTE_ADDR = 0xFF47;
        std::uint8_t control_byte = memory->read(LCD_CONTROL_BYTE_ADDRESS);
        // TODO: This is incorrect for unsigned tile addressing
        std::uint16_t bg_window_tile_start_addr = ((control_byte & 0b00010000) != 0)
            ? 0x8000
            : 0x8800;

        // Draw background
        std::uint8_t color_palette = memory->read(COLOR_PALETTE_ADDR);
        for (std::uint16_t i = 0; i < NUM_BACKGROUND_TILES; ++i) {
            std::uint8_t tile_idx = memory->read(BACKGROUND_TILEMAP_START_ADDRESS + i);
            static constexpr auto tile_entry_size = sizeof(TileEntry::value_type) * std::tuple_size_v<TileEntry>;
            TileEntry tile_entry;
            memory->copy_from(bg_window_tile_start_addr + tile_idx * tile_entry_size, reinterpret_cast<std::uint8_t*>(&tile_entry), tile_entry_size);
            std::uint8_t x_offset = (i % 32) * 8;
            std::uint8_t y_offset = static_cast<std::uint8_t>((i / 32) * 8);
            draw_tile(tile_entry, x_offset, y_offset, color_palette, false, frame);
        }

        // TODO: Draw window

        // Draw objects if object rendering is enabled
        bool is_obj_rendering_enabled = control_byte & 0b00000010;
        if (is_obj_rendering_enabled) {
            draw_objects(color_palette, frame);
        }

        return crop_frame(frame);
    }

    std::vector<std::uint8_t> PPU::dump_vram() const {
        std::vector<std::uint8_t> result;
        result.resize(0x2000); // 8kB
        memory->copy_from(0x8000, result.data(), result.size());
        return result;
    }

    void PPU::draw_objects(std::uint8_t color_palette, PPU::FullFrame& frame) const {
        // TODO: This object support is very rudimentary and many things (such as object flags) are ignored at the moment.
        static constexpr std::uint16_t OBJ_TILE_START_ADDR = 0x8000;
        static constexpr std::uint16_t OAM_START_ADDR = 0xFE00;
        static constexpr std::uint8_t NUM_OAM_ENTRIES = 40;

        for (std::uint8_t i = 0; i < NUM_OAM_ENTRIES; ++i) {
            static constexpr auto object_entry_size = sizeof(OAMEntry);
            // Copy object attribute mapping entry from VRAM
            OAMEntry object_entry;
            memory->copy_from(OAM_START_ADDR + i * object_entry_size, reinterpret_cast<std::uint8_t*>(&object_entry), object_entry_size);
            // Copy tile data corresponding to OAM
            static constexpr auto tile_entry_size = sizeof(TileEntry::value_type) * std::tuple_size_v<TileEntry>;
            TileEntry tile_entry;
            std::uint16_t tile_offset = object_entry.tile_index * tile_entry_size;
            memory->copy_from(OBJ_TILE_START_ADDR + tile_offset, reinterpret_cast<std::uint8_t*>(tile_entry.data()), tile_entry_size);
            draw_tile(tile_entry, object_entry.x, object_entry.y, color_palette, true, frame);
        }
    }

    void PPU::draw_tile(
        const PPU::TileEntry& tile,
        std::uint8_t x_offset,
        std::uint8_t y_offset,
        std::uint8_t color_palette,
        bool is_object,
        FullFrame& frame) const {
        // Write color data to frame buffer
        for (std::uint8_t y = 0; y < 8; ++y) {
            std::uint16_t row_colors = tile[y];
            for (std::uint8_t x = 0; x < 8; ++x) {
                auto pixel_color_index = get_tile_color_index(row_colors, x);
                // For objects color 0 means transparent - simply skip the pixel
                if (is_object && pixel_color_index == 0) {
                    continue;
                }
                auto color = color_index_to_grayscale_value(color_palette, pixel_color_index);
                // TODO: Double check if the offset logic is correct
                frame.at((y + y_offset) * 256 + x + x_offset) = color;
            }
        }
    }

    PPU::CroppedFrame PPU::crop_frame(const FullFrame& frame) const {
        static constexpr std::uint16_t SCROLL_Y_ADDR = 0xFF42;
        static constexpr std::uint16_t SCROLL_X_ADDR = 0xFF43;
        CroppedFrame result;
        std::uint8_t scroll_y_value = memory->read(SCROLL_Y_ADDR);
        std::uint8_t scroll_x_value = memory->read(SCROLL_X_ADDR);
        for (std::uint8_t y = 0; y < LCD_HEIGHT; ++y) {
            for (std::uint8_t x = 0; x < LCD_WIDTH; ++x) {
                // Here we are essentially abusing that unsigned integers are guaranteed to wrap-around,
                // so we force these values to wrap around then use the resulting values to reindex the full frame.
                std::uint8_t final_y = y + scroll_y_value;
                std::uint8_t final_x = x + scroll_x_value;
                result[y * LCD_WIDTH + x] = frame[final_y * 256 + final_x];
            }
        }

        return result;
    }

}