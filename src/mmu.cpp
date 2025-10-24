#include "mmu.h"

namespace sickboy {

    std::uint8_t MMU::read(std::uint16_t address) const {
        return ram[address];
    }

    void MMU::write(std::uint16_t address, std::uint8_t value) {
        ram[address] = value;
    }

    void MMU::copy(std::uint16_t address, const std::uint8_t* data, std::size_t len) {
        std::memcpy(&ram[address], data, len);
    }

}