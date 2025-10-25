#include "ppu.h"

#include <stdexcept>

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

    struct __attribute__((packed)) OAMEntry {
        std::uint8_t y;
        std::uint8_t x;
        std::uint8_t tile_index;
        std::uint8_t flags;
    };

    using TileEntry = std::array<std::uint16_t, 8>;

    std::uint8_t get_tile_color_index(std::uint16_t row_colors, std::uint8_t pixel) {
        std::uint8_t higher_bits = ((row_colors & 0xFF00) >> 8);
        std::uint8_t lower_bits = (row_colors & 0xFF);
        // Somewhat confusingly the high byte gives the lower bit of the returned color index
        return (((lower_bits & (1 << pixel)) >> pixel) << 1) | ((higher_bits & (1 << pixel)) >> pixel);
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
        return COLOR_VALUE_LOOKUP[color_value];
    }

    PPU::Frame PPU::compute_frame() const {
        PPU::Frame frame{};
        // TODO: This frame initialization to white probably should be removed
        for (std::size_t i = 0; i < frame.size(); ++i) {
            frame[i] = 0xFF;
        }

        // TODO: For now only objects are rendered, windows and BG objects are not.
        // TODO: Additionally even for objects flags (such as flip X/Y, etc.) are ignored.
        static constexpr std::uint16_t OBJ_TILE_START_ADDR = 0x8000;
        static constexpr std::uint16_t OAM_START_ADDR = 0xFE00;
        static constexpr std::uint16_t COLOR_PALETTE_ADDR = 0xFF47;
        static constexpr auto NUM_OAM_ENTRIES = 40;
        for (std::size_t i = 0; i < NUM_OAM_ENTRIES; ++i) {
            static constexpr auto object_entry_size = sizeof(OAMEntry);
            // Copy object attribute mapping entry from VRAM
            OAMEntry object_entry;
            memory->copy_from(OAM_START_ADDR + i * object_entry_size, reinterpret_cast<std::uint8_t*>(&object_entry), object_entry_size);
            // Copy tile data corresponding to OAM
            static constexpr auto tile_entry_size = sizeof(TileEntry::value_type) * std::tuple_size_v<TileEntry>;
            TileEntry tile_entry;
            std::uint16_t tile_offset = object_entry.tile_index * tile_entry_size;
            memory->copy_from(OBJ_TILE_START_ADDR + tile_offset, reinterpret_cast<std::uint8_t*>(tile_entry.data()), tile_entry_size);
            // Write color data to frame buffer
            std::uint8_t color_palette = memory->read(COLOR_PALETTE_ADDR);
            for (std::uint8_t y = 0; y < 8; ++y) {
                std::uint16_t row_colors = tile_entry[y];
                for (std::uint8_t x = 0; x < 8; ++x) {
                    auto pixel_color_index = get_tile_color_index(row_colors, x);
                    // Ignore 0 which means transparent
                    if (pixel_color_index > 0) {
                        auto color = color_index_to_grayscale_value(color_palette, pixel_color_index);
                        frame[y * LCD_WIDTH + x] = color;
                    }
                }
            }
        }

        return frame;
    }

}