#include "mmu.h"

namespace sickboy {

    MMU::MMU() : ram({}), boot_rom({}), boot_rom_enabled(true) {}

    std::uint8_t MMU::read(std::uint16_t address) const {
        if (boot_rom_enabled && address <= 0xFF) {
            return boot_rom[address];
        }
        return ram[address];
    }

    void MMU::write(std::uint16_t address, std::uint8_t value) {
        ram[address] = value;
        // Writes to 0xFF50 disable the boot ROM
        if (address == 0xFF50) {
            set_boot_rom_enabled(false);
        }
    }

    void MMU::copy_to(std::uint16_t address, const std::uint8_t* data, std::size_t len) {
        std::memcpy(&ram[address], data, len);
    }

    void MMU::copy_from(std::uint16_t address, std::uint8_t* destination, std::size_t len) const {
        std::memcpy(destination, &ram[address], len);
    }

    void MMU::copy_to_boot_rom(const std::uint8_t* data) {
        std::memcpy(boot_rom.data(), data, boot_rom.size());
    }

    void MMU::set_boot_rom_enabled(bool on) {
        boot_rom_enabled = on;
    }
    
}