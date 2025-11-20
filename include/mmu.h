#pragma once

#include "cartridge.h"

#include <array>
#include <memory>
#include <cstdint>

namespace sickboy {

    struct MMU {

        MMU();

        std::uint8_t read(std::uint16_t address) const;
        void write(std::uint16_t address, std::uint8_t value);
        
        // Never use this for writing data that could trigger memory mapped I/O
        void copy_to(std::uint16_t address, const std::uint8_t* data, std::size_t length);
        void copy_from(std::uint16_t address, std::uint8_t* destination, std::size_t length) const;

        void copy_to_boot_rom(const std::uint8_t* data);
        void set_boot_rom_enabled(bool on);
        bool poll_interrupt_request();
        void set_cartridge(std::unique_ptr<Cartridge> cartridge);

    private:

        std::unique_ptr<Cartridge> cartridge;
        // TODO: This is inaccurate and inefficient as this RAM size currently accounts
        // for cartridge data and cartridge RAM as well. This should be split up into
        // multiple arrays and memory mapping logic should choose the correct array
        // depending on the address range.
        std::array<std::uint8_t, 0xFFFF + 1> ram;
        std::array<std::uint8_t, 0x00FF + 1> boot_rom;
        bool boot_rom_enabled;
        bool had_interrupt_request;

    };

}