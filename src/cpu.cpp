#include "cpu.h"
#include "utils.h"

#include <stdexcept>

namespace sickboy {

    std::uint8_t& Registers::a() { return *(reinterpret_cast<std::uint8_t*>(&af) + 1); }
    std::uint8_t& Registers::f() { return *(reinterpret_cast<std::uint8_t*>(&af) + 0); }
    std::uint8_t& Registers::b() { return *(reinterpret_cast<std::uint8_t*>(&bc) + 1); }
    std::uint8_t& Registers::c() { return *(reinterpret_cast<std::uint8_t*>(&bc) + 0); }
    std::uint8_t& Registers::d() { return *(reinterpret_cast<std::uint8_t*>(&de) + 1); }
    std::uint8_t& Registers::e() { return *(reinterpret_cast<std::uint8_t*>(&de) + 0); }
    std::uint8_t& Registers::h() { return *(reinterpret_cast<std::uint8_t*>(&hl) + 1); }
    std::uint8_t& Registers::l() { return *(reinterpret_cast<std::uint8_t*>(&hl) + 0); }

    void Registers::set_flag_z(bool on) {
        static constexpr auto bit_offset = 7;
        f() = (f() & ~(1 << bit_offset)) | ((on ? 1 : 0) << bit_offset);
    }

    void Registers::set_flag_n(bool on) {
        static constexpr auto bit_offset = 6;
        f() = (f() & ~(1 << bit_offset)) | ((on ? 1 : 0) << bit_offset);
    }

    void Registers::set_flag_h(bool on) {
        static constexpr auto bit_offset = 5;
        f() = (f() & ~(1 << bit_offset)) | ((on ? 1 : 0) << bit_offset);
    }

    void Registers::set_flag_c(bool on) {
        static constexpr auto bit_offset = 4;
        f() = (f() & ~(1 << bit_offset)) | ((on ? 1 : 0) << bit_offset);
    }

    bool Registers::get_flag_z() {
        return f() & 0b10000000;
    }

    bool Registers::get_flag_n() {
        return f() & 0b01000000;
    }

    bool Registers::get_flag_h() {
        return f() & 0b00100000;
    }

    bool Registers::get_flag_c() {
        return f() & 0b00010000;
    }

    CPU::CPU(const std::shared_ptr<MMU>& memory) : registers({}), memory(memory), is_prefixed(false) {}

    std::uint8_t CPU::tick() {
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
        // Fetch instruction
        auto was_prefixed = is_prefixed;
        auto instruction_code = memory->read(registers.pc);
        const auto& instruction = is_prefixed
            ? lookup_prefixed_instruction(instruction_code)
            : lookup_instruction(instruction_code);
        auto additional_cycles = instruction.implementation(*this);
        // We upcast and then downcast our new PC address to protect against overflow - on narrowing static cast C++
        // will modulo the PC address which is the exact behavior (wrapping around PC) of the DMG CPU in this case.
        registers.pc = static_cast<std::uint16_t>(static_cast<std::uint32_t>(registers.pc) + static_cast<std::uint32_t>(instruction.length));
        // If the cycle started out prefixed reset the prefix
        if (was_prefixed) {
            is_prefixed = false;
        }

        // Return the number of master clock cycles used
        return instruction.cycles + additional_cycles;
    }

    std::uint8_t r8_get_value(CPU& cpu, std::uint8_t reg_code) {
        switch (reg_code) {
            case 0: return cpu.registers.b();
            case 1: return cpu.registers.c();
            case 2: return cpu.registers.d();
            case 3: return cpu.registers.e();
            case 4: return cpu.registers.h();
            case 5: return cpu.registers.l();
            case 6: return cpu.memory->read(cpu.registers.hl);
            case 7: return cpu.registers.a();
            default: throw std::runtime_error("Unknown register code in r8_lookup.");
        }
    }

    void r8_set_value(CPU& cpu, std::uint8_t reg_code, std::uint8_t value) {
        switch (reg_code) {
            case 0: cpu.registers.b() = value; break;
            case 1: cpu.registers.c() = value; break;
            case 2: cpu.registers.d() = value; break;
            case 3: cpu.registers.e() = value; break;
            case 4: cpu.registers.h() = value; break;
            case 5: cpu.registers.l() = value; break;
            case 6: cpu.memory->write(cpu.registers.hl, value); break;
            case 7: cpu.registers.a() = value; break;
            default: throw std::runtime_error("Unknown register code in r8_lookup.");
        }
    }

