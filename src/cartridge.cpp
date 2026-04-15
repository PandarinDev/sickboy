#include "cartridge.h"
#include "mmu.h"

#include <string>
#include <stdexcept>
#include <algorithm>

namespace sickboy {

    static constexpr std::size_t ROM_BANK_SIZE = 0x4000; // 16KiB
    static constexpr std::size_t RAM_BANK_SIZE = 0x2000; // 8KiB

    CartridgeMBC0::CartridgeMBC0(const std::vector<std::uint8_t>& contents) : rom({}) {
        if (contents.size() > (2 * ROM_BANK_SIZE)) {
            throw std::runtime_error("Cartridge of type MBC0 exceeds size limit of 32kB.");
        }
        std::memcpy(rom.data(), contents.data(), contents.size());
    }

    std::uint8_t CartridgeMBC0::read(std::uint16_t address) const {
        return rom.at(address);
    }

    void CartridgeMBC0::write(std::uint16_t, std::uint8_t) {
        // Writes are ignored for MBC0
    }

    CartridgeMBC1::CartridgeMBC1(
        const std::vector<std::uint8_t>& contents,
        std::uint32_t rom_size,
        std::uint32_t ram_size) :
        rom(), ram(), ram_enabled(false), rom_bank_number(0), ram_bank_number(0), banking_mode(BankingMode::SIMPLE) {
        // Initialize ROM
        if (contents.size() > rom_size) {
            throw std::runtime_error("Cartridge size " + std::to_string(contents.size()) +
                " exceeds MBC1 ROM size of " + std::to_string(rom_size));
        }
        rom.resize(rom_size);
        std::memcpy(rom.data(), contents.data(), contents.size());

        // Initialize RAM
        ram.resize(ram_size);
    }

    std::uint8_t CartridgeMBC1::read(std::uint16_t address) const {
        // TODO: Add support for MBC1M addressing - though not super relevant for legit ROMs.
        // We potentially go above 16 bit addressing here (21 bits total for large ROMs) so we need to use uint32_t here
        const std::uint32_t rom_size_mask = static_cast<std::uint32_t>(rom.size() - 1);
        // ROM bank 0
        if (address < ROM_BANK_SIZE) {
            // For simple banking mode we do not need to mask the input address here since the
            // condition guarantees only bits [0,13] are set, however, for advanced banking we
            // want to use only the bits that are relevant for our bank size (throw away the upper bits if needed).
            return (banking_mode == BankingMode::SIMPLE)
                ? rom.at(address)
                : rom.at(((ram_bank_number << 19) | address) & rom_size_mask);
        }
        // Switchable ROM bank
        else if (address >= 0x4000 && address < 0x8000) {
            std::uint16_t masked_address = address & 0x3FFF;
            // Reading from ROM bank 0 in this address range is always corrected to ROM bank 1
            return (rom_bank_number == 0)
                ? rom.at(((ram_bank_number << 19) | (1 << 14) | masked_address) & rom_size_mask)
                : rom.at(((ram_bank_number << 19) | (rom_bank_number << 14) | masked_address) & rom_size_mask);
        }
        // Switchable RAM bank
        else if (address >= 0xA000 && address < 0xC000) {
            // Return garbage (typically 0xFF) when RAM is disabled
            if (!ram_enabled) {
                return 0xFF;
            }
            std::uint16_t masked_address = address & 0x1FFF;
            return (banking_mode == BankingMode::SIMPLE)
                ? ram.at(masked_address % ram.size())
                : ram.at(((ram_bank_number << 13) | masked_address) % ram.size());
        }
        throw std::runtime_error("Read with invalid address " + std::to_string((int) address) + " in MBC1 cartridge.");
    }

