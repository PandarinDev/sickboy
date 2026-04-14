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

    void Registers::set_ime(bool on) {
        ime = on;
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

    CPU::CPU(const std::shared_ptr<MMU>& memory) :
        registers({}), memory(memory), is_prefixed(false), is_halted(false), stop_requested(false),
        enable_ime_requested(false), trace_instructions(false) {}

    std::uint8_t interrupt_jump_vector_lookup(std::uint8_t bit) {
        switch (bit) {
            case 0: return 0x40;
            case 1: return 0x48;
            case 2: return 0x50;
            case 3: return 0x58;
            case 4: return 0x60;
            default: throw std::runtime_error("Unknown bit in interrupt jump vector lookup.");
        }
    }

    void push_value(CPU& cpu, std::uint16_t value) {
        std::uint8_t high_bits = (value & (0xFF << 8)) >> 8;
        std::uint8_t low_bits = value & 0xFF;
        cpu.memory->write(--cpu.registers.sp, high_bits);
        cpu.memory->write(--cpu.registers.sp, low_bits);
    }

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

        // Check if we had any interrupt requests - if we had then we need to exit halt regardless of IE and IME
        if (memory->poll_interrupt_request()) {
            is_halted = false;
        }

        // Check if we need to call any interrupt routines
        static constexpr std::uint8_t INTERRUPT_HANDLING_CYCLES = 20;
        static constexpr std::uint16_t IF_ADDRESS = 0xFF0F;
        static constexpr std::uint16_t IE_ADDRESS = 0xFFFF;
        std::uint8_t interrupt_requested = memory->read(IF_ADDRESS);
        std::uint8_t interrupt_enabled = memory->read(IE_ADDRESS);
        const auto has_interrupt = (interrupt_requested & interrupt_enabled) != 0;
        if (has_interrupt) {
            // If interrupt is requested we need to exit halt regardless of the value of IME
            is_halted = false;
            // If IME is enabled we need to actually call the interrupt routine
            if (registers.ime) {
                static constexpr std::uint8_t num_interrupt_bits = 5;
                // Priority of interrupts is from lowest bit to highest, so VBlank is top priority.
                for (std::uint8_t i = 0; i < num_interrupt_bits; ++i) {
                    bool should_call = ((interrupt_requested & (1 << i)) & (interrupt_enabled & (1 << i))) != 0;
                    if (should_call) {
                        // Clear IME to prevent further interrupts until it is re-enabled
                        registers.set_ime(false);
                        // Clear interrupt bit in IF
                        std::uint8_t updated_if = interrupt_requested & ~(1 << i);
                        memory->write(IF_ADDRESS, updated_if);
                        // Call interrupt routine
                        std::uint8_t new_address = interrupt_jump_vector_lookup(i);
                        push_value(*this, registers.pc);
                        registers.pc = new_address;

                        return INTERRUPT_HANDLING_CYCLES;
                    }
                }
            }
        }

        // Enable IME if requested
        if (enable_ime_requested) {
            registers.set_ime(true);
            enable_ime_requested = false;
        }

        // If the CPU is halted simply lie that we consumed 4 cycles to tick the rest of the system
        // TODO: Need to add support for the HALT bug (halt requested during interrupt requested)
        if (is_halted) {
            return 4;
        }

        // Fetch instruction
        auto was_prefixed = is_prefixed;
        auto instruction_code = memory->read(registers.pc);
        const auto& instruction = is_prefixed
            ? lookup_prefixed_instruction(instruction_code)
            : lookup_instruction(instruction_code);
        auto additional_cycles = instruction.implementation(*this);
        // Since we are adding unsigned ints here wrap around is guaranteed in case of PC overflow
        registers.pc = registers.pc + instruction.length;
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
            A_R16MEM = 0b1010,
            IMM16MEM_SP = 0b1000
        };

        static const auto execute_reg_operation = [](CPU& cpu, RegisterOperation reg_op) {
            if (reg_op == RegisterOperation::HL_INCREMENT) {
                cpu.registers.hl++;
            }
            else if (reg_op == RegisterOperation::HL_DECREMENT) {
                cpu.registers.hl--;
            }
            else if (reg_op != RegisterOperation::NONE) {
                throw std::runtime_error("Unknown register operation in load16_impl.");
            }
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
            execute_reg_operation(cpu, reg_op);
        }
        else if (load_type == LoadType::A_R16MEM) {
            std::uint8_t reg_code = (instruction & 0b00110000) >> 4;
            auto [reg_value, reg_op] = r16mem_lookup(cpu, reg_code);
            auto value = cpu.memory->read(reg_value);
            cpu.registers.a() = value;
            execute_reg_operation(cpu, reg_op);
        }
        else if (load_type == LoadType::IMM16MEM_SP) {
            std::uint16_t address = 
                (cpu.memory->read(cpu.registers.pc + 2) << 8) |
                (cpu.memory->read(cpu.registers.pc + 1) << 0);
            cpu.memory->write(address, cpu.registers.sp & 0xFF);
            cpu.memory->write(address + 1, (cpu.registers.sp & 0xFF00) >> 8);
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
        else if (load_type == LoadType::A_IMM16MEM) {
            std::uint16_t value =
                (cpu.memory->read(cpu.registers.pc + 2) << 8) |
                (cpu.memory->read(cpu.registers.pc + 1) << 0);
            cpu.registers.a() = cpu.memory->read(value);
        }
        else throw std::runtime_error("Unimplemented load type in load16_imm16mem_impl.");

        return 0;
    }

    std::uint8_t load_sp_hl(CPU& cpu) {
        // This is the only r16 to r16 load that the CPU supports
        cpu.registers.sp = cpu.registers.hl;
        return 0;
    }

    std::uint8_t load_hl_sp_r8(CPU& cpu) {
        std::uint16_t first = cpu.registers.sp;
        // Important: The offset is signed
        std::int8_t second = static_cast<std::int8_t>(cpu.memory->read(cpu.registers.pc + 1));
        std::uint16_t result = first + second;
        cpu.registers.hl = result;

        cpu.registers.set_flag_z(false);
        cpu.registers.set_flag_n(false);

        // Half carry and carry logic are tricky due to signed addition
        bool half_carry = ((first ^ (std::uint16_t) second ^ result) & 0x10) != 0;
        bool carry = ((first ^ (std::uint16_t) second ^ result) & 0x100) != 0;
        cpu.registers.set_flag_h(half_carry);
        cpu.registers.set_flag_c(carry);
        
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
        else if (load_type == LoadType::A_C) {
            cpu.registers.a() = cpu.memory->read(0xFF00 + cpu.registers.c());
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

    std::uint8_t and_impl(CPU& cpu) {
        enum class AndType : std::uint8_t {
            R8 = 0b10100,
            IMM8 = 0b11100
        };
        std::uint8_t instruction = cpu.memory->read(cpu.registers.pc);
        auto and_type = static_cast<AndType>((instruction & 0b11111000) >> 3);
        std::uint8_t value = (and_type == AndType::R8)
            ? r8_get_value(cpu, instruction & 0b111)
            : cpu.memory->read(cpu.registers.pc + 1);
        std::uint8_t result = cpu.registers.a() & value;
        cpu.registers.a() = result;

        cpu.registers.set_flag_z(result == 0);
        cpu.registers.set_flag_n(false);
        cpu.registers.set_flag_h(true);
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
            std::uint8_t condition_flag_code = (instruction & 0b00011000) >> 3;
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
        if (jump_type == JumpType::CONDITIONAL) {
            std::uint8_t condition_flag_code = (instruction & 0b00011000) >> 3;
            auto flag_value = flag_lookup(cpu, condition_flag_code);
            if (!flag_value) {
                return 0;
            }
            std::uint16_t address =
                (cpu.memory->read(cpu.registers.pc + 2) << 8) |
                (cpu.memory->read(cpu.registers.pc + 1) << 0);
            // Unconditional jumps need to subtract the jump instruction length from target address
            // because the length of the instruction in the instruction table cannot be 0, since if
            // the jump condition is false we need to progress PC.
            cpu.registers.pc = address - 3;
            return 4;
        }
        else if (jump_type == JumpType::UNCONDITIONAL) {
            std::uint16_t address =
                (cpu.memory->read(cpu.registers.pc + 2) << 8) |
                (cpu.memory->read(cpu.registers.pc + 1) << 0);
            cpu.registers.pc = address;
            return 4;
        }
        else if (jump_type == JumpType::HL) {
            cpu.registers.pc = cpu.registers.hl;
            return 0;
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
        else if (call_type == CallType::CONDITIONAL) {
            std::uint8_t flag_code = (instruction & 0b00011000) >> 3;
            auto flag_value = flag_lookup(cpu, flag_code);
            if (!flag_value) {
                return 0;
            }

            std::uint16_t next_address = cpu.registers.pc + 3;
            push_value(cpu, next_address);

            // Set PC to the new address and subtract 3 - this is because for conditional
            // calls the instruction length must not be 0 since we need to increment PC even
            // if the condition is false.
            std::uint16_t new_address =
                (cpu.memory->read(cpu.registers.pc + 2) << 8) |
                (cpu.memory->read(cpu.registers.pc + 1) << 0);
            cpu.registers.pc = new_address - 3;

            return 12;
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
        new_value |= (cpu.registers.get_flag_c() ? 1 : 0);
        r8_set_value(cpu, reg_code, new_value);

        cpu.registers.set_flag_z(set_zero_flag ? (new_value == 0) : false);
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

    std::uint8_t rotate_left_circular_impl(CPU& cpu, bool set_zero_flag) {
        auto instruction = cpu.memory->read(cpu.registers.pc);
        std::uint8_t reg_code = instruction & 0b111;
        std::uint8_t reg_value = r8_get_value(cpu, reg_code);
        auto new_carry_value = (reg_value & 0b10000000) != 0;
        std::uint8_t new_value = (reg_value << 1) | (new_carry_value ? 1 : 0);
        r8_set_value(cpu, reg_code, new_value);

        cpu.registers.set_flag_z(set_zero_flag ? (new_value == 0) : false);
        cpu.registers.set_flag_n(false);
        cpu.registers.set_flag_h(false);
        cpu.registers.set_flag_c(new_carry_value);

        return 0;
    }

    std::uint8_t rotate_left_circular_set_zero_impl(CPU& cpu) {
        return rotate_left_circular_impl(cpu, true);
    }

    std::uint8_t rotate_left_circular_clear_zero_impl(CPU& cpu) {
        return rotate_left_circular_impl(cpu, false);
    }

    std::uint8_t rotate_right_circular_impl(CPU& cpu, bool set_zero_flag) {
        auto instruction = cpu.memory->read(cpu.registers.pc);
        std::uint8_t reg_code = instruction & 0b111;
        std::uint8_t old_value = r8_get_value(cpu, reg_code);
        // Circular rotate puts the 0th bit into the 7th bit instead of carry
        std::uint8_t rotated_bit = old_value & 0b1;
        std::uint8_t result = (old_value >> 1) | (rotated_bit << 7);
        r8_set_value(cpu, reg_code, result);

        cpu.registers.set_flag_z(set_zero_flag ? (result == 0) : false);
        cpu.registers.set_flag_n(false);
        cpu.registers.set_flag_h(false);
        cpu.registers.set_flag_c(rotated_bit != 0);
        return 0;
    }

    std::uint8_t rotate_right_circular_clear_zero_impl(CPU& cpu) {
        return rotate_right_circular_impl(cpu, false);
    }

    std::uint8_t rotate_right_circular_set_zero_impl(CPU& cpu) {
        return rotate_right_circular_impl(cpu, true);
    }

    std::uint8_t rotate_right_impl(CPU& cpu, bool set_zero) {
        auto instruction = cpu.memory->read(cpu.registers.pc);
        std::uint8_t reg_code = instruction & 0b111;

        std::uint8_t old_value = r8_get_value(cpu, reg_code);
        std::uint8_t rotated_bit = old_value & 0b1;
        std::uint8_t new_value = (old_value >> 1) |
            ((cpu.registers.get_flag_c() ? 1 : 0) << 7);
        r8_set_value(cpu, reg_code, new_value);

        cpu.registers.set_flag_z(set_zero ? (new_value == 0) : false);
        cpu.registers.set_flag_n(false);
        cpu.registers.set_flag_h(false);
        cpu.registers.set_flag_c(rotated_bit != 0);
        return 0;
    }

    std::uint8_t rotate_right_set_zero_impl(CPU& cpu) {
        return rotate_right_impl(cpu, true);
    }

    std::uint8_t rotate_right_clear_zero_impl(CPU& cpu) {
        return rotate_right_impl(cpu, false);
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
        // If we are POPing to AF mask out the lowest 4 bits
        if (reg == &cpu.registers.af) {
            *reg &= 0xFFF0;
        }

        // The only case when we are setting flags is when we are popping AF,
        // because then of course we are restoring values to the F register.

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

    std::uint8_t dec16_impl(CPU& cpu) {
        auto instruction = cpu.memory->read(cpu.registers.pc);
        std::uint8_t reg_code = (instruction & 0b00110000) >> 4;
        std::uint16_t* reg = r16_lookup(cpu, reg_code);
        (*reg)--;

        // 16 bit decrement operations do not change flags

        return 0;
    }

    std::uint8_t ret_impl(CPU& cpu) {
        enum class ReturnType : std::uint8_t {
            CONDITIONAL = 0b0,
            UNCONDITIONAL = 0b1
        };
        auto instruction = cpu.memory->read(cpu.registers.pc);
        auto return_type = static_cast<ReturnType>(instruction & 0b1);

        if (return_type == ReturnType::CONDITIONAL) {
            std::uint8_t flag_code = (instruction & 0b00011000) >> 3;
            auto flag_value = flag_lookup(cpu, flag_code);
            if (!flag_value) {
                return 0;
            } 
            cpu.registers.pc = pop_value(cpu);
            cpu.registers.pc |= pop_value(cpu) << 8;
            // PC needs to be decremented by the length of RET since that will be added by tick
            cpu.registers.pc--;
            return 12;
        }
        else {
            cpu.registers.pc = pop_value(cpu);
            cpu.registers.pc |= pop_value(cpu) << 8;
            return 0;
        }
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

    std::uint8_t sub_carry_impl(CPU& cpu) {
        enum class SubCarryType : std::uint8_t {
            R8 = 0b00000000,
            IMM8 = 0b0100000
        };
        auto instruction = cpu.memory->read(cpu.registers.pc);
        auto sub_type = static_cast<SubCarryType>(instruction & 0b01000000);
        auto first = cpu.registers.a();
        auto second = (sub_type == SubCarryType::R8)
            ? r8_get_value(cpu, instruction & 0b111)
            : cpu.memory->read(cpu.registers.pc + 1);

        std::uint8_t carry_value = cpu.registers.get_flag_c() ? 1 : 0;
        std::uint8_t result = 
            static_cast<std::uint16_t>(static_cast<int>(first) -
            static_cast<int>(second)) -
            carry_value;
        cpu.registers.a() = result;

        cpu.registers.set_flag_z(result == 0);
        cpu.registers.set_flag_n(true);
        cpu.registers.set_flag_h((first & 0xF) < ((second & 0xF) + carry_value));
        cpu.registers.set_flag_c(first < (second + carry_value));

        return 0;
    }

    std::uint8_t add8_impl(CPU& cpu) {
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

    std::uint8_t add8_carry_impl(CPU& cpu) {
        enum class AddCarryType : std::uint8_t {
            R8 = 0b00000000,
            IMM8 = 0b01000000
        };
        auto instruction = cpu.memory->read(cpu.registers.pc);
        auto add_type = static_cast<AddCarryType>(instruction & 0b01000000);
        auto first = cpu.registers.a();
        auto second = (add_type == AddCarryType::R8)
            ? r8_get_value(cpu, instruction & 0b111)
            : cpu.memory->read(cpu.registers.pc + 1);
        
        std::uint8_t carry_value = cpu.registers.get_flag_c() ? 1 : 0;
        auto result_16_bit = 
            static_cast<std::uint16_t>(first) +
            static_cast<std::uint16_t>(second) +
            carry_value;
        auto result = static_cast<std::uint8_t>(result_16_bit);
        cpu.registers.a() = result;

        cpu.registers.set_flag_z(result == 0);
        cpu.registers.set_flag_n(false);
        cpu.registers.set_flag_h(((first & 0xF) + (second & 0xF) + carry_value) > 0xF);
        cpu.registers.set_flag_c(result_16_bit > 0xFF);

        return 0;
    }

    std::uint8_t add_sp_impl(CPU& cpu) {
        std::uint16_t first = cpu.registers.sp;
        // Important: For SP add the second operand is a signed integer
        std::int8_t second = static_cast<std::int8_t>(cpu.memory->read(cpu.registers.pc + 1));
        std::uint16_t result = first + second;
        cpu.registers.sp = result;

        cpu.registers.set_flag_z(false);
        cpu.registers.set_flag_n(false);

        // Computing the half-carry and carry here is much trickier than normally, since
        // because of the signed second operand this could either be an addition or subtraction.
        bool half_carry = ((first ^ (std::uint16_t) second ^ result) & 0x10) == 0x10;
        bool carry = ((first ^ (std::uint16_t) second ^ result) & 0x100) == 0x100;

        cpu.registers.set_flag_h(half_carry);
        cpu.registers.set_flag_c(carry);

        return 0;
    }

    std::uint8_t add16_impl(CPU& cpu) {
        auto instruction = cpu.memory->read(cpu.registers.pc);
        std::uint8_t reg_code = (instruction & 0b00110000) >> 4;
        std::uint16_t reg_value = *r16_lookup(cpu, reg_code);
        std::uint16_t hl_value = cpu.registers.hl;
        std::uint32_t new_value = static_cast<std::uint32_t>(hl_value) + reg_value;
        cpu.registers.hl = static_cast<std::uint16_t>(new_value);

        cpu.registers.set_flag_n(false);
        // Half carry for r16 add indicates overflow from 11th to 12th bit because
        // 16 bit adds are really compromised of two 8 bit adds in the ALU.
        cpu.registers.set_flag_h((hl_value & 0xFFF) + (reg_value & 0xFFF) > 0xFFF);
        cpu.registers.set_flag_c(new_value > 0xFFFF);

        return 0;
    }

    std::uint8_t or_impl(CPU& cpu) {
        enum class OrType : std::uint8_t {
            R8 = 0b10110,
            IMM8 = 0b11110
        };
        auto instruction = cpu.memory->read(cpu.registers.pc);
        auto or_type = static_cast<OrType>((instruction & 0b11111000) >> 3);
        std::uint8_t reg_code = instruction & 0b111;
        std::uint8_t value = (or_type == OrType::R8)
            ? r8_get_value(cpu, reg_code)
            : cpu.memory->read(cpu.registers.pc + 1);
        std::uint8_t new_value = cpu.registers.a() | value;
        cpu.registers.a() = new_value;

        cpu.registers.set_flag_z(new_value == 0);
        cpu.registers.set_flag_n(false);
        cpu.registers.set_flag_h(false);
        cpu.registers.set_flag_c(false);

        return 0;
    }

    std::uint8_t complement_impl(CPU& cpu) {
        cpu.registers.a() = ~cpu.registers.a();

        cpu.registers.set_flag_n(true);
        cpu.registers.set_flag_h(true);

        return 0;
    }

    std::uint8_t restart_impl(CPU& cpu) {
        auto instruction = cpu.memory->read(cpu.registers.pc);
        std::uint8_t target_code = (instruction & 0b00111000) >> 3;
        std::uint8_t target_address = target_code * 8;

        push_value(cpu, cpu.registers.pc + 1);
        cpu.registers.pc = target_address;

        return 0;
    }

    std::uint8_t enable_master_interrupt(CPU& cpu) {
        // Interrupt enable is delayed by one instruction so this only needs to be flagged
        cpu.enable_ime_requested = true;
        return 0;
    }

    std::uint8_t disable_master_interrupt(CPU& cpu) {
        cpu.registers.set_ime(false);
        return 0;   
    }

    std::uint8_t ret_interrupt_impl(CPU& cpu) {
        // Same as return but we also need to re-enable IME
        cpu.registers.set_ime(true);
        cpu.registers.pc = pop_value(cpu);
        cpu.registers.pc |= pop_value(cpu) << 8;
        return 0;
    }

    std::uint8_t binary_coded_decimal_impl(CPU& cpu) {
        std::uint8_t offset = 0;
        std::uint8_t value = cpu.registers.a();
        bool addition = !cpu.registers.get_flag_n();
        bool carry_value = false;
        // If the lower nibble is incorrect or there was a half carry adjust lower digit
        if ((addition && (value & 0x0F) > 0x09) || cpu.registers.get_flag_h()) {
            offset |= 0x06;
        }
        // If the value exceeds max digit or there was a carry adjust higher digit
        if ((addition && value > 0x99) || cpu.registers.get_flag_c()) {
            offset |= 0x60;
            carry_value = true;
        }
        cpu.registers.a() += addition ? offset : -offset;

        std::uint8_t final_value = cpu.registers.a();
        cpu.registers.set_flag_z(final_value == 0);
        cpu.registers.set_flag_h(0);
        cpu.registers.set_flag_c(carry_value);

        return 0;
    }

    std::uint8_t set_carry_flag_impl(CPU& cpu) {
        cpu.registers.set_flag_n(false);
        cpu.registers.set_flag_h(false);
        cpu.registers.set_flag_c(true);
        return 0;
    }

    std::uint8_t flip_carry_flag_impl(CPU& cpu) {
        cpu.registers.set_flag_n(false);
        cpu.registers.set_flag_h(false);
        cpu.registers.set_flag_c(!cpu.registers.get_flag_c());
        return 0;
    }

    std::uint8_t stop_impl(CPU& cpu) {
        // The precondition for stop is that no buttons are pressed and neither buttons nor dpad is selected
        static constexpr std::uint16_t JOYPAD_ADDRESS = 0xFF00;
        static constexpr std::uint8_t EXPECTED_JOYPAD_STATE = 0b00111111;
        if ((cpu.memory->read(JOYPAD_ADDRESS) & EXPECTED_JOYPAD_STATE) != EXPECTED_JOYPAD_STATE) {
            return 0;
        }

        cpu.stop_requested = true;
        // In addition to setting the stop request flag we also need to clear IE
        static constexpr std::uint16_t IE_ADDRESS = 0xFFFF;
        cpu.memory->write(IE_ADDRESS, 0);
        return 0;
    }

    std::uint8_t halt_impl(CPU& cpu) {
        cpu.is_halted = true;
        return 0;
    }

    std::uint8_t nop_impl(CPU&) {
        return 0;
    }

    // For instructions where we do not want PC to be modified after the instruction (such as CALL, RET, etc.)
    // we set the instruction length to be 0. Take care when looking up instruction length as this might yield unexpected values.
    std::unordered_map<std::uint8_t, Instruction> CPU::instruction_set = {
        { 0x00, Instruction { .name = "NOP", .length = 1, .cycles = 4, .implementation = nop_impl } },
        { 0x01, Instruction { .name = "LD BC, IMM16", .length = 3, .cycles = 16, .implementation = load16_impl } },
        { 0x02, Instruction { .name = "LD [BC], A", .length = 1, .cycles = 8, .implementation = load16_impl } },
        { 0x03, Instruction { .name = "INC BC", .length = 1, .cycles = 8, .implementation = inc16_impl } },
        { 0x04, Instruction { .name = "INC B", .length = 1, .cycles = 4, .implementation = inc8_impl } },
        { 0x05, Instruction { .name = "DEC B", .length = 1, .cycles = 4, .implementation = dec8_impl } },
        { 0x06, Instruction { .name = "LD B, IMM8", .length = 2, .cycles = 8, .implementation = load8_imm8_impl } },
        { 0x07, Instruction { .name = "RLCA", .length = 1, .cycles = 4, .implementation = rotate_left_circular_clear_zero_impl } },
        { 0x08, Instruction { .name = "LD [IMM16], SP", .length = 3, .cycles = 20, .implementation = load16_impl } },
        { 0x09, Instruction { .name = "ADD HL, BC", .length = 1, .cycles = 8, .implementation = add16_impl } },
        { 0x0A, Instruction { .name = "LD A, [BC]", .length = 1, .cycles = 8, .implementation = load16_impl } },
        { 0x0B, Instruction { .name = "DEC BC", .length = 1, .cycles = 8, .implementation = dec16_impl } },
        { 0x0C, Instruction { .name = "INC C", .length = 1, .cycles = 4, .implementation = inc8_impl } },
        { 0x0D, Instruction { .name = "DEC C", .length = 1, .cycles = 4, .implementation = dec8_impl } },
        { 0x0E, Instruction { .name = "LD E, IMM8", .length = 2, .cycles = 8, .implementation = load8_imm8_impl } },
        { 0x0F, Instruction { .name = "RRCA", .length = 1, .cycles = 4, .implementation = rotate_right_circular_clear_zero_impl } },
        { 0x10, Instruction { .name = "STOP 0", .length = 2, .cycles = 4, .implementation = stop_impl } },
        { 0x11, Instruction { .name = "LD DE, IMM16", .length = 3, .cycles = 16, .implementation = load16_impl } },
        { 0x12, Instruction { .name = "LD [DE], A", .length = 1, .cycles = 8, .implementation = load16_impl } },
        { 0x13, Instruction { .name = "INC DE", .length = 1, .cycles = 8, .implementation = inc16_impl } },
        { 0x14, Instruction { .name = "INC D", .length = 1, .cycles = 4, .implementation = inc8_impl } },
        { 0x15, Instruction { .name = "DEC D", .length = 1, .cycles = 4, .implementation = dec8_impl } },
        { 0x16, Instruction { .name = "LD D, IMM8", .length = 2, .cycles = 8, .implementation = load8_imm8_impl } },
        { 0x17, Instruction { .name = "RLA", .length = 1, .cycles = 4, .implementation = rotate_left_clear_zero_impl } },
        { 0x18, Instruction { .name = "JR IMM8", .length = 2, .cycles = 12, .implementation = jump_relative_impl } },
        { 0x19, Instruction { .name = "ADD HL, DE", .length = 1, .cycles = 8, .implementation = add16_impl } },
        { 0x1A, Instruction { .name = "LD A, [DE]", .length = 1, .cycles = 8, .implementation = load16_impl } },
        { 0x1B, Instruction { .name = "DEC DE", .length = 1, .cycles = 8, .implementation = dec16_impl } },
        { 0x1C, Instruction { .name = "INC E", .length = 1, .cycles = 4, .implementation = inc8_impl } },
        { 0x1D, Instruction { .name = "DEC E", .length = 1, .cycles = 4, .implementation = dec8_impl } },
        { 0x1E, Instruction { .name = "LD E, IMM8", .length = 2, .cycles = 8, .implementation = load8_imm8_impl } },
        { 0x1F, Instruction { .name = "RRA", .length = 1, .cycles = 4, .implementation = rotate_right_clear_zero_impl } },
        { 0x20, Instruction { .name = "JR NZ, IMM8", .length = 2, .cycles = 8, .implementation = jump_relative_impl } },
        { 0x21, Instruction { .name = "LD HL, IMM16", .length = 3, .cycles = 16, .implementation = load16_impl } },
        { 0x22, Instruction { .name = "LD [HL+], A", .length = 1, .cycles = 8, .implementation = load16_impl } },
        { 0x23, Instruction { .name = "INC HL", .length = 1, .cycles = 8, .implementation = inc16_impl } },
        { 0x24, Instruction { .name = "INC H", .length = 1, .cycles = 4, .implementation = inc8_impl } },
        { 0x25, Instruction { .name = "DEC H", .length = 1, .cycles = 4, .implementation = dec8_impl } },
        { 0x26, Instruction { .name = "LD H, IMM8", .length = 2, .cycles = 8, .implementation = load8_imm8_impl } },
        { 0x27, Instruction { .name = "DAA", .length = 1, .cycles = 4, .implementation = binary_coded_decimal_impl } },
        { 0x28, Instruction { .name = "JR Z, IMM8", .length = 2, .cycles = 8, .implementation = jump_relative_impl } },
        { 0x29, Instruction { .name = "ADD HL, HL", .length = 1, .cycles = 8, .implementation = add16_impl } },
        { 0x2A, Instruction { .name = "LD A, [HL+]", .length = 1, .cycles = 8, .implementation = load16_impl } },
        { 0x2B, Instruction { .name = "DEC HL", .length = 1, .cycles = 8, .implementation = dec16_impl } },
        { 0x2C, Instruction { .name = "INC L", .length = 1, .cycles = 4, .implementation = inc8_impl } },
        { 0x2D, Instruction { .name = "DEC L", .length = 1, .cycles = 4, .implementation = dec8_impl } },
        { 0x2E, Instruction { .name = "LD L, IMM8", .length = 2, .cycles = 8, .implementation = load8_imm8_impl } },
        { 0x2F, Instruction { .name = "CPL", .length = 1, .cycles = 4, .implementation = complement_impl } },
        { 0x30, Instruction { .name = "JR NC, IMM8", .length = 2, .cycles = 8, .implementation = jump_relative_impl } },
        { 0x31, Instruction { .name = "LD SP, IMM16", .length = 3, .cycles = 16, .implementation = load16_impl } },
        { 0x32, Instruction { .name = "LD SP, IMM16", .length = 1, .cycles = 16, .implementation = load16_impl } },
        { 0x33, Instruction { .name = "INC SP", .length = 1, .cycles = 8, .implementation = inc16_impl } },
        { 0x34, Instruction { .name = "INC [HL]", .length = 1, .cycles = 12, .implementation = inc8_impl } },
        { 0x35, Instruction { .name = "DEC [HL]", .length = 1, .cycles = 12, .implementation = dec8_impl } },
        { 0x36, Instruction { .name = "LD [HL], IMM8", .length = 2, .cycles = 12, .implementation = load8_imm8_impl } },
        { 0x37, Instruction { .name = "SCF", .length = 1, .cycles = 4, .implementation = set_carry_flag_impl } },
        { 0x38, Instruction { .name = "JR C, r8", .length = 2, .cycles = 8, .implementation = jump_relative_impl } },
        { 0x39, Instruction { .name = "ADD HL, SP", .length = 1, .cycles = 8, .implementation = add16_impl } },
        { 0x3A, Instruction { .name = "LD A, [HL-]", .length = 1, .cycles = 8, .implementation = load16_impl } },
        { 0x3B, Instruction { .name = "DEC SP", .length = 1, .cycles = 8, .implementation = dec16_impl } },
        { 0x3C, Instruction { .name = "INC A", .length = 1, .cycles = 4, .implementation = inc8_impl } },
        { 0x3D, Instruction { .name = "DEC A", .length = 1, .cycles = 4, .implementation = dec8_impl } },
        { 0x3E, Instruction { .name = "LD A, IMM8", .length = 2, .cycles = 8, .implementation = load8_imm8_impl } },
        { 0x3F, Instruction { .name = "CCF", .length = 1, .cycles = 4, .implementation = flip_carry_flag_impl } },
        { 0x40, Instruction { .name = "LD B, B", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x41, Instruction { .name = "LD B, C", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x42, Instruction { .name = "LD B, D", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x43, Instruction { .name = "LD B, E", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x44, Instruction { .name = "LD B, H", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x45, Instruction { .name = "LD B, L", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x46, Instruction { .name = "LD B, [HL]", .length = 1, .cycles = 8, .implementation = load8_r8_impl } },
        { 0x47, Instruction { .name = "LD B, A", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x48, Instruction { .name = "LD C, B", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x49, Instruction { .name = "LD C, C", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x4A, Instruction { .name = "LD C, D", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x4B, Instruction { .name = "LD C, E", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x4C, Instruction { .name = "LD C, H", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x4D, Instruction { .name = "LD C, L", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x4E, Instruction { .name = "LD C, [HL]", .length = 1, .cycles = 8, .implementation = load8_r8_impl } },
        { 0x4F, Instruction { .name = "LD C, A", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x50, Instruction { .name = "LD D, B", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x51, Instruction { .name = "LD D, C", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x52, Instruction { .name = "LD D, D", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x53, Instruction { .name = "LD D, E", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x54, Instruction { .name = "LD D, H", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x55, Instruction { .name = "LD D, L", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x56, Instruction { .name = "LD D, [HL]", .length = 1, .cycles = 8, .implementation = load8_r8_impl } },
        { 0x57, Instruction { .name = "LD D, A", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x58, Instruction { .name = "LD E, B", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x59, Instruction { .name = "LD E, C", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x5A, Instruction { .name = "LD E, D", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x5B, Instruction { .name = "LD E, E", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x5C, Instruction { .name = "LD E, H", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x5D, Instruction { .name = "LD E, L", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x5E, Instruction { .name = "LD E, [HL]", .length = 1, .cycles = 8, .implementation = load8_r8_impl } },
        { 0x5F, Instruction { .name = "LD E, A", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x60, Instruction { .name = "LD H, B", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x61, Instruction { .name = "LD H, C", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x62, Instruction { .name = "LD H, D", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x63, Instruction { .name = "LD H, E", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x64, Instruction { .name = "LD H, H", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x65, Instruction { .name = "LD H, L", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x66, Instruction { .name = "LD H, [HL]", .length = 1, .cycles = 8, .implementation = load8_r8_impl } },
        { 0x67, Instruction { .name = "LD H, A", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x68, Instruction { .name = "LD L, B", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x69, Instruction { .name = "LD L, C", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x6A, Instruction { .name = "LD L, D", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x6B, Instruction { .name = "LD L, E", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x6C, Instruction { .name = "LD L, H", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x6D, Instruction { .name = "LD L, L", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x6E, Instruction { .name = "LD L, [HL]", .length = 1, .cycles = 8, .implementation = load8_r8_impl } },
        { 0x6F, Instruction { .name = "LD L, A", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x70, Instruction { .name = "LD [HL], B", .length = 1, .cycles = 8, .implementation = load8_r8_impl } },
        { 0x71, Instruction { .name = "LD [HL], C", .length = 1, .cycles = 8, .implementation = load8_r8_impl } },
        { 0x72, Instruction { .name = "LD [HL], D", .length = 1, .cycles = 8, .implementation = load8_r8_impl } },
        { 0x73, Instruction { .name = "LD [HL], E", .length = 1, .cycles = 8, .implementation = load8_r8_impl } },
        { 0x74, Instruction { .name = "LD [HL], H", .length = 1, .cycles = 8, .implementation = load8_r8_impl } },
        { 0x75, Instruction { .name = "LD [HL], L", .length = 1, .cycles = 8, .implementation = load8_r8_impl } },
        { 0x76, Instruction { .name = "HALT", .length = 1, .cycles = 4, .implementation = halt_impl } },
        { 0x77, Instruction { .name = "LD [HL], A", .length = 1, .cycles = 8, .implementation = load8_r8_impl } },
        { 0x78, Instruction { .name = "LD A, B", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x79, Instruction { .name = "LD A, C", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x7A, Instruction { .name = "LD A, D", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x7B, Instruction { .name = "LD A, E", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x7C, Instruction { .name = "LD A, H", .length = 1, .cycles = 4, .implementation = load8_r8_impl} },
        { 0x7D, Instruction { .name = "LD A, L", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x7E, Instruction { .name = "LD A, [HL]", .length = 1, .cycles = 8, .implementation = load8_r8_impl } },
        { 0x7F, Instruction { .name = "LD A, A", .length = 1, .cycles = 4, .implementation = load8_r8_impl } },
        { 0x80, Instruction { .name = "ADD A, B", .length = 1, .cycles = 4, .implementation = add8_impl } },
        { 0x81, Instruction { .name = "ADD A, C", .length = 1, .cycles = 4, .implementation = add8_impl } },
        { 0x82, Instruction { .name = "ADD A, D", .length = 1, .cycles = 4, .implementation = add8_impl } },
        { 0x83, Instruction { .name = "ADD A, E", .length = 1, .cycles = 4, .implementation = add8_impl } },
        { 0x84, Instruction { .name = "ADD A, H", .length = 1, .cycles = 4, .implementation = add8_impl } },
        { 0x85, Instruction { .name = "ADD A, L", .length = 1, .cycles = 4, .implementation = add8_impl } },
        { 0x86, Instruction { .name = "ADD A, [HL]", .length = 1, .cycles = 8, .implementation = add8_impl } },
        { 0x87, Instruction { .name = "ADD A, A", .length = 1, .cycles = 4, .implementation = add8_impl } },
        { 0x88, Instruction { .name = "ADC A, B", .length = 1, .cycles = 4, .implementation = add8_carry_impl } },
        { 0x89, Instruction { .name = "ADC A, C", .length = 1, .cycles = 4, .implementation = add8_carry_impl } },
        { 0x8A, Instruction { .name = "ADC A, D", .length = 1, .cycles = 4, .implementation = add8_carry_impl } },
        { 0x8B, Instruction { .name = "ADC A, E", .length = 1, .cycles = 4, .implementation = add8_carry_impl } },
        { 0x8C, Instruction { .name = "ADC A, H", .length = 1, .cycles = 4, .implementation = add8_carry_impl } },
        { 0x8D, Instruction { .name = "ADC A, L", .length = 1, .cycles = 4, .implementation = add8_carry_impl } },
        { 0x8E, Instruction { .name = "ADC A, [HL]", .length = 1, .cycles = 8, .implementation = add8_carry_impl } },
        { 0x8F, Instruction { .name = "ADC A, A", .length = 1, .cycles = 4, .implementation = add8_carry_impl } },
        { 0x90, Instruction { .name = "SUB A, B", .length = 1, .cycles = 4, .implementation = sub_impl } },
        { 0x91, Instruction { .name = "SUB A, C", .length = 1, .cycles = 4, .implementation = sub_impl } },
        { 0x92, Instruction { .name = "SUB A, D", .length = 1, .cycles = 4, .implementation = sub_impl } },
        { 0x93, Instruction { .name = "SUB A, E", .length = 1, .cycles = 4, .implementation = sub_impl } },
        { 0x94, Instruction { .name = "SUB A, H", .length = 1, .cycles = 4, .implementation = sub_impl } },
        { 0x95, Instruction { .name = "SUB A, L", .length = 1, .cycles = 4, .implementation = sub_impl } },
        { 0x96, Instruction { .name = "SUB A, [HL]", .length = 1, .cycles = 8, .implementation = sub_impl } },
        { 0x97, Instruction { .name = "SUB A, A", .length = 1, .cycles = 4, .implementation = sub_impl } },
        { 0x98, Instruction { .name = "SBC A, B", .length = 1, .cycles = 4, .implementation = sub_carry_impl } },
        { 0x99, Instruction { .name = "SBC A, C", .length = 1, .cycles = 4, .implementation = sub_carry_impl } },
        { 0x9A, Instruction { .name = "SBC A, D", .length = 1, .cycles = 4, .implementation = sub_carry_impl } },
        { 0x9B, Instruction { .name = "SBC A, E", .length = 1, .cycles = 4, .implementation = sub_carry_impl } },
        { 0x9C, Instruction { .name = "SBC A, H", .length = 1, .cycles = 4, .implementation = sub_carry_impl } },
        { 0x9D, Instruction { .name = "SBC A, L", .length = 1, .cycles = 4, .implementation = sub_carry_impl } },
        { 0x9E, Instruction { .name = "SBC A, [HL]", .length = 1, .cycles = 8, .implementation = sub_carry_impl } },
        { 0x9F, Instruction { .name = "SBC A, A", .length = 1, .cycles = 4, .implementation = sub_carry_impl } },
        { 0xA0, Instruction { .name = "AND A, B", .length = 1, .cycles = 4, .implementation = and_impl } },
        { 0xA1, Instruction { .name = "AND A, C", .length = 1, .cycles = 4, .implementation = and_impl } },
        { 0xA2, Instruction { .name = "AND A, D", .length = 1, .cycles = 4, .implementation = and_impl } },
        { 0xA3, Instruction { .name = "AND A, E", .length = 1, .cycles = 4, .implementation = and_impl } },
        { 0xA4, Instruction { .name = "AND A, H", .length = 1, .cycles = 4, .implementation = and_impl } },
        { 0xA5, Instruction { .name = "AND A, L", .length = 1, .cycles = 4, .implementation = and_impl } },
        { 0xA6, Instruction { .name = "AND A, [HL]", .length = 1, .cycles = 8, .implementation = and_impl } },
        { 0xA7, Instruction { .name = "AND A, A", .length = 1, .cycles = 4, .implementation = and_impl } },
        { 0xA8, Instruction { .name = "XOR A, B", .length = 1, .cycles = 4, .implementation = xor_impl } },
        { 0xA9, Instruction { .name = "XOR A, C", .length = 1, .cycles = 4, .implementation = xor_impl } },
        { 0xAA, Instruction { .name = "XOR A, D", .length = 1, .cycles = 4, .implementation = xor_impl } },
        { 0xAB, Instruction { .name = "XOR A, E", .length = 1, .cycles = 4, .implementation = xor_impl } },
        { 0xAC, Instruction { .name = "XOR A, H", .length = 1, .cycles = 4, .implementation = xor_impl } },
        { 0xAD, Instruction { .name = "XOR A, L", .length = 1, .cycles = 4, .implementation = xor_impl } },
        { 0xAE, Instruction { .name = "XOR [HL]", .length = 1, .cycles = 8, .implementation = xor_impl } },
        { 0xAF, Instruction { .name = "XOR A, A", .length = 1, .cycles = 4, .implementation = xor_impl } },
        { 0xB0, Instruction { .name = "OR B", .length = 1, .cycles = 4, .implementation = or_impl } },
        { 0xB1, Instruction { .name = "OR C", .length = 1, .cycles = 4, .implementation = or_impl }},
        { 0xB2, Instruction { .name = "OR D", .length = 1, .cycles = 4, .implementation = or_impl }},
        { 0xB3, Instruction { .name = "OR E", .length = 1, .cycles = 4, .implementation = or_impl }},
        { 0xB4, Instruction { .name = "OR H", .length = 1, .cycles = 4, .implementation = or_impl }},
        { 0xB5, Instruction { .name = "OR L", .length = 1, .cycles = 4, .implementation = or_impl }},
        { 0xB6, Instruction { .name = "OR [HL]", .length = 1, .cycles = 8, .implementation = or_impl }},
        { 0xB7, Instruction { .name = "OR A", .length = 1, .cycles = 4, .implementation = or_impl }},
        { 0xB8, Instruction { .name = "CP A, B", .length = 1, .cycles = 4, .implementation = compare_impl } },
        { 0xB9, Instruction { .name = "CP A, C", .length = 1, .cycles = 4, .implementation = compare_impl } },
        { 0xBA, Instruction { .name = "CP A, D", .length = 1, .cycles = 4, .implementation = compare_impl } },
        { 0xBB, Instruction { .name = "CP A, E", .length = 1, .cycles = 4, .implementation = compare_impl } },
        { 0xBC, Instruction { .name = "CP A, H", .length = 1, .cycles = 4, .implementation = compare_impl } },
        { 0xBD, Instruction { .name = "CP A, L", .length = 1, .cycles = 4, .implementation = compare_impl } },
        { 0xBE, Instruction { .name = "CP A, [HL]", .length = 1, .cycles = 8, .implementation = compare_impl } },
        { 0xBF, Instruction { .name = "CP A, A", .length = 1, .cycles = 4, .implementation = compare_impl } },
        { 0xC0, Instruction { .name = "RET NZ", .length = 1, .cycles = 8, .implementation = ret_impl } },
        { 0xC1, Instruction { .name = "POP BC", .length = 1, .cycles = 12, .implementation = pop_impl } },
        { 0xC2, Instruction { .name = "JP NZ, IMM16", .length = 3, .cycles = 12, .implementation = jump_absolute_impl } },
        { 0xC3, Instruction { .name = "JP IMM16", .length = 0, .cycles = 12, .implementation = jump_absolute_impl } },
        { 0xC4, Instruction { .name = "CALL NZ, IMM16", .length = 3, .cycles = 12, .implementation = call_impl } },
        { 0xC5, Instruction { .name = "PUSH BC", .length = 1, .cycles = 16, .implementation = push_impl } },
        { 0xC6, Instruction { .name = "ADD A, IMM8", .length = 2, .cycles = 8, .implementation = add8_impl } },
        { 0xC7, Instruction { .name = "RST 00H", .length = 0, .cycles = 16, .implementation = restart_impl } },
        { 0xC8, Instruction { .name = "RET Z", .length = 1, .cycles = 8, .implementation = ret_impl } },
        { 0xC9, Instruction { .name = "RET", .length = 0, .cycles = 16, .implementation = ret_impl } },
        { 0xCA, Instruction { .name = "JP Z, IMM16", .length = 3, .cycles = 12, .implementation = jump_absolute_impl } },
        { 0xCB, Instruction { .name = "PREFIX", .length = 1, .cycles = 4, .implementation = enable_prefix } },
        { 0xCC, Instruction { .name = "CALL Z, IMM16", .length = 3, .cycles = 12, .implementation = call_impl } },
        { 0xCD, Instruction { .name = "CALL IMM16", .length = 0, .cycles = 24, .implementation = call_impl } },
        { 0xCE, Instruction { .name = "ADC A, IMM8", .length = 2, .cycles = 8, .implementation = add8_carry_impl } },
        { 0xCF, Instruction { .name = "RST 08H", .length = 0, .cycles = 16, .implementation = restart_impl } },
        { 0xD0, Instruction { .name = "RET NC", .length = 1, .cycles = 8, .implementation = ret_impl } },
        { 0xD1, Instruction { .name = "POP DE", .length = 1, .cycles = 12, .implementation = pop_impl } },
        { 0xD2, Instruction { .name = "JP NC, IMM16", .length = 3, .cycles = 12, .implementation = jump_absolute_impl } },
        // 0xD3 is not a valid instruction
        { 0xD4, Instruction { .name = "CALL NC, IMM16", .length = 3, .cycles = 12, .implementation = call_impl } },
        { 0xD5, Instruction { .name = "PUSH DE", .length = 1, .cycles = 16, .implementation = push_impl } },
        { 0xD6, Instruction { .name = "SUB IMM8", .length = 2, .cycles = 8, .implementation = sub_impl } },
        { 0xD7, Instruction { .name = "RST 10H", .length = 0, .cycles = 16, .implementation = restart_impl } },
        { 0xD8, Instruction { .name = "RET C", .length = 1, .cycles = 8, .implementation = ret_impl } },
        { 0xD9, Instruction { .name = "RETI", .length = 0, .cycles = 16, .implementation = ret_interrupt_impl } },
        { 0xDA, Instruction { .name = "JP C, IMM16", .length = 3, .cycles = 12, .implementation = jump_absolute_impl } },
        // 0xDB is not a valid instruction
        { 0xDC, Instruction { .name = "CALL C, IMM16", .length = 3, .cycles = 12, .implementation = call_impl } },
        // 0xDD is not a valid instruction
        { 0xDE, Instruction { .name = "SBC A, IMM8", .length = 2, .cycles = 8, .implementation = sub_carry_impl } },
        { 0xDF, Instruction { .name = "RST 18H", .length = 0, .cycles = 16, .implementation = restart_impl } },
        { 0xE0, Instruction { .name = "LDH [IMM8], A", .length = 2, .cycles = 12, .implementation = load8_high_impl } },
        { 0xE1, Instruction { .name = "POP HL", .length = 1, .cycles = 12, .implementation = pop_impl } },
        { 0xE2, Instruction { .name = "LDH [C], A", .length = 1, .cycles = 8, .implementation = load8_high_impl } },
        // 0xE3 is not a valid instruction
        // 0xE4 is not a valid instruction
        { 0xE5, Instruction { .name = "PUSH HL", .length = 1, .cycles = 16, .implementation = push_impl} },
        { 0xE6, Instruction { .name = "AND IMM8", .length = 2, .cycles = 8, .implementation =  and_impl } },
        { 0xE7, Instruction { .name = "RST 20H", .length = 0, .cycles = 16, .implementation = restart_impl } },
        { 0xE8, Instruction { .name = "ADD SP, IMM8", .length = 2, .cycles = 16, .implementation = add_sp_impl } },
        { 0xE9, Instruction { .name = "JP HL", .length = 0, .cycles = 4, .implementation = jump_absolute_impl } },
        { 0xEA, Instruction { .name = "LD [IMM16], A", .length = 3, .cycles = 16, .implementation = load16_imm16mem_impl } },
        // 0xEB is not a valid instruction
        { 0xEC, Instruction { .name = "CALL C, IMM16", .length = 3, .cycles = 12, .implementation = call_impl } },
        // 0xED is not a valid instruction
        { 0xEE, Instruction { .name = "XOR IMM8", .length = 2, .cycles = 8, .implementation = xor_impl } },
        { 0xEF, Instruction { .name = "RST 28H", .length = 0, .cycles = 16, .implementation = restart_impl } },
        { 0xF0, Instruction { .name = "LDH A, IMM8", .length = 2, .cycles = 12, .implementation = load8_high_impl } },
        { 0xF1, Instruction { .name = "POP AF", .length = 1, .cycles = 12, .implementation = pop_impl} },
        { 0xF2, Instruction { .name = "LD A, [C]", .length = 1, .cycles = 8, .implementation = load8_high_impl } },
        { 0xF3, Instruction { .name = "DI", .length = 1, .cycles = 4, .implementation = disable_master_interrupt } },
        // 0xF4 is not a valid instruction
        { 0xF5, Instruction { .name = "PUSH AF", .length = 1, .cycles = 16, .implementation = push_impl } },
        { 0xF6, Instruction { .name = "OR IMM8", .length = 2, .cycles = 8, .implementation = or_impl } },
        { 0xF7, Instruction { .name = "RST 30H", .length = 0, .cycles = 16, .implementation = restart_impl } },
        { 0xF8, Instruction { .name = "LD HL, SP + IMM8", .length = 2, .cycles = 12, .implementation = load_hl_sp_r8 } },
        { 0xF9, Instruction { .name = "LD SP, HL", .length = 1, .cycles = 8, .implementation = load_sp_hl } },
        { 0xFA, Instruction { .name = "LD A, [IMM16]", .length = 3, .cycles = 16, .implementation = load16_imm16mem_impl } },
        { 0xFB, Instruction { .name = "EI", .length = 1, .cycles = 4, .implementation = enable_master_interrupt } },
        // 0xFC is not a valid instruction
        // 0xFD is not a valid instruction
        { 0xFE, Instruction { .name = "CP A, IMM8", .length = 2, .cycles = 8, .implementation = compare_impl } },
        { 0xFF, Instruction { .name = "RST 38H", .length = 0, .cycles = 16, .implementation = restart_impl } },
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

    std::uint8_t swap_impl(CPU& cpu) {
        auto instruction = cpu.memory->read(cpu.registers.pc);
        std::uint8_t register_code = instruction & 0b111;
        std::uint8_t register_value = r8_get_value(cpu, register_code);
        std::uint8_t new_value = ((register_value & 0x0F) << 4) | ((register_value & 0xF0) >> 4);
        r8_set_value(cpu, register_code, new_value);

        cpu.registers.set_flag_z(new_value == 0);
        cpu.registers.set_flag_n(false);
        cpu.registers.set_flag_h(false);
        cpu.registers.set_flag_c(false);

        return 0;
    }

    std::uint8_t reset_impl(CPU& cpu) {
        auto instruction = cpu.memory->read(cpu.registers.pc);
        std::uint8_t register_code = instruction & 0b111;
        std::uint8_t register_value = r8_get_value(cpu, register_code);
        std::uint8_t bit_index = (instruction & 0b00111000) >> 3;
        std::uint8_t mask = 0b11111111 & (~(1 << bit_index));
        std::uint8_t new_value = register_value & mask;
        r8_set_value(cpu, register_code, new_value);
        return 0;
    }

    std::uint8_t set_impl(CPU& cpu) {
        auto instruction = cpu.memory->read(cpu.registers.pc);
        std::uint8_t register_code = instruction & 0b111;
        std::uint8_t register_value = r8_get_value(cpu, register_code);
        std::uint8_t bit_index = (instruction & 0b00111000) >> 3;
        std::uint8_t new_value = register_value | (1 << bit_index);
        r8_set_value(cpu, register_code, new_value);
        return 0;
    }

    std::uint8_t shift_left_impl(CPU& cpu) {
        auto instruction = cpu.memory->read(cpu.registers.pc);
        std::uint8_t register_code = instruction & 0b111;
        std::uint8_t register_value = r8_get_value(cpu, register_code);
        bool carry_value = (register_value & 0b10000000) != 0;
        std::uint8_t result = register_value << 1;
        r8_set_value(cpu, register_code, result);

        cpu.registers.set_flag_z(result == 0);
        cpu.registers.set_flag_n(false);
        cpu.registers.set_flag_h(false);
        cpu.registers.set_flag_c(carry_value);

        return 0;
    }

    std::uint8_t shift_right_impl(CPU& cpu, bool clear_highest_bit) {
        auto instruction = cpu.memory->read(cpu.registers.pc);
        std::uint8_t register_code = instruction & 0b111;
        std::uint8_t register_value = r8_get_value(cpu, register_code);
        bool carry_value = (register_value & 0b1) != 0;
        std::uint8_t previous_highest_bit = register_value & 0b10000000;
        std::uint8_t result = register_value >> 1;
        // If we are not supposed to clear the highest bit, restore it
        if (!clear_highest_bit) {
            result |= previous_highest_bit;
        }
        r8_set_value(cpu, register_code, result);

        cpu.registers.set_flag_z(result == 0);
        cpu.registers.set_flag_n(false);
        cpu.registers.set_flag_h(false);
        cpu.registers.set_flag_c(carry_value);
    
        return 0;
    }

    std::uint8_t shift_right_clear_impl(CPU& cpu) {
        return shift_right_impl(cpu, true);
    }

    std::uint8_t shift_right_no_clear_impl(CPU& cpu) {
        return shift_right_impl(cpu, false);
    }

    // Instruction length and cycles here do NOT contain the length and cycle count of the prefix instruction itself.
    std::unordered_map<std::uint8_t, Instruction> CPU::prefixed_instruction_set = {
        { 0x00, Instruction { .name = "RLC B", .length = 1, .cycles = 4, .implementation = rotate_left_circular_set_zero_impl } },
        { 0x01, Instruction { .name = "RLC C", .length = 1, .cycles = 4, .implementation = rotate_left_circular_set_zero_impl } },
        { 0x02, Instruction { .name = "RLC D", .length = 1, .cycles = 4, .implementation = rotate_left_circular_set_zero_impl } },
        { 0x03, Instruction { .name = "RLC E", .length = 1, .cycles = 4, .implementation = rotate_left_circular_set_zero_impl } },
        { 0x04, Instruction { .name = "RLC H", .length = 1, .cycles = 4, .implementation = rotate_left_circular_set_zero_impl } },
        { 0x05, Instruction { .name = "RLC L", .length = 1, .cycles = 4, .implementation = rotate_left_circular_set_zero_impl } },
        { 0x06, Instruction { .name = "RLC [HL]", .length = 1, .cycles = 4, .implementation = rotate_left_circular_set_zero_impl } },
        { 0x07, Instruction { .name = "RLC A", .length = 1, .cycles = 4, .implementation = rotate_left_circular_set_zero_impl } },
        { 0x08, Instruction { .name = "RRC B", .length = 1, .cycles = 4, .implementation = rotate_right_circular_set_zero_impl } },
        { 0x09, Instruction { .name = "RRC C", .length = 1, .cycles = 4, .implementation = rotate_right_circular_set_zero_impl } },
        { 0x0A, Instruction { .name = "RRC D", .length = 1, .cycles = 4, .implementation = rotate_right_circular_set_zero_impl } },
        { 0x0B, Instruction { .name = "RRC E", .length = 1, .cycles = 4, .implementation = rotate_right_circular_set_zero_impl } },
        { 0x0C, Instruction { .name = "RRC H", .length = 1, .cycles = 4, .implementation = rotate_right_circular_set_zero_impl } },
        { 0x0D, Instruction { .name = "RRC L", .length = 1, .cycles = 4, .implementation = rotate_right_circular_set_zero_impl } },
        { 0x0E, Instruction { .name = "RRC [HL]", .length = 1, .cycles = 4, .implementation = rotate_right_circular_set_zero_impl } },
        { 0x0F, Instruction { .name = "RRC A", .length = 1, .cycles = 4, .implementation = rotate_right_circular_set_zero_impl } },
        { 0x10, Instruction { .name = "RL B", .length = 1, .cycles = 4, .implementation = rotate_left_set_zero_impl } },
        { 0x11, Instruction { .name = "RL C", .length = 1, .cycles = 4, .implementation = rotate_left_set_zero_impl } },
        { 0x12, Instruction { .name = "RL D", .length = 1, .cycles = 4, .implementation = rotate_left_set_zero_impl } },
        { 0x13, Instruction { .name = "RL E", .length = 1, .cycles = 4, .implementation = rotate_left_set_zero_impl } },
        { 0x14, Instruction { .name = "RL H", .length = 1, .cycles = 4, .implementation = rotate_left_set_zero_impl } },
        { 0x15, Instruction { .name = "RL L", .length = 1, .cycles = 4, .implementation = rotate_left_set_zero_impl } },
        { 0x16, Instruction { .name = "RL [HL]", .length = 1, .cycles = 12, .implementation = rotate_left_set_zero_impl } },
        { 0x17, Instruction { .name = "RL A", .length = 1, .cycles = 4, .implementation = rotate_left_set_zero_impl } },
        { 0x18, Instruction { .name = "RR B", .length = 1, .cycles = 4, .implementation = rotate_right_set_zero_impl } },
        { 0x19, Instruction { .name = "RR C", .length = 1, .cycles = 4, .implementation = rotate_right_set_zero_impl } },
        { 0x1A, Instruction { .name = "RR D", .length = 1, .cycles = 4, .implementation = rotate_right_set_zero_impl } },
        { 0x1B, Instruction { .name = "RR E", .length = 1, .cycles = 4, .implementation = rotate_right_set_zero_impl } },
        { 0x1C, Instruction { .name = "RR H", .length = 1, .cycles = 4, .implementation = rotate_right_set_zero_impl } },
        { 0x1D, Instruction { .name = "RR L", .length = 1, .cycles = 4, .implementation = rotate_right_set_zero_impl } },
        { 0x1E, Instruction { .name = "RR [HL]", .length = 1, .cycles = 12, .implementation = rotate_right_set_zero_impl } },
        { 0x1F, Instruction { .name = "RR A", .length = 1, .cycles = 4, .implementation = rotate_right_set_zero_impl } },
        { 0x20, Instruction { .name = "SLA B", .length = 1, .cycles = 4, .implementation = shift_left_impl } },
        { 0x21, Instruction { .name = "SLA C", .length = 1, .cycles = 4, .implementation = shift_left_impl } },
        { 0x22, Instruction { .name = "SLA D", .length = 1, .cycles = 4, .implementation = shift_left_impl } },
        { 0x23, Instruction { .name = "SLA E", .length = 1, .cycles = 4, .implementation = shift_left_impl } },
        { 0x24, Instruction { .name = "SLA H", .length = 1, .cycles = 4, .implementation = shift_left_impl } },
        { 0x25, Instruction { .name = "SLA L", .length = 1, .cycles = 4, .implementation = shift_left_impl } },
        { 0x26, Instruction { .name = "SLA [HL]", .length = 1, .cycles = 12, .implementation = shift_left_impl } },
        { 0x27, Instruction { .name = "SLA A", .length = 1, .cycles = 4, .implementation = shift_left_impl } },
        { 0x28, Instruction { .name = "SRA B", .length = 1, .cycles = 4, .implementation = shift_right_no_clear_impl } },
        { 0x29, Instruction { .name = "SRA C", .length = 1, .cycles = 4, .implementation = shift_right_no_clear_impl } },
        { 0x2A, Instruction { .name = "SRA D", .length = 1, .cycles = 4, .implementation = shift_right_no_clear_impl } },
        { 0x2B, Instruction { .name = "SRA E", .length = 1, .cycles = 4, .implementation = shift_right_no_clear_impl } },
        { 0x2C, Instruction { .name = "SRA H", .length = 1, .cycles = 4, .implementation = shift_right_no_clear_impl } },
        { 0x2D, Instruction { .name = "SRA L", .length = 1, .cycles = 4, .implementation = shift_right_no_clear_impl } },
        { 0x2E, Instruction { .name = "SRA [HL]", .length = 1, .cycles = 12, .implementation = shift_right_no_clear_impl } },
        { 0x2F, Instruction { .name = "SRA A", .length = 1, .cycles = 4, .implementation = shift_right_no_clear_impl } },
        { 0x30, Instruction { .name = "SWAP B", .length = 1, .cycles = 4, .implementation = swap_impl } },
        { 0x31, Instruction { .name = "SWAP C", .length = 1, .cycles = 4, .implementation = swap_impl } },
        { 0x32, Instruction { .name = "SWAP D", .length = 1, .cycles = 4, .implementation = swap_impl } },
        { 0x33, Instruction { .name = "SWAP E", .length = 1, .cycles = 4, .implementation = swap_impl } },
        { 0x34, Instruction { .name = "SWAP H", .length = 1, .cycles = 4, .implementation = swap_impl } },
        { 0x35, Instruction { .name = "SWAP L", .length = 1, .cycles = 4, .implementation = swap_impl } },
        { 0x36, Instruction { .name = "SWAP [HL]", .length = 1, .cycles = 12, .implementation = swap_impl } },
        { 0x37, Instruction { .name = "SWAP A", .length = 1, .cycles = 4, .implementation = swap_impl } },
        { 0x38, Instruction { .name = "SRL B", .length = 1, .cycles = 4, .implementation = shift_right_clear_impl } },
        { 0x39, Instruction { .name = "SRL C", .length = 1, .cycles = 4, .implementation = shift_right_clear_impl } },
        { 0x3A, Instruction { .name = "SRL D", .length = 1, .cycles = 4, .implementation = shift_right_clear_impl } },
        { 0x3B, Instruction { .name = "SRL E", .length = 1, .cycles = 4, .implementation = shift_right_clear_impl } },
        { 0x3C, Instruction { .name = "SRL H", .length = 1, .cycles = 4, .implementation = shift_right_clear_impl } },
        { 0x3D, Instruction { .name = "SRL L", .length = 1, .cycles = 4, .implementation = shift_right_clear_impl } },
        { 0x3E, Instruction { .name = "SRL [HL]", .length = 1, .cycles = 12, .implementation = shift_right_clear_impl } },
        { 0x3F, Instruction { .name = "SRL A", .length = 1, .cycles = 4, .implementation = shift_right_clear_impl } },
        { 0x40, Instruction { .name = "BIT 0, B", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x41, Instruction { .name = "BIT 0, C", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x42, Instruction { .name = "BIT 0, D", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x43, Instruction { .name = "BIT 0, E", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x44, Instruction { .name = "BIT 0, H", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x45, Instruction { .name = "BIT 0, L", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x46, Instruction { .name = "BIT 0, [HL]", .length = 1, .cycles = 12, .implementation = bit_impl } },
        { 0x47, Instruction { .name = "BIT 0, A", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x48, Instruction { .name = "BIT 1, B", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x49, Instruction { .name = "BIT 1, C", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x4A, Instruction { .name = "BIT 1, D", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x4B, Instruction { .name = "BIT 1, E", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x4C, Instruction { .name = "BIT 1, H", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x4D, Instruction { .name = "BIT 1, L", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x4E, Instruction { .name = "BIT 1, [HL]", .length = 1, .cycles = 12, .implementation = bit_impl } },
        { 0x4F, Instruction { .name = "BIT 1, A", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x50, Instruction { .name = "BIT 2, B", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x51, Instruction { .name = "BIT 2, C", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x52, Instruction { .name = "BIT 2, D", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x53, Instruction { .name = "BIT 2, E", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x54, Instruction { .name = "BIT 2, H", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x55, Instruction { .name = "BIT 2, L", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x56, Instruction { .name = "BIT 2, [HL]", .length = 1, .cycles = 12, .implementation = bit_impl } },
        { 0x57, Instruction { .name = "BIT 2, A", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x58, Instruction { .name = "BIT 3, B", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x59, Instruction { .name = "BIT 3, C", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x5A, Instruction { .name = "BIT 3, D", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x5B, Instruction { .name = "BIT 3, E", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x5C, Instruction { .name = "BIT 3, H", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x5D, Instruction { .name = "BIT 3, L", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x5E, Instruction { .name = "BIT 3, [HL]", .length = 1, .cycles = 12, .implementation = bit_impl } },
        { 0x5F, Instruction { .name = "BIT 3, A", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x60, Instruction { .name = "BIT 4, B", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x61, Instruction { .name = "BIT 4, C", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x62, Instruction { .name = "BIT 4, D", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x63, Instruction { .name = "BIT 4, E", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x64, Instruction { .name = "BIT 4, H", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x65, Instruction { .name = "BIT 4, L", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x66, Instruction { .name = "BIT 4, [HL]", .length = 1, .cycles = 12, .implementation = bit_impl } },
        { 0x67, Instruction { .name = "BIT 4, A", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x68, Instruction { .name = "BIT 5, B", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x69, Instruction { .name = "BIT 5, C", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x6A, Instruction { .name = "BIT 5, D", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x6B, Instruction { .name = "BIT 5, E", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x6C, Instruction { .name = "BIT 5, H", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x6D, Instruction { .name = "BIT 5, L", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x6E, Instruction { .name = "BIT 5, [HL]", .length = 1, .cycles = 12, .implementation = bit_impl } },
        { 0x6F, Instruction { .name = "BIT 5, A", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x70, Instruction { .name = "BIT 6, B", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x71, Instruction { .name = "BIT 6, C", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x72, Instruction { .name = "BIT 6, D", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x73, Instruction { .name = "BIT 6, E", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x74, Instruction { .name = "BIT 6, H", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x75, Instruction { .name = "BIT 6, L", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x76, Instruction { .name = "BIT 6, [HL]", .length = 1, .cycles = 12, .implementation = bit_impl } },
        { 0x77, Instruction { .name = "BIT 6, A", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x78, Instruction { .name = "BIT 7, B", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x79, Instruction { .name = "BIT 7, C", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x7A, Instruction { .name = "BIT 7, D", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x7B, Instruction { .name = "BIT 7, E", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x7C, Instruction { .name = "BIT 7, H", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x7D, Instruction { .name = "BIT 7, L", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x7E, Instruction { .name = "BIT 7, [HL]", .length = 1, .cycles = 12, .implementation = bit_impl } },
        { 0x7F, Instruction { .name = "BIT 7, A", .length = 1, .cycles = 4, .implementation = bit_impl } },
        { 0x80, Instruction { .name = "RES 0, B", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0x81, Instruction { .name = "RES 0, C", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0x82, Instruction { .name = "RES 0, D", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0x83, Instruction { .name = "RES 0, E", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0x84, Instruction { .name = "RES 0, H", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0x85, Instruction { .name = "RES 0, L", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0x86, Instruction { .name = "RES 0, [HL]", .length = 1, .cycles = 12, .implementation = reset_impl } },
        { 0x87, Instruction { .name = "RES 0, A", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0x88, Instruction { .name = "RES 1, B", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0x89, Instruction { .name = "RES 1, C", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0x8A, Instruction { .name = "RES 1, D", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0x8B, Instruction { .name = "RES 1, E", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0x8C, Instruction { .name = "RES 1, H", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0x8D, Instruction { .name = "RES 1, L", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0x8E, Instruction { .name = "RES 1, [HL]", .length = 1, .cycles = 12, .implementation = reset_impl } },
        { 0x8F, Instruction { .name = "RES 1, A", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0x90, Instruction { .name = "RES 2, B", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0x91, Instruction { .name = "RES 2, C", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0x92, Instruction { .name = "RES 2, D", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0x93, Instruction { .name = "RES 2, E", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0x94, Instruction { .name = "RES 2, H", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0x95, Instruction { .name = "RES 2, L", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0x96, Instruction { .name = "RES 2, [HL]", .length = 1, .cycles = 12, .implementation = reset_impl } },
        { 0x97, Instruction { .name = "RES 2, A", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0x98, Instruction { .name = "RES 3, B", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0x99, Instruction { .name = "RES 3, C", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0x9A, Instruction { .name = "RES 3, D", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0x9B, Instruction { .name = "RES 3, E", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0x9C, Instruction { .name = "RES 3, H", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0x9D, Instruction { .name = "RES 3, L", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0x9E, Instruction { .name = "RES 3, [HL]", .length = 1, .cycles = 12, .implementation = reset_impl } },
        { 0x9F, Instruction { .name = "RES 3, A", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xA0, Instruction { .name = "RES 4, B", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xA1, Instruction { .name = "RES 4, C", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xA2, Instruction { .name = "RES 4, D", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xA3, Instruction { .name = "RES 4, E", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xA4, Instruction { .name = "RES 4, H", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xA5, Instruction { .name = "RES 4, L", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xA6, Instruction { .name = "RES 4, [HL]", .length = 1, .cycles = 12, .implementation = reset_impl } },
        { 0xA7, Instruction { .name = "RES 4, A", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xA8, Instruction { .name = "RES 5, B", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xA9, Instruction { .name = "RES 5, C", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xAA, Instruction { .name = "RES 5, D", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xAB, Instruction { .name = "RES 5, E", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xAC, Instruction { .name = "RES 5, H", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xAD, Instruction { .name = "RES 5, L", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xAE, Instruction { .name = "RES 5, [HL]", .length = 1, .cycles = 12, .implementation = reset_impl } },
        { 0xAF, Instruction { .name = "RES 5, A", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xB0, Instruction { .name = "RES 6, B", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xB1, Instruction { .name = "RES 6, C", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xB2, Instruction { .name = "RES 6, D", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xB3, Instruction { .name = "RES 6, E", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xB4, Instruction { .name = "RES 6, H", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xB5, Instruction { .name = "RES 6, L", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xB6, Instruction { .name = "RES 6, [HL]", .length = 1, .cycles = 12, .implementation = reset_impl } },
        { 0xB7, Instruction { .name = "RES 6, A", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xB8, Instruction { .name = "RES 7, B", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xB9, Instruction { .name = "RES 7, C", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xBA, Instruction { .name = "RES 7, D", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xBB, Instruction { .name = "RES 7, E", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xBC, Instruction { .name = "RES 7, H", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xBD, Instruction { .name = "RES 7, L", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xBE, Instruction { .name = "RES 7, [HL]", .length = 1, .cycles = 12, .implementation = reset_impl } },
        { 0xBF, Instruction { .name = "RES 7, A", .length = 1, .cycles = 4, .implementation = reset_impl } },
        { 0xC0, Instruction { .name = "SET 0, B", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xC1, Instruction { .name = "SET 0, C", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xC2, Instruction { .name = "SET 0, D", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xC3, Instruction { .name = "SET 0, E", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xC4, Instruction { .name = "SET 0, H", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xC5, Instruction { .name = "SET 0, L", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xC6, Instruction { .name = "SET 0, [HL]", .length = 1, .cycles = 12, .implementation = set_impl } },
        { 0xC7, Instruction { .name = "SET 0, A", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xC8, Instruction { .name = "SET 1, B", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xC9, Instruction { .name = "SET 1, C", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xCA, Instruction { .name = "SET 1, D", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xCB, Instruction { .name = "SET 1, E", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xCC, Instruction { .name = "SET 1, H", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xCD, Instruction { .name = "SET 1, L", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xCE, Instruction { .name = "SET 1, [HL]", .length = 1, .cycles = 12, .implementation = set_impl } },
        { 0xCF, Instruction { .name = "SET 1, A", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xD0, Instruction { .name = "SET 2, B", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xD1, Instruction { .name = "SET 2, C", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xD2, Instruction { .name = "SET 2, D", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xD3, Instruction { .name = "SET 2, E", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xD4, Instruction { .name = "SET 2, H", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xD5, Instruction { .name = "SET 2, L", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xD6, Instruction { .name = "SET 2, [HL]", .length = 1, .cycles = 12, .implementation = set_impl } },
        { 0xD7, Instruction { .name = "SET 2, A", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xD8, Instruction { .name = "SET 3, B", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xD9, Instruction { .name = "SET 3, C", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xDA, Instruction { .name = "SET 3, D", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xDB, Instruction { .name = "SET 3, E", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xDC, Instruction { .name = "SET 3, H", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xDD, Instruction { .name = "SET 3, L", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xDE, Instruction { .name = "SET 3, [HL]", .length = 1, .cycles = 12, .implementation = set_impl } },
        { 0xDF, Instruction { .name = "SET 3, A", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xE0, Instruction { .name = "SET 4, B", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xE1, Instruction { .name = "SET 4, C", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xE2, Instruction { .name = "SET 4, D", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xE3, Instruction { .name = "SET 4, E", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xE4, Instruction { .name = "SET 4, H", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xE5, Instruction { .name = "SET 4, L", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xE6, Instruction { .name = "SET 4, [HL]", .length = 1, .cycles = 12, .implementation = set_impl } },
        { 0xE7, Instruction { .name = "SET 4, A", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xE8, Instruction { .name = "SET 5, B", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xE9, Instruction { .name = "SET 5, C", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xEA, Instruction { .name = "SET 5, D", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xEB, Instruction { .name = "SET 5, E", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xEC, Instruction { .name = "SET 5, H", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xED, Instruction { .name = "SET 5, L", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xEE, Instruction { .name = "SET 5, [HL]", .length = 1, .cycles = 12, .implementation = set_impl } },
        { 0xEF, Instruction { .name = "SET 5, A", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xF0, Instruction { .name = "SET 6, B", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xF1, Instruction { .name = "SET 6, C", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xF2, Instruction { .name = "SET 6, D", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xF3, Instruction { .name = "SET 6, E", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xF4, Instruction { .name = "SET 6, H", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xF5, Instruction { .name = "SET 6, L", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xF6, Instruction { .name = "SET 6, [HL]", .length = 1, .cycles = 12, .implementation = set_impl } },
        { 0xF7, Instruction { .name = "SET 6, A", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xF8, Instruction { .name = "SET 7, B", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xF9, Instruction { .name = "SET 7, C", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xFA, Instruction { .name = "SET 7, D", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xFB, Instruction { .name = "SET 7, E", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xFC, Instruction { .name = "SET 7, H", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xFD, Instruction { .name = "SET 7, L", .length = 1, .cycles = 4, .implementation = set_impl } },
        { 0xFE, Instruction { .name = "SET 7, [HL]", .length = 1, .cycles = 12, .implementation = set_impl } },
        { 0xFF, Instruction { .name = "SET 7, A", .length = 1, .cycles = 4, .implementation = set_impl } },
    };

}