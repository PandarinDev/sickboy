#pragma once

#include "mmu.h"
#include "cpu.h"
#include "ppu.h"
#include "timer.h"

#include <memory>
#include <filesystem>

namespace sickboy {

    struct System {

        std::shared_ptr<MMU> memory;
        Timer timer;
        CPU cpu;
        PPU ppu;

        System(
            const std::filesystem::path& boot_rom_path,
            const std::filesystem::path& cartridge_path);

        // Returns true if a new frame should be rendered
        bool tick();

    private:

        bool stopped;

    };

}