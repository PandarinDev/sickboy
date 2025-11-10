#include "system.h"
#include "utils.h"

#include <iostream>

namespace sickboy {

    System::System(const std::filesystem::path& cartridge_path) :
        memory(std::make_shared<MMU>()), cpu(memory), ppu(memory), stopped(false) {
        // Load boot ROM contents
        {
            auto rom_contents = FileUtils::read_binary("assets/dmg_boot.bin");
            if (rom_contents.size() != 256) {
                throw std::runtime_error("Invalid size of boot ROM.");
            }
            memory->copy_to_boot_rom(rom_contents.data());
        }

        // Load cartridge data
        {
            static constexpr auto max_cartridge_size = 0x8000;
            auto cartridge_contents = FileUtils::read_binary(cartridge_path);
            auto cartridge_size = cartridge_contents.size();
            if (cartridge_size > max_cartridge_size) {
                cartridge_size = max_cartridge_size;
                std::cerr << "WARNING: Cartridge data is getting truncated because it is too large." << std::endl;
            }
            memory->copy_to(0, cartridge_contents.data(), cartridge_size);
        }
    }

    bool System::tick() {
        // If the system is currently stopped (by a previous STOP instruction) we need to only poll inputs
        // If any of the buttons are pressed we need to resume the system exactly where we left off
        if (stopped) {
            // TODO: Add input handling and waking up on input
            return false;
        }
        // First tick the CPU then catch up the PPU by giving it an equivalent amount of cycles (dots)
        // This is of course not entirely accurate since these subsystems are meant to run asynchronously
        // so in an accurate emulation the PPU might read something from the CPU in-between instructions.
        auto used_cycles = cpu.tick();
        // Check if a stop was requested
        if (cpu.stop_requested) {
            stopped = true;
            cpu.stop_requested = false;
            return false;
        }
        if (!ppu.is_lcd_and_ppu_enabled()) {
            // Clear PPU mode in LCD status register when PPU is disabled
            static constexpr std::uint16_t LCD_STATUS_ADDRESS = 0xFF41;
            std::uint8_t lcd_status = memory->read(LCD_STATUS_ADDRESS);
            memory->write(LCD_STATUS_ADDRESS, lcd_status & ~0b11);
            return false;
        }
        auto should_render_new_frame = false;
        for (std::uint8_t i = 0; i < used_cycles; ++i) {
            should_render_new_frame |= ppu.tick();
        }
        return should_render_new_frame;
    }

}