    std::uint16_t* r16_lookup(CPU& cpu, std::uint8_t reg_code) {
        switch (reg_code) {
            case 0: return &cpu.registers.bc;
            case 1: return &cpu.registers.de;
            case 2: return &cpu.registers.hl;
            case 3: return &cpu.registers.sp;
            default: throw std::runtime_error("Unknown register code in r16_lookup.");
        }
    };

    enum class RegisterOperation {
        NONE,
        HL_INCREMENT,
        HL_DECREMENT
    };

    std::pair<std::uint16_t, RegisterOperation> r16mem_lookup(CPU& cpu, std::uint8_t reg_code) {
        switch (reg_code) {
            case 0: return std::make_pair(cpu.registers.bc, RegisterOperation::NONE);
            case 1: return std::make_pair(cpu.registers.de, RegisterOperation::NONE);
            case 2: return std::make_pair(cpu.registers.hl, RegisterOperation::HL_INCREMENT);
            case 3: return std::make_pair(cpu.registers.hl, RegisterOperation::HL_DECREMENT);
            default: throw std::runtime_error("Unknown register code in r16mem_lookup.");
        }
    }

    std::uint8_t load16_impl(CPU& cpu) {
        enum class LoadType : std::uint8_t {
            R16_IMM16 = 0b0001,
            R16MEM_A = 0b0010,
            A_R16_MEM = 0b1010,
            IMM16MEM_SP = 0b1000
        };

        auto instruction = cpu.memory->read(cpu.registers.pc);
        // For 8 bit loads the last 3 bits are unique, while for 16 bit loads the last 4 bits of the LD instruction encode the LD type
        auto load_type = static_cast<LoadType>(instruction & 0b00001111);
        if (load_type == LoadType::R16_IMM16) {
            std::uint8_t reg_code = (instruction & 0b00110000) >> 4;
            std::uint16_t* reg = r16_lookup(cpu, reg_code);
            std::uint16_t value = 
                (cpu.memory->read(cpu.registers.pc + 2) << 8) |
                (cpu.memory->read(cpu.registers.pc + 1) << 0);
            *reg = value;
        }
        else if (load_type == LoadType::R16MEM_A) {
            std::uint8_t reg_code = (instruction & 0b00110000) >> 4;
            auto [reg_value, reg_op] = r16mem_lookup(cpu, reg_code);
            cpu.memory->write(reg_value, cpu.registers.a());
            // Check if we need to execute a register operation
            if (reg_op == RegisterOperation::HL_INCREMENT) {
                cpu.registers.hl++;
            }
            else if (reg_op == RegisterOperation::HL_DECREMENT) {
                cpu.registers.hl--;
            }
            else if (reg_op != RegisterOperation::NONE) {
                throw std::runtime_error("Unknown register operation in load16_impl.");
            }
        }
        else if (load_type == LoadType::A_R16_MEM) {
            std::uint8_t reg_code = (instruction & 0b00110000) >> 4;
            std::uint16_t* reg = r16_lookup(cpu, reg_code);
            auto value = cpu.memory->read(*reg);
            cpu.registers.a() = value;
        }
        else throw std::runtime_error("Unimplemented load type in load16_impl.");

        return 0;
    }

    std::uint8_t load16_imm16mem_impl(CPU& cpu) {
        enum class LoadType : std::uint8_t {
            IMM16MEM_A = 0b0,
            A_IMM16MEM = 0b1,
        };
        auto instruction = cpu.memory->read(cpu.registers.pc);
        auto load_type = static_cast<LoadType>((instruction & 0b00010000) >> 4);
        if (load_type == LoadType::IMM16MEM_A) {
            std::uint16_t value =
                (cpu.memory->read(cpu.registers.pc + 2) << 8) |
                (cpu.memory->read(cpu.registers.pc + 1) << 0);
            cpu.memory->write(value, cpu.registers.a());
        }
        else throw std::runtime_error("Unimplemented load type in load16_imm16mem_impl.");

        return 0;
    }

    std::uint8_t load8_imm8_impl(CPU& cpu) {
        auto instruction = cpu.memory->read(cpu.registers.pc);
        std::uint8_t reg_code = (instruction & 0b00111000) >> 3;
        std::uint8_t value = cpu.memory->read(cpu.registers.pc + 1);
        r8_set_value(cpu, reg_code, value);

        return 0;
    }

