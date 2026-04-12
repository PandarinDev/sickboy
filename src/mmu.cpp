#include "mmu.h"

namespace sickboy {

    MMU::MMU() : cartridge(), ram({}), boot_rom({}), boot_rom_enabled(true), had_interrupt_request(false) {}

    std::uint8_t MMU::read(std::uint16_t address) const {
        return ram.at(address);
    }

    void MMU::write(std::uint16_t address, std::uint8_t value) {
        ram.at(address) = value;
    }

    void MMU::direct_write(std::uint16_t address, std::uint8_t value) {
        ram[address] = value;
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
    
    bool MMU::poll_interrupt_request() {
        const auto result = had_interrupt_request;
        had_interrupt_request = false;
        return result;
    }

    void MMU::set_cartridge(std::unique_ptr<Cartridge> new_cartridge) {
        cartridge = std::move(new_cartridge);
    }

}