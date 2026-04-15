#include "mmu.h"

namespace sickboy {

    MMU::MMU() : cartridge(), ram({}), boot_rom({}), boot_rom_enabled(true), had_interrupt_request(false) {}

    std::uint8_t MMU::read(std::uint16_t address) const {
        // While boot ROM is enabled all reads between 0x00-0xFF go to the boot ROM
        if (boot_rom_enabled && address <= 0xFF) {
            return boot_rom.at(address);
        }

        // Cartridge reads should be handled by the cartridge mapper
        if (address < 0x8000 || (address >= 0xA000 && address <= 0xBFFF)) {
            return cartridge->read(address);
        }

        // Echo RAM redirects all reads from 0xE000-0xFDFF to C000-DDFF
        if (address >= 0xE000 && address <= 0xFDFF) {
            return ram.at(address - 0x2000);
        }

        // If trying to read unsupported CGB registers return 0xFF
        if (address == 0xFF4C || address == 0xFF4D) {
            return 0xFF;
        }
        return ram.at(address);
    }

    void MMU::write(std::uint16_t address, std::uint8_t value) {
        static constexpr std::uint16_t BOOT_ROM_DISABLE_ADDRESS = 0xFF50;
        static constexpr std::uint16_t OAM_DMA_COPY_ADDRESS = 0xFF46;
        static constexpr std::uint16_t INTERRUPT_REQUEST_ADDRESS = 0xFF0F;
        static constexpr std::uint16_t TIMER_DIVIDER_ADDRESS = 0xFF04;

        // Cartridge writes should be handled by the cartridge mapper
        if (!boot_rom_enabled && (address < 0x8000 || (address >= 0xA000 && address <= 0xBFFF))) {
            cartridge->write(address, value);
            return;
        }

        const auto is_write_to_vram = address >= 0x8000 && address < 0xA000;
        const auto is_write_to_wram = address >= 0xC000 && address < 0xE000;
        const auto is_write_to_oam = address >= 0xFE00 && address < 0xFEA0;
        const auto is_write_to_high_region = address >= 0xFF00;
        if (is_write_to_vram || is_write_to_wram || is_write_to_oam || is_write_to_high_region) {
            direct_write(address, value);
        }
        // Handle writes that disable the boot ROM
        if (address == BOOT_ROM_DISABLE_ADDRESS) {
            set_boot_rom_enabled(false);
        }
        // Handle writes that trigger an OAM DMA copy
        else if (address == OAM_DMA_COPY_ADDRESS) {
            // TODO: This is wildly inaccurate - in reality this copy takes 160 memory cycles or 640 master clock cycles
            // During the period of the copy RAM is unavailable and the program can only read from HRAM. This should
            // be implemented by adding ticking to the MMU and locking read/write operations for 640 master clock cycles.
            std::uint16_t source = value << 8;
            static constexpr std::uint16_t OAM_MEMORY = 0xFE00;
            copy_to(OAM_MEMORY, ram.data() + source, 160);
        }
        // Handle writes that trigger timer divider reset
        else if (address == TIMER_DIVIDER_ADDRESS) {
            direct_write(address, 0x00);
        }
        else if (address == INTERRUPT_REQUEST_ADDRESS) {
            had_interrupt_request = true;
        }
    }

    void MMU::direct_write(std::uint16_t address, std::uint8_t value) {
        ram.at(address) = value;
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