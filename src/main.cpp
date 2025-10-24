#include "cpu.h"
#include "utils.h"

#include <iostream>

using namespace sickboy;

int main() {
    auto memory = std::make_shared<MMU>();
    CPU cpu(memory);
    // Read the boot ROM into RAM
    auto rom_contents = FileUtils::read_binary("assets/dmg_boot.bin");
    if (rom_contents.size() != 256) {
        throw std::runtime_error("Invalid size of boot ROM.");
    }
    memory->copy(0, rom_contents.data(), rom_contents.size());
    try {
        auto lookup_instruction = [](std::uint8_t instruction_code) -> const Instruction& {
            auto instruction_it = CPU::instruction_set.find(instruction_code);
            if (instruction_it == CPU::instruction_set.cend()) {
                throw std::runtime_error("Unimplemented CPU instruction '" + StringUtils::to_hex(instruction_code) + "', cannot continue.");
            }
            return instruction_it->second;
        };
        auto lookup_prefixed_instruction = [](std::uint8_t instruction_code) -> const Instruction& {
            auto instruction_it = CPU::prefixed_instruction_set.find(instruction_code);
            if (instruction_it == CPU::prefixed_instruction_set.cend()) {
                throw std::runtime_error("Unimplemented prefixed CPU instruction '" + StringUtils::to_hex(instruction_code) + "', cannot continue.");
            }
            return instruction_it->second;
        };

        // TODO: We are currently getting into an infinite loop because boot ROM is trying to wait for LCD status register 0xFF44 to be 144 to indicate
        // that currently a VBlank period is going on. Since a) there is no MMU implementation, b) there is no LCD/GPU implementation this never happens
        while (true) {
            // Fetch instruction
            auto currently_prefixed = cpu.is_prefixed;
            auto instruction_code = cpu.memory->read(cpu.registers.pc);
            const auto& instruction = currently_prefixed
                ? lookup_prefixed_instruction(instruction_code)
                : lookup_instruction(instruction_code);
            [[maybe_unused]]
            auto additional_cycles = instruction.implementation(cpu);
            // We upcast and then downcast our new PC address to protect against overflow - on narrowing static cast C++
            // will modulo the PC address which is the exact behavior (wrapping around PC) of the DMG CPU in this case.
            cpu.registers.pc = static_cast<std::uint16_t>(static_cast<std::uint32_t>(cpu.registers.pc) + static_cast<std::uint32_t>(instruction.length));
            // If the cycle started out prefixed reset the prefix
            if (currently_prefixed) {
                cpu.is_prefixed = false;
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Encountered exception: " << e.what() << std::endl;
        return 1;
    }
}