    std::uint8_t load8_r8_impl(CPU& cpu) {
        auto instruction = cpu.memory->read(cpu.registers.pc);
        std::uint8_t source_code = instruction & 0b111;
        std::uint8_t dest_code = (instruction & 0b111000) >> 3;
        std::uint8_t source_value = r8_get_value(cpu, source_code);
        r8_set_value(cpu, dest_code, source_value);

        return 0;
    }

    std::uint8_t load8_high_impl(CPU& cpu) {
        enum class LoadType : std::uint8_t {
            C_A = 0b00010,
            IMM8_A = 0b00000,
            A_C = 0b10010,
            A_IMM8 = 0b10000,
        };

        auto instruction = cpu.memory->read(cpu.registers.pc);
        auto load_type = static_cast<LoadType>(instruction & 0b00011111);
        if (load_type == LoadType::C_A) {
            cpu.memory->write(0xFF00 + cpu.registers.c(), cpu.registers.a());
        }
        else if (load_type == LoadType::IMM8_A) {
            auto offset = cpu.memory->read(cpu.registers.pc + 1);
            cpu.memory->write(0xFF00 + offset, cpu.registers.a());
        }
        else if (load_type == LoadType::A_IMM8) {
            auto offset = cpu.memory->read(cpu.registers.pc + 1);
            auto value = cpu.memory->read(0xFF00 + offset);
            cpu.registers.a() = value;
        }
        else throw std::runtime_error("Unimplemented load type in load8_high_impl.");

        return 0;
    }

    std::uint8_t xor_impl(CPU& cpu) {
        enum class XORType : std::uint8_t {
            A_R8 = 0b00010101,
            A_IMM8 = 0b00011101
        };
        auto instruction = cpu.memory->read(cpu.registers.pc);
        // XOR type is stored in the upper 5 bits of the instruction
        auto xor_type = static_cast<XORType>((instruction & 0b11111000) >> 3);
        if (xor_type == XORType::A_R8) {
            std::uint8_t reg_code = instruction & 0b00000111;
            std::uint8_t reg_value = r8_get_value(cpu, reg_code);
            cpu.registers.a() = cpu.registers.a() ^ reg_value;
        }
        else if (xor_type == XORType::A_IMM8) {
            std::uint8_t value = cpu.memory->read(cpu.registers.pc + 1);
            cpu.registers.a() = cpu.registers.a() ^ value;
        }

        // XOR always zeroes all flags, except Z which is set based on the result
        cpu.registers.set_flag_z(cpu.registers.a() == 0);
        cpu.registers.set_flag_n(false);
        cpu.registers.set_flag_h(false);
        cpu.registers.set_flag_c(false);

        return 0;
    }

    std::uint8_t enable_prefix(CPU& cpu) {
        cpu.is_prefixed = true;
        return 0;
    }

    bool flag_lookup(CPU& cpu, std::uint8_t flag_code) {
        switch (flag_code) {
            case 0: return !cpu.registers.get_flag_z(); // NZ
            case 1: return cpu.registers.get_flag_z();  // Z
            case 2: return !cpu.registers.get_flag_c(); // NC
            case 3: return cpu.registers.get_flag_c();  // C
            default: throw std::runtime_error("Unknown flag code in flag_lookup.");
        }
    }

    std::uint8_t jump_relative_impl(CPU& cpu) {
        auto instruction = cpu.memory->read(cpu.registers.pc);
        auto is_conditional = ((instruction & 0b00100000) >> 5) != 0;
        auto read_offset_and_jump = [&cpu]() {
            // The offset for the jump is a signed relative offset from the address AFTER the current instruction (including its parameter)
            // However, since CPU tick logic will add the length of the current instruction (2) to PC anyways we will exclude that here.
            auto offset = static_cast<std::int8_t>(cpu.memory->read(cpu.registers.pc + 1));
            // We upcast the values to 32 bit integers than downcast back to 16 bit unsigned integer for PC.
            // This is to protect PC against underflows/overflows. The narrowing static cast will modulo the result,
            // which is exactly the behavior the the DMG CPU does for underflow/overflow.
            cpu.registers.pc = static_cast<std::uint16_t>(static_cast<std::int32_t>(cpu.registers.pc) + static_cast<std::int32_t>(offset));
        };
        if (is_conditional) {
            std::uint8_t condition_flag_code = (instruction & 00011000) >> 3;
            auto flag_value = flag_lookup(cpu, condition_flag_code);
            // If the flag is not set do nothing
            if (!flag_value) {
                return 0;
            }
            read_offset_and_jump();
        }
        else {
            read_offset_and_jump();
        }
        // If we jumped this instruction takes 4 cycles longer
        return 4;
    }

