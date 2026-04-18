#include "cartridge.h"
#include "mmu.h"

#include <string>
#include <stdexcept>
#include <algorithm>
#include <chrono>

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
            // Max allowed number of ROM banks for MBC1 is 32, meaning that we need 5 bits for ROM bank
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
        // Otherwise we received a write with an invalid address
        else {
            throw std::runtime_error("Write with invalid address " + std::to_string((int) address) + " in MBC1 cartridge.");
        }
    }

    CartridgeMBC3::CartridgeMBC3(
        const std::vector<std::uint8_t>& contents,
        std::uint32_t rom_size,
        std::uint32_t ram_size) :
        rom(), ram(), ram_rtc_enabled(false), rom_bank_number(0),
        ram_bank_number_rtc_register(0), latch_clock_value(0xFF), rtc_data() {
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

    std::uint8_t CartridgeMBC3::read(std::uint16_t address) const {
        // We potentially go above 16 bit addressing here (21 bits total for large ROMs) so we need to use uint32_t here
        const std::uint32_t rom_size_mask = static_cast<std::uint32_t>(rom.size() - 1);
        // ROM bank 0
        if (address < ROM_BANK_SIZE) {
            return rom.at(address);
        }
        // Switchable ROM bank
        else if (address >= 0x4000 && address < 0x8000) {
            std::uint16_t masked_address = address & 0x3FFF;
            // TODO: Documentation says $20, $40 and $60 is now readable but it also says that
            // writing to ROM bank number the value 0 is still corrected to 1 - so which is it?
            return rom.at(((rom_bank_number << 14) | masked_address) & rom_size_mask);
        }
        // RAM/RTC data
        else if (address >= 0xA000 && address < 0xC000) {
            // Return garbage (typically 0xFF) when RAM/RTC is disabled
            if (!ram_rtc_enabled) {
                return 0xFF;
            }
            if (is_rtc_selected()) {
                // We have to subtract 8 to normalize the RTC address
                return rtc_data.at(ram_bank_number_rtc_register - 0x08);
            }
            else {
                // While the bank number register allows selecting RAM banks 0-7, only 0-3 are valid
                // This is because there are only 4 RAM banks physically present for MBC3, rest should be ignored
                if (ram_bank_number_rtc_register > 3) {
                    return 0xFF;
                }
                std::uint16_t masked_address = address & 0x1FFF;
                return ram.at(((ram_bank_number_rtc_register << 13) | masked_address) % ram.size());
            }
        }
        throw std::runtime_error("Read with invalid address " + std::to_string((int) address) + " in MBC3 cartridge.");
    }

    void CartridgeMBC3::write(std::uint16_t address, std::uint8_t value) {
        // RAM/RTC enable
        if (address < 0x2000) {
            static constexpr std::uint8_t ENABLE_RAM_VALUE = 0xA;
            ram_rtc_enabled = ((value & 0x0F) == ENABLE_RAM_VALUE);
        }
        // ROM bank number (7 bits + 2 bits from RAM bank)
        else if (address >= 0x2000 && address < 0x4000) {
            // Max allowed number of ROM banks for MBC3 is 128, meaning that we need 7 bits for ROM bank
            rom_bank_number = value & 0b1111111;
        }
        // RAM bank/RTC register select
        else if (address >= 0x4000 && address < 0x6000) {
            ram_bank_number_rtc_register = value & 0b1111;
        }
        // Latch clock data
        else if (address >= 0x6000 && address < 0x8000) {
            // If the latch value went 0->1 we need to populate RTC data
            if (latch_clock_value == 0 && value == 1) {
                latch_rtc_data();
            }
            latch_clock_value = value;
        }
        // Writes to RAM/RTC
        else if (address >= 0xA000 && address < 0xC000) {
            if (!ram_rtc_enabled) {
                return;
            }
            if (is_rtc_selected()) {
                // TODO: This should modify the selected register of the RTC, effectively changing time
                // Note that this should not modify the latched data but the internal reference time point
            }
            else {
                // While the bank number register allows selecting RAM banks 0-7, only 0-3 are valid
                // This is because there are only 4 RAM banks physically present for MBC3, rest should be ignored
                if (ram_bank_number_rtc_register > 3) {
                    return;
                }
                std::uint16_t masked_address = address & 0x1FFF;
                std::uint16_t ram_offset = (ram_bank_number_rtc_register << 13) | masked_address;
                ram.at(ram_offset % ram.size()) = value;
            }
        }
        // Otherwise we received a write with an invalid address
        else {
            throw std::runtime_error("Write with invalid address " + std::to_string((int) address) + " in MBC3 cartridge.");
        }
    }

    bool CartridgeMBC3::is_rtc_selected() const {
        return (ram_bank_number_rtc_register & 0b1000) != 0;
    }

    void CartridgeMBC3::latch_rtc_data() {
        // TODO: This implementation is incorrect. We should instead be calculating time relative to
        // to a reference point that is "stored" on the cartridge and keeps being incremented. This
        // is tricky, because the time should increase even when the DMG is turned off, but we want
        // it to behave well even when we allow fast-forward (time should pass 2x/3x/etc. as fast?)

        // Calculate time and day of year
        const std::chrono::zoned_time zoned_time(std::chrono::current_zone(), std::chrono::system_clock::now());
        const auto local_time = zoned_time.get_local_time();
        const auto days = std::chrono::floor<std::chrono::days>(local_time);
        std::chrono::hh_mm_ss time(local_time - days);
        std::chrono::year_month_day year_month_day(days);
        std::chrono::year_month_day first_of_year(year_month_day.year(), std::chrono::January, std::chrono::day(1));
        const auto day_of_year = (std::chrono::sys_days(year_month_day) - std::chrono::sys_days(first_of_year)).count() + 1;

        // Write back to RTC data
        rtc_data[0] = static_cast<std::uint8_t>(time.seconds().count());
        rtc_data[1] = static_cast<std::uint8_t>(time.minutes().count());
        rtc_data[2] = static_cast<std::uint8_t>(time.hours().count());
        rtc_data[3] = day_of_year & 0xFF;
        // TODO: This only accounts for the 9th bit of the day of year, no halt or carry
        rtc_data[4] = (day_of_year & 0x100) >> 8;
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
            case MBCMode::MBC3: return std::make_unique<CartridgeMBC3>(contents, header.rom_size, header.ram_size);
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