#pragma once

#include "mmu.h"
#include "cpu.h"
#include "ppu.h"

#include <memory>

namespace sickboy {

    struct System {

        static constexpr std::uint32_t MASTER_CLOCK_HZ = 4194304;
        static constexpr std::uint8_t MASTER_CLOCK_PER_SYSTEM_CLOCK = 4;

        std::shared_ptr<MMU> memory;
        CPU cpu;
        PPU ppu;
        double last_frame;

        System();

        void tick();

    };

}