    std::uint8_t jump_absolute_impl(CPU& cpu) {
        enum class JumpType : std::uint8_t {
            CONDITIONAL = 0b10,
            UNCONDITIONAL = 0b11,
            HL = 0b01
        };
        auto instruction = cpu.memory->read(cpu.registers.pc);
        auto jump_type = static_cast<JumpType>(instruction & 0b11);
        if (jump_type == JumpType::UNCONDITIONAL) {
            std::uint16_t address =
                (cpu.memory->read(cpu.registers.pc + 2) << 8) |
                (cpu.memory->read(cpu.registers.pc + 1) << 0);
            cpu.registers.pc = address;
            return 4;
        }
        else throw std::runtime_error("Unimplemented jump type in jump_absolute_impl.");
    }

    std::uint8_t inc8_impl(CPU& cpu) {
        auto instruction = cpu.memory->read(cpu.registers.pc);
        std::uint8_t reg_code = (instruction & 0b00111000) >> 3;
        std::uint8_t reg_value = r8_get_value(cpu, reg_code);
        auto half_carry = ((reg_value & 0x0F) + 1) > 0x0F;
        std::uint8_t new_value = (reg_value == 0xFF) ? 0 : (reg_value + 1);
        r8_set_value(cpu, reg_code, new_value);

        cpu.registers.set_flag_z(new_value == 0);
        cpu.registers.set_flag_n(false);
        cpu.registers.set_flag_h(half_carry);

        return 0;
    }

    std::uint8_t inc16_impl(CPU& cpu) {
        auto instruction = cpu.memory->read(cpu.registers.pc);
        std::uint8_t reg_code = (instruction & 0b00110000) >> 4;
        std::uint16_t* reg = r16_lookup(cpu, reg_code);
        *reg = ((*reg) == 0xFFFF) ? 0 : ((*reg) + 1);

        return 0;
    }

    void push_value(CPU& cpu, std::uint16_t value) {
        std::uint8_t high_bits = (value & (0xFF << 8)) >> 8;
        std::uint8_t low_bits = value & 0xFF;
        cpu.memory->write(--cpu.registers.sp, high_bits);
        cpu.memory->write(--cpu.registers.sp, low_bits);
    }

    std::uint8_t call_impl(CPU& cpu) {
        enum class CallType : std::uint8_t {
            UNCONDITIONAL = 0b101,
            CONDITIONAL = 0b100
        };
        auto instruction = cpu.memory->read(cpu.registers.pc);
        auto call_type = static_cast<CallType>(instruction & 0b111);
        if (call_type == CallType::UNCONDITIONAL) {
            // First we need to store the address of the next instruction at SP
            // Since CALL is 3 bytes we just add 3 to PC to get the next instruction address
            // then split that into two 8 bit values and push them onto the stack.
            std::uint16_t next_address = cpu.registers.pc + 3;
            push_value(cpu, next_address);

            // Set PC to the new address
            std::uint16_t new_address =
                (cpu.memory->read(cpu.registers.pc + 2) << 8) |
                (cpu.memory->read(cpu.registers.pc + 1) << 0);
            cpu.registers.pc = new_address;

            return 0;
        }
        else throw std::runtime_error("Unsuppported call type in call_impl.");
    }

    std::uint16_t* r16stk_lookup(CPU& cpu, std::uint8_t reg_code) {
        switch (reg_code) {
            case 0: return &cpu.registers.bc;
            case 1: return &cpu.registers.de;
            case 2: return &cpu.registers.hl;
            case 3: return &cpu.registers.af;
            default: throw std::runtime_error("Unknown register code in r16stk_lookup.");
        }
    }

    std::uint8_t push_impl(CPU& cpu) {
        auto instruction = cpu.memory->read(cpu.registers.pc);
        std::uint8_t reg_code = (instruction & 0b00110000) >> 4;
        auto reg = r16stk_lookup(cpu, reg_code);
        push_value(cpu, *reg);

        return 0;
    }

