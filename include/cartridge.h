#pragma once

#include <array>
#include <vector>
#include <cstdint>
#include <memory>

namespace sickboy {

    enum class MBCMode : std::uint8_t {
        MBC0,
        MBC1,
        MBC2,
        MBC3,
        MBC5,
        MBC6,
        MBC7
    };

    struct Cartridge {

        virtual ~Cartridge() = default;
        virtual std::uint8_t read(std::uint16_t address) const = 0;
        virtual void write(std::uint16_t address, std::uint8_t value) = 0;

    };

    struct CartridgeMBC0 : Cartridge {

        CartridgeMBC0(const std::vector<std::uint8_t>& contents);

        std::uint8_t read(std::uint16_t address) const override;
        void write(std::uint16_t address, std::uint8_t value) override;

    private:

        std::array<std::uint8_t, 0x8000> rom;

    };

    struct CartridgeMBC1 : Cartridge {

        CartridgeMBC1(
            const std::vector<std::uint8_t>& contents,
            std::uint32_t rom_size,
            std::uint32_t ram_size);

        std::uint8_t read(std::uint16_t address) const override;
        void write(std::uint16_t address, std::uint8_t value) override;

    private:

        enum class BankingMode : std::uint8_t {
            SIMPLE = 0,
            ADVANCED = 1
        };

        std::vector<std::uint8_t> rom;
        std::vector<std::uint8_t> ram;
        bool ram_enabled;
        std::uint8_t rom_bank_number;
        std::uint8_t ram_bank_number;
        BankingMode banking_mode;

        const std::uint8_t* get_rom_bank_address(std::uint8_t bank, std::uint16_t address) const;
        const std::uint8_t* get_ram_bank_address(std::uint8_t bank, std::uint16_t address) const;
        std::uint8_t* get_ram_bank_address(std::uint8_t bank, std::uint16_t address);

    };

    struct CartridgeHeader {
        MBCMode mbc_mode;
        std::uint32_t rom_size;
        std::uint32_t ram_size;
    };

    struct CartridgeUtils {

        CartridgeUtils() = delete;

        static std::unique_ptr<Cartridge> create_cartridge(const std::vector<std::uint8_t>& contents);

    private:

        static CartridgeHeader parse_cartridge_header(const std::vector<std::uint8_t>& cartridge_data);
        static MBCMode cartridge_type_to_mbc_mode(std::uint8_t cartridge_type);
        static std::uint32_t rom_exponent_to_rom_size(std::uint8_t rom_exponent);
        static std::uint32_t ram_type_to_ram_size(std::uint8_t ram_type);

    };

}