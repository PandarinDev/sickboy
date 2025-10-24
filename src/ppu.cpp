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

    void PPU::tick() {
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
        }
    }


    void PPU::increment_scanline() {
        current_scanline = (current_scanline + 1) % MAX_SCANLINES;
        static constexpr std::uint16_t LCD_Y_COORD_ADDRESS = 0xFF44;
        memory->write(LCD_Y_COORD_ADDRESS, current_scanline);
    }

}