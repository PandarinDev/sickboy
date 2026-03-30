#include "ppu.h"

#include <stdexcept>
#include <algorithm>

#include <iostream>

namespace sickboy {

    static constexpr std::uint8_t MAX_INTERSECTING_OBJECTS_PER_SCANLINE = 10;

    std::uint16_t get_max_mode_dot_length(PPUMode mode) {
        switch (mode) {
            case PPUMode::VERTICAL_BLANK: return 4560;
            case PPUMode::OAM_SCAN: return 80;
            // TODO: Drawing can take up to 289 dots, implement draw penalties
            case PPUMode::DRAWING: return 172;
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
        memory(memory), mode(PPUMode::OAM_SCAN), current_mode_dots(0),
        last_draw_dots_length(0), current_scanline(0), current_column(0),
        scanline_intersecting_objects(), frame({}) {
        scanline_intersecting_objects.reserve(MAX_INTERSECTING_OBJECTS_PER_SCANLINE);
    }

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
        else if (mode == PPUMode::OAM_SCAN) {
            execute_oam_scan();
        }
        else if (mode == PPUMode::DRAWING) {
            draw_pixel();
        }
        // When in vertical blank mode we need to increment the scanline every Nth dot
        else if (mode == PPUMode::VERTICAL_BLANK) {
            if (current_mode_dots % VBLANK_DOTS_PER_SCANLINE == 0) {
                increment_scanline();
            }
        }
        // If we reached the maximum number of dots in a mode change to the next mode
        if (current_mode_dots >= max_mode_length) {
            // Remember the drawing length to later reduce HBlank length and reset scanline column
            if (mode == PPUMode::DRAWING) {
                last_draw_dots_length = current_mode_dots;
            }
            // Increment the scanline after every HBlank
            else if (mode == PPUMode::HORIZONTAL_BLANK) {
                increment_scanline();
            }
            mode = get_next_mode(mode, current_scanline);
            current_mode_dots = 0;
            // Write mode to status register
            static constexpr std::uint16_t LCD_STATUS_ADDRESS = 0xFF41;
            static constexpr std::uint16_t IF_ADDRESS = 0xFF0F;
            std::uint8_t lcd_status = memory->read(LCD_STATUS_ADDRESS);
            std::uint8_t mode_value = static_cast<std::uint8_t>(mode);
            memory->write(LCD_STATUS_ADDRESS, (lcd_status & ~0b11) | mode_value);
            // Check if mode select is requested and trigger STAT interrupt if mode matches
            if (((lcd_status & 0b00001000) != 0 && mode_value == 0) ||
                ((lcd_status & 0b00010000) != 0 && mode_value == 1) ||
                ((lcd_status & 0b00100000) != 0 && mode_value == 2)) {
                memory->write(IF_ADDRESS, memory->read(IF_ADDRESS) | 0b10);
            }

            // When we are switching to VBlank mode signal that a new frame needs to be rendered
            if (mode == PPUMode::VERTICAL_BLANK) {
                // Set VBlank in interrupt request flag
                memory->write(IF_ADDRESS, memory->read(IF_ADDRESS) | 0b1);
                return true;
            }
        }
        return false;
    }

    void PPU::increment_scanline() {
        current_column = 0;
        current_scanline = (current_scanline + 1) % MAX_SCANLINES;
        static constexpr std::uint16_t LCD_Y_COORD_ADDRESS = 0xFF44;
        memory->write(LCD_Y_COORD_ADDRESS, current_scanline);

        static constexpr std::uint16_t LY_COMPARE_ADDRESS = 0xFF45;
        static constexpr std::uint16_t LCD_STATUS_ADDRESS = 0xFF41;
        std::uint8_t ly_compare = memory->read(LY_COMPARE_ADDRESS);
        std::uint8_t lcd_status = memory->read(LCD_STATUS_ADDRESS);
        std::uint8_t final_status = lcd_status;
        bool lyc_equals = current_scanline == ly_compare;
        // Set LYC == LY on every scanline increment
        final_status = (final_status & ~0b100) |
            (lyc_equals ? 0b100 : 0b000);
        // If LYC compare is enabled request STAT interrupt if LYC == LY
        if (lyc_equals && (lcd_status & 0b01000000) != 0) {
            static constexpr std::uint16_t IF_ADDRESS = 0xFF0F;
            memory->write(IF_ADDRESS, memory->read(IF_ADDRESS) | 0b10);
        }
        memory->write(LCD_STATUS_ADDRESS, final_status);
        // Reset scanned OAM entries for the scanline
        scanline_intersecting_objects.clear();
    }

    std::uint8_t color_index_to_grayscale_value(std::uint8_t color_palette, std::uint8_t color_index) {
        // TODO: Really unsure if these color values are correct or not, check in documentation
        static constexpr std::array<std::uint8_t, 4> COLOR_VALUE_LOOKUP = {
            0xFF, // 0 = White
            0xAA, // 1 = Light gray
            0x55, // 2 = Dark gray
            0x00, // 3 = Black
        };
        auto color_value = (color_palette & (0b11 << (color_index * 2))) >> (color_index * 2);
        return COLOR_VALUE_LOOKUP.at(color_value);
    }

    std::uint8_t get_tile_color_index(std::uint16_t row_colors, std::uint8_t pixel) {
        std::uint8_t higher_bits = ((row_colors & 0xFF00) >> 8);
        std::uint8_t lower_bits = (row_colors & 0xFF);
        // High bits are the first pixels so pixel 0 is the 7th bit
        std::uint8_t shift = 7 - pixel;
        // Somewhat confusingly the high byte gives the lower bit of the returned color index
        return (((lower_bits & (1 << shift)) >> shift) << 1) | ((higher_bits & (1 << shift)) >> shift);
    }

    void PPU::draw_pixel() {
        // There is a 12 dot penalty due to tile fetching at the beginning of draw
        static constexpr std::uint8_t DRAW_START_PENALTY_DOTS = 12;
        if (current_mode_dots <= DRAW_START_PENALTY_DOTS) {
            return;
        }
        // We are storing background color index separately as it makes it easier
        // to decide if an object pixel with low priority should be drawn or not
        std::uint8_t background_color_index = 0;
        std::uint8_t pixel_color = 0;

        // TODO: We are currently ignoring some of LCD control data (such as OBJ size)
        static constexpr std::uint16_t LCD_CONTROL_BYTE_ADDRESS = 0xFF40;
        std::uint8_t control_byte = memory->read(LCD_CONTROL_BYTE_ADDRESS);
        bool is_background_and_window_enabled = (control_byte & 0b1) != 0;

        // Draw window and background
        if (is_background_and_window_enabled) {
            static constexpr std::uint16_t BACKGROUND_COLOR_PALETTE_ADDRESS = 0xFF47;
            static constexpr std::uint16_t WY_ADDRESS = 0xFF4A;
            static constexpr std::uint16_t WX_ADDRESS = 0xFF4B;
            bool is_window_enabled = (control_byte & 0b00100000) != 0;
            std::uint8_t background_color_palette = memory->read(BACKGROUND_COLOR_PALETTE_ADDRESS);
            std::uint8_t window_start_y = memory->read(WY_ADDRESS);
            std::uint16_t window_start_x = static_cast<std::int16_t>(memory->read(WX_ADDRESS)) - 7;
            if (is_window_enabled &&
                current_scanline >= window_start_y &&
                current_column >= window_start_x) {
                background_color_index = fetch_window_color_index(control_byte);
                pixel_color = color_index_to_grayscale_value(background_color_palette, background_color_index);
            }
            else {
                background_color_index = fetch_background_color_index(control_byte);
                pixel_color = color_index_to_grayscale_value(background_color_palette, background_color_index);
            }
        }

        // Draw objects
        bool is_obj_rendering_enabled = (control_byte & 0b00000010) != 0;
        if (is_obj_rendering_enabled) {
            const auto obj_pixel_info = fetch_object_pixel_info();
            if (obj_pixel_info.has_value() &&
                obj_pixel_info->color_idx != 0 &&
                (!obj_pixel_info->draw_below_background || background_color_index == 0)) {
                static constexpr std::uint16_t OBJECT_PALETTE_ADDRESS = 0xFF48;
                std::uint8_t object_palette = memory->read(OBJECT_PALETTE_ADDRESS + obj_pixel_info->palette_idx);
                pixel_color = color_index_to_grayscale_value(object_palette, obj_pixel_info->color_idx);
            }
        }

        // Scanline is guaranteed to be within [0, 143] during draw mode
        const auto pixel_idx = current_scanline * LCD_WIDTH + current_column; 
        frame[pixel_idx] = pixel_color;
        ++current_column;
    }

    void PPU::execute_oam_scan() {
        // OAM search is spread out over 80 dots for 40 OAM entries - this means that every other dot we should
        // check the current object for intersection if we haven't already reached the limit (10) of intersecting OBJs.
        if (current_mode_dots % 2 == 0 ||
            scanline_intersecting_objects.size() == MAX_INTERSECTING_OBJECTS_PER_SCANLINE) {
            return;
        }
        static constexpr std::size_t OAM_ENTRY_SIZE = sizeof(OAMEntry);
        static constexpr std::uint16_t OAM_START_ADDR = 0xFE00;

        // Load the current OAM entry
        OAMEntry entry;
        // Since OAM scan dots are [1, 80] this has an upper bound of 40 so comfortably fits in uint8_t - also since we are
        // only entering this on odd dots this is guaranteed to round down to [0, 39] for indexing OAM entries.
        const auto entry_index = static_cast<std::uint8_t>(current_mode_dots / 2);
        memory->copy_from(OAM_START_ADDR + entry_index * OAM_ENTRY_SIZE, reinterpret_cast<std::uint8_t*>(&entry), OAM_ENTRY_SIZE);

        // Check for intersection
        static const std::uint16_t LCD_CONTROL_BYTE_ADDRESS = 0xFF40;
        const auto lcd_control = memory->read(LCD_CONTROL_BYTE_ADDRESS);
        std::uint8_t obj_size = (lcd_control & 0b00000100) == 0 ? 8 : 16;
        std::int16_t start_y = static_cast<std::int16_t>(entry.y) - 16;
        // Note that since start_y is inclusive we are adding 1 less pixel (7/15) instead of 8/16 in order to get one tile worth of pixels
        std::int16_t end_y = start_y + obj_size - 1;
        if (start_y > current_scanline || end_y < current_scanline) {
            return;
        }
        scanline_intersecting_objects.push_back(ScannedOAMEntry{
            .index = entry_index,
            .y_position = entry.y
        });
    }

    std::uint8_t PPU::fetch_background_color_index(std::uint8_t control_byte) const {
        enum class TileAddressingMode : std::uint8_t {
            SIGNED = 0,
            UNSIGNED = 1
        };
        auto addressing_mode = static_cast<TileAddressingMode>((control_byte & 0b00010000) >> 4);
        std::uint16_t tile_data_area = (addressing_mode == TileAddressingMode::UNSIGNED)
            ? 0x8000
            : 0x9000;
        std::uint16_t tile_map_area = ((control_byte & 0b00001000) != 0)
            ? 0x9C00
            : 0x9800;

        // Compute tilemap index and fetch the corresponding tile data from VRAM
        const auto tilemap_info = compute_background_tilemap_info();
        TileEntry tile_entry;
        static constexpr auto tile_entry_size = sizeof(TileEntry::value_type) * std::tuple_size_v<TileEntry>;
        if (addressing_mode == TileAddressingMode::UNSIGNED) {
            std::uint8_t tile_idx = memory->read(tile_map_area + tilemap_info.tile_idx);
            memory->copy_from(tile_data_area + tile_idx * tile_entry_size, reinterpret_cast<std::uint8_t*>(tile_entry.data()), tile_entry_size);
        }
        else {
            std::int8_t tile_idx = static_cast<std::int8_t>(memory->read(tile_map_area + tilemap_info.tile_idx));
            memory->copy_from(tile_data_area + tile_idx * tile_entry_size, reinterpret_cast<std::uint8_t*>(tile_entry.data()), tile_entry_size);
        }

        // Get the corresponding row in the tile and compute the color index at X
        std::uint16_t row_colors = tile_entry[tilemap_info.y_offset];
        return get_tile_color_index(row_colors, tilemap_info.x_offset);
    }

    std::uint8_t PPU::fetch_window_color_index([[maybe_unused]] std::uint8_t control_byte) const {
        /*
        std::uint16_t window_tilemap = ((control_byte & 0b01000000) != 0)
            ? 0x9C00
            : 0x9800;
        */
        // TODO: Implement
        return 0;
    }

    std::optional<PPU::ObjectPixelInfo> PPU::fetch_object_pixel_info() const {
        static constexpr std::uint16_t OBJ_TILE_START_ADDR = 0x8000;
        static constexpr std::uint16_t OAM_START_ADDR = 0xFE00;
        static constexpr std::uint8_t NUM_OAM_ENTRIES = 40;
        static const std::uint16_t LCD_CONTROL_BYTE_ADDRESS = 0xFF40;
        const auto lcd_control = memory->read(LCD_CONTROL_BYTE_ADDRESS);

        std::uint8_t obj_size = (lcd_control & 0b00000100) == 0 ? 8 : 16;
        // We already done OAM scan but out of all the intersecting objects for this scanline we need to
        // select the one that intersects the current column and has the lowest start X value.
        struct ObjectWithTile {
            OAMEntry object;
            TileEntry tile;
            std::int16_t start_x;
            std::int16_t start_y;
            std::int16_t end_x;
            std::int16_t end_y;
        };
        std::vector<ObjectWithTile> intersecting_objects;
        for (const auto& scanned_entry : scanline_intersecting_objects) {
            static constexpr auto object_entry_size = sizeof(OAMEntry);
            // Copy object attribute mapping entry from VRAM
            OAMEntry object_entry;
            memory->copy_from(OAM_START_ADDR + scanned_entry.index * object_entry_size, reinterpret_cast<std::uint8_t*>(&object_entry), object_entry_size);
            // Overwrite Y position with the value from scan as that is locked
            object_entry.y = scanned_entry.y_position;

            std::int16_t start_x = static_cast<std::int16_t>(object_entry.x) - 8;
            std::int16_t end_x = start_x + 7;
            // Skip the object if it does not intersect the current column - we only need to check X since scanline intersection is already guaranteed
            if (start_x > current_column || end_x < current_column) {
                continue;
            }
            std::int16_t start_y = static_cast<std::int16_t>(object_entry.y) - 16;
            std::int16_t end_y = start_y + obj_size - 1;

            // Copy tile data corresponding to OAM
            static constexpr auto tile_entry_size = sizeof(TileEntry::value_type) * std::tuple_size_v<TileEntry>;
            TileEntry tile_entry;
            std::uint16_t tile_offset = object_entry.tile_index * tile_entry_size;
            memory->copy_from(OBJ_TILE_START_ADDR + tile_offset, reinterpret_cast<std::uint8_t*>(tile_entry.data()), tile_entry_size);
            
            intersecting_objects.emplace_back(ObjectWithTile{
                .object = std::move(object_entry),
                .tile = std::move(tile_entry),
                .start_x = start_x,
                .start_y = start_y,
                .end_x = end_x,
                .end_y = end_y
            });
            if (intersecting_objects.size() >= MAX_INTERSECTING_OBJECTS_PER_SCANLINE) {
                break;
            }
        }
        if (intersecting_objects.empty()) {
            return std::nullopt;
        }
        std::stable_sort(intersecting_objects.begin(), intersecting_objects.end(), [](const auto& first, const auto& second) {
            return first.object.x < second.object.x;
        });
        const auto& object = intersecting_objects[0];
        bool flip_vertically = (object.object.flags & 0b01000000) != 0;
        bool flip_horizontally = (object.object.flags & 0b00100000) != 0;
        std::uint16_t row_colors = object.tile[flip_vertically
            ? (obj_size - 1 - (current_scanline - object.start_y))
            : (current_scanline - object.start_y)];
        std::uint8_t color_idx = get_tile_color_index(row_colors, flip_horizontally
            ? static_cast<std::uint8_t>(obj_size - 1 - (current_column - object.start_x))
            : static_cast<std::uint8_t>(current_column - object.start_x));
        std::uint8_t palette_idx = (object.object.flags & 0b00010000) >> 4;
        bool draw_below_background = (object.object.flags & 0b10000000) != 0;
        return ObjectPixelInfo {
            .color_idx = color_idx,
            .palette_idx = palette_idx,
            .draw_below_background = draw_below_background
        };
    }

    PPU::BackgroundTileMapInfo PPU::compute_background_tilemap_info() const {
        // Background tilemap index is not trivial to calculate due to the
        // posibility of scrolling the background using SCX and SCY.
        static constexpr std::uint16_t SCY_ADDRESS = 0xFF42;
        static constexpr std::uint16_t SCX_ADDRESS = 0xFF43;
        std::uint8_t scy = memory->read(SCY_ADDRESS);
        std::uint8_t scx = memory->read(SCX_ADDRESS);
        // Note that the background spans 256x256 pixels
        std::uint8_t final_y = current_scanline + scy;
        std::uint8_t final_x = current_column + scx;
        // From the final coordinates we can calculate the tile indices
        std::uint8_t tile_y = final_y / 8;
        std::uint8_t tile_x = final_x / 8;
        std::uint16_t tile_idx = tile_y * 32 + tile_x;
        return BackgroundTileMapInfo {
            .tile_idx = tile_idx,
            .x_offset = static_cast<std::uint8_t>(final_x % 8),
            .y_offset = static_cast<std::uint8_t>(final_y % 8)
        };
    }

}