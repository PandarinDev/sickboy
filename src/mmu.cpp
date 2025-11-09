#include "mmu.h"

namespace sickboy {

    MMU::MMU() :
        ram({}), boot_rom({}), boot_rom_enabled(true), had_interrupt_request(false),
        bank_lower(1), bank_upper(0), bank_mode(BankingMode::SIMPLE) {}

    std::uint8_t MMU::read(std::uint16_t address) const {
        static constexpr std::uint16_t JOYPAD_ADDRESS = 0xFF00;

        // TODO: Add address translation based on bank mode and bank value ((higher << 5) | lower).

        // While boot ROM is enabled all reads between 0x00-0xFF go to the boot ROM
        if (boot_rom_enabled && address <= 0xFF) {
            return boot_rom[address];
        }
        // Echo RAM redirects all reads from 0xE000-0xFDFF to C000-DDFF
        if (address >= 0xE000 && address <= 0xFDFF) {
            return ram[address - 0x2000];
        }
        // When reading joypad register currently always report no buttons pressed
        // TODO: Wire up input handling here to the corresponding bits in the register
        if (address == JOYPAD_ADDRESS) {
            return ram[address] | 0b00001111;
        }
        return ram[address];
    }

    void MMU::write(std::uint16_t address, std::uint8_t value) {
        static constexpr std::uint16_t BOOT_ROM_DISABLE_ADDRESS = 0xFF50;
        static constexpr std::uint16_t OAM_DMA_COPY_ADDRESS = 0xFF46;
        static constexpr std::uint16_t INTERRUPT_REQUEST_ADDRESS = 0xFF0F;

        // Handle ROM bank registers
        if (address >= 0x2000 && address < 0x4000) {
            // TODO: Bank masking should depend on the number of banks the cartridge has
            // E.g. a 256kB cartridge should use only a 4 bit bank mask, not a 5 bit one.
            std::uint8_t bank_value = value & 0b00011111;
            // Treat bank#0 as bank#1
            if (bank_value == 0) bank_value = 1;
            bank_lower = bank_value;
            return;
        }
        else if (address >= 4000 && address < 6000) {
            bank_upper = value & 0b11;
            return;
        }
        else if (address >= 6000 && address < 8000) {
            bank_mode = static_cast<BankingMode>(value & 0b1);
            return;
        }

        ram[address] = value;
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
        else if (address == INTERRUPT_REQUEST_ADDRESS) {
            had_interrupt_request = true;
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
    
    bool MMU::poll_interrupt_request() {
        const auto result = had_interrupt_request;
        had_interrupt_request = false;
        return result;
    }

}