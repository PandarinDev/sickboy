#pragma once

#include <array>
#include <cstdint>

namespace sickboy {

    struct MMU {

        std::uint8_t read(std::uint16_t address) const;
        void write(std::uint16_t address, std::uint8_t value);
        
        // Never use this for writing data that could trigger memory mapped I/O
        void copy(std::uint16_t address, const std::uint8_t* data, std::size_t length);

    private:

        std::array<std::uint8_t, 0xFFFF> ram;

    };

}