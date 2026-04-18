#include "system.h"
#include "utils.h"
#include "cartridge.h"

namespace sickboy {

    System::System(
        const std::filesystem::path& boot_rom_path,
        const std::filesystem::path& cartridge_path) :
        memory(std::make_shared<MMU>()), timer(memory),
        cpu(memory), ppu(memory), stopped(false) {
        // Load boot ROM contents
        {
            auto rom_contents = FileUtils::read_binary(boot_rom_path);
            if (rom_contents.size() != 256) {
                throw std::runtime_error("Invalid size of boot ROM.");
            }
            memory->copy_to_boot_rom(rom_contents.data());
        }

        // Load cartridge data
        {
            auto cartridge_contents = FileUtils::read_binary(cartridge_path);
            auto cartridge = CartridgeUtils::create_cartridge(cartridge_contents);
            memory->set_cartridge(std::move(cartridge));
        }
    }

    bool System::tick() {
        timer.tick_emulator();
        // If the system is currently stopped (by a previous STOP instruction) we need to only poll inputs
        // If any of the buttons are pressed we need to resume the system exactly where we left off
        const auto no_buttons_pressed = [this]() {
            static constexpr std::uint16_t JOYPAD_INPUT_ADDRESS = 0xFF00;
            const std::uint8_t joypad_value = memory->read(JOYPAD_INPUT_ADDRESS);
            // We only care about the lower nibble - all 1s mean no buttons pressed
            return (joypad_value & 0x0F) == 0x0F;
        };
        if (stopped) {
            if (no_buttons_pressed()) {
                return false;
            }
            else {
                stopped = false;
            }
        }
        // First tick the CPU then catch up the PPU by giving it an equivalent amount of cycles (dots)
        // This is of course not entirely accurate since these subsystems are meant to run asynchronously
        // so in an accurate emulation the PPU might read something from the CPU in-between instructions.
        const auto used_t_cycles = cpu.tick();
        const auto used_m_cycles = used_t_cycles / 4;
        // Check if a stop was requested
        if (cpu.stop_requested) {
            stopped = true;
            cpu.stop_requested = false;
            timer.reset_divider_register();
            return false;
        }
        // Timer should be ticked after the CPU with M-cycles
        for (std::uint8_t i = 0; i < used_m_cycles; ++i) {
            timer.tick_system();
        }
        if (!ppu.is_lcd_and_ppu_enabled()) {
            // Clear PPU mode in LCD status register when PPU is disabled
            static constexpr std::uint16_t LCD_STATUS_ADDRESS = 0xFF41;
            std::uint8_t lcd_status = memory->read(LCD_STATUS_ADDRESS);
            memory->write(LCD_STATUS_ADDRESS, lcd_status & ~0b11);
            return false;
        }
        auto should_render_new_frame = false;
        for (std::uint8_t i = 0; i < used_t_cycles; ++i) {
            should_render_new_frame |= ppu.tick();
        }
        return should_render_new_frame;
    }

}