#pragma once

#include "mmu.h"

#include <memory>
#include <cstdint>
#include <variant>
#include <optional>
#include <functional>
#include <unordered_map>

namespace sickboy {

    struct Registers {

        std::uint16_t af;
        std::uint16_t bc;
        std::uint16_t de;
        std::uint16_t hl;
        std::uint16_t sp;
        std::uint16_t pc;

        // Access to 8 bit registers
        std::uint8_t& a();
        std::uint8_t& f();
        std::uint8_t& b();
        std::uint8_t& c();
        std::uint8_t& d();
        std::uint8_t& e();
        std::uint8_t& h();
        std::uint8_t& l();

        // Access to flag registers
        void set_flag_z(bool on);
        void set_flag_n(bool on);
        void set_flag_h(bool on);
        void set_flag_c(bool on);

        bool get_flag_z();
        bool get_flag_n();
        bool get_flag_h();
        bool get_flag_c();

    };

    struct CPU;

    struct Register8 {
        std::uint8_t* register_ptr;
    };

    struct Register16 {
        std::uint16_t* register_ptr;
    };

    struct Register16Addr {
        std::uint16_t* register_ptr;
    };

    struct Immediate8 {
        std::uint8_t value;
    };

    struct Immediate16 {
        std::uint16_t value;
    };

    using InstructionParam = std::variant<
        Register8,
        Register16,
        Register16Addr,
        Immediate8,
        Immediate16
    >;

    using InstructionParamFetcher = std::function<std::optional<InstructionParam>(CPU&)>;

    struct Instruction {
        std::uint8_t length;
        std::uint8_t cycles;
        std::function<std::uint8_t(CPU&)> implementation;
    };

    struct CPU {

        // TODO: Change this to an array once we support all 255 instructions
        static std::unordered_map<std::uint8_t, Instruction> instruction_set;
        static std::unordered_map<std::uint8_t, Instruction> prefixed_instruction_set;

        CPU(const std::shared_ptr<MMU>& memory);

        Registers registers;
        std::shared_ptr<MMU> memory;
        bool is_prefixed;

    };

}