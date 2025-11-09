#pragma once

#include "mmu.h"
#include "cpu.h"
#include "ppu.h"

#include <memory>
#include <filesystem>

namespace sickboy {

    struct System {

        static constexpr std::uint32_t MASTER_CLOCK_HZ = 4194304;
        static constexpr std::uint8_t MASTER_CLOCK_PER_SYSTEM_CLOCK = 4;

        std::shared_ptr<MMU> memory;
        CPU cpu;
        PPU ppu;

        System(const std::filesystem::path& cartridge_path);

        // Returns true if a new frame should be rendered
        bool tick();

    private:

        bool stopped;

    };

}