    void CartridgeMBC1::write(std::uint16_t address, std::uint8_t value) {
        // RAM enable
        if (address < 0x2000) {
            static constexpr std::uint8_t ENABLE_RAM_VALUE = 0xA;
            ram_enabled = ((value & 0x0F) == ENABLE_RAM_VALUE);
        }
        // ROM bank number (5 bits + 2 bits from RAM bank)
        else if (address >= 0x2000 && address < 0x4000) {
            rom_bank_number = value & 0b11111;
        }
        // RAM bank number (2 bits)
        else if (address >= 0x4000 && address < 0x6000) {
            ram_bank_number = (value & 0b11);
        }
        // Banking mode select (1 bit)
        else if (address >= 0x6000 && address < 0x8000) {
            banking_mode = static_cast<BankingMode>(value & 0b1);
        }
        // Writes to RAM
        else if (address >= 0xA000 && address < 0xC000) {
            if (ram_enabled) {
                std::uint16_t masked_address = address & 0x1FFF;
                std::uint16_t ram_offset = (banking_mode == BankingMode::SIMPLE)
                    ? masked_address
                    : (ram_bank_number << 13) | masked_address;
                ram.at(ram_offset % ram.size()) = value;
            }
        }
    }

    CartridgeHeader CartridgeUtils::parse_cartridge_header(const std::vector<std::uint8_t>& cartridge_data) {
        static constexpr std::size_t CARTRIDGE_TYPE_OFFSET = 0x147;
        static constexpr std::size_t ROM_EXPONENT_OFFSET = 0x148;
        static constexpr std::size_t RAM_TYPE_OFFSET = 0x149;
        if (cartridge_data.size() < RAM_TYPE_OFFSET) {
            throw std::runtime_error("Invalid cartridge - data not long enough to read cartridge header.");
        }
        const auto mbc_mode = cartridge_type_to_mbc_mode(cartridge_data.at(CARTRIDGE_TYPE_OFFSET));
        const auto rom_size = rom_exponent_to_rom_size(cartridge_data.at(ROM_EXPONENT_OFFSET));
        const auto ram_size = ram_type_to_ram_size(cartridge_data.at(RAM_TYPE_OFFSET));
        return CartridgeHeader {
            .mbc_mode = mbc_mode,
            .rom_size = rom_size,
            .ram_size = ram_size
        };
    }

    std::unique_ptr<Cartridge> CartridgeUtils::create_cartridge(const std::vector<std::uint8_t>& contents) {
        const auto header = parse_cartridge_header(contents);
        switch (header.mbc_mode) {
            case MBCMode::MBC0: return std::make_unique<CartridgeMBC0>(contents);
            case MBCMode::MBC1: return std::make_unique<CartridgeMBC1>(contents, header.rom_size, header.ram_size);
            default: throw std::runtime_error("Unsupported MBCMode in create_cartridge: " + std::to_string((int) header.mbc_mode));
        }
    }

    MBCMode CartridgeUtils::cartridge_type_to_mbc_mode(std::uint8_t cartridge_type) {
        switch (cartridge_type) {
            case 0x00:
                return MBCMode::MBC0;
            case 0x01:
            case 0x02:
            case 0x03:
                return MBCMode::MBC1;
            case 0x05:
            case 0x06:
                return MBCMode::MBC2;
            // TODO: Add support for MMM01
            case 0x0F:
            case 0x10:
            case 0x11:
            case 0x12:
            case 0x13:
                return MBCMode::MBC3;
            case 0x19:
            case 0x1A:
            case 0x1B:
            case 0x1C:
            case 0x1D:
            case 0x1E:
                return MBCMode::MBC5;
            case 0x20:
                return MBCMode::MBC6;
            case 0x22:
                return MBCMode::MBC7;
            // TODO: Add support for pocket camera, bandai tama5, HuC3 and HuC1
            default: throw std::runtime_error("Unknown cartridge type '" +
                std::to_string((int) cartridge_type) + "', cannot map to MBC mode.");
        }
    }

    std::uint32_t CartridgeUtils::rom_exponent_to_rom_size(std::uint8_t rom_exponent) {
        return (2 * ROM_BANK_SIZE) * static_cast<std::uint16_t>(1 << rom_exponent); // 32KiB * 2^rom_exponent
    }

    std::uint32_t CartridgeUtils::ram_type_to_ram_size(std::uint8_t ram_type) {
        switch (ram_type) {
            case 0x00: return 0 * RAM_BANK_SIZE;
            case 0x01: return 0 * RAM_BANK_SIZE;
            case 0x02: return 1 * RAM_BANK_SIZE;
            case 0x03: return 4 * RAM_BANK_SIZE;
            case 0x04: return 16 * RAM_BANK_SIZE;
            case 0x05: return 8 * RAM_BANK_SIZE;
            default: throw std::runtime_error("Unknown RAM type " + std::to_string((int) ram_type) + ".");
        }
    }

}