    std::uint8_t rotate_left_impl(CPU& cpu, bool set_zero_flag) {
        auto instruction = cpu.memory->read(cpu.registers.pc);
        std::uint8_t reg_code = instruction & 0b111;
        std::uint8_t reg_value = r8_get_value(cpu, reg_code);
        auto new_carry_value = (reg_value & 0b10000000) != 0;
        std::uint8_t new_value = reg_value << 1;
        new_value &= 0b11111110;        
        new_value |= (cpu.registers.get_flag_c() ? 1 : 0);
        r8_set_value(cpu, reg_code, new_value);

        cpu.registers.set_flag_z(set_zero_flag ? (new_value == 0) : 0);
        cpu.registers.set_flag_n(false);
        cpu.registers.set_flag_h(false);
        cpu.registers.set_flag_c(new_carry_value);
        return 0;
    }

    std::uint8_t rotate_left_set_zero_impl(CPU& cpu) {
        return rotate_left_impl(cpu, true);
    }

    std::uint8_t rotate_left_clear_zero_impl(CPU& cpu) {
        return rotate_left_impl(cpu, false);
    }

    std::uint8_t pop_value(CPU& cpu) {
        return cpu.memory->read(cpu.registers.sp++);
    }

    std::uint8_t pop_impl(CPU& cpu) {
        auto instruction = cpu.memory->read(cpu.registers.pc);
        std::uint8_t reg_code = (instruction & 0b00110000) >> 4;
        auto reg = r16stk_lookup(cpu, reg_code);
        *reg = pop_value(cpu) << 0;
        *reg |= pop_value(cpu) << 8;

        return 0;
    }

    std::uint8_t dec8_impl(CPU& cpu) {
        auto instruction = cpu.memory->read(cpu.registers.pc);
        std::uint8_t reg_code = (instruction & 0b00111000) >> 3;
        std::uint8_t reg_value = r8_get_value(cpu, reg_code);
        auto half_carry = (reg_value & 0x0F) == 0;
        std::uint8_t new_value = (reg_value == 0) ? 0xFF : (reg_value - 1);
        r8_set_value(cpu, reg_code, new_value);

        cpu.registers.set_flag_z(new_value == 0);
        cpu.registers.set_flag_n(true);
        cpu.registers.set_flag_h(half_carry);

        return 0;
    }

    std::uint8_t ret_impl(CPU& cpu) {
        cpu.registers.pc = pop_value(cpu);
        cpu.registers.pc |= pop_value(cpu) << 8;

        return 0;
    }

    std::uint8_t compare_impl(CPU& cpu) {
        enum class CompareType : std::uint8_t {
            REG = 0b10111,
            IMM8 = 0b11111,
        };
        auto instruction = cpu.memory->read(cpu.registers.pc);
        auto compare_type = static_cast<CompareType>((instruction & 0b11111000) >> 3);
        auto first = cpu.registers.a();
        auto second = (compare_type == CompareType::IMM8)
            ? cpu.memory->read(cpu.registers.pc + 1)
            : r8_get_value(cpu, instruction & 0b111);

        cpu.registers.set_flag_z(first == second);
        cpu.registers.set_flag_n(true);
        cpu.registers.set_flag_h((first & 0xf) < (second & 0xf));
        cpu.registers.set_flag_c(first < second);

        return 0;
    }

    std::uint8_t sub_impl(CPU& cpu) {
        enum class SubType : std::uint8_t {
            R8 = 0b10010,
            IMM8 = 0b11010
        };
        auto instruction = cpu.memory->read(cpu.registers.pc);
        auto sub_type = static_cast<SubType>((instruction & 0b11111000) >> 3);
        auto first = cpu.registers.a();
        auto second = (sub_type == SubType::R8)
            ? r8_get_value(cpu, instruction & 0b111)
            : cpu.memory->read(cpu.registers.pc + 1);

        cpu.registers.a() = static_cast<std::uint16_t>(static_cast<int>(first) - static_cast<int>(second));

        cpu.registers.set_flag_z(first == second);
        cpu.registers.set_flag_n(true);
        cpu.registers.set_flag_h((first & 0xf) < (second & 0xf));
        cpu.registers.set_flag_c(first < second);

        return 0;
    }

    std::uint8_t add_impl(CPU& cpu) {
        enum class AddType : std::uint8_t {
            R8 = 0b10000,
            IMM8 = 0b11000
        };
        auto instruction = cpu.memory->read(cpu.registers.pc);
        auto add_type = static_cast<AddType>((instruction & 0b11111000) >> 3);
        auto first = cpu.registers.a();
        auto second = (add_type == AddType::R8)
            ? r8_get_value(cpu, instruction & 0b111)
            : cpu.memory->read(cpu.registers.pc + 1);
        
        auto result_16_bit = static_cast<std::uint16_t>(first) + static_cast<std::uint16_t>(second);
        auto result = static_cast<std::uint8_t>(result_16_bit);
        cpu.registers.a() = result;

        cpu.registers.set_flag_z(result == 0);
        cpu.registers.set_flag_n(false);
        cpu.registers.set_flag_h(((first & 0xF) + (second & 0xF)) > 0xF);
        cpu.registers.set_flag_c(result_16_bit > 0xFF);

        return 0;
    }

    std::uint8_t nop_impl(CPU&) {
        return 0;
    }

    // For instructions where we do not want PC to be modified after the instruction (such as CALL, RET, etc.)
    // we set the instruction length to be 0. Take care when looking up instruction length as this might yield unexpected values.
    std::unordered_map<std::uint8_t, Instruction> CPU::instruction_set = {
        { 0x00, Instruction { .length = 1, .cycles = 4, .implementation = nop_impl } },                    // NOP
        { 0x01, Instruction { .length = 3, .cycles = 16, .implementation = load16_impl } },                // LD BC, IMM16
        { 0x04, Instruction { .length = 1, .cycles = 4, .implementation = inc8_impl } },                   // INC B
        { 0x05, Instruction { .length = 1, .cycles = 4, .implementation = dec8_impl } },                   // DEC B
        { 0x06, Instruction { .length = 2, .cycles = 8, .implementation = load8_imm8_impl } },             // LD B, IMM8
        { 0x0C, Instruction { .length = 1, .cycles = 4, .implementation = inc8_impl } },                   // INC C
        { 0x0D, Instruction { .length = 1, .cycles = 4, .implementation = dec8_impl } },                   // DEC C
        { 0x0E, Instruction { .length = 2, .cycles = 8, .implementation = load8_imm8_impl } },             // LD E, IMM8
        { 0x11, Instruction { .length = 3, .cycles = 16, .implementation = load16_impl } },                // LD DE, IMM16
        { 0x13, Instruction { .length = 1, .cycles = 8, .implementation = inc16_impl } },                  // INC DE
        { 0x15, Instruction { .length = 1, .cycles = 4, .implementation = dec8_impl } },                   // DEC D
        { 0x16, Instruction { .length = 2, .cycles = 8, .implementation = load8_imm8_impl } },             // LD D, IMM8
        { 0x17, Instruction { .length = 1, .cycles = 4, .implementation = rotate_left_clear_zero_impl } }, // RLA
        { 0x18, Instruction { .length = 2, .cycles = 12, .implementation = jump_relative_impl } },         // JR IMM8
        { 0x1A, Instruction { .length = 1, .cycles = 8, .implementation = load16_impl } },                 // LD A, [DE]
        { 0x1D, Instruction { .length = 1, .cycles = 4, .implementation = dec8_impl } },                   // DEC E
        { 0x1E, Instruction { .length = 2, .cycles = 8, .implementation = load8_imm8_impl } },             // LD E, IMM8
        { 0x20, Instruction { .length = 2, .cycles = 8, .implementation = jump_relative_impl } },          // JR NZ, IMM8 (signed)
        { 0x21, Instruction { .length = 3, .cycles = 16, .implementation = load16_impl } },                // LD HL, IMM16
        { 0x22, Instruction { .length = 1, .cycles = 8, .implementation = load16_impl } },                 // LD [HL+], A
        { 0x23, Instruction { .length = 1, .cycles = 8, .implementation = inc16_impl } },                  // INC HL
        { 0x24, Instruction { .length = 1, .cycles = 4, .implementation = inc8_impl } },                   // INC H
        { 0x28, Instruction { .length = 2, .cycles = 8, .implementation = jump_relative_impl } },          // JR Z, IMM8
        { 0x31, Instruction { .length = 3, .cycles = 16, .implementation = load16_impl } },                // LD SP, IMM16
        { 0x32, Instruction { .length = 1, .cycles = 16, .implementation = load16_impl } },                // LD SP, IMM16
        { 0x3D, Instruction { .length = 1, .cycles = 4, .implementation = dec8_impl } },                   // DEC A
        { 0x3E, Instruction { .length = 2, .cycles = 8, .implementation = load8_imm8_impl } },             // LD A, IMM8
        { 0x4F, Instruction { .length = 1, .cycles = 4, .implementation = load8_imm8_impl } },             // LD C, A
        { 0x57, Instruction { .length = 1, .cycles = 4, .implementation = load8_r8_impl } },               // LD D, A
        { 0x67, Instruction { .length = 1, .cycles = 4, .implementation = load8_r8_impl } },               // LD H, A
        { 0x77, Instruction { .length = 1, .cycles = 8, .implementation = load8_imm8_impl } },             // LD [HL], A
        { 0x78, Instruction { .length = 1, .cycles = 4, .implementation = load8_r8_impl } },               // LD A, B
        { 0x7B, Instruction { .length = 1, .cycles = 4, .implementation = load8_r8_impl } },               // LD A, E
        { 0x7C, Instruction { .length = 1, .cycles = 4, .implementation = load8_r8_impl} },                // LD A, H
        { 0x7D, Instruction { .length = 1, .cycles = 4, .implementation = load8_r8_impl } },               // LD A, L
        { 0x86, Instruction { .length = 1, .cycles = 8, .implementation = add_impl } },                    // ADD A, [HL]
        { 0x90, Instruction { .length = 1, .cycles = 4, .implementation = sub_impl } },                    // SUB A, B
        { 0xAF, Instruction { .length = 1, .cycles = 16, .implementation = xor_impl } },                   // XOR A, R8
        { 0xBE, Instruction { .length = 1, .cycles = 8, .implementation = compare_impl } },                // CP A, [HL]
        { 0xC1, Instruction { .length = 1, .cycles = 12, .implementation = pop_impl} },                    // POP BC
        // { 0xC3, Instruction { .length = 0, .cycles = 12, .implementation = jump_absolute_impl } },         // JP IMM16
        { 0xC5, Instruction { .length = 1, .cycles = 16, .implementation = push_impl } },                  // PUSH BC
        { 0xC9, Instruction { .length = 0, .cycles = 16, .implementation = ret_impl } },                   // RET
        { 0xCB, Instruction { .length = 1, .cycles = 4, .implementation = enable_prefix } },               // PREFIX
        { 0xCD, Instruction { .length = 0, .cycles = 24, .implementation = call_impl } },                  // CALL IMM16
        { 0xE0, Instruction { .length = 2, .cycles = 12, .implementation = load8_high_impl } },            // LDH [IMM8], A
        { 0xEA, Instruction { .length = 3, .cycles = 16, .implementation = load16_imm16mem_impl } },       // LD [IMM16], A
        { 0xE2, Instruction { .length = 1, .cycles = 8, .implementation = load8_high_impl } },             // LDH [C], A
        { 0xF0, Instruction { .length = 2, .cycles = 12, .implementation = load8_high_impl } },            // LDH A, IMM8
        { 0xFE, Instruction { .length = 2, .cycles = 8, .implementation = compare_impl } },                // CP A, IMM8
    };

    std::uint8_t bit_impl(CPU& cpu) {
        auto instruction = cpu.memory->read(cpu.registers.pc);
        auto bit = (instruction & 0b00111000) >> 3;
        std::uint8_t reg_code = instruction & 0b00000111;
        auto reg_value = r8_get_value(cpu, reg_code);
        // We need to the complement of the Nth bit of reg
        auto flag_value = !((reg_value & (0b1 << bit)) >> bit);

        // Flags:
        // Zero is set depending on the result
        // N is always 0
        // H is always 1
        // C is left untouched
        cpu.registers.set_flag_z(flag_value);
        cpu.registers.set_flag_n(false);
        cpu.registers.set_flag_h(true);

        return 0;
    }

    // Instruction length and cycles here do NOT contain the length and cycle count of the prefix instruction itself.
    std::unordered_map<std::uint8_t, Instruction> CPU::prefixed_instruction_set = {
        { 0x11, Instruction { .length = 1, .cycles = 4, .implementation = rotate_left_set_zero_impl } }, // RL C
        { 0x7C, Instruction { .length = 1, .cycles = 4, .implementation = bit_impl } },                  // BIT 7, H